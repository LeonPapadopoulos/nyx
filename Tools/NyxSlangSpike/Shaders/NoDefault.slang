// A material with a field that has no default value, for check 6: what is reported when the
// engine asks for its defaults.
import nyx.surface;

struct NoDefault : ISurface
{
    float Metallic;
    float Roughness = 0.5;

    SurfaceOutput shade(SurfaceInput input)
    {
        SurfaceOutput o = SurfaceOutput::defaults(input);
        o.Metallic = Metallic;
        o.Roughness = Roughness;
        return o;
    }
}
