#version 450

layout(location = 0) noperspective out vec2 vNdc;

vec2 positions[3] = vec2[](
    vec2(-1.0, -1.0),
    vec2( 3.0, -1.0),
    vec2(-1.0,  3.0)
);

void main()
{
    vec2 pos = positions[gl_VertexIndex];
    vNdc = pos;
    gl_Position = vec4(pos, 0.0, 1.0);
}