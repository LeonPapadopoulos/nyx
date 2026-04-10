#version 450

layout(binding = 1) uniform sampler2D uTexture;

layout(location = 0) in vec3 vColor;
layout(location = 1) in vec2 vUV;

layout(location = 0) out vec4 outColor;

void main()
{
    vec4 tex = texture(uTexture, vUV);
    outColor = tex * vec4(vColor, 1.0);
}