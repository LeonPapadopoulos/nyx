#version 450
// GLSL with a typo on line 9, for the diagnostics test. Keep the line and column of 'colr' in
// step with ShaderCompilerTests.cpp.

layout(location = 0) out vec4 outColor;

void main()
{
    vec4 color = colr;
    outColor = color;
}
