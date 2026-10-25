// A view's colour grading baked into its lookup table (VulkanColorGrading), when its grade changes:
// each texel the tonemapped picture's colour it stands for, sRGB-encoded, graded. The tonemap reads
// the table, one lookup a pixel. ColorGrading.h has the same math on the CPU (GradeColor()).

import nyx.color;

// RGBA: a core storage format
[vk::binding(0, 0)] [vk::image_format("rgba16f")] RWTexture3D<float4> Table;

// ColorGradingConstants in ColorGrading.h: a grade in range
struct ColorGradingConstants
{
    // The white balance, rows of a matrix on linear light
    float4 WhiteBalance[3];
    // xyz: the lift; w: the contrast
    float4 LiftContrast;
    // xyz: 1 / the gamma; w: the saturation
    float4 InverseGammaSaturation;
    // xyz: the gain; w: the tint balance
    float4 GainBalance;
    // xyz: what the shadows' and the highlights' light is multiplied by at most, less 1
    float4 ShadowTint;
    float4 HighlightTint;
}
[vk::push_constant] ConstantBuffer<ColorGradingConstants> Grade;

// ColorGradingTableSize in ColorGrading.h
static const uint TableSize = 32;
// Mid grey, sRGB-encoded (0.18 linear): what contrast turns round
static const float ContrastPivot = 0.46135612;

// Darks darker and lights lighter round mid grey, 0 and 1 kept: a power curve each side
float3 contrast(float3 shown, float amount)
{
    const float3 below = ContrastPivot * pow(shown / ContrastPivot, amount);
    const float3 above = 1.0 - (1.0 - ContrastPivot) * pow((1.0 - shown) / (1.0 - ContrastPivot), amount);
    return select(shown < ContrastPivot, below, above);
}

// A colour of the tonemapped picture, sRGB-encoded, graded: white balance, contrast, lift, gamma and
// gain, split toning, saturation
float3 gradeColor(float3 encoded)
{
    // The white balance, on light
    float3 light = nyx::srgbToLinear(saturate(encoded));
    light = max(float3(dot(Grade.WhiteBalance[0].xyz, light), dot(Grade.WhiteBalance[1].xyz, light), dot(Grade.WhiteBalance[2].xyz, light)), 0.0);

    // Contrast, lift, gamma and gain, on what is shown
    float3 shown = contrast(nyx::linearToSrgb(light), Grade.LiftContrast.w);
    shown = Grade.GainBalance.xyz * (shown + Grade.LiftContrast.xyz * (1.0 - shown));
    shown = pow(max(shown, 0.0), Grade.InverseGammaSaturation.xyz);

    // The shadows and the highlights tinted apart, by how bright each is shown
    light = nyx::srgbToLinear(shown);
    const float bright = saturate(nyx::linearToSrgb(nyx::luminance(light).xxx).x + 0.5 * Grade.GainBalance.w);
    const float highlights = bright * bright * (3.0 - 2.0 * bright);
    light *= (1.0 + Grade.ShadowTint.xyz * (1.0 - highlights)) * (1.0 + Grade.HighlightTint.xyz * highlights);

    // Saturation, the luminance kept
    const float luminance = nyx::luminance(light);
    light = luminance + Grade.InverseGammaSaturation.w * (light - luminance);
    return nyx::linearToSrgb(max(light, 0.0));
}

[shader("compute")]
[numthreads(4, 4, 4)]
void colorGradingBake(uint3 texel : SV_DispatchThreadID)
{
    if (any(texel >= TableSize))
    {
        return;
    }
    Table[texel] = float4(gradeColor(float3(texel) / float(TableSize - 1)), 1.0);
}
