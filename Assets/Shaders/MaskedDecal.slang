// A masked decal, for surface decals (SurfaceDecalComponent): a mask texture, white on black,
// drawn with soft, antialiased edges and an optional glow around them, tinted by the decal's colour
// times its intensity. The decal's Reveal uncovers the mask along a direction, out from its middle
// or round it, like a wipe; its Pulse makes it throb. It gives off light and covers nothing, unless
// Cover says so. On a mesh, as in a material's preview, it shows white, wholly revealed, on black.

import nyx.surface;

struct MaskedDecal : ISurface
{
    // A mask's grey: data, read as stored. Black unset: nothing is drawn.
    [Category("Mask"), Default("Black"), Linear, Tooltip("White on black; edges that fade over a few texels make the smoothest edges")] nyx::Texture Mask;
    [Category("Mask"), Range(0, 1), Tooltip("The mask's grey where its edge is")] float Threshold = 0.5;
    [Category("Mask"), Range(0.5, 8), Tooltip("Across how many pixels the edge fades: 1 is just antialiased")] float Softness = 1.0;
    [Category("Mask"), Range(0, 1), Tooltip("How much the mask covers what is beneath it: 0 only adds light")] float Cover = 0.0;

    [Category("Glow"), Range(0, 4), Tooltip("How bright the glow around the mask is, against the mask; 0 for none")] float GlowStrength = 0.6;
    [Category("Glow"), Range(0, 8), Tooltip("How far the glow reaches: the mask's mip level it is read from, each level twice as far")] float GlowReach = 4.0;

    [Category("Reveal"), Enum("Along a direction, Out from the middle, Round the middle"), Tooltip("How Reveal uncovers the decal as it goes from 0 to 1")] uint RevealMode = 2;
    [Category("Reveal"), DragSpeed(0.01), Tooltip("Along a direction: where the decal is uncovered towards, in UV")] float2 RevealDirection = float2(1.0, 0.0);
    [Category("Reveal"), Range(0, 1), Tooltip("Round the middle: where it starts, in turns from the top, clockwise")] float RevealStart = 0.0;
    [Category("Reveal"), Range(0.01, 1), Tooltip("How wide the edge between uncovered and covered is")] float RevealSoftness = 0.08;
    [Category("Reveal"), Range(0, 8), Tooltip("How much brighter the edge is while it moves")] float RevealEdgeGlow = 2.0;

    [Category("Pulse"), DragSpeed(0.01), Tooltip("Throbs a second")] float PulseRate = 1.0;

    // 0 where the reveal starts, 1 where it ends
    float revealProgress(float2 uv)
    {
        const float2 fromMiddle = uv - 0.5;
        switch (RevealMode)
        {
        case 1:
            // The corners last
            return saturate(length(fromMiddle) / 0.70710678);
        case 2:
        {
            // Clockwise from the top as seen on the surface: v runs down the mask
            const float turns = atan2(fromMiddle.x, -fromMiddle.y) / (2.0 * 3.14159265) + 0.5;
            return frac(turns + 0.5 - RevealStart);
        }
        default:
        {
            const float2 direction = length(RevealDirection) > 1e-4 ? normalize(RevealDirection) : float2(1.0, 0.0);
            // The square's corners farthest back and ahead along the direction are 0 and 1
            const float reach = 0.5 * (abs(direction.x) + abs(direction.y));
            return saturate(dot(fromMiddle, direction) / (2.0 * reach) + 0.5);
        }
        }
    }

    SurfaceOutput shade(SurfaceInput input)
    {
        // The mask's grey against the threshold, over as many pixels as Softness, however near or
        // far the decal is
        const float grey = Mask.Sample(nyx::LinearClamp, input.UV).r;
        const float edgeWidth = max(fwidth(grey) * Softness, 1e-4);
        const float covered = saturate((grey - Threshold) / edgeWidth + 0.5);

        // The glow: the mask blurred by its mips, outside it
        const float blurred = Mask.SampleLevel(nyx::LinearClamp, input.UV, GlowReach).r;
        const float glow = saturate(blurred * 2.0) * (1.0 - covered) * GlowStrength;

        // Uncovered up to the reveal, nothing at 0, all at 1; the edge between brighter
        const DecalInput decal = input.Decal;
        const float reveal = saturate(decal.Reveal);
        const float uncovered = saturate((reveal * (1.0 + RevealSoftness) - revealProgress(input.UV)) / RevealSoftness);
        const float edge = uncovered * (1.0 - uncovered) * 4.0 * RevealEdgeGlow;

        // Throbbing from full brightness down to 1 - Pulse and back
        const float throb = 1.0 - saturate(decal.Pulse) * (0.5 - 0.5 * cos(2.0 * 3.14159265 * PulseRate * View.Time));

        SurfaceOutput o = SurfaceOutput::defaults(input);
        o.Unlit = true;
        o.BaseColor = float3(0.0);
        o.Emission = decal.Color * decal.Intensity * throb * ((covered * (1.0 + edge) + glow) * uncovered);
        o.Opacity = covered * uncovered * Cover;
        // A click picks the mask where it is uncovered, not the glow around it
        o.Coverage = covered * uncovered;
        return o;
    }
}
