// Colour: sRGB, luminance and tonemapping (Design/shader-materials.md). Linear values are light;
// the 8-bit pictures the editor shows hold sRGB-encoded values.

namespace nyx
{
    // Rec. 709 / sRGB primaries
    public float luminance(float3 linear)
    {
        return dot(linear, float3(0.2126, 0.7152, 0.0722));
    }

    public float3 linearToSrgb(float3 linear)
    {
        const float3 c = saturate(linear);
        return select(c <= 0.0031308, c * 12.92, 1.055 * pow(c, 1.0 / 2.4) - 0.055);
    }

    public float3 srgbToLinear(float3 encoded)
    {
        return select(encoded <= 0.04045, encoded / 12.92, pow((encoded + 0.055) / 1.055, 2.4));
    }

    // Khronos PBR Neutral (glTF's): colours up to 0.76 pass through all but unchanged, brighter
    // ones are compressed towards white, keeping their hue. As the glTF Sample Viewer shows them.
    public float3 tonemapPbrNeutral(float3 color)
    {
        const float startCompression = 0.8 - 0.04;
        const float desaturation = 0.15;

        const float x = min(color.r, min(color.g, color.b));
        const float offset = x < 0.08 ? x - 6.25 * x * x : 0.04;
        color -= offset;

        const float peak = max(color.r, max(color.g, color.b));
        if (peak < startCompression)
        {
            return color;
        }

        const float d = 1.0 - startCompression;
        const float newPeak = 1.0 - d * d / (peak + d - startCompression);
        color *= newPeak / peak;

        const float g = 1.0 - 1.0 / (desaturation * (peak - newPeak) + 1.0);
        return lerp(color, float3(newPeak), g);
    }
}
