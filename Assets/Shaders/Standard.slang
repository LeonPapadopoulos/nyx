// The standard surface: glTF's metallic-roughness material (Design/shader-materials.md,
// recommendation 6). Each texture multiplies its factor, as in glTF; unset, a texture changes
// nothing.

import nyx.surface;

struct Standard : ISurface
{
    [Category("Surface"), Color] float3 BaseColor = float3(1.0, 1.0, 1.0);
    [Category("Surface"), Default("White")] nyx::Texture BaseColorTexture;
    [Category("Surface"), Range(0, 1)] float Metallic = 0.0;
    [Category("Surface"), Range(0, 1)] float Roughness = 0.5;
    // Roughness in green, metallic in blue, as glTF packs them
    [Category("Surface"), Default("White"), Linear, DisplayName("Metallic Roughness Texture")] nyx::Texture MetallicRoughnessTexture;

    [Category("Normal"), Default("FlatNormal"), Linear] nyx::Texture NormalTexture;
    [Category("Normal"), DragSpeed(0.01), Tooltip("How deep the normal map's bumps are")] float NormalScale = 1.0;

    [Category("Occlusion"), Default("White"), Linear, Tooltip("How much of the sky's light reaches each texel, in red")] nyx::Texture OcclusionTexture;
    [Category("Occlusion"), Range(0, 1)] float OcclusionStrength = 1.0;

    [Category("Emission"), Color, HDR] float3 Emission = float3(0.0, 0.0, 0.0);
    [Category("Emission"), Default("White")] nyx::Texture EmissionTexture;

    [Category("Opacity"), Range(0, 1)] float Opacity = 1.0;
    // Cut out where the opacity falls below the cutoff; otherwise opaque. (Blended comes later.)
    [Category("Opacity"), Switch] bool Masked = false;
    [Category("Opacity"), Range(0, 1)] float AlphaCutoff = 0.5;

    [Category("UV"), DragSpeed(0.01)] float2 Tiling = float2(1.0, 1.0);

    SurfaceOutput shade(SurfaceInput input)
    {
        const float2 uv = input.UV * Tiling;
        const float4 base = BaseColorTexture.Sample(nyx::Anisotropic, uv) * float4(BaseColor, Opacity);
        if (Masked && base.a < AlphaCutoff)
        {
            discard;
        }

        const float4 metallicRoughness = MetallicRoughnessTexture.Sample(nyx::Anisotropic, uv);
        SurfaceOutput o = SurfaceOutput::defaults(input);
        o.BaseColor = base.rgb;
        o.Metallic = Metallic * metallicRoughness.b;
        o.Roughness = Roughness * metallicRoughness.g;
        o.Normal = nyx::normalFromMap(NormalTexture.Sample(nyx::Anisotropic, uv), input, NormalScale);
        o.Occlusion = lerp(1.0, OcclusionTexture.Sample(nyx::Anisotropic, uv).r, OcclusionStrength);
        o.Emission = Emission * EmissionTexture.Sample(nyx::Anisotropic, uv).rgb;
        o.Opacity = 1.0;
        return o;
    }
}
