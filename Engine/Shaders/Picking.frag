#version 450

// ObjectConstants in nyx/bindless.slang and VulkanRenderer.h
layout(push_constant) uniform ObjectConstants
{
    mat4 uModel;
    uint uMaterialOffset;
    uint uEntityId;
    uvec2 _Padding;
    vec4 uCustomData;
};

layout(location = 0) out uint outEntityId;

void main()
{
    outEntityId = uEntityId;
}