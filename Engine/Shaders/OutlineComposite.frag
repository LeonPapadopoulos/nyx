#version 450

layout(binding = 0) uniform sampler2D uSelectionMask;

layout(push_constant) uniform OutlineCompositePushConstants
{
    vec2 uTexelSize;
    float uThickness;
    float _Padding0;
    vec4 uOutlineColor;
};

layout(location = 0) in vec2 vUV;
layout(location = 0) out vec4 outColor;

float SampleMask(vec2 uv)
{
    return texture(uSelectionMask, uv).r;
}

void main()
{
    float center = SampleMask(vUV);

    float left   = SampleMask(vUV + vec2(-uTexelSize.x * uThickness, 0.0));
    float right  = SampleMask(vUV + vec2( uTexelSize.x * uThickness, 0.0));
    float up     = SampleMask(vUV + vec2(0.0, -uTexelSize.y * uThickness));
    float down   = SampleMask(vUV + vec2(0.0,  uTexelSize.y * uThickness));

    float maxNeighbor = max(max(left, right), max(up, down));
    float edge = maxNeighbor - center;

    if (edge <= 0.001)
    {
        discard;
    }

    outColor = vec4(uOutlineColor.rgb, edge * uOutlineColor.a);
}