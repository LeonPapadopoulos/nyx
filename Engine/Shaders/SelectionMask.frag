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

layout(location = 0) out vec4 outMask;

void main()
{
    // The renderer puts this object's value in the mask into the custom data
    outMask = vec4(uCustomData.x, 0.0, 0.0, 1.0);
}