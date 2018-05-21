#include <iostream>
#include <array>
#include <GL/glew.h>
#include <RadeonProRender.h>
#include <RadeonProRender_GL.h>
#include <RprSupport.h>
#include <ProRenderGLTF.h>

#include "GL/glew.h"
#include "GLFW/glfw3.h"
#include "math/float3.h"
#include "math/matrix.h"
#include "math/mathutils.h"

#ifdef WIN32
    const char* kTahoeLibName = "Tahoe64.dll";
#elif defined __linux__
    const char* kTahoeLibName = "libTahoe64.so";
#endif

GLFWwindow* window = nullptr;
rpr_context context = nullptr;
rpr_material_system materialSystem = nullptr;
rprx_context uberMatContext = nullptr;
rpr_framebuffer framebuffer = nullptr;
rpr_camera camera = nullptr;
rpr_light light = nullptr;

GLuint texture = 0;
GLuint blitProgram = 0;
GLuint emptyVao = 0;

float cameraPitch = 0.0f;
float cameraYaw = 0.0f;
float cameraZoom = 1.0f;
RadeonRays::float3 cameraTarget;
RadeonRays::float3 cameraUp;
RadeonRays::matrix cameraTransform;
bool clearFramebuffer = false;

#define LOG_FATAL(msg, error) throw std::runtime_error(msg);

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

void ResizeHandler(GLFWwindow* window, int width, int height)
{
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
    fr_int status = rprContextCreateFramebufferFromGLTexture2D(context, GL_TEXTURE_2D, 0, texture, &framebuffer);
    if (status != RPR_SUCCESS)
        LOG_FATAL("rprContextCreateFramebufferFromGLTexture2D failed, error ", status);

    // Set framebuffer as color aov.
    status = rprContextSetAOV(context, RPR_AOV_COLOR, framebuffer);
    if (status != RPR_SUCCESS)
        LOG_FATAL("rprContextSetAOV failed, error ", status);
}

void Keyhandler(GLFWwindow* window, int key, int scancode, int action, int mods)
{
    if (key == GLFW_KEY_S && action == GLFW_RELEASE)
    {
        rprFrameBufferSaveToFile(framebuffer, "screenshot.png");
    }
}

RadeonRays::float3 CameraDir()
{
    //return (glm::rotate(cameraYaw, RadeonRays::float3(0.0f, 1.0f, 0.0f)) * glm::rotate(cameraPitch, RadeonRays::float3(1.0f, 0.0f, 0.0f)) * RadeonRays::float3(0.0f, 0.0f, -1.0f, 0.0f)).xyz();
    return RadeonRays::rotation_y(cameraYaw) * RadeonRays::rotation_x(cameraPitch) * RadeonRays::float3(0.0f, 0.0f, -1.0f, 0.0f);
    //return RadeonRays::float3(0.0f, 1.0f, 0.0f, 0.0f);
}

RadeonRays::float3 CameraRight()
{
    return RadeonRays::cross(cameraUp, CameraDir());
}

RadeonRays::float3 CameraEye()
{
    return cameraTarget + CameraDir() * cameraZoom;
}

void UpdateCamera()
{
    if (camera)
    {
        RadeonRays::float3  dir = CameraDir();
        RadeonRays::float3  eye = CameraEye();
        rprCameraLookAt(camera, eye.x, eye.y, eye.z, cameraTarget.x, cameraTarget.y, cameraTarget.z, cameraUp.x, cameraUp.y, cameraUp.z);
        clearFramebuffer = true;
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

            RadeonRays::float3 right = CameraRight();
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
    window = glfwCreateWindow(640, 480, "Radeon ProRender GLFW Import", nullptr, nullptr);
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

void InitProRender(const char* filename)
{
    // Register Tahoe Radeon ProRender backend.
    rpr_int tahoePluginID = rprRegisterPlugin(kTahoeLibName);

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

    // Create Radeon ProRender uber material context.
    rprxCreateContext(materialSystem, 0, &uberMatContext);
    
    // Load the GLTF file from disk.
    rpr_scene scene;
    if (rprImportFromGLTF(filename, context, materialSystem, uberMatContext, &scene) != GLTF_SUCCESS)
        LOG_FATAL("RPR GLTF import failed!");

    // Get the scene bounding box.
    float bbox[2][3] = { { 0 } };
    rprSceneGetInfo(scene, RPR_SCENE_AABB, sizeof(float) * 6, &bbox[0][0], nullptr);

    // Get the scene camera.
    rprSceneGetCamera(scene, &camera);

    // If no camera was found in gltf file, create one.
    if (!camera)
    {
        rprContextCreateCamera(context, &camera);
        rprSceneSetCamera(scene, camera);
    }

    // Set initial camera parameters.
    cameraTarget = RadeonRays::float3((bbox[0][0] + bbox[1][0]) * 0.5f, (bbox[0][1] + bbox[1][1]) * 0.5f, (bbox[0][2] + bbox[1][2]) * 0.5f);
    cameraUp = RadeonRays::float3(0.0f, 1.0f, 0.0f);
    //cameraZoom = 3.0f * glm::max(bbox[1][0] - bbox[0][0], glm::max(bbox[1][1] - bbox[0][1], bbox[2][1] - bbox[2][0]));
    cameraZoom = 3.0f * std::max(bbox[1][0] - bbox[0][0], std::max(bbox[1][1] - bbox[0][1], bbox[2][1] - bbox[2][0]));

    UpdateCamera();

    // If no lights were present, add an all white 1x1 pixel IBL.
    size_t lightCount;
    rprSceneGetInfo(scene, RPR_SCENE_LIGHT_COUNT, sizeof(lightCount), &lightCount, nullptr);
#if 1
    {
        // Load environment map image.
        rpr_image image = nullptr;
        status = rprContextCreateImageFromFile(context, "../Resources/Textures/studio015.hdr", &image);
        if (status != RPR_SUCCESS)
            LOG_FATAL("rprContextCreateImageFromFile failed error ", status);

        // Add an environment light to the scene with the image attached.
        rpr_light light;
        status = rprContextCreateEnvironmentLight(context, &light);
        status = rprEnvironmentLightSetImage(light, image);
        status = rprSceneAttachLight(scene, light);

        // Set the background image to a solid color.
        std::array<uint8_t, 3> backgroundColor = { 255, 255, 255 };
        rpr_image_format format = { 3, RPR_COMPONENT_TYPE_UINT8 };
        rpr_image_desc desc = { 1, 1, 1, 3, 3 };
        status = rprContextCreateImage(context, format, &desc, backgroundColor.data(), &image);
        if (status != RPR_SUCCESS)
            LOG_FATAL("rprContextCreateImage failed error ", status);

        rpr_light backgroundImageLight;
        status = rprContextCreateEnvironmentLight(context, &backgroundImageLight);
        status = rprEnvironmentLightSetImage(backgroundImageLight, image);
        status = rprSceneSetEnvironmentOverride(scene, RPR_SCENE_ENVIRONMENT_OVERRIDE_BACKGROUND, backgroundImageLight);

    }
#else
    {
        // Set the background image to a solid color.
        std::array<uint8_t, 3> backgroundColor = { 255, 255, 255 };
        rpr_image_format format = { 3, RPR_COMPONENT_TYPE_UINT8 };
        rpr_image_desc desc = { 1, 1, 1, 3, 3 };
        rpr_image image = nullptr;
        status = rprContextCreateImage(context, format, &desc, backgroundColor.data(), &image);
        if (status != RPR_SUCCESS)
            LOG_FATAL("rprContextCreateImage failed error ", status);

        // Add an environment light to the scene with the image attached.
        rpr_light light;
        status = rprContextCreateEnvironmentLight(context, &light);
        status = rprEnvironmentLightSetImage(light, image);
        status = rprSceneAttachLight(scene, light);
    }
#endif
}

int main(int argc, char** argv)
{
    // Check command line arguments.
    if (argc < 2)
    {
        std::cout << "Usage: Import.exe [gltf filename]\n";
        return -1;
    }

    // Set logger output.
    //LOG_TO_FILE("Import.log");

    // Initialize OpenGL window.
    InitGLWindow();

    // Initialize Radeon ProRender library and load GLTF scene.
    InitProRender(argv[1]);

    // Force initial resize call to allocate framebuffer.
    int width, height;
    glfwGetWindowSize(window, &width, &height);
    ResizeHandler(window, width, height);

    // Loop until either the ESCAPE key is pressed or the window is closed.
    while (!glfwGetKey(window, GLFW_KEY_ESCAPE) && !glfwWindowShouldClose(window))
    {
        // Render the ProRender scene.
        //rprContextSetParameter1u(context, "rendermode", RPR_RENDER_MODE_TEXCOORD);
        if (clearFramebuffer)
        {
            rprFrameBufferClear(framebuffer);
            clearFramebuffer = false;
        }
        rprContextRender(context);

        // Get the current window dimensions.
        glfwGetWindowSize(window, &width, &height);

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

    return 0;
}
