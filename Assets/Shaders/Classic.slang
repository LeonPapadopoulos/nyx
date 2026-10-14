// The look the engine's cubes had before the Standard material: a texture lit by the sun, mixed
// with the sky's reflection, more so at grazing angles.

import nyx.surface;

struct Classic : ISurface
{
    [Category("Surface"), Default("White")] nyx::Texture Albedo;
    [Category("Surface"), Range(0, 1)] float Reflectivity = 0.0;

    SurfaceOutput shade(SurfaceInput input)
    {
        const float4 albedo = Albedo.Sample(nyx::LinearWrap, input.UV);

        const float3 N = input.NormalWS;
        const float3 V = normalize(View.CameraPosition.xyz - input.PositionWS);
        const float3 reflected = View.Environment.Sample(nyx::LinearClamp, reflect(-V, N)).rgb;
        const float fresnel = pow(1.0 - max(dot(N, V), 0.0), 5.0);
        const float reflectivity = clamp(Reflectivity + fresnel * 0.5, 0.0, 1.0);

        // The domain lights the base colour and adds the emission: the lit texture, mixed with
        // the reflection
        SurfaceOutput o = SurfaceOutput::defaults(input);
        o.BaseColor = albedo.rgb * (1.0 - reflectivity);
        o.Emission = reflected * reflectivity;
        o.Opacity = albedo.a;
        return o;
    }
}
