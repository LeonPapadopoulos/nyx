// A material with an array after two other fields. Slang 2026.1 crashes generating code for it,
// so the compiler refuses it before generating any (NyxShaderCompilerTests). When a Slang update
// fixes the crash, arrays can be allowed again.
import nyx.surface;

struct WithArray : ISurface
{
    float A;
    float B;
    float Weights[3];

    SurfaceOutput shade(SurfaceInput input) { return SurfaceOutput::defaults(input); }
}
