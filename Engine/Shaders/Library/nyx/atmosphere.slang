// The world's air (EnvironmentComponent; Atmosphere.h works out the same on the CPU): height fog
// between the camera and what it sees, thick near the ground and thinning upwards, which takes
// the light of what is behind it away and adds its own, glowing towards the sun; and the sky's
// tint and its glow round the sun. Without an environment, the fog's density is 0 and the tint
// white: nothing changes. Under the sky made by the air's scattering (nyx.sky), the fog's own
// colour is that sky's the way the ray looks (at the horizon for a ray below it), so that the fog
// and the sky agree.

import nyx.sky;
import nyx.view;

namespace nyx
{
    // How far away the sky is for the fog, in metres (SkyFogDistance)
    public static const float SkyFogDistance = 10000.0;

    // Henyey and Greenstein's phase function: how much light scattered once goes off at an angle
    // whose cosine is `cosine`, for anisotropy `g`; over the sphere it sums to 1
    public float henyeyGreenstein(float cosine, float g)
    {
        const float g2 = g * g;
        const float denominator = max(1.0 + g2 - 2.0 * g * cosine, 1e-6);
        return (1.0 - g2) / (4.0 * 3.14159265358979 * denominator * sqrt(denominator));
    }

    // The fog along a ray `reach` metres long that starts `startHeight` up and climbs `dirY` a
    // metre: the density integrated along it as it thins (or thickens) with the height, the
    // exponents held below 80 (FogDepthAlong() in Atmosphere.cpp)
    float fogDepthAlong(float density, float falloff, float fogHeight, float startHeight, float dirY, float reach)
    {
        const float atStart = -falloff * (startHeight - fogHeight);
        const float x = falloff * dirY * reach;
        if (abs(x) < 1e-3)
        {
            return density * exp(min(atStart, 80.0)) * reach * (1.0 - x * 0.5 + x * x / 6.0);
        }
        const float atEnd = atStart - x;
        const float larger = max(atStart, atEnd);
        return density * exp(min(larger, 80.0)) * (exp(atStart - larger) - exp(atEnd - larger)) / (falloff * dirY);
    }

    // The fog along a ray: how much of the light from its end gets through, and what the fog adds
    public struct Fog
    {
        public float Transmittance;
        public float3 Inscattering;

        // A colour seen through the fog
        public float3 apply(float3 color)
        {
            return color * Transmittance + Inscattering * (1.0 - Transmittance);
        }

        // A colour already weighed by its opacity, as a decal blends it (premultiplied alpha)
        public float3 applyPremultiplied(float3 color, float opacity)
        {
            return color * Transmittance + Inscattering * ((1.0 - Transmittance) * opacity);
        }
    }

    // The fog from the camera along `direction` (normalised) for `distance` metres, from where it
    // starts (its start distance)
    public Fog heightFogAlong(float3 direction, float distance)
    {
        Fog fog;
        fog.Transmittance = 1.0;
        fog.Inscattering = float3(0.0);
        const float density = View.FogColor.a;
        const float startDistance = View.FogShape.z;
        if (density <= 0.0 || distance <= startDistance)
        {
            return fog;
        }

        const float startHeight = View.CameraPosition.y + direction.y * startDistance;
        const float depth = fogDepthAlong(density, View.FogShape.x, View.FogShape.y, startHeight, direction.y, distance - startDistance);
        fog.Transmittance = max(exp(-depth), 1.0 - View.FogShape.w);
        const float3 own = hasSkyAtmosphere() ? skyAtmosphereFogColor(direction) * View.FogColor.rgb : View.FogColor.rgb;
        fog.Inscattering = own + View.FogSun.rgb * henyeyGreenstein(dot(direction, normalize(View.LightDirection.xyz)), View.FogSun.w);
        return fog;
    }

    // The fog between the camera and a point of the world
    public Fog heightFog(float3 positionWS)
    {
        const float3 ray = positionWS - View.CameraPosition.xyz;
        const float distance = length(ray);
        return heightFogAlong(distance > 0.0 ? ray / distance : float3(0.0, 0.0, -1.0), distance);
    }

    // The fog in front of the sky, looking along `direction` (normalised): thickest on the
    // horizon, where a ray stays low longest
    public Fog skyFog(float3 direction)
    {
        return heightFogAlong(direction, SkyFogDistance);
    }

    // What the sky and its light are multiplied by: the sun's hue and the daylight
    public float3 skyTint()
    {
        return View.SkyTint.rgb;
    }

    // The sky's glow round the sun, looking along `direction` (normalised)
    public float3 sunGlow(float3 direction)
    {
        return View.SunGlow.rgb * henyeyGreenstein(dot(direction, normalize(View.LightDirection.xyz)), View.SunGlow.w);
    }
}
