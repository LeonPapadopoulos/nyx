# NyxShaderCompiler.exe: compiles shaders for live reload, in a process of its own
# (RemoteShaderCompileService), so a crash of the compiler doesn't end the editor or the game.
add_executable(NyxShaderCompiler
    main.cpp
)

target_link_libraries(NyxShaderCompiler
    PRIVATE NyxEngine
)
add_dependencies(NyxShaderCompiler NyxGeneratedReflection)
set_property(TARGET NyxShaderCompiler PROPERTY FOLDER "Tools")
nyx_copy_shader_compiler_next_to(NyxShaderCompiler)
