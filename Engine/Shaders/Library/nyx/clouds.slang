// The high cloud layer (VulkanCloudLayer; CloudLayer.h has the same math on the CPU): a thin shell
// round the planet at an altitude, planet-centred as the sky's tables are, in kilometres. Its pass
// (CloudLayer.slang) works out, per pixel, the clouds' light along the pixel's ray and the part of
// the light from behind them that gets through, at the view's size; the sky lies behind them, and
// whatever of the world the ray reaches beyond the shell (the ground under the layer, seen from
// above it; a treetop above it, seen from below) is covered by them as the sky is, before the fog.
// The clouds along a ray (cloudLayerAlong()) are worked out from what describes them (CloudParams),
// so the sky's cube, which lights the world, draws them too (SkyAtmosphere.slang).

import nyx.bindless;
import nyx.sky;
import nyx.view;

namespace nyx
{
    public bool hasCloudLayer()
    {
        return View.CloudLayer != 0;
    }

    // The camera's height above the ground for the shell, in kilometres: as it is, not the height
    // the sky's tables were made for, which may be a metre and 1 % off, the wrong side of a layer
    // the camera is close to
    public float cloudCameraHeight()
    {
        return max(View.CameraPosition.y * 0.001, 0.001);
    }

    // Where a ray meets the shell `altitude` up from a camera `height` up, `cosZenith` from the up
    // axis: none, one, or two from above (in, then out on its far side), nearest first; nothing
    // behind the ground (GetCloudShellHits())
    public struct CloudShellHits
    {
        public int Count;
        public float2 Distances;
    }

    public CloudShellHits cloudShellHits(SkyPlanet planet, float height, float cosZenith, float altitude)
    {
        CloudShellHits hits;
        hits.Count = 0;
        hits.Distances = float2(0.0);
        const float r = planet.Radius + height;
        const float b = r * cosZenith;
        const float c = (height - altitude) * (2.0 * planet.Radius + height + altitude);
        const float discriminant = b * b - c;
        if (c <= 0.0)
        {
            const float root = sqrt(max(discriminant, 0.0));
            hits.Count = 1;
            hits.Distances.x = max(b > 0.0 ? -c / (b + root) : root - b, 0.0);
        }
        else if (b < 0.0 && discriminant >= 0.0)
        {
            const float root = sqrt(discriminant);
            hits.Count = 2;
            hits.Distances = float2(c / (root - b), root - b);
        }

        const float ground = skyDistanceToGround(planet, height, cosZenith);
        if (ground >= 0.0)
        {
            if (hits.Count == 2 && hits.Distances.y > ground)
            {
                hits.Count = 1;
            }
            if (hits.Count == 1 && hits.Distances.x > ground)
            {
                hits.Count = 0;
            }
        }
        return hits;
    }

    // The clouds before something: what of its light gets through them, and their own light,
    // along its ray from the camera (the cloud pass's, at its pixel)
    public struct CloudCover
    {
        public float Transmittance;
        public float3 Light;

        public float3 apply(float3 color)
        {
            return color * Transmittance + Light;
        }

        // A colour already weighed by its opacity, as a decal or a particle blends it: the
        // clouds' light only for the part of what is beneath that it covers
        public float3 applyPremultiplied(float3 color, float opacity)
        {
            return color * Transmittance + Light * opacity;
        }
    }

    // The cloud pass's light and transmittance at a pixel of the view
    public float4 cloudLightAt(float2 pixel)
    {
        return View.CloudLight.get().Load(int3(int2(pixel), 0));
    }

    // Whether the view draws volumetric clouds (nyx.volumetricclouds)
    public bool hasVolumetricClouds()
    {
        return View.VolumetricClouds != 0;
    }

    // Their light and transmittance at a pixel of the view, filtered up from the size they are
    // marched at: the mean of four bilinear taps half a texel round the pixel, a 2 x 2 box over the
    // bilinear filter, smooth where bilinear filtering alone shows the texels as creases
    public float4 volumetricCloudsAt(float2 pixel)
    {
        const float2 texel = 1.0 / View.VolumetricCloudDetailOrigin.zw;
        const float2 uv = pixel / View.ViewportSize;
        const float2 half = 0.5 * texel;
        const Texture2D clouds = View.VolumetricCloudLight.get();
        return 0.25 * (clouds.SampleLevel(LinearClamp, uv + float2(-half.x, -half.y), 0.0) + clouds.SampleLevel(LinearClamp, uv + float2(half.x, -half.y), 0.0)
            + clouds.SampleLevel(LinearClamp, uv + float2(-half.x, half.y), 0.0) + clouds.SampleLevel(LinearClamp, uv + float2(half.x, half.y), 0.0));
    }

    // Where along a pixel's ray they are, in kilometres, at their nearest pixel: their light's mean
    // depth (x), where they begin to hide what is behind (y) and where they have hidden nine tenths
    // of it (z); and what gets through them without the haze (w)
    public float4 volumetricCloudDepthAt(float2 pixel)
    {
        const float2 extent = View.VolumetricCloudDetailOrigin.zw;
        const int2 texel = clamp(int2(pixel / View.ViewportSize * extent), int2(0), int2(extent) - 1);
        return View.VolumetricCloudDepth.get().Load(int3(texel, 0));
    }

    // What of the sun's disk gets through every cloud along a pixel's ray, `direction` the way it
    // looks: the volumetric clouds' own transmittance, without the haze between, which would let a
    // bright disk shine through a thick cloud; the high layer's worked out along the ray, as its
    // target keeps only what gets through with the haze
    public float cloudSunThroughAt(float2 pixel, float3 direction)
    {
        float through = 1.0;
        if (hasVolumetricClouds())
        {
            through = View.VolumetricCloudDepth.SampleLevel(LinearClamp, pixel / View.ViewportSize, 0.0).w;
        }
        if (hasCloudLayer())
        {
            through *= cloudLayerTransmittanceAlong(viewCloudParams(), direction, ddx(direction), ddy(direction));
        }
        return through;
    }

    // Every cloud along a pixel's ray: the volumetric clouds before the high layer, which lies
    // above them. Their light (rgb), and what of the light from behind them gets through (a).
    public float4 cloudsAt(float2 pixel)
    {
        float4 clouds = float4(0.0, 0.0, 0.0, 1.0);
        if (hasCloudLayer())
        {
            clouds = cloudLightAt(pixel);
        }
        if (hasVolumetricClouds())
        {
            const float4 volume = volumetricCloudsAt(pixel);
            clouds = float4(volume.rgb + clouds.rgb * volume.a, clouds.a * volume.a);
        }
        return clouds;
    }

    // The clouds between the camera and a point of the world, at its pixel: the high layer's only
    // where its shell lies between them, the camera on one side and the point on the other; the
    // volumetric clouds' as much as the point is farther along the ray than where they begin
    public CloudCover cloudCoverBefore(float3 positionWS, float2 pixel)
    {
        CloudCover cover;
        cover.Transmittance = 1.0;
        cover.Light = float3(0.0);
        if (!hasCloudLayer() && !hasVolumetricClouds())
        {
            return cover;
        }
        const float3 ray = (positionWS - View.CameraPosition.xyz) * 0.001;
        const float distance = length(ray);
        if (distance <= 0.0)
        {
            return cover;
        }
        if (hasCloudLayer())
        {
            const SkyPlanet planet = viewPlanet();
            const float height = cloudCameraHeight();
            const float pointHeight = skyHeightAlong(planet, height, ray.y / distance, distance);
            const float altitude = View.CloudShell.x;
            if ((height - altitude) * (pointHeight - altitude) < 0.0)
            {
                const float4 clouds = cloudLightAt(pixel);
                cover.Transmittance = clouds.a;
                cover.Light = clouds.rgb;
            }
        }
        if (hasVolumetricClouds())
        {
            // As much of them as lies before the point: none before they begin, all of them beyond
            // where they all but end, between as far as it is between, so a hillside among them is
            // in fog rather than cut off
            const float4 depths = volumetricCloudDepthAt(pixel);
            const float before = saturate((distance - depths.y) / max(depths.z - depths.y, 1e-3));
            if (before > 0.0)
            {
                const float4 volume = volumetricCloudsAt(pixel);
                cover.Light = volume.rgb * before + cover.Light * lerp(1.0, volume.a, before);
                cover.Transmittance *= lerp(1.0, volume.a, before);
            }
        }
        return cover;
    }

    // What the clouds along the view's rays are made from, by its set: the sky's light on them from
    // its irradiance cube, above and below
    public CloudParams viewCloudParams()
    {
        CloudParams p;
        p.Planet = viewPlanet();
        p.CameraHeight = cloudCameraHeight();
        p.Shell = View.CloudShell;
        p.Shape = View.CloudShape;
        p.Wind = View.CloudWind;
        p.Origin = View.CloudOrigin;
        p.SunLight = View.CloudSun.rgb;
        p.TowardsSun = normalize(View.LightDirection.xyz);
        p.Noise = View.CloudNoise.Index;
        p.Transmittance = View.TransmittanceLut.Index;
        const TextureCube irradiance = View.SkyIrradiance.get();
        p.SkyAbove = irradiance.SampleLevel(LinearClamp, float3(0.0, 1.0, 0.0), 0.0).rgb;
        p.SkyBelow = irradiance.SampleLevel(LinearClamp, float3(0.0, -1.0, 0.0), 0.0).rgb;
        return p;
    }

    // -------------------------------------------------------------------------
    // The clouds along a ray (CloudLayer.slang per pixel; SkyAtmosphere.slang's sky cube, for the
    // world's light), from what describes them (CloudParams), not the view's set, so the sky's
    // compute passes can use them too
    // -------------------------------------------------------------------------

    // The cloudlets' cells across the noise's square (CloudNoiseCells)
    static const float CloudNoiseCells = 16.0;
    // What of the light reaching them the clouds scatter
    static const float CloudAlbedo = 0.9;
    // The forward lobe of their scattering, which makes the silver lining (Henyey-Greenstein's g), and
    // the most it gives, as a multiple of light scattered the same every way; and the softer lobe of
    // the light a thin cloud lets through, which a thick one scatters about evenly
    static const float CloudRimLobe = 0.75;
    static const float CloudMostRim = 12.0;
    static const float CloudThroughLobe = 0.6;
    // How far towards the sun the clouds' own shadow is looked up, in cells, with the sun at the
    // horizon, and how deep it is through a whole cloud there
    static const float CloudShadowReach = 0.35;
    static const float CloudShadowDepth = 1.5;
    // The least slant of a ray through the layer, for its path through the clouds: grazing rays see
    // them as thick as at about 1.7 degrees; and of the sun's light
    static const float CloudLeastSlant = 0.03;
    static const float CloudLeastSunSlant = 0.5;
    // The part of a cloud's optical depth that turns light back, as two-stream scattering has it:
    // 3/4 (1 - g), g about 0.85 for the droplets' strong forward scattering. A cloud of depth 9 lets
    // about half the light through, scattered, and sends the other half back.
    static const float CloudBackScattering = 0.11;
    // How long a footprint over the pattern may be against its width, filtered anisotropically: a
    // grazing ray's is much longer, and wider samples along it cost less than more
    static const float CloudMostAnisotropy = 4.0;

    // What the clouds along a ray are made from: the planet, the camera's height above the ground
    // (kilometres), the layer as the view's set has it (ViewConstants.CloudShell to CloudOrigin), the
    // sun's light at the top of the air and the way to it, the noise and the transmittance table in
    // the bindless array, and the sky's light on the clouds from above and below (a radiance)
    public struct CloudParams
    {
        public SkyPlanet Planet;
        public float CameraHeight;
        public float4 Shell;
        public float4 Shape;
        public float4 Wind;
        public float4 Origin;
        public float3 SunLight;
        public float3 TowardsSun;
        public uint Noise;
        public uint Transmittance;
        public float3 SkyAbove;
        public float3 SkyBelow;
    }

    // The noise at `uv` (squares), filtered over the footprint there: anisotropically for the fine
    // shapes, which a grazing ray stretches long, the footprint widened to at most
    // CloudMostAnisotropy to one; trilinearly for the large, smooth ones
    float4 cloudNoise(CloudParams p, float2 uv, float2 dx, float2 dy)
    {
        const float lengthX = length(dx);
        const float lengthY = length(dy);
        if (lengthX > CloudMostAnisotropy * lengthY)
        {
            const float2 across = lengthY > 1e-12 ? dy / lengthY : float2(-dx.y, dx.x) / max(lengthX, 1e-12);
            dy = across * (lengthX / CloudMostAnisotropy);
        }
        else if (lengthY > CloudMostAnisotropy * lengthX)
        {
            const float2 across = lengthX > 1e-12 ? dx / lengthX : float2(dy.y, -dy.x) / max(lengthY, 1e-12);
            dx = across * (lengthY / CloudMostAnisotropy);
        }
        Texture noise;
        noise.Index = p.Noise;
        return noise.get().SampleGrad(Anisotropic, uv, dx, dy);
    }

    float4 cloudSmoothNoise(CloudParams p, float2 uv, float2 dx, float2 dy)
    {
        Texture noise;
        noise.Index = p.Noise;
        return noise.get().SampleGrad(LinearWrap, uv, dx, dy);
    }

    // What of the clouds changes slowly over the pattern, round a point of it: the pattern there bent
    // by the large noise (b, a as a vector), the coverage there, and how far the footprint is from
    // seeing single cloudlets (on the horizon it covers many, and sees their mean). `q` in squares
    // along the wind and across it, `dx` and `dy` how far it moves to the neighbouring footprints.
    struct CloudField
    {
        float2 Warped;
        float Coverage;
        float Far;
    }

    CloudField cloudField(CloudParams p, float2 q, float2 dx, float2 dy)
    {
        CloudField field;
        const float4 large = cloudSmoothNoise(p, q * 0.125, dx * 0.125, dy * 0.125);
        field.Warped = q + (large.ba - 0.5) * (0.2 * p.Wind.z);
        // More here and less there by the largest noise, but not at none or all
        const float coverage = p.Shape.x;
        const float patches = cloudSmoothNoise(p, q * 0.0625, dx * 0.0625, dy * 0.0625).a - 0.5;
        field.Coverage = saturate(coverage + patches * 1.2 * min(coverage, 1.0 - coverage));
        field.Far = smoothstep(0.5, 2.0, sqrt(length(dx) * length(dy)) * CloudNoiseCells);
        return field;
    }

    // How much cloud there is `offset` (squares) from the field's point, 0 to 1: cloudlets, a little
    // longer across the wind than along it, and streaks along it, by the type; with `bDetail`, their
    // edges broken up by finer cells; thresholded by the coverage, thicker towards their middles,
    // their edges soft over Softness of the way in. Towards the coverage's own mean as the footprint
    // covers many cloudlets, which the threshold doesn't give.
    float cloudDensity(CloudParams p, CloudField field, float2 offset, float2 dx, float2 dy, bool bDetail)
    {
        const float2 w = field.Warped + offset;
        const float type = p.Shape.y;
        float shape = 0.0;
        if (type < 1.0)
        {
            const float2 scale = float2(1.25, 1.0);
            shape += cloudNoise(p, w * scale, dx * scale, dy * scale).r * (1.0 - type);
        }
        if (type > 0.0)
        {
            const float2 scale = float2(0.25, 1.0);
            shape += cloudNoise(p, w * scale, dx * scale, dy * scale).b * type;
        }
        if (bDetail)
        {
            const float detail = cloudNoise(p, w * 2.0 + float2(0.37, 0.61), dx * 2.0, dy * 2.0).g;
            shape *= 1.0 - p.Shape.w * (1.0 - detail) * 0.6;
        }
        const float threshold = 1.0 - 1.15 * field.Coverage;
        const float inside = saturate((shape - threshold) / max(1.0 - threshold, 1e-3));
        const float sharp = smoothstep(0.0, p.Shape.z, inside) * (0.35 + 0.65 * inside);
        const float mean = saturate(field.Coverage * 1.15 - 0.1) * 0.8;
        return lerp(sharp, mean, field.Far);
    }

    float3 cloudTransmittanceTable(CloudParams p, float height, float cosZenith)
    {
        Texture table;
        table.Index = p.Transmittance;
        return table.get().SampleLevel(LinearClamp, transmittanceUv(p.Planet, height, cosZenith), 0.0).rgb;
    }

    // The part of the light that gets through the air between the camera and a point of the shell
    // along a ray, where the ray's slant to the shell is `slant`: the table's from the camera over
    // the table's from the point, along the ray, or for a ray down into the ground along it turned
    // back, which reaches the top of the air
    float3 cloudAirTransmittance(CloudParams p, float3 direction, float slant)
    {
        const float altitude = p.Shell.x;
        float3 near;
        float3 far;
        if (direction.y >= skyHorizonCosine(p.Planet, p.CameraHeight))
        {
            near = cloudTransmittanceTable(p, p.CameraHeight, direction.y);
            far = cloudTransmittanceTable(p, altitude, slant);
        }
        else
        {
            near = cloudTransmittanceTable(p, altitude, -slant);
            far = cloudTransmittanceTable(p, p.CameraHeight, -direction.y);
        }
        return saturate(near / max(far, float3(1e-6)));
    }

    // The sun's light on the layer where the sun's cosine with the shell's up is `sunCosine`: through
    // the air from the layer's height, none once the sun is below the horizon seen from there, fading
    // over about half a degree (its disk going down)
    public float3 cloudSunLight(CloudParams p, float sunCosine)
    {
        const float altitude = p.Shell.x;
        const float horizon = skyHorizonCosine(p.Planet, altitude);
        return p.SunLight * cloudTransmittanceTable(p, altitude, max(sunCosine, horizon)) * smoothstep(horizon - 0.01, horizon + 0.01, sunCosine);
    }

    // Henyey and Greenstein's phase function times 4 pi: 1 for light scattered the same every way
    float cloudLobe(float cosine, float g)
    {
        const float g2 = g * g;
        const float denominator = max(1.0 + g2 - 2.0 * g * cosine, 1e-6);
        return (1.0 - g2) / (denominator * sqrt(denominator));
    }

    struct CloudSample
    {
        float3 Light;
        float Opacity;
    }

    // The clouds where a ray meets the shell, `distance` along it: their light as the camera sees
    // it, through the air, and how much of what is behind them they hide. `direction` and its
    // changes to the neighbouring footprints (a pixel's, or a cube texel's).
    CloudSample cloudAt(CloudParams p, float3 direction, float3 directionDx, float3 directionDy, float distance)
    {
        CloudSample sample;
        sample.Light = float3(0.0);
        sample.Opacity = 0.0;

        const float thickness = p.Shell.y;
        const float3 hit = float3(0.0, p.Planet.Radius + p.CameraHeight, 0.0) + direction * distance;
        const float3 up = normalize(hit);
        // Above 0 seen from below, below 0 from above
        const float slant = dot(up, direction);
        const float safeSlant = slant >= 0.0 ? max(slant, 1e-3) : min(slant, -1e-3);

        // The point over the pattern, and how far it moves to the neighbouring footprints' (their rays
        // meeting the plane the shell touches there)
        const float2 along = p.Wind.xy;
        const float2 across = float2(-along.y, along.x);
        const float tile = p.Shell.z;
        const float2 offset = direction.xz * distance;
        const float2 q = p.Origin.xy + float2(dot(offset, along), dot(offset, across)) / tile;
        const float3 dPdx = distance * (directionDx - direction * (dot(up, directionDx) / safeSlant));
        const float3 dPdy = distance * (directionDy - direction * (dot(up, directionDy) / safeSlant));
        const float2 qdx = float2(dot(dPdx.xz, along), dot(dPdx.xz, across)) / tile;
        const float2 qdy = float2(dot(dPdy.xz, along), dot(dPdy.xz, across)) / tile;

        const CloudField field = cloudField(p, q, qdx, qdy);
        const float density = cloudDensity(p, field, float2(0.0), qdx, qdy, true);
        const float fade = smoothstep(0.25, 2.0, distance / thickness);
        if (density <= 0.0 || fade <= 0.0)
        {
            return sample;
        }
        // How deep the cloud is straight through, and how much of what is behind it it hides
        const float depth = density * p.Shell.w;
        const float cover = 1.0 - exp(-depth / max(abs(slant), CloudLeastSlant));
        const float opacity = cover * fade;

        // The sun's light at the layer, and the cloud towards the sun in its way
        const float3 sun = p.TowardsSun;
        const float sunCosine = dot(up, sun);
        const float3 sunLight = cloudSunLight(p, sunCosine);
        float3 direct = float3(0.0);
        if (any(sunLight > 0.0))
        {
            // A low sun's light comes in from the side, through the cloud towards it; a high one's
            // through its top or base
            const float2 sunWay = float2(dot(sun.xz, along), dot(sun.xz, across));
            const float sunward = cloudDensity(p, field, sunWay * (CloudShadowReach / CloudNoiseCells), qdx, qdy, false);
            const float shadow = exp(-CloudShadowDepth * sunward * (1.0 - abs(sunCosine)));
            // What the cloud sends back towards the sun's side and lets through to the other, of the
            // sun's light along its slanted path through it (two-stream: through 1 / (1 + b depth)),
            // the light that goes straight through without scattering left out; per part of what it
            // hides, as it is blended
            const float sunDepth = depth / max(abs(sunCosine), CloudLeastSunSlant);
            const float through = 1.0 / (1.0 + CloudBackScattering * sunDepth);
            const float forward = min(cloudLobe(dot(direction, sun), CloudThroughLobe), CloudMostRim);
            const float scatteredThrough = max(through - (1.0 - cover), 0.0) * lerp(1.0, forward, exp(-0.15 * sunDepth));
            const float sunSide = smoothstep(-0.1, 0.1, slant >= 0.0 ? -sunCosine : sunCosine);
            const float scattered = lerp(scatteredThrough, 1.0 - through, sunSide) / max(cover, 1e-3);
            // The silver lining: their thin edges, against the sun
            const float lobe = min(cloudLobe(dot(direction, sun), CloudRimLobe), CloudMostRim);
            const float rim = lobe * p.Wind.w * (1.0 - density) * (1.0 - density) * 0.35;
            direct = sunLight * shadow * (min(scattered, 1.5) + rim);
        }

        // The sky's light, from above and below, more of the side the camera sees
        const float3 ambient = lerp(p.SkyAbove, p.SkyBelow, slant >= 0.0 ? 0.65 : 0.25) * (p.Origin.w * (1.0 - 0.3 * density));

        const float3 light = (direct + ambient) * (CloudAlbedo * p.Origin.z);
        const float3 air = cloudAirTransmittance(p, direction, slant);
        sample.Light = light * air * opacity;
        sample.Opacity = opacity * dot(air, float3(1.0 / 3.0));
        return sample;
    }

    // The clouds along a ray from the camera, nearest first: their light (rgb), and what of the
    // light from behind them gets through (a). `direction` normalised, and its changes to the
    // neighbouring footprints.
    // What gets through the layer along a ray, without the haze between: its clouds' opacity where the
    // ray meets its shell, nearest first; for the sun's disk (cloudSunThroughAt())
    public float cloudLayerTransmittanceAlong(CloudParams p, float3 direction, float3 directionDx, float3 directionDy)
    {
        const CloudShellHits hits = cloudShellHits(p.Planet, p.CameraHeight, direction.y, p.Shell.x);
        float through = 1.0;
        for (int index = 0; index < hits.Count; ++index)
        {
            const float distance = index == 0 ? hits.Distances.x : hits.Distances.y;
            const float3 hit = float3(0.0, p.Planet.Radius + p.CameraHeight, 0.0) + direction * distance;
            const float3 up = normalize(hit);
            const float slant = dot(up, direction);
            const float safeSlant = slant >= 0.0 ? max(slant, 1e-3) : min(slant, -1e-3);
            const float2 along = p.Wind.xy;
            const float2 across = float2(-along.y, along.x);
            const float tile = p.Shell.z;
            const float2 offset = direction.xz * distance;
            const float2 q = p.Origin.xy + float2(dot(offset, along), dot(offset, across)) / tile;
            const float3 dPdx = distance * (directionDx - direction * (dot(up, directionDx) / safeSlant));
            const float3 dPdy = distance * (directionDy - direction * (dot(up, directionDy) / safeSlant));
            const float2 qdx = float2(dot(dPdx.xz, along), dot(dPdx.xz, across)) / tile;
            const float2 qdy = float2(dot(dPdy.xz, along), dot(dPdy.xz, across)) / tile;
            const float density = cloudDensity(p, cloudField(p, q, qdx, qdy), float2(0.0), qdx, qdy, true);
            const float cover = 1.0 - exp(-density * p.Shell.w / max(abs(slant), CloudLeastSlant));
            through *= 1.0 - cover * smoothstep(0.25, 2.0, distance / p.Shell.y);
        }
        return through;
    }

    public float4 cloudLayerAlong(CloudParams p, float3 direction, float3 directionDx, float3 directionDy)
    {
        const CloudShellHits hits = cloudShellHits(p.Planet, p.CameraHeight, direction.y, p.Shell.x);
        float3 light = float3(0.0);
        float through = 1.0;
        for (int index = 0; index < hits.Count; ++index)
        {
            const CloudSample sample = cloudAt(p, direction, directionDx, directionDy, index == 0 ? hits.Distances.x : hits.Distances.y);
            light += sample.Light * through;
            through *= 1.0 - sample.Opacity;
        }
        return float4(light, through);
    }
}
