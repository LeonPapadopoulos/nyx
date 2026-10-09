# Headless file tests: they exercise the same writer used by scene saving.
add_executable(NyxArchiveTests
    ArchiveTests.cpp
)

target_link_libraries(NyxArchiveTests PRIVATE NyxEngine)
add_dependencies(NyxArchiveTests NyxGeneratedReflection)
set_property(TARGET NyxArchiveTests PROPERTY FOLDER "Tests")

add_test(NAME NyxArchiveTests COMMAND NyxArchiveTests)

add_executable(NyxRuntimeInspectionTests RuntimeInspectionTests.cpp)
target_link_libraries(NyxRuntimeInspectionTests PRIVATE NyxEngine)
add_dependencies(NyxRuntimeInspectionTests NyxGeneratedReflection)
set_property(TARGET NyxRuntimeInspectionTests PROPERTY FOLDER "Tests")
add_test(NAME NyxRuntimeInspectionTests COMMAND NyxRuntimeInspectionTests)

if(MSVC)
    add_test(NAME NyxRuntimeInspectionSymbols
        COMMAND ${CMAKE_COMMAND}
            "-DLINKER=${CMAKE_LINKER}"
            "-DRUNTIME_LIBRARY=$<TARGET_FILE:NyxEngine>"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/RuntimeInspectionSymbols.cmake")
endif()
