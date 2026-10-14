// A material with a field that has no default value: it starts at 0 (the compiler's sessions
// zero-initialise). NyxShaderCompilerGpuTests checks it.
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
