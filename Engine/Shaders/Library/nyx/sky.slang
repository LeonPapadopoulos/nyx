// The sky made by the air's scattering (Hillaire 2020; VulkanSkyAtmosphere makes its tables,
// SkyAtmosphere.h has the same math on the CPU): how the transmittance and sky view tables are
// laid out, for the passes that make them and the pixels that read them; and, while a view draws
// that sky (View.SkyAtmosphere), the sky, the sun's disk and the horizon's colour for the fog.
//
// The air's math is in kilometres round a planet whose top the camera is always above: heights
// above the ground are carried as they are, never as the difference of two radii, which a single
// float can't hold a metre apart at 6360 km.

import nyx.bindless;
import nyx.view;

namespace nyx
{
    public static const float SkyPi = 3.14159265358979;

    // The tables' sizes (VulkanSkyAtmosphere's)
    public static const float TransmittanceLutWidth = 256.0;
    public static const float TransmittanceLutHeight = 64.0;
    public static const float MultipleScatteringLutSize = 32.0;
    public static const float SkyViewLutWidth = 192.0;
    public static const float SkyViewLutHeight = 108.0;

    // A planet and its air: their radii, in kilometres
    public struct SkyPlanet
    {
        public float Radius;
        public float Top;
    }

    // From a 0 to 1 coordinate to the middles of a table's first and last texels, and back: so that
    // the ends are sampled where they were made (Hillaire's sub-UVs)
    public float unitToSubUv(float u, float resolution)
    {
        return (u + 0.5 / resolution) * (resolution / (resolution + 1.0));
    }

    public float subUvToUnit(float u, float resolution)
    {
        return (u - 0.5 / resolution) * (resolution / (resolution - 1.0));
    }

    // How far a point `height` up is from the planet's middle, squared, less the planet's radius
    // squared: exactly, by the height
    public float skyHeightSquare(SkyPlanet planet, float height)
    {
        return height * (2.0 * planet.Radius + height);
    }

    // The cosine of the horizon's angle from the up axis, `height` up: 0 on the ground, below 0
    // higher up (GetHorizonCosine())
    public float skyHorizonCosine(SkyPlanet planet, float height)
    {
        return -sqrt(skyHeightSquare(planet, max(height, 0.0))) / (planet.Radius + height);
    }

    // How far a ray from `height` up, `cosZenith` from the up axis, goes to the top of the air
    public float skyDistanceToTop(SkyPlanet planet, float height, float cosZenith)
    {
        const float r = planet.Radius + height;
        const float b = r * cosZenith;
        const float airHeight = planet.Top - planet.Radius;
        const float c = (height - airHeight) * (2.0 * planet.Radius + height + airHeight);
        return max(-b + sqrt(max(b * b - c, 0.0)), 0.0);
    }

    // How far it goes to the ground, or -1 if it misses it
    public float skyDistanceToGround(SkyPlanet planet, float height, float cosZenith)
    {
        if (cosZenith >= skyHorizonCosine(planet, height))
        {
            return -1.0;
        }
        const float r = planet.Radius + height;
        const float b = r * cosZenith;
        const float c = skyHeightSquare(planet, height);
        // The nearer root, without the cancellation of -b - sqrt(b² - c)
        return c / max(-b + sqrt(max(b * b - c, 0.0)), 1e-9);
    }

    // The height of the point `t` kilometres along a ray from `height` up, `cosZenith` from the up
    // axis: (|p|² - radius²) / (|p| + radius), the square's difference taken by the heights
    public float skyHeightAlong(SkyPlanet planet, float height, float cosZenith, float t)
    {
        const float r = planet.Radius + height;
        const float q = skyHeightSquare(planet, height) + t * (t + 2.0 * r * cosZenith);
        const float length = sqrt(max(planet.Radius * planet.Radius + q, 0.0));
        return q / (length + planet.Radius);
    }

    // The transmittance table, laid out as the 2008 precomputed scattering's: u by the distance to
    // the top of the air, between its least and most for the height, v by the height's distance to
    // the horizon
    public float2 transmittanceUv(SkyPlanet planet, float height, float cosZenith)
    {
        const float h = sqrt(skyHeightSquare(planet, planet.Top - planet.Radius));
        const float rho = sqrt(skyHeightSquare(planet, max(height, 0.0)));
        const float d = skyDistanceToTop(planet, height, cosZenith);
        const float dMin = planet.Top - planet.Radius - height;
        const float dMax = rho + h;
        return float2((d - dMin) / max(dMax - dMin, 1e-6), rho / h);
    }

    public void transmittanceParams(SkyPlanet planet, float2 uv, out float height, out float cosZenith)
    {
        const float h = sqrt(skyHeightSquare(planet, planet.Top - planet.Radius));
        const float rho = h * uv.y;
        const float r = sqrt(rho * rho + planet.Radius * planet.Radius);
        height = rho * rho / (r + planet.Radius);
        const float dMin = planet.Top - planet.Radius - height;
        const float dMax = rho + h;
        const float d = dMin + uv.x * (dMax - dMin);
        cosZenith = d == 0.0 ? 1.0 : clamp((h * h - rho * rho - d * d) / (2.0 * r * d), -1.0, 1.0);
    }

    // The multiple scattering table: u by the sun's cosine with the up axis, v by the height
    public float2 multipleScatteringUv(SkyPlanet planet, float height, float sunCosZenith)
    {
        const float2 uv = saturate(float2(sunCosZenith * 0.5 + 0.5, height / (planet.Top - planet.Radius)));
        return float2(unitToSubUv(uv.x, MultipleScatteringLutSize), unitToSubUv(uv.y, MultipleScatteringLutSize));
    }

    // The sky view table round a camera `height` up: u by the angle round the up axis from the sun
    // (0 towards it, 1 away; the sky is the same either side), squared so that more of it is near
    // the sun; v from the zenith (0) through the horizon (0.5) to the nadir (1), most of it near the
    // horizon, wherever the horizon is at that height
    public float2 skyViewUv(SkyPlanet planet, float height, float viewCosZenith, float lightViewCosine, bool bGround)
    {
        const float rho = sqrt(skyHeightSquare(planet, max(height, 0.0)));
        const float beta = acos(clamp(rho / (planet.Radius + height), -1.0, 1.0));
        const float zenithHorizonAngle = SkyPi - beta;
        float2 uv;
        if (!bGround)
        {
            float coord = acos(clamp(viewCosZenith, -1.0, 1.0)) / zenithHorizonAngle;
            coord = 1.0 - sqrt(max(1.0 - coord, 0.0));
            uv.y = coord * 0.5;
        }
        else
        {
            const float coord = (acos(clamp(viewCosZenith, -1.0, 1.0)) - zenithHorizonAngle) / max(beta, 1e-6);
            uv.y = sqrt(saturate(coord)) * 0.5 + 0.5;
        }
        uv.x = sqrt(saturate(-lightViewCosine * 0.5 + 0.5));
        return float2(unitToSubUv(uv.x, SkyViewLutWidth), unitToSubUv(uv.y, SkyViewLutHeight));
    }

    public void skyViewParams(SkyPlanet planet, float height, float2 uv, out float viewCosZenith, out float lightViewCosine)
    {
        uv = float2(subUvToUnit(uv.x, SkyViewLutWidth), subUvToUnit(uv.y, SkyViewLutHeight));
        const float rho = sqrt(skyHeightSquare(planet, max(height, 0.0)));
        const float beta = acos(clamp(rho / (planet.Radius + height), -1.0, 1.0));
        const float zenithHorizonAngle = SkyPi - beta;
        if (uv.y < 0.5)
        {
            float coord = 1.0 - 2.0 * uv.y;
            coord = 1.0 - coord * coord;
            viewCosZenith = cos(zenithHorizonAngle * coord);
        }
        else
        {
            float coord = uv.y * 2.0 - 1.0;
            viewCosZenith = cos(zenithHorizonAngle + beta * coord * coord);
        }
        const float coord = uv.x * uv.x;
        lightViewCosine = -(coord * 2.0 - 1.0);
    }

    // The cosine of the angle round the up axis (y) between `direction` and `towards`: 1 for the
    // same side, also where either is straight up or down
    public float skyAzimuthCosine(float3 direction, float3 towards)
    {
        const float2 d = direction.xz;
        const float2 s = towards.xz;
        const float lengths = sqrt(dot(d, d) * dot(s, s));
        return lengths > 1e-8 ? clamp(dot(d, s) / lengths, -1.0, 1.0) : 1.0;
    }

    // -------------------------------------------------------------------------
    // The view's sky, while it draws the air's (View.SkyAtmosphere)
    // -------------------------------------------------------------------------

    public bool hasSkyAtmosphere()
    {
        return View.SkyAtmosphere != 0;
    }

    SkyPlanet viewPlanet()
    {
        SkyPlanet planet;
        planet.Radius = View.PlanetRadius;
        planet.Top = View.AtmosphereTopRadius;
        return planet;
    }

    // The sky's light the camera sees along `direction` (normalised), without the sun's disk
    public float3 skyAtmosphereRadiance(float3 direction)
    {
        const SkyPlanet planet = viewPlanet();
        const float height = View.SkyCamera.x;
        const bool bGround = direction.y < skyHorizonCosine(planet, height);
        const float2 uv = skyViewUv(planet, height, direction.y, skyAzimuthCosine(direction, normalize(View.LightDirection.xyz)), bGround);
        return View.SkyViewLut.SampleLevel(LinearClamp, uv, 0.0).rgb;
    }

    // The part of the light from the top of the air that reaches the camera along `direction`
    public float3 skyAtmosphereTransmittance(float3 direction)
    {
        const SkyPlanet planet = viewPlanet();
        const float height = View.SkyCamera.x;
        if (direction.y < skyHorizonCosine(planet, height))
        {
            return float3(0.0);
        }
        return View.TransmittanceLut.SampleLevel(LinearClamp, transmittanceUv(planet, height, direction.y), 0.0).rgb;
    }

    // The sun's disk along `direction`: its light through the air, darker and redder towards its
    // edge (limb darkening, as Hillaire's Frostbite sky has it), its edge smoothed over a pixel
    public float3 sunDiskRadiance(float3 direction)
    {
        const float3 sun = normalize(View.LightDirection.xyz);
        const float cosine = dot(direction, sun);
        // The sine of the angle from its middle, exact for small angles, against its radius's
        const float sine = length(cross(direction, sun));
        const float radiusSine = sqrt(max(1.0 - View.SunDisk.a * View.SunDisk.a, 1e-12));
        const float x = sine / radiusSine;
        const float edge = max(fwidth(x), 1e-4);
        if (cosine <= 0.0 || x >= 1.0 + edge)
        {
            return float3(0.0);
        }
        const float mu = sqrt(max(1.0 - min(x * x, 1.0), 0.0));
        const float3 limb = pow(max(mu, 1e-4), float3(0.397, 0.503, 0.652));
        const float coverage = saturate((1.0 - x) / edge + 0.5);
        return View.SunDisk.rgb * limb * coverage * skyAtmosphereTransmittance(direction);
    }

    // The sky's colour at the horizon round the up axis the way `direction` looks: the row of the
    // sky view just above the horizon, warmer towards the sun. What the fog takes as its own.
    public float3 skyAtmosphereHorizon(float3 direction)
    {
        const float2 uv = float2(unitToSubUv(sqrt(saturate(-skyAzimuthCosine(direction, normalize(View.LightDirection.xyz)) * 0.5 + 0.5)), SkyViewLutWidth),
            (floor(0.5 * SkyViewLutHeight) - 0.5) / SkyViewLutHeight);
        return View.SkyViewLut.SampleLevel(LinearClamp, uv, 0.0).rgb;
    }
}
