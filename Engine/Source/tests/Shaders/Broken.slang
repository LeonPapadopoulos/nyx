// Stone with a typo on line 13, for the diagnostics test. Keep the line and column of
// 'Tilling' in step with ShaderCompilerTests.cpp.
import nyx.surface;

struct Broken : ISurface
{
    [Category("UV")] float2 Tiling = float2(1, 1);
    nyx::Texture Albedo;

    SurfaceOutput shade(SurfaceInput input)
    {
        SurfaceOutput o = SurfaceOutput::defaults(input);
        float2 uv = input.UV * Tilling;
        o.BaseColor = Albedo.Sample(nyx::LinearWrap, uv).rgb;
        return o;
    }
}
