#version 450

layout(push_constant) uniform SelectionMaskPushConstants
{
    mat4 uModel;
    float uSelectedValue;
};

layout(location = 0) out vec4 outMask;

void main()
{
    outMask = vec4(uSelectedValue, 0.0, 0.0, 1.0);
}