#version 450
// GLSL that includes another file, for the dependencies test.
#include "Common.glsl"

layout(location = 0) out vec4 outColor;

void main()
{
    outColor = vec4(Brighten(vec3(0.25)), 1.0);
}
