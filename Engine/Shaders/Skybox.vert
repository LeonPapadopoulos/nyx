#version 450

layout(binding = 0) uniform SkyboxUBO
{
    mat4 uViewProj;
};

layout(location = 0) in vec3 aPosition;

layout(location = 0) out vec3 vDirection;

void main()
{
    vDirection = aPosition;

    vec4 pos = uViewProj * vec4(aPosition, 1.0);

    // Force skybox to the far plane
    gl_Position = pos.xyww;
}