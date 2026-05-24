#version 450

layout(binding = 0) uniform SceneUBO
{
    mat4 uViewProj;
    mat4 uInvViewProj;
    vec2 uViewportSize;
    vec2 _Padding0;
    vec4 uCameraWorldPos;
    vec4 uLightDirectionWS;
    vec4 uLightColor;
};

layout(push_constant) uniform PickingPushConstants
{
    mat4 uModel;
    uint uEncodedEntityId;
};

layout(location = 0) in vec3 aPosition;

void main()
{
    gl_Position = uViewProj * uModel * vec4(aPosition, 1.0);
}