// The wind of the view's world, for what moves with it as it is drawn: grass, leaves, trees. The
// world's wind (View.Wind, sampled by nyx.wind's contract) and, on top of it, a game's local air
// round the camera (View.LocalWind), as the CPU's particles take them (Docs/Design/wind.md). A
// material that sways reads nyx::windAt() and keeps only how it answers: how far it bends, how
// stiff it is, how it flutters.

import nyx.bindless;
import nyx.view;
import nyx.wind;

namespace nyx
{
    // What a game's local air adds at `position` (metres, in the world), in metres a second: 0
    // without its texture or outside it, fading to 0 over its outermost cells so nothing shows its
    // edge
    public float3 localWindAt(float3 position)
    {
        if (View.LocalWind.Index == 0)
        {
            return float3(0.0, 0.0, 0.0);
        }
        const float3 uvw = (position - View.LocalWindOrigin.xyz) * View.LocalWindScale.xyz;
        if (any(uvw < 0.0) || any(uvw > 1.0))
        {
            return float3(0.0, 0.0, 0.0);
        }
        // The cells across each axis, and how far into them the point is from the nearest side
        const float3 cells = View.LocalWindOrigin.w / View.LocalWindScale.xyz;
        const float3 inward = min(uvw, 1.0 - uvw) * cells;
        // Along y, only the top and bottom cell fade: a texture only a few layers high stays whole
        const float fade = saturate(min(min(inward.x, inward.z) - 0.5, (inward.y - 0.25) * 2.0));
        return View.LocalWind.SampleLevel(nyx::LinearClamp, uvw, 0.0).xyz * fade;
    }

    // The air's velocity at `position` as the view draws it, in metres a second: the world's wind at
    // the world's `time` (a vertex function's SurfaceVertex.Time; View.Time the frame's own, another
    // carries its fronts and eddies on or back at their rates) and a game's local air on top, as it
    // is this frame
    public float3 windAt(float3 position, float time)
    {
        return nyx::sampleWind(View.Wind, position, time) + localWindAt(position);
    }
}
