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
// The instance's transform, relative to uModel: the second vertex stream, one mat4 per instance,
// the identity for an entity drawn once (VertexInput in nyx/surface.slang)
layout(location = 5) in vec4 aInstance0;
layout(location = 6) in vec4 aInstance1;
layout(location = 7) in vec4 aInstance2;
layout(location = 8) in vec4 aInstance3;

void main()
{
    gl_Position = uViewProj * uModel * mat4(aInstance0, aInstance1, aInstance2, aInstance3) * vec4(aPosition, 1.0);
}