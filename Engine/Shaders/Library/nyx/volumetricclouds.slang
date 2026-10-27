// The volumetric clouds (VulkanVolumetricClouds; VolumetricClouds.h has the same band math on the
// CPU): a band between two shells round the planet, planet-centred as the sky's tables are, in
// kilometres, marched along a ray from the camera. A weather map laid over the ground says how much
// cloud there is (its coverage), of which type (how tall it grows) and how dense; 3D noise gives
// the shapes, worn at their edges by smaller noise, billowy at the top and wispy at the base. Each
// sample in a cloud is lit by the sun through the air (the transmittance table) and through the
// cloud towards it (Beer's law, a few samples), scattered once and again by octaves of a dual-lobe
// Henyey-Greenstein phase (the silver lining forwards, the multiple scattering's softer light in
// the shade), the powder of thin edges seen away from the sun; and by the sky above and the ground
// below. What the camera sees of them is faded by the air between (the transmittance table's
// ratio), so far off they melt into the sky behind them, which stands in for the haze's own light.
// The march is worked out from what describes the clouds (VolumetricCloudParams), not the view's
// set, so other passes (the sky's cube, later) can use it too.

import nyx.bindless;
import nyx.clouds;
import nyx.sky;
import nyx.view;

namespace nyx
{
    // EVolumetricCloudsDebug
    public static const uint VolumetricCloudDebugOff = 0;
    public static const uint VolumetricCloudDebugWeather = 1;
    public static const uint VolumetricCloudDebugSunLight = 2;
    public static const uint VolumetricCloudDebugSkyLight = 3;
    public static const uint VolumetricCloudDebugUpdates = 4;

    // The noises' texels a side (VolumetricClouds.h)
    static const float VolumetricCloudShapeTexels = 128.0;
    static const float VolumetricCloudDetailTexels = 32.0;
    // From which of the shapes' mips their field's values huddle round its middle, as the mip
    // averages blobs and gaps together, and how fast: spread out again by as much, so the clouds
    // cover as much far off as near (VolumetricCloudNoise.slang)
    static const float VolumetricCloudShapeHuddle = 3.1;
    static const float VolumetricCloudShapeHuddleRate = 0.75;
    // The orders of scattering summed: the first, and three of the multiple scattering's octaves,
    // each scattered more evenly and through more of the cloud than the one before (Wrenninge's
    // approximation, as Hillaire's Frostbite clouds have it)
    static const int VolumetricCloudOrders = 4;
    static const float VolumetricCloudOrderExtinction = 0.4;
    static const float VolumetricCloudOrderLobe = 0.5;
    // The lobes: forwards from 0.9 (tight) to 0.5 (wide) by the spread, and a weak one backwards
    static const float VolumetricCloudTightLobe = 0.9;
    static const float VolumetricCloudWideLobe = 0.5;
    static const float VolumetricCloudBackLobe = -0.2;
    // The light carried through a cloud's body by many scatterings at a Translucency of 1, against
    // the sun's, and how fast it falls off with the depth towards the sun (two-stream diffusion:
    // about 3/4 (1 - g) with the droplets' g, softened)
    static const float VolumetricCloudDiffused = 0.15;
    static const float VolumetricCloudDiffusion = 0.1;
    // How deep a thin edge is taken to be for its powder, in kilometres
    static const float VolumetricCloudPowderDepth = 0.06;
    // How far towards the sun a sample looks for its shadow, as a part of the band's thickness and
    // at most, in kilometres; one more sample twice as far for the shadow of a large cloud beyond
    static const float VolumetricCloudLightReach = 0.35;
    static const float VolumetricCloudMostLightReach = 1.5;
    // How much of the sky's light reaches the band's bottom and top, and of the ground's from below
    static const float VolumetricCloudSkyBelow = 0.15;
    static const float VolumetricCloudSkyAbove = 0.7;
    static const float VolumetricCloudGroundLight = 0.25;
    // The cells (Cellularity): the shapes' cellular noise from which they start and where they are
    // whole; how far the curl noise swirls the details, in their repeats; the finer details' size
    // against the details' and their share of the wear
    static const float VolumetricCloudCellEdge = 0.25;
    static const float VolumetricCloudCellMiddle = 0.7;
    static const float VolumetricCloudSwirl = 0.3;
    static const float VolumetricCloudFinerDetail = 3.07;
    static const float VolumetricCloudFinerWeight = 0.35;
    // From which of the details' mips (their 32 texels averaged to 2) they no longer show
    static const float VolumetricCloudDetailShown = 4.0;
    // What of the light still gets through where the clouds are taken to begin, and to all but end
    // (nine tenths hidden), for what stands among them (nyx::cloudCoverBefore())
    static const float VolumetricCloudFrontThrough = 0.98;
    static const float VolumetricCloudBackThrough = 0.1;
    // Where a ray's march stops, its light all but blocked
    static const float VolumetricCloudOpaque = 0.005;
    // The steps each coarse step through cloud is taken in again
    static const int VolumetricCloudFineSteps = 4;

    // What the clouds along a ray are made from: the planet, the camera's height above the ground
    // (kilometres), the band as the view's set has it (ViewConstants.VolumetricCloudBand to
    // VolumetricCloudSun), the sun's light at the top of the air and the way to it, the steps, the
    // angle a pixel covers, the sky's light on them from above and below (a radiance), the noises
    // and the transmittance table in the bindless array
    public struct VolumetricCloudParams
    {
        public SkyPlanet Planet;
        public float CameraHeight;
        // Bottom, top, the farthest marched (kilometres), the extinction a kilometre where the clouds
        // are densest (Density times VolumetricCloudExtinction)
        public float4 Band;
        // Coverage, its variation, type, its variation
        public float4 Coverage;
        // Details, billowiness, edges' softness, brightness
        public float4 Erosion;
        // 1 / the weather map's, the shapes' and the details' repeats (kilometres), the sky's light
        public float4 Repeats;
        public float2 WeatherOrigin;
        public float2 ShapeOrigin;
        public float2 DetailOrigin;
        // Silver lining, its spread, powder, multiple scattering
        public float4 Lighting;
        public float3 SunlitTint;
        public float3 ShadowTint;
        // How much the air between hazes them (a power of its transmittance), how much their details
        // are swirled, how much they part into cells
        public float AerialPerspective;
        public float Wispiness;
        public float Cellularity;
        // How much of the sun's light the clouds carry through their bodies by scattering
        public float Translucency;
        public float3 SunLight;
        public float3 TowardsSun;
        public uint Steps;
        public uint LightSteps;
        public uint Debug;
        public float PixelAngle;
        public float3 SkyAbove;
        public float3 SkyBelow;
        public uint Shape;
        public uint Detail;
        public uint Curl;
        public uint Weather;
        public uint Transmittance;
    }

    // What the clouds along the view's rays are made from, by its set
    public VolumetricCloudParams viewVolumetricCloudParams()
    {
        VolumetricCloudParams p;
        p.Planet = viewPlanet();
        p.CameraHeight = cloudCameraHeight();
        p.Band = View.VolumetricCloudBand;
        p.Coverage = View.VolumetricCloudCoverage;
        p.Erosion = View.VolumetricCloudErosion;
        p.Repeats = View.VolumetricCloudRepeats;
        p.WeatherOrigin = View.VolumetricCloudOrigins.xy;
        p.ShapeOrigin = View.VolumetricCloudOrigins.zw;
        p.DetailOrigin = View.VolumetricCloudDetailOrigin.xy;
        p.Lighting = View.VolumetricCloudLighting;
        p.SunlitTint = View.VolumetricCloudSunlitTint.rgb;
        p.ShadowTint = View.VolumetricCloudShadowTint.rgb;
        p.AerialPerspective = View.VolumetricCloudShadowTint.w;
        p.Wispiness = View.VolumetricCloudSunlitTint.w;
        p.Cellularity = View.VolumetricCloudShaping.x;
        p.Translucency = View.VolumetricCloudShaping.y;
        p.SunLight = View.VolumetricCloudSun.rgb;
        p.TowardsSun = normalize(View.LightDirection.xyz);
        p.Steps = View.VolumetricCloudSteps;
        p.LightSteps = View.VolumetricCloudLightSteps;
        p.Debug = View.VolumetricCloudMode >> 8;
        // A pixel of the clouds' picture, across: the projection's height over its pixels
        p.PixelAngle = 2.0 * abs(View.ProjectionInfo.y) / max(View.VolumetricCloudDetailOrigin.w, 1.0);
        const TextureCube irradiance = View.SkyIrradiance.get();
        p.SkyAbove = irradiance.SampleLevel(LinearClamp, float3(0.0, 1.0, 0.0), 0.0).rgb;
        p.SkyBelow = irradiance.SampleLevel(LinearClamp, float3(0.0, -1.0, 0.0), 0.0).rgb;
        p.Shape = View.VolumetricCloudShape.Index;
        p.Detail = View.VolumetricCloudDetail.Index;
        p.Curl = View.VolumetricCloudCurl.Index;
        p.Weather = View.VolumetricCloudWeather.Index;
        p.Transmittance = View.TransmittanceLut.Index;
        return p;
    }

    // Where a ray `cosZenith` from the up axis is inside the band, from the camera: (enter, exit),
    // exit no further than enter where it never is (GetCloudBandSpan())
    public float2 volumetricCloudBandSpan(VolumetricCloudParams p, float cosZenith)
    {
        const float bottom = p.Band.x;
        const float top = p.Band.y;
        const CloudShellHits low = cloudShellHits(p.Planet, p.CameraHeight, cosZenith, bottom);
        const CloudShellHits high = cloudShellHits(p.Planet, p.CameraHeight, cosZenith, top);
        if (p.CameraHeight < bottom)
        {
            if (low.Count == 0 || high.Count == 0)
            {
                return float2(0.0);
            }
            return float2(low.Distances.x, high.Distances.x);
        }
        if (p.CameraHeight < top)
        {
            return float2(0.0, low.Count > 0 ? low.Distances.x : high.Distances.x);
        }
        if (high.Count == 0)
        {
            return float2(0.0);
        }
        return float2(high.Distances.x, low.Count > 0 ? low.Distances.x : (high.Count > 1 ? high.Distances.y : high.Distances.x));
    }

    float volumetricCloudRemap(float value, float low, float high, float newLow, float newHigh)
    {
        return newLow + (value - low) / max(high - low, 1e-5) * (newHigh - newLow);
    }

    float3 volumetricCloudTable(VolumetricCloudParams p, float height, float cosZenith)
    {
        Texture table;
        table.Index = p.Transmittance;
        return table.get().SampleLevel(LinearClamp, transmittanceUv(p.Planet, height, cosZenith), 0.0).rgb;
    }

    // The part of the light that gets through the air between the camera and a point `pointHeight`
    // up along a ray whose cosine with the point's up is `pointCosine`: the table's from the camera
    // over the table's from the point, or for a ray down into the ground along it turned back
    float3 volumetricCloudAir(VolumetricCloudParams p, float3 direction, float pointHeight, float pointCosine)
    {
        float3 near;
        float3 far;
        if (direction.y >= skyHorizonCosine(p.Planet, p.CameraHeight))
        {
            near = volumetricCloudTable(p, p.CameraHeight, direction.y);
            far = volumetricCloudTable(p, pointHeight, pointCosine);
        }
        else
        {
            near = volumetricCloudTable(p, pointHeight, -pointCosine);
            far = volumetricCloudTable(p, p.CameraHeight, -direction.y);
        }
        return saturate(near / max(far, float3(1e-6)));
    }

    // The sun's light at a point `height` up whose up's cosine with the sun is `sunCosine`: through
    // the air, none once the sun is below the horizon seen from there, fading as its disk goes down
    float3 volumetricCloudSunLight(VolumetricCloudParams p, float height, float sunCosine)
    {
        const float horizon = skyHorizonCosine(p.Planet, height);
        return p.SunLight * volumetricCloudTable(p, height, max(sunCosine, horizon)) * smoothstep(horizon - 0.01, horizon + 0.01, sunCosine);
    }

    // How much of the band's height clouds of a type fill, 0 to 1 at a height `h` through the band
    // (0 its bottom, 1 its top): stratus a thin sheet at the bottom, small puffs the lower part,
    // towering cumulus most of it; flat at the base, rounding off towards the top
    public float volumetricCloudHeightProfile(float h, float type)
    {
        const float stratus = saturate(h / 0.04) * (1.0 - smoothstep(0.1, 0.2, h));
        const float puffs = saturate(h / 0.06) * (1.0 - smoothstep(0.2, 0.45, h));
        const float towers = saturate(h / 0.08) * (1.0 - smoothstep(0.5, 0.95, h));
        return type < 0.5 ? lerp(stratus, puffs, type * 2.0) : lerp(puffs, towers, type * 2.0 - 1.0);
    }

    // The weather where a point is over the ground, `xz` kilometres from the camera: how much of the
    // sky the clouds cover there, their type, and how dense they are
    public struct VolumetricCloudWeather
    {
        public float Coverage;
        public float Type;
        public float Density;
    }

    public VolumetricCloudWeather volumetricCloudWeatherAt(VolumetricCloudParams p, float2 xz)
    {
        Texture weather;
        weather.Index = p.Weather;
        const float4 map = weather.get().SampleLevel(LinearWrap, p.WeatherOrigin + xz * p.Repeats.x, 0.0);
        VolumetricCloudWeather result;
        // Moved up and down by the map, but not at none or all: no cover stays clear, all overcast
        const float coverage = p.Coverage.x;
        const float room = min(min(coverage, 1.0 - coverage) * 4.0, 1.0);
        result.Coverage = saturate(coverage + (map.r - 0.5) * 1.6 * p.Coverage.y * room);
        result.Type = saturate(p.Coverage.z + (map.g - 0.5) * 2.0 * p.Coverage.w);
        result.Density = 0.6 + 0.8 * map.b;
        return result;
    }

    // The clouds' density at a point `xz` kilometres from the camera over the ground and `height`
    // up, 0 or more (times the band's extinction a kilometre): the shapes' noise, parted into cells
    // by Cellularity, rounded by the type's height and thresholded by the coverage; with `bDetail`,
    // worn away at its edges by the details, swirled by curl noise (wisps), wispy at the base and
    // billowy at the top in two sizes (many-lobed tops); thinning out over a fringe as wide as asked.
    // `shapeLevel` and `detailLevel` the noises' mips for the footprint there.
    public float volumetricCloudDensity(VolumetricCloudParams p, float2 xz, float height, VolumetricCloudWeather weather, float shapeLevel, float detailLevel, bool bDetail)
    {
        const float h = (height - p.Band.x) / (p.Band.y - p.Band.x);
        if (h <= 0.0 || h >= 1.0 || weather.Coverage <= 0.0)
        {
            return 0.0;
        }
        const float profile = volumetricCloudHeightProfile(h, weather.Type);
        if (profile <= 0.0)
        {
            return 0.0;
        }
        Volume shape;
        shape.Index = p.Shape;
        const float level = max(shapeLevel, 0.0);
        const float2 noise = shape.SampleLevel(LinearWrap, float3(p.ShapeOrigin + xz * p.Repeats.y, height * p.Repeats.y), level).rg;
        const float spread = exp2(VolumetricCloudShapeHuddleRate * max(level - VolumetricCloudShapeHuddle, 0.0));
        // About evenly spread from 0 to 1, so a coverage c leaves about c of the sky's space inside;
        // kept to the cells' middles (g, 1 at each cell's point) as much as asked
        float field = saturate(0.5 + (noise.r - 0.5) * spread);
        field *= lerp(1.0, smoothstep(VolumetricCloudCellEdge, VolumetricCloudCellMiddle, noise.g), p.Cellularity);
        float base = saturate(volumetricCloudRemap(field * profile, 1.0 - weather.Coverage, 1.0, 0.0, 1.0));
        if (base <= 0.0)
        {
            return 0.0;
        }
        if (bDetail && p.Erosion.x > 0.0)
        {
            Volume detail;
            detail.Index = p.Detail;
            float3 uvw = float3(p.DetailOrigin + xz * p.Repeats.z, height * p.Repeats.z);
            // Swirled by curl noise twice as large, the most at the base, so the wisps tear and curl;
            // not where the details are finer than the footprint, which the swirls couldn't show
            if (p.Wispiness > 0.0 && detailLevel < VolumetricCloudDetailShown)
            {
                Volume curl;
                curl.Index = p.Curl;
                const float3 swirl = curl.SampleLevel(LinearWrap, uvw * 0.5, max(detailLevel - 1.0, 0.0)).rgb * 2.0 - 1.0;
                uvw += swirl * (p.Wispiness * VolumetricCloudSwirl * lerp(1.0, 0.35, h));
            }
            // The cells' octaves, spread over 0 to 1 (they lie between 0.28 and 0.68 mostly), and the
            // same three times smaller, for the billows on the billows
            const float3 weights = float3(0.625, 0.25, 0.125);
            const float wisps = saturate((dot(detail.SampleLevel(LinearWrap, uvw, max(detailLevel, 0.0)).rgb, weights) - 0.28) * 2.5);
            // The finer billows only where the footprint shows them; else their mean
            const float finerLevel = detailLevel + 1.6;
            const float finer = finerLevel < VolumetricCloudDetailShown
                ? saturate((dot(detail.SampleLevel(LinearWrap, uvw * VolumetricCloudFinerDetail + 0.37, max(finerLevel, 0.0)).rgb, weights) - 0.28) * 2.5)
                : 0.5;
            // Cells worn into wisps at the base; round billows, the gaps between them worn, higher up
            const float billows = saturate(h * 4.0) * p.Erosion.y;
            const float wear = lerp(lerp(wisps, 1.0 - wisps, billows), lerp(finer, 1.0 - finer, billows), VolumetricCloudFinerWeight);
            base = saturate(volumetricCloudRemap(base, wear * 0.7 * p.Erosion.x, 1.0, 0.0, 1.0));
        }
        // A fringe: thin at the edges, rising smoothly to its fullest within Edge Softness of the way
        // in, never cut off
        const float softness = p.Erosion.z;
        return base * (1.0 + softness) / (base + softness) * weather.Density;
    }

    // The optical depth from a point towards the sun through the clouds: a few samples further and
    // further apart, the first with its details, then one twice as far for a large cloud beyond.
    // `point` the point from the camera (kilometres), `height` its height, `sunCosine` the sun's
    // cosine with its up; `jitter`, 0 to 1, where in each of their stretches the samples after the
    // first are, so their steps don't show as bands of shadow.
    float volumetricCloudDepthTowardsSun(VolumetricCloudParams p, float3 point, float height, float sunCosine, float shapeLevel, float detailLevel, float jitter)
    {
        const float reach = min((p.Band.y - p.Band.x) * VolumetricCloudLightReach, VolumetricCloudMostLightReach);
        // Half the samples where the shapes are averaged over the footprint: their shadows are soft
        const uint count = shapeLevel > VolumetricCloudShapeHuddle ? max(p.LightSteps / 2u, 1u) : p.LightSteps;
        const float steps = float(count);
        float depth = 0.0;
        for (uint step = 0; step < count; ++step)
        {
            const float near = float(step) / steps;
            const float far = float(step + 1) / steps;
            // The first in the middle of its stretch: right by the cloud's surface, a jitter there
            // would speckle its sunlit side
            const float t = reach * lerp(near * near, far * far, step == 0 ? 0.5 : jitter);
            const float3 sample = point + p.TowardsSun * t;
            const float sampleHeight = skyHeightAlong(p.Planet, height, sunCosine, t);
            const VolumetricCloudWeather weather = volumetricCloudWeatherAt(p, sample.xz);
            depth += volumetricCloudDensity(p, sample.xz, sampleHeight, weather, shapeLevel + float(step) * 0.5, detailLevel, step == 0) * reach * (far * far - near * near);
        }
        const float3 beyond = point + p.TowardsSun * (reach * 2.0);
        const float beyondHeight = skyHeightAlong(p.Planet, height, sunCosine, reach * 2.0);
        depth += volumetricCloudDensity(p, beyond.xz, beyondHeight, volumetricCloudWeatherAt(p, beyond.xz), shapeLevel + 2.0, detailLevel, false) * reach;
        return depth * p.Band.w;
    }

    // Henyey and Greenstein's phase function times 4 pi: 1 for light scattered the same every way
    float volumetricCloudLobe(float cosine, float g)
    {
        const float g2 = g * g;
        const float denominator = max(1.0 + g2 - 2.0 * g * cosine, 1e-6);
        return (1.0 - g2) / (denominator * sqrt(denominator));
    }

    // What of the sun's light a sample scatters towards the camera, per unit of the light: the
    // orders of scattering, each through the cloud towards the sun (`depthToSun`) less and by a
    // softer lobe than the one before, all in the sunlit tint, the multiple scattering's in the shadow
    // tint too; the powder of a
    // thin edge (`extinction` its own), seen away from the sun. `cosine` between the ray and the sun.
    float3 volumetricCloudSunScattering(VolumetricCloudParams p, float cosine, float depthToSun, float extinction)
    {
        const float forward = lerp(VolumetricCloudTightLobe, VolumetricCloudWideLobe, p.Lighting.y);
        float3 sum = float3(0.0);
        float through = 1.0;
        float strength = 1.0;
        float lobe = 1.0;
        for (int order = 0; order < VolumetricCloudOrders; ++order)
        {
            // The silver lining: the forward lobe's light above even scattering, times its strength
            const float silver = max(1.0 + (volumetricCloudLobe(cosine, forward * lobe) - 1.0) * p.Lighting.x, 0.0);
            const float phase = 0.5 * (volumetricCloudLobe(cosine, VolumetricCloudBackLobe * lobe) + silver);
            sum += (order == 0 ? p.SunlitTint : p.SunlitTint * p.ShadowTint) * (strength * phase * exp(-depthToSun * through));
            through *= VolumetricCloudOrderExtinction;
            strength *= p.Lighting.w;
            lobe *= VolumetricCloudOrderLobe;
        }
        // The light carried through the cloud's body by many scatterings, as diffusion carries it:
        // falling off as 1 / (1 + k depth), not by Beer's law, so a body backlit by a low sun glows
        // softly in its colour rather than going black
        sum += p.SunlitTint * p.ShadowTint * (p.Translucency * VolumetricCloudDiffused / (1.0 + VolumetricCloudDiffusion * depthToSun));
        const float powder = 1.0 - exp(-extinction * VolumetricCloudPowderDepth);
        return sum * lerp(1.0, powder, p.Lighting.z * saturate(0.5 - 0.5 * cosine));
    }

    // What a march along a ray has gathered so far: the clouds' light, what of the light from
    // behind gets through, and the depths where it was hidden, weighed by how much; where the clouds
    // began to hide it (Front, -1 until they do), where they had hidden nine tenths (Back, -1 until
    // then) and the last sample in a cloud
    struct VolumetricCloudMarch
    {
        float3 Light;
        float Through;
        float DepthSum;
        float Front;
        float Back;
        float Last;
        // The ray's jitter, for its samples' shadows too
        float Jitter;
    }

    // The shapes alone `t` along a ray, without the details: whether there may be cloud there
    float volumetricCloudShapeAlong(VolumetricCloudParams p, float3 direction, float t)
    {
        const float height = skyHeightAlong(p.Planet, p.CameraHeight, direction.y, t);
        const float2 xz = direction.xz * t;
        const float footprint = max(t * p.PixelAngle, 1e-5);
        const float shapeLevel = log2(footprint * p.Repeats.y * VolumetricCloudShapeTexels);
        return volumetricCloudDensity(p, xz, height, volumetricCloudWeatherAt(p, xz), shapeLevel, 0.0, false);
    }

    // One sample `t` along a ray, standing for `length` of it: the cloud there with its details, lit
    // by the sun through the air and through the clouds towards it, and by the sky above and the
    // ground below; what it scatters towards the camera as much as it hides (energy-conserving)
    void volumetricCloudSample(VolumetricCloudParams p, float3 direction, float t, float length, inout VolumetricCloudMarch march)
    {
        const float height = skyHeightAlong(p.Planet, p.CameraHeight, direction.y, t);
        const float3 point = direction * t;
        const VolumetricCloudWeather weather = volumetricCloudWeatherAt(p, point.xz);
        const float footprint = max(t * p.PixelAngle, 1e-5);
        const float shapeLevel = log2(footprint * p.Repeats.y * VolumetricCloudShapeTexels);
        const float detailLevel = log2(footprint * p.Repeats.z * VolumetricCloudDetailTexels);
        const float density = volumetricCloudDensity(p, point.xz, height, weather, shapeLevel, detailLevel, true);
        if (density <= 0.0)
        {
            return;
        }
        const float extinction = density * p.Band.w;

        // The sun's light at the sample, through the air and the clouds towards the sun
        const float3 camera = float3(0.0, p.Planet.Radius + p.CameraHeight, 0.0);
        const float sunCosine = dot(normalize(camera + point), p.TowardsSun);
        float3 sunLight = float3(0.0);
        if (p.Debug != VolumetricCloudDebugSkyLight)
        {
            sunLight = volumetricCloudSunLight(p, height, sunCosine);
            if (any(sunLight > 0.0))
            {
                const float depthToSun = volumetricCloudDepthTowardsSun(p, point, height, sunCosine, shapeLevel, detailLevel, march.Jitter);
                sunLight *= volumetricCloudSunScattering(p, dot(direction, p.TowardsSun), depthToSun, extinction);
            }
        }
        // The sky's light from above, less of it the lower in the band (the clouds above hide it),
        // and the ground's from below, in the shadow tint
        float3 skyLight = float3(0.0);
        if (p.Debug != VolumetricCloudDebugSunLight)
        {
            const float h = saturate((height - p.Band.x) / (p.Band.y - p.Band.x));
            skyLight = (p.SkyAbove * lerp(VolumetricCloudSkyBelow, VolumetricCloudSkyAbove, h) + p.SkyBelow * VolumetricCloudGroundLight) * (p.Repeats.w * p.ShadowTint);
        }

        const float stepThrough = exp(-extinction * length);
        const float hidden = march.Through * (1.0 - stepThrough);
        march.Light += (sunLight + skyLight) * (hidden * p.Erosion.w);
        march.DepthSum += hidden * t;
        march.Through *= stepThrough;
        march.Last = t;
        if (march.Front < 0.0 && march.Through < VolumetricCloudFrontThrough)
        {
            march.Front = t;
        }
        if (march.Back < 0.0 && march.Through < VolumetricCloudBackThrough)
        {
            march.Back = t;
        }
    }

    // The clouds along a ray from the camera: their light and what of the light from behind them gets
    // through, the haze between taken in; how far along the ray they are (kilometres: where their
    // light comes from on average, where they begin to hide what is behind, where they have hidden
    // nine tenths of it or end; the band's end for a ray through none); and what gets through them
    // without the haze, for the sun's disk
    public struct VolumetricCloudsResult
    {
        public float3 Light;
        public float Transmittance;
        public float Depth;
        public float Front;
        public float Back;
        public float Through;
    }

    // Where the march's step `x` (0 to `count`, fractions between) is along the ray: evenly from
    // `enter` over `span`, or with the camera inside the band, closer together near it (x² spacing),
    // where its clouds are big on the picture
    float volumetricCloudStepAt(float enter, float span, float count, float x, bool bNear)
    {
        const float u = x / count;
        return enter + span * (bNear ? u * u : u);
    }

    // `direction` normalised; `jitter`, 0 to 1, where in each step its sample is, so the steps don't
    // show as bands
    public VolumetricCloudsResult volumetricCloudsAlong(VolumetricCloudParams p, float3 direction, float jitter)
    {
        VolumetricCloudsResult result;
        result.Light = float3(0.0);
        result.Transmittance = 1.0;
        result.Depth = p.Band.z;
        result.Front = p.Band.z;
        result.Back = p.Band.z;
        result.Through = 1.0;

        const float2 span = volumetricCloudBandSpan(p, direction.y);
        const float enter = span.x;
        const float exit = min(span.y, p.Band.z);
        if (exit <= enter)
        {
            return result;
        }
        result.Depth = exit;
        result.Front = exit;
        result.Back = exit;
        if (p.Debug == VolumetricCloudDebugWeather)
        {
            const VolumetricCloudWeather weather = volumetricCloudWeatherAt(p, direction.xz * enter);
            result.Light = float3(weather.Coverage, weather.Type, weather.Density * 0.5);
            result.Transmittance = 0.0;
            result.Depth = enter;
            result.Front = enter;
            result.Back = enter;
            return result;
        }

        // More steps along the longer rays, towards the horizon, up to Steps: coarse through the clear
        // air, by the shapes alone; where they find cloud, the step before (where it may have begun)
        // and every step through it again in VolumetricCloudFineSteps, with the details and the light
        const float thickness = p.Band.y - p.Band.x;
        const float length = exit - enter;
        const uint count = clamp(uint(float(p.Steps) * 0.5 * sqrt(length / thickness)), max(p.Steps / 4u, 8u), p.Steps);
        const bool bNear = enter <= 0.0;
        VolumetricCloudMarch march;
        march.Light = float3(0.0);
        march.Through = 1.0;
        march.DepthSum = 0.0;
        march.Front = -1.0;
        march.Back = -1.0;
        march.Last = exit;
        march.Jitter = jitter;
        bool bInside = false;
        int fineUntil = -1;
        for (int index = 0; index < int(count) && march.Through >= VolumetricCloudOpaque; ++index)
        {
            const float probe = volumetricCloudStepAt(enter, length, float(count), float(index) + jitter, bNear);
            const bool bFound = volumetricCloudShapeAlong(p, direction, probe) > 0.0;
            if (bFound || bInside)
            {
                for (int coarse = max(index - 1, fineUntil + 1); coarse <= index; ++coarse)
                {
                    const float from = volumetricCloudStepAt(enter, length, float(count), float(coarse), bNear);
                    const float to = volumetricCloudStepAt(enter, length, float(count), float(coarse + 1), bNear);
                    const float fineStep = (to - from) / float(VolumetricCloudFineSteps);
                    for (int fine = 0; fine < VolumetricCloudFineSteps && march.Through >= VolumetricCloudOpaque; ++fine)
                    {
                        volumetricCloudSample(p, direction, from + (float(fine) + jitter) * fineStep, fineStep, march);
                    }
                }
                fineUntil = index;
            }
            bInside = bFound;
        }
        if (march.Through < 1.0 - 1e-4)
        {
            result.Depth = march.DepthSum / (1.0 - march.Through);
            result.Front = march.Front >= 0.0 ? march.Front : result.Depth;
            result.Back = march.Back >= 0.0 ? march.Back : march.Last;
        }
        // Blocked once the march stopped: no sun's disk shows through what it gave up on
        result.Through = march.Through < VolumetricCloudOpaque ? 0.0 : march.Through;

        // Faded into the air between the camera and where they are, as much as Aerial Perspective says
        const float3 camera = float3(0.0, p.Planet.Radius + p.CameraHeight, 0.0);
        const float3 point = camera + direction * result.Depth;
        const float pointHeight = skyHeightAlong(p.Planet, p.CameraHeight, direction.y, result.Depth);
        const float3 air = pow(volumetricCloudAir(p, direction, pointHeight, dot(normalize(point), direction)), p.AerialPerspective);
        result.Light = march.Light * air;
        result.Transmittance = 1.0 - (1.0 - march.Through) * dot(air, float3(1.0 / 3.0));
        return result;
    }
}
