
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
set(ENGINE_SOURCES
	src/Enginepch.cpp
	src/Core.cpp
	src/Application.cpp
	src/Assertions.cpp
	src/TypedHandle.cpp
	src/Entity.cpp
	src/Log.cpp
)
set(ENGINE_HEADERS
	src/Enginepch.h	# Only meant to be included by .cpp files
	include/Engine.h
	include/EntryPoint.h
	include/Core.h
	include/Application.h
	include/Assertions.h
	include/TypedHandle.h
	include/Entity.h
	include/Log.h
)

# SHARED to switch from static library to dynamic one producing .dll
add_library(Engine SHARED ${ENGINE_SOURCES} ${ENGINE_HEADERS})

# Link against spdlog; Needs to be AFTER adding of Engine library
# @todo: Make PRIVATE after we hid away the spdlog calls from the Application Side
target_link_libraries(Engine
	PUBLIC EnginePCH
	PUBLIC spdlog::spdlog
)

# Expose Engine Module Source Directory to linked Modules
target_include_directories(Engine 
	PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include
)

# Tell Visual Studio to mimick actual file hierarchy
source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} FILES
	${ENGINE_SOURCES}
	${ENGINE_HEADERS}
)

# define ENGINE_EXPORTS so EngineAPI.h dll macros toggle from export to import
# PRIVATE, so it doesn't apply for modules linking to Engine
target_compile_definitions(Engine 
	PRIVATE ENGINE_EXPORTS
	PUBLIC $<$<CONFIG:Debug>:ENGINE_DEBUG>
    PUBLIC $<$<CONFIG:Release>:ENGINE_RELEASE>
	PUBLIC $<$<CONFIG:Debug>:ASSERTIONS_ENABLED>
)

# Precompiled Headers | src/... , as it is only meant to be included by .cpp files
target_precompile_headers(Engine 
	PRIVATE src/Enginepch.h
)
