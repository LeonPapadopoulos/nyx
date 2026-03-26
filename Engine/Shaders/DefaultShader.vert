#version 450

layout(binding = 0) uniform SceneUBO
{
    mat4 uViewProj;
    mat4 uInvViewProj;
    mat4 uModel;
    vec2 uViewportSize;
    vec2 _Padding0;
    vec3 uCameraWorldPos;
    float _Padding1;
};

layout(location = 0) in vec3 aPosition;
layout(location = 1) in vec3 aColor;

layout(location = 0) out vec3 vColor;

void main()
{
    gl_Position = uViewProj * uModel * vec4(aPosition, 1.0);
    vColor = aColor;
}