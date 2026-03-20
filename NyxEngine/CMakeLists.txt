
# automatically collect and include all .h/.cpp files
# CONFIGURE_DEPENDS to automatically check for changes upon build request
# Many sources online stating it is better to refrain from this approach; to be investigated
#[[
file(GLOB_RECURSE ENGINE_SOURCES
    CONFIGURE_DEPENDS
    src/*.cpp
)
file(GLOB_RECURSE ENGINE_HEADERS
    CONFIGURE_DEPENDS
    include/*.h
)
]]

# List all .h/.cpp files explicitly
set(NYX_ENGINE_SOURCES
	src/NyxEnginePCH.cpp
	src/Application.cpp
	src/TypedHandle.cpp
	src/Entity.cpp
	src/ResourceManager.cpp
	src/WindowsWindow.cpp # only include on Windows
	src/ImGuiTheme.cpp
    src/VulkanUtil.cpp
    src/ImGuiVulkanUtil.cpp
)
set(NYX_ENGINE_HEADERS
	src/NyxEnginePCH.h
	include/NyxEngineAPI.h
	include/Engine.h
	include/EntryPoint.h
	include/Application.h
	include/TypedHandle.h
	include/Entity.h
	include/ResourceManager.h
	include/Window.h
	include/WindowsWindow.h # only include on Windows
	include/ImGuiTheme.h
    include/VulkanUtil.h
    include/ImGuiVulkanUtil.h
)

# SHARED to switch from static library to dynamic one producing .dll
add_library(NyxEngine SHARED ${NYX_ENGINE_SOURCES} ${NYX_ENGINE_HEADERS})

target_compile_features(NyxEngine PUBLIC cxx_std_20)

# Dependencies
target_link_libraries(NyxEngine
	PUBLIC NyxCore
	PUBLIC spdlog::spdlog
	PRIVATE glfw
	PRIVATE imgui::imgui
	PUBLIC glm::glm
	PRIVATE stb_image
)

# Expose Engine Module Source Directory to linked Modules
target_include_directories(NyxEngine 
	PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include
)

# Tell Visual Studio to mimick actual file hierarchy
source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} FILES
	${NYX_ENGINE_SOURCES}
	${NYX_ENGINE_HEADERS}
)

# define ENGINE_EXPORTS so EngineAPI.h dll macros toggle from export to import
# PRIVATE, so it doesn't apply for modules linking to Engine
target_compile_definitions(NyxEngine 
	PRIVATE NYXENGINE_EXPORTS
	PUBLIC $<$<CONFIG:Debug>:ENGINE_DEBUG>
    PUBLIC $<$<CONFIG:Release>:ENGINE_RELEASE>
	PUBLIC $<$<CONFIG:Debug>:ASSERTIONS_ENABLED>
    PRIVATE VULKAN_HPP_HANDLE_ERROR_OUT_OF_DATE_AS_SUCCESS # essential for as long as we use vulkan inside NyxEngine
)

# Precompiled Headers | src/... , as it is only meant to be included by .cpp files
target_precompile_headers(NyxEngine 
	PRIVATE src/NyxEnginePCH.h
)
