# NyxSlangSpike.exe: checks Slang against the shader and material plan (Design/shader-materials.md,
# stage 1, change 1) and prints what it found. Slang, its DLLs and SPIRV-Tools come from the Vulkan SDK.
find_path(NYX_SLANG_INCLUDE_DIR slang/slang.h HINTS "$ENV{VULKAN_SDK}/Include" REQUIRED)
find_library(NYX_SLANG_LIBRARY slang HINTS "$ENV{VULKAN_SDK}/Lib" REQUIRED)
find_library(NYX_SPIRV_TOOLS_LIBRARY SPIRV-Tools HINTS "$ENV{VULKAN_SDK}/Lib" REQUIRED)
find_library(NYX_SPIRV_TOOLS_DEBUG_LIBRARY SPIRV-Toolsd HINTS "$ENV{VULKAN_SDK}/Lib" REQUIRED)
get_filename_component(NYX_VULKAN_SDK_BIN "${NYX_SLANG_INCLUDE_DIR}/../Bin" ABSOLUTE)

add_executable(NyxSlangSpike
    main.cpp
    Shaders/nyx/surface.slang
    Shaders/Stone.slang
    Shaders/Broken.slang
    Shaders/LayoutProbe.slang
    Shaders/NoDefault.slang
)

target_include_directories(NyxSlangSpike PRIVATE "${NYX_SLANG_INCLUDE_DIR}")
target_link_libraries(NyxSlangSpike
    PRIVATE
        NyxEngine # VulkanContext, for the layout probe on the GPU
        "${NYX_SLANG_LIBRARY}"
        optimized "${NYX_SPIRV_TOOLS_LIBRARY}"
        debug "${NYX_SPIRV_TOOLS_DEBUG_LIBRARY}"
)
add_dependencies(NyxSlangSpike NyxGeneratedReflection)

# The spike reads its shaders where they are, so editing one needs no build
target_compile_definitions(NyxSlangSpike PRIVATE NYX_SLANG_SPIKE_SHADERS="${CMAKE_CURRENT_SOURCE_DIR}/Shaders")
source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} FILES Shaders/nyx/surface.slang Shaders/Stone.slang Shaders/Broken.slang Shaders/LayoutProbe.slang Shaders/NoDefault.slang)

# slang.dll is a small front that loads slang-compiler.dll. Nothing else is needed: without
# slang-glslang.dll the SPIR-V comes out byte for byte the same.
add_custom_command(TARGET NyxSlangSpike POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${NYX_VULKAN_SDK_BIN}/slang.dll"
        "${NYX_VULKAN_SDK_BIN}/slang-compiler.dll"
        "$<TARGET_FILE_DIR:NyxSlangSpike>"
)

set_property(TARGET NyxSlangSpike PROPERTY FOLDER "Tools")
add_test(NAME NyxSlangSpike COMMAND NyxSlangSpike)
