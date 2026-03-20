set(NYX_CORE_SOURCES
    src/NyxCorePCH.cpp
    src/Assertions.cpp
    src/Core.cpp
    src/Log.cpp
)

set(NYX_CORE_HEADERS
    src/NyxCorePCH.h
    include/Assertions.h
    include/Core.h
    include/Log.h
)

add_library(NyxCore SHARED ${NYX_CORE_SOURCES} ${NYX_CORE_HEADERS})

target_compile_features(NyxCore PUBLIC cxx_std_20)

target_link_libraries(NyxCore
	PUBLIC spdlog::spdlog
)

target_include_directories(NyxCore
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_compile_definitions(NyxCore
    # PUBLIC NYXCORE_STATIC # for STATIC library
    PRIVATE NYXCORE_EXPORTS # For SHARED library
    PRIVATE ASSERTIONS_ENABLED
)

source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} FILES
    ${NYX_CORE_SOURCES}
    ${NYX_CORE_HEADERS}
)

target_precompile_headers(NyxCore 
    PRIVATE src/NyxCorePCH.h
)