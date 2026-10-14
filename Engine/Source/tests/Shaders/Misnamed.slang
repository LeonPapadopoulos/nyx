// A material whose struct isn't named like its file: the compiler says so.
import nyx.surface;

struct Pebble : ISurface
{
    float Roughness = 0.5;

    SurfaceOutput shade(SurfaceInput input) { return SurfaceOutput::defaults(input); }
}
