#include <iostream>
#include <array>

#define GLM_SWIZZLE
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/glm.hpp>
#include <glm/ext.hpp>

#include <glew/include/GL/glew.h>
#include <GLFW/glfw3.h>
#include <chrono>
#include <RadeonProRender.h>
#include "math/mathutils.h"
#include "load_obj_test.h"

#define  LOG_FATAL(msg) throw std::runtime_error(msg);

using namespace RadeonRays;

static const char* BLIT_VERTEX_SHADER = "\
    #version 450\n\
    layout (location = 1) uniform float FramebufferAspect;\n\
    layout (location = 2) uniform float ViewportAspect;\n\
    out VS_OUT\n\
    {\n\
        vec2 UV;\n\
    } vs_out;\n\
    void main()\n\
    {\n\
        const vec2 vertexPositions[4] = {\n\
            vec2(-1.0f, -1.0f),\n\
            vec2(1.0f, -1.0f),\n\
            vec2(-1.0f, 1.0f),\n\
            vec2(1.0f, 1.0f)\n\
        };\n\
        \n\
        vec2 pos = vertexPositions[gl_VertexID];\n\
        if (FramebufferAspect >= ViewportAspect)\n\
            pos.y /= FramebufferAspect / ViewportAspect;\n\
        else\n\
            pos.x *= FramebufferAspect / ViewportAspect;\n\
        \n\
        vs_out.UV = vertexPositions[gl_VertexID] * 0.5f + 0.5f;\n\
        vs_out.UV.y = 1.f - vs_out.UV.y;\n\
        gl_Position = vec4(pos, 0.0f, 1.0f);\n\
    }\n\
";

static const char* BLIT_FRAGMENT_SHADER = "\
    #version 450\n\
    layout (binding = 0) uniform sampler2D tex;\n\
    layout (location = 0) out vec4 FragColor;\n\
    in VS_OUT\n\
    {\n\
        vec2 UV;\n\
    } ps_in;\n\
    \n\
    void main()\n\
    {\n\
        vec4 color = texture(tex, vec2(ps_in.UV.x, 1.0f - ps_in.UV.y));\n\
        color /= color.w;\n\
        FragColor = color;\n\
    }\n\
";

static uint32_t CompileAndLinkBlitProgram()
{
    // Compile vertex shader.
    GLuint vert = glCreateShader(GL_VERTEX_SHADER);
    glShaderSource(vert, 1, (const GLchar**)&BLIT_VERTEX_SHADER, 0);
    glCompileShader(vert);

    int status;
    glGetShaderiv(vert, GL_COMPILE_STATUS, &status);
    if (status != GL_TRUE)
    {
        int length;
        char log[1024];
        glGetShaderInfoLog(vert, 1024, &length, log);
        LOG_FATAL("Error compiling vertex shader:\n", log);
    }

    // Compile fragment shader.
    GLuint frag = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(frag, 1, (const GLchar**)&BLIT_FRAGMENT_SHADER, 0);
    glCompileShader(frag);

    glGetShaderiv(frag, GL_COMPILE_STATUS, &status);
    if (status != GL_TRUE)
    {
        int length;
        char log[1024];
        glGetShaderInfoLog(frag, 1024, &length, log);
        LOG_FATAL("Error compiling fragment shader:\n", log);
    }

    // Link program.
    uint32_t program = glCreateProgram();
    glAttachShader(program, vert);
    glAttachShader(program, frag);
    glLinkProgram(program);

    glGetProgramiv(program, GL_LINK_STATUS, &status);
    if (status != GL_TRUE)
    {
        int length;
        char log[1024];
        glGetProgramInfoLog(program, 1024, &length, log);
        LOG_FATAL("Error linking m_program:\n", log);
    }

    return program;
}

GLFWwindow* window = nullptr;
rpr_context context = nullptr;
rpr_material_system materialSystem = nullptr;
rpr_framebuffer framebuffer = nullptr;
rpr_camera camera = nullptr;
rpr_light light = nullptr;

GLuint texture = 0;
GLuint blitProgram = 0;
GLuint emptyVao = 0;

float cameraPitch = 0.0f;
float cameraYaw = 0.0f;
float cameraZoom = 1.0f;
glm::vec3 cameraTarget;
glm::vec3 cameraUp;
glm::mat4 cameraTransform;
bool clearFramebuffer = false;

static bool g_is_left_pressed = false;
static bool g_is_right_pressed = false;
static bool g_is_fwd_pressed = false;
static bool g_is_back_pressed = false;
static bool g_is_home_pressed = false;
static bool g_is_end_pressed = false;

void ResizeHandler(GLFWwindow* window, int width, int height)
{
    fr_int status = RPR_SUCCESS;

    // Free previous resource handles.
    if (glIsTexture(texture))
    {
        rprObjectDelete(framebuffer);
        glDeleteTextures(1, &texture);
    }

    // Allocate an OpenGL texture.
    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32F, width, height, 0, GL_RGBA, GL_FLOAT, NULL);
    glBindTexture(GL_TEXTURE_2D, 0);

    // Allocate RPR framebuffer from GL texture.
    //fr_int status = rprContextCreateFramebufferFromGLTexture2D(context, GL_TEXTURE_2D, 0, texture, &framebuffer);
    //if (status != RPR_SUCCESS)
    //    LOG_FATAL("rprContextCreateFramebufferFromGLTexture2D failed, error ", status);


    rpr_framebuffer_desc desc;
    desc.fb_width = width;
    desc.fb_height = height;
    rpr_framebuffer_format fmt = { 4, RPR_COMPONENT_TYPE_FLOAT32 };
    status = rprContextCreateFrameBuffer(context, fmt, &desc, &framebuffer);
    assert(status == RPR_SUCCESS);

    // Set framebuffer as color aov.
    status = rprContextSetAOV(context, RPR_AOV_COLOR, framebuffer);
    if (status != RPR_SUCCESS)
        LOG_FATAL("rprContextSetAOV failed, error ", status);
}

void CopyFramebuffer(int width, int height)
{
    fr_int status = RPR_SUCCESS;
    std::vector<RadeonRays::float3> data;
    data.resize(width * height);
    size_t size_ret = 0;
    status = rprFrameBufferGetInfo(framebuffer, RPR_FRAMEBUFFER_DATA, data.size() * sizeof(RadeonRays::float3), data.data(), &size_ret);
    assert(status == RPR_SUCCESS);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, width, height, GL_RGBA, GL_FLOAT, &data[0]);
    glBindTexture(GL_TEXTURE_2D, 0);
}


glm::vec3 CameraDir()
{
    glm::vec4 dir = (glm::rotate(cameraYaw, glm::vec3(0.0f, 1.0f, 0.0f)) * glm::rotate(cameraPitch, glm::vec3(1.0f, 0.0f, 0.0f)) * glm::vec4(0.0f, 0.0f, -1.0f, 0.0f));
    return xyz(dir);
}

glm::vec3 CameraRight()
{
    return glm::cross(cameraUp, CameraDir());
}

glm::vec3 CameraEye()
{
    return cameraTarget + CameraDir() * cameraZoom;
}

void UpdateCamera()
{
    if (camera)
    {
        glm::vec3 dir = CameraDir();
        glm::vec3 eye = CameraEye();
        rprCameraLookAt(camera, eye.x, eye.y, eye.z, cameraTarget.x, cameraTarget.y, cameraTarget.z, cameraUp.x, cameraUp.y, cameraUp.z);
        clearFramebuffer = true;
    }
}

void Keyhandler(GLFWwindow* window, int key, int scancode, int action, int mods)
{
    const bool press_or_repeat = action == GLFW_PRESS || action == GLFW_REPEAT;

    if (key == GLFW_KEY_S && action == GLFW_RELEASE)
    {
        rprFrameBufferSaveToFile(framebuffer, "screenshot.png");
    }

    switch (key)
    {
    case GLFW_KEY_UP:
        g_is_fwd_pressed = press_or_repeat;
        break;
    case GLFW_KEY_DOWN:
        g_is_back_pressed = press_or_repeat;
        break;
    case GLFW_KEY_LEFT:
        g_is_left_pressed = press_or_repeat;
        break;
    case GLFW_KEY_RIGHT:
        g_is_right_pressed = press_or_repeat;
        break;
    case GLFW_KEY_HOME:
        g_is_home_pressed = press_or_repeat;
        break;
    case GLFW_KEY_END:
        g_is_end_pressed = press_or_repeat;
        break;
    }
}

void Update()
{
    static auto prevtime = std::chrono::high_resolution_clock::now();
    auto time = std::chrono::high_resolution_clock::now();
    auto dt = std::chrono::duration_cast<std::chrono::duration<double>>(time - prevtime);
    prevtime = time;
    bool cam_updated = false;

    glm::vec3 forward = CameraDir();
    glm::vec3 right = CameraRight();
    glm::vec3 up(0.f, 1.f, 0.f);
    float speed = 25.f;

    bool update_cam = false;

    if (g_is_fwd_pressed)
    {
        cameraTarget -= (float)dt.count() * speed * forward;
        update_cam = true;
    }
    if (g_is_back_pressed)
    {
        cameraTarget += (float)dt.count() * speed * forward;
        update_cam = true;
    }
    if (g_is_left_pressed)
    {
        cameraTarget -= (float)dt.count() * speed * right;
        update_cam = true;
    }
    if (g_is_right_pressed)
    {
        cameraTarget += (float)dt.count() * speed * right;
        update_cam = true;
    }
    if (g_is_home_pressed)
    {
        cameraTarget += (float)dt.count() * speed * up;
        update_cam = true;
    }
    if (g_is_end_pressed)
    {
        cameraTarget -= (float)dt.count() * speed * up;
        update_cam = true;
    }

    if (update_cam)
    {
        UpdateCamera();
    }
}


void MousePositionHandler(GLFWwindow* window, double x, double y)
{
    if (camera)
    {
        static int lastX = 0, lastY = 0, lastButtonState = 0;
        int buttonState = 0;
        if ((buttonState = glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_LEFT)) == GLFW_PRESS)
        {
            if (lastButtonState != buttonState)
            {
                lastX = static_cast<int>(x);
                lastY = static_cast<int>(y);
            }

            cameraYaw += (lastX - static_cast<int>(x)) * 0.005f;
            cameraPitch -= (lastY - static_cast<int>(y)) * 0.005f;
            UpdateCamera();
        }
        else if ((buttonState = glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_RIGHT)) == GLFW_PRESS)
        {
            if (lastButtonState != buttonState)
            {
                lastX = static_cast<int>(x);
                lastY = static_cast<int>(y);
            }

            int dx = lastX - static_cast<int>(x);
            int dy = lastY - static_cast<int>(y);

            glm::vec3 right = CameraRight();
            cameraTarget += right * (dx * cameraZoom * 1e-3f);
            cameraTarget += cameraUp * (-dy * cameraZoom * 1e-3f);
            UpdateCamera();
        }

        lastButtonState = buttonState;
        lastX = static_cast<int>(x);
        lastY = static_cast<int>(y);
    }
}

void MouseWheelHandler(GLFWwindow* window, double xoffset, double yoffset)
{
    if (yoffset > 0) cameraZoom *= 0.9f;
    else cameraZoom *= 1.1f;
    UpdateCamera();
}

void InitGLWindow()
{
    // Create GLFW window and OpenGL context.
    glfwInit();
    glfwWindowHint(GLFW_VISIBLE, GL_FALSE);
    //glfwWindowHint(GLFW_RESIZABLE, GL_FALSE);
    window = glfwCreateWindow(640, 480, "RprDemo", nullptr, nullptr);
    glfwMakeContextCurrent(window);
    glfwShowWindow(window);

    // Set window event callbacks.
    glfwSetWindowSizeCallback(window, ResizeHandler);
    glfwSetKeyCallback(window, Keyhandler);
    glfwSetCursorPosCallback(window, MousePositionHandler);
    glfwSetScrollCallback(window, MouseWheelHandler);

    // Initialize OpenGL extensions.
    glewInit();

    // Create blit shader program.
    blitProgram = CompileAndLinkBlitProgram();

    // Allocate an empty vao for executing fullscreen blit pass.
    glGenVertexArrays(1, &emptyVao);
}

void InitProRender(const std::string& basepath, const std::string& filename)
{
    rpr_int tahoePluginID = rprRegisterPlugin("");

    // Create Radeon ProRender context.
    rpr_int status = rprCreateContext(RPR_API_VERSION, &tahoePluginID, 1, RPR_CREATION_FLAGS_ENABLE_GPU0 | RPR_CREATION_FLAGS_ENABLE_GL_INTEROP, nullptr, nullptr, &context);
    if (status != RPR_SUCCESS)
        LOG_FATAL("rprCreateContext returned error ", status);

    // Set ray cast epsilon to deal with 'z-fighting'.
    status = rprContextSetParameter1f(context, "raycastepsilon", 1e-4f);
    status = rprContextSetParameter1u(context, "maxRecursion", 5);

    // Enable trace output.
    /*status = rprContextSetParameterString(context, "tracingfolder", "./trace");
    status = rprContextSetParameter1u(context, "tracing", 1);*/

    // Create Radeon ProRender material system.
    rprContextCreateMaterialSystem(context, 0, &materialSystem);

    // Load the GLTF file from disk.
    rpr_scene scene = NULL; status = rprContextCreateScene(context, &scene);
    assert(status == RPR_SUCCESS);


    if (!LoadObj(basepath, filename, context, materialSystem, scene))
    {
        LOG_FATAL("RPR GLTF import failed!");
    }

    //camera
    {
        RadeonRays::float3 eye = { -6.89f, 27.17f, 1.25f };
        RadeonRays::float3 forward = { -1.f, -0.13f, 0.11f };
        RadeonRays::float3 at = forward + eye;
        RadeonRays::float3 up = { 0.f, 1.f, 0.f };
        status = rprContextCreateCamera(context, &camera);
        assert(status == RPR_SUCCESS);


        cameraTarget = glm::vec3(eye.x, eye.y, eye.z);
        cameraUp = glm::vec3(0.0f, 1.0f, 0.0f);
        cameraZoom = 1.f;
        cameraYaw = -1.56f;
        cameraZoom = 0.13f;

        UpdateCamera();

        assert(status == RPR_SUCCESS);
        status = rprCameraSetSensorSize(camera, 22.5f, 15.f);
        assert(status == RPR_SUCCESS);
        status = rprCameraSetFocalLength(camera, 35.f);
        assert(status == RPR_SUCCESS);
        status = rprCameraSetFStop(camera, 6.4f);
        assert(status == RPR_SUCCESS);
        status = rprSceneSetCamera(scene, camera);
        assert(status == RPR_SUCCESS);
    }

    //lights
    {
        float height = -35.0f;

        rpr_light light[3] = { NULL, NULL, NULL };
        status = rprContextCreateSpotLight(context, &light[0]);
        assert(status == RPR_SUCCESS);
        status = rprContextCreateSpotLight(context, &light[1]);
        assert(status == RPR_SUCCESS);
        status = rprContextCreateSpotLight(context, &light[2]);
        assert(status == RPR_SUCCESS);

        InitLight(light[0], RadeonRays::float3(23.8847504f, -16.0555954f, 5.01268339f),
            RadeonRays::float3(23.0653629f, -15.9814873f, 4.44425392f),
            RadeonRays::float3(5000.0f, 5000.0f, 5000.0f));

        InitLight(light[1], RadeonRays::float3(0.0f, height, 0.0f),
            RadeonRays::float3(-1.0f, height, 0.0f),
            RadeonRays::float3(3000.0f, 3000.0f, 3000.0f));

        InitLight(light[2], RadeonRays::float3(24.2759151f, -17.7952175f, -4.77304792f),
            RadeonRays::float3(23.3718319f, -17.5660172f, -4.41235495f),
            RadeonRays::float3(5000.0f, 5000.0f, 5000.0f));

        status = rprSceneAttachLight(scene, light[0]);
        assert(status == RPR_SUCCESS);
        status = rprSceneAttachLight(scene, light[1]);
        assert(status == RPR_SUCCESS);
        status = rprSceneAttachLight(scene, light[2]);
        assert(status == RPR_SUCCESS);
    }
}

int main(int argc, char** argv)
{
    try
    {
        // Initialize OpenGL window.
        InitGLWindow();

        // Initialize Radeon ProRender library and load GLTF scene.
        InitProRender("../Resources/Sponza", "sponza_pbr.obj");

        // Force initial resize call to allocate framebuffer.
        int width, height;
        glfwGetWindowSize(window, &width, &height);
        ResizeHandler(window, width, height);

        // Loop until either the ESCAPE key is pressed or the window is closed.
        while (!glfwGetKey(window, GLFW_KEY_ESCAPE) && !glfwWindowShouldClose(window))
        {
            // Render the ProRender scene.
            //rprContextSetParameter1u(context, "rendermode", RPR_RENDER_MODE_TEXCOORD);
            Update();
            if (clearFramebuffer)
            {
                rprFrameBufferClear(framebuffer);
                clearFramebuffer = false;
            }
            rprContextRender(context);

            // Get the current window dimensions.
            glfwGetWindowSize(window, &width, &height);
            CopyFramebuffer(width, height);
            // Setup viewport.
            glViewport(0, 0, width, height);
            glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
            glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

            // Bind blit program and display framebuffer texture.
            float aspectRatio = width / static_cast<float>(height);
            glUseProgram(blitProgram);
            glUniform1f(1, aspectRatio);
            glUniform1f(2, aspectRatio);
            glBindTextureUnit(0, texture);
            glBindVertexArray(emptyVao);
            glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
            glBindVertexArray(0);
            glBindTextureUnit(0, 0);
            glUseProgram(0);

            glfwPollEvents();
            glfwSwapBuffers(window);
        }

        // Destroy the Radeon ProRender context.
        rprObjectDelete(context);
    }
    catch (std::exception& ex)
    {
        std::cout << ex.what() << std::endl;
        return -1;
    }

    return 0;
}
