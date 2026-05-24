#version 450

layout(push_constant) uniform PickingPushConstants
{
    mat4 uModel;
    uint uEncodedEntityId;
};

layout(location = 0) out uint outEntityId;

void main()
{
    outEntityId = uEncodedEntityId;
}