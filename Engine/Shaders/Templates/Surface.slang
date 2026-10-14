// A surface material: its fields are its parameters, which the Parameters window shows and the
// material file sets; shade() is its look. nyx.surface lights what shade() returns.
import nyx.surface;

struct NyxTemplateMaterial : ISurface
{
    [Category("Surface"), Color] float3 BaseColor = float3(0.8, 0.8, 0.8);
    [Category("Surface"), Range(0, 1)] float Metallic = 0.0;
    [Category("Surface"), Range(0, 1)] float Roughness = 0.5;

    SurfaceOutput shade(SurfaceInput input)
    {
        SurfaceOutput o = SurfaceOutput::defaults(input);
        o.BaseColor = BaseColor;
        o.Metallic = Metallic;
        o.Roughness = Roughness;
        return o;
    }
}
