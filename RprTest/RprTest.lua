project "RprTest"
    kind "ConsoleApp"
    location "../RprTest"
    links { "RadeonRays", 
            "CLW", 
            "Calc", 
            "RadeonProRender", 
            "ProRenderGLTF", 
            "RprSupport64", 
            "RprLoadStore64",
            "Baikal"}
    files { "../RprTest/**.h", "../RprTest/**.cpp", "../RprTest/**.cl", "../RprTest/**.fsh", "../RprTest/**.vsh" }

    includedirs{"../Rpr",
                "../Baikal",
                ".", 
                "../RadeonRays/RadeonRays/include",
                "../3rdParty/RprLoadStore/include",
                "../3rdParty/RprSupport/include",
                "../3rdparty/radeonrays-next/radeonrays/inc",
                "../3rdparty/glm",
                "../3rdparty/gli",
                "../3rdparty",
                "../3rdparty/assimp/include",
                "../3rdParty/ProRenderGLTF/include" }

    if os.is("macosx") then
        sysincludedirs {"/usr/local/include"}
        libdirs {"/usr/local/lib"}
        buildoptions "-std=c++11 -stdlib=libc++"
        links {"OpenImageIO"}
    end

    if os.is("windows") then
        includedirs { "../3rdparty/oiio/include"  }
        links {"RadeonRays", "assimp"}
        libdirs {   "../3rdparty/freeglut/lib/%{cfg.platform}", 
                    "../3rdparty/embree/lib/%{cfg.platform}", 
                    "../3rdparty/oiio/lib/%{cfg.platform}",
                    "../3rdparty/ProRenderGLTF/lib/%{cfg.platform}",
                    "../3rdparty/RprLoadStore/lib/%{cfg.platform}",
                    "../3rdparty/RprSupport/lib/%{cfg.platform}",
                    "../3rdparty/assimp/lib/",
                    "../3rdparty/vulkan/lib/",
                     }
        configuration {"Debug"}
            links {"OpenImageIOD"}
        configuration {"Release"}
            links {"OpenImageIO"}
        configuration {}


        defines{"VK_USE_PLATFORM_WIN32_KHR",
                "NOMINMAX"}
    end

    if os.is("linux") then
        buildoptions "-std=c++11"
        links {"OpenImageIO", "pthread",}
        os.execute("rm -rf obj");
    end

    configuration {"x32", "Debug"}
        targetdir "../Bin/Debug/x86"
    configuration {"x64", "Debug"}
        targetdir "../Bin/Debug/x64"
    configuration {"x32", "Release"}
        targetdir "../Bin/Release/x86"
    configuration {"x64", "Release"}
        targetdir "../Bin/Release/x64"
    configuration {}
    
    os.mkdir("Output")

    if os.is("windows") then
        postbuildcommands  { 
          'copy "..\\3rdparty\\glew\\bin\\%{cfg.platform}\\glew32.dll" "%{cfg.buildtarget.directory}"', 
          'copy "..\\3rdparty\\freeglut\\bin\\%{cfg.platform}\\freeglut.dll" "%{cfg.buildtarget.directory}"', 
          'copy "..\\3rdparty\\embree\\bin\\%{cfg.platform}\\embree.dll" "%{cfg.buildtarget.directory}"',
          'copy "..\\3rdparty\\embree\\bin\\%{cfg.platform}\\tbb.dll" "%{cfg.buildtarget.directory}"',
          'copy "..\\3rdparty\\oiio\\bin\\%{cfg.platform}\\OpenImageIO.dll" "%{cfg.buildtarget.directory}"',
          'copy "..\\3rdparty\\oiio\\bin\\%{cfg.platform}\\OpenImageIOD.dll" "%{cfg.buildtarget.directory}"',
          'copy "..\\3rdparty\\ProRenderGLTF\\bin\\%{cfg.platform}\\ProRenderGLTF.dll" "%{cfg.buildtarget.directory}"',
          'copy "..\\3rdparty\\RprLoadStore\\bin\\%{cfg.platform}\\RprLoadStore64.dll" "%{cfg.buildtarget.directory}"',
          'copy "..\\3rdparty\\RprSupport\\bin\\%{cfg.platform}\\RprSupport64.dll" "%{cfg.buildtarget.directory}"'
        }
    end