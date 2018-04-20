project "vkw"
    kind "ConsoleApp"
    location "../BaikalStandalone"
    links {}
    files { "../vkw/**.comp", "../vkw/**.h", "../vkw/**.cpp"}

    includedirs{"../Baikal/Vulkan",
                "../3rdparty",
                "."}

    if os.is("windows") then
        includedirs {}
        links {"vulkan-1"}
        libdirs {   "../3rdparty/vulkan/lib/",
                    }

        defines{"VK_USE_PLATFORM_WIN32_KHR",
                "_USE_MATH_DEFINES",
                "NOMINMAX"}
    end

    if os.is("linux") then
        buildoptions "-std=c++14"
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

    if os.is("windows") then
        postbuildcommands  {
        }
    end
