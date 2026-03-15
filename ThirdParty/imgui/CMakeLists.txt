set(IMGUI_DIR ${CMAKE_SOURCE_DIR}/Dependencies/imgui)

add_library(imgui STATIC
	${IMGUI_DIR}/imgui.cpp
	${IMGUI_DIR}/imgui_draw.cpp
	${IMGUI_DIR}/imgui_tables.cpp
	${IMGUI_DIR}/imgui_widgets.cpp

	${IMGUI_DIR}/backends/imgui_impl_glfw.cpp
	${IMGUI_DIR}/backends/imgui_impl_vulkan.cpp
)

target_include_directories(imgui
	PUBLIC
		${IMGUI_DIR}
		${IMGUI_DIR}/backends
)

find_package(Vulkan QUIET)

if(NOT Vulkan_FOUND)
    message(FATAL_ERROR
        "Vulkan SDK not found.\n"
        "Install the Vulkan SDK, then regenerate project files.")
endif()

target_link_libraries(imgui
	PUBLIC
		glfw
		Vulkan::Vulkan
)

add_library(imgui::imgui ALIAS imgui)