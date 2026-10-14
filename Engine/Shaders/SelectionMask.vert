#version 450

// The view's set (nyx/view.slang); only the fields used here
layout(set = 1, binding = 0) uniform SceneUBO
{
    mat4 uViewProj;
    mat4 uInvViewProj;
    vec2 uViewportSize;
    vec2 _Padding0;
    vec4 uCameraWorldPos;
    vec4 uLightDirectionWS;
    vec4 uLightColor;
};

// ObjectConstants in nyx/bindless.slang and VulkanRenderer.h
layout(push_constant) uniform ObjectConstants
{
    mat4 uModel;
    uint uMaterialOffset;
    uint uEntityId;
    uvec2 _Padding;
    vec4 uCustomData;
};

layout(location = 0) in vec3 aPosition;

void main()
{
    gl_Position = uViewProj * uModel * vec4(aPosition, 1.0);
}