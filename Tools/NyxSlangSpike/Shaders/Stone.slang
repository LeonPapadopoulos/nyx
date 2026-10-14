// A material as a user writes it: fields are its parameters, shade() is its look.
import nyx.surface;

struct Stone : ISurface
{
    [Category("Surface"), Color]       float3 BaseColor = float3(0.62, 0.60, 0.56);
    [Category("Surface"), Range(0, 1)] float  Roughness = 0.78;
    [Category("Surface"), Range(0, 1)] float  Wetness   = 0.3;

    [Category("Textures"), Default("White")] nyx::Texture Albedo;

    [Category("UV"), DragSpeed(0.01)] float2 Tiling = float2(1, 1);

    SurfaceOutput shade(SurfaceInput input)
    {
        float2 uv = input.UV * Tiling;
        SurfaceOutput o = SurfaceOutput::defaults(input);
        float3 albedo = Albedo.Sample(nyx::LinearWrap, uv).rgb * BaseColor;
        o.BaseColor = lerp(albedo, albedo * 0.45, Wetness);
        o.Roughness = lerp(Roughness, 0.08, Wetness);
        return o;
    }
}
