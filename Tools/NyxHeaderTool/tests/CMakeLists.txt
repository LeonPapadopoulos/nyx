add_executable(NyxHeaderToolTests
    TestDriver.cpp
)

target_compile_features(NyxHeaderToolTests PRIVATE cxx_std_20)

add_test(
    NAME NyxHeaderToolTests
    COMMAND NyxHeaderToolTests $<TARGET_FILE:NyxHeaderTool> ${CMAKE_CURRENT_SOURCE_DIR}/fixtures
)