add_executable(NyxHeaderTool
    src/main.cpp
    src/main_regex.cpp
    src/Lexer.cpp
    src/Parser.cpp
    src/ReflectionSemantics.cpp
    src/CodeGenerator.cpp
    src/SpecifierRegistry.cpp

    src/Lexer.h
    src/Token.h
    src/Parser.h
    src/ReflectionAst.h
    src/ReflectionSemantics.h
    src/CodeGenerator.h
    src/SpecifierRegistry.h
)

target_compile_features(NyxHeaderTool PRIVATE cxx_std_20)
target_include_directories(NyxHeaderTool PRIVATE src)

if(BUILD_TESTING)
    add_subdirectory(tests)
endif()