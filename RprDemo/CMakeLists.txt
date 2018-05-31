set(SOURCES
    main.cpp)

find_package(GLFW3 REQUIRED)
set(GLEW_USE_STATIC_LIBS ON)

add_definitions(-DGLEW_STATIC)
link_directories(${Baikal_SOURCE_DIR}/3rdparty/glew/lib/x64
                ${Baikal_SOURCE_DIR}/3rdparty/ProRenderGLTF/lib)

add_executable(RprDemo ${SOURCES})
target_compile_features(RprDemo PRIVATE cxx_std_14)
target_include_directories(RprDemo PRIVATE .
                            PRIVATE ${ProRenderGLTF_HEADER})
target_link_libraries(RprDemo PRIVATE GTest RprSupport RadeonProRender ProRenderGLTF glfw3::glfw3 OpenGL::GL glew32s)
set_target_properties(RprDemo
    PROPERTIES VS_DEBUGGER_WORKING_DIRECTORY ${Baikal_SOURCE_DIR}/RprDemo)
