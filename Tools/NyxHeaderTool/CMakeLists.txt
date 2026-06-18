add_executable(NyxHeaderTool
    src/main.cpp
    src/main_regex.cpp
    src/Lexer.cpp
    src/Parser.cpp
    src/CodeGenerator.cpp

    src/Lexer.h
    src/Token.h
    src/Parser.h
    src/ReflectionAst.h
    src/CodeGenerator.h
)

target_compile_features(NyxHeaderTool PRIVATE cxx_std_20)
target_include_directories(NyxHeaderTool PRIVATE src)