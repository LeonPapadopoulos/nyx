// The sky made by the air's scattering (Hillaire 2020, "A Scalable and Production Ready Sky and
// Atmosphere Rendering Technique"), its tables made by VulkanSkyAtmosphere when the air, the sun or
// the camera's height changes, in this order:
//   - skyTransmittance: how much light gets through the air from a height, at an angle, to its
//     top (256 x 64);
//   - skyMultipleScattering: the light scattered more than once, as a factor of the sun's, by the
//     height and the sun's angle (32 x 32), each texel 64 directions summed in a group;
//   - skySkyView: the sky round the camera, at its height, by the angle from the sun round the up
//     axis and from the zenith (192 x 108): once scattered light with the multiple scattering on
//     top, and the planet's lit ground below the horizon;
//   - skyCube, skyCubeDownsample: the sky view as a cube with mips (64 a side), for the world's
//     light, the high cloud layer drawn into it while it lights the world (nyx.clouds); skyIrradiance: its diffuse light (16 a side); skySpecular: as a rougher surface
//     reflects it with each mip (64 a side down to 2), as Environment.slang prefilters a cube map;
//   - skyPreview: a table, or the cube unfolded, as an 8-bit picture for the editor's debug window.
// Kilometres throughout (nyx.sky); images in General layout while they are written.

import nyx.bindless;
import nyx.clouds;
import nyx.sky;

// SkyConstants in VulkanSkyAtmosphere.cpp: the view's air, in kilometres
struct SkyConstants
{
    // xyz: per kilometre at the ground; w: the height it falls to 1 / e by
    float4 RayleighScattering;
    float4 MieScattering;
    // xyz: Mie's scattering and absorption; w: its anisotropy (g)
    float4 MieExtinction;
    // xyz: the ozone's absorption where it is thickest; w: the planet's radius
    float4 OzoneAbsorption;
    // xyz: the ground's albedo; w: the top of the air's radius
    float4 GroundAlbedo;
    // xyz: towards the sun; w: the camera's height above the ground
    float4 TowardsSun;
    // The sun's illuminance at the top of the air
    float4 SunIlluminance;
    // What the sky's light is multiplied by, and the night's light added to it
    float4 WarmthTint;
    float4 NightLight;
    // The tables made before, in the bindless array
    uint TransmittanceLut;
    uint MultipleScatteringLut;
    uint SkyViewLut;
    uint SkyCube;
    // The high cloud layer the cube draws (ViewConstants.CloudShell to CloudOrigin): 1 while it
    // does; its noise in the bindless array; how much of it the cube has (Light Strength)
    uint CloudLayer;
    uint CloudNoise;
    float CloudStrength;
    uint CloudPadding;
    float4 CloudShell;
    float4 CloudShape;
    float4 CloudWind;
    float4 CloudOrigin;
}
[vk::binding(0, 1)] ConstantBuffer<SkyConstants> Sky;
[vk::binding(1, 1)] [vk::image_format("rgba16f")] RWTexture2DArray<float4> Target;
[vk::binding(2, 1)] [vk::image_format("rgba16f")] RWTexture2DArray<float4> Previous;
[vk::binding(3, 1)] [vk::image_format("rgba8")] RWTexture2D<float4> Preview;

// SkyDispatch in VulkanSkyAtmosphere.cpp: one dispatch's
struct SkyDispatch
{
    // Of the image written, per side
    uint Width;
    uint Height;
    // The cube or table read, in the bindless array
    uint Source;
    // What skyPreview shows (0 transmittance, 1 multiple scattering, 2 sky view, 3 the cube)
    uint Mode;
    float Roughness;
    // The source's mip to read; the source's first mip's size
    float SourceLevel;
    float SourceSize;
    // What skyPreview multiplies the light by
    float Exposure;
}
[vk::push_constant] ConstantBuffer<SkyDispatch> Dispatch;

static const int TransmittanceSteps = 40;
static const int MultipleScatteringSteps = 20;
static const int SkyViewMinSteps = 4;
static const int SkyViewMaxSteps = 30;
// The multiple scattering's directions, a side of a square
static const uint MultipleScatteringSqrtDirections = 8;

nyx::SkyPlanet planet()
{
    nyx::SkyPlanet p;
    p.Radius = Sky.OzoneAbsorption.w;
    p.Top = Sky.GroundAlbedo.w;
    return p;
}

float4 sampleTable(uint slot, float2 uv)
{
    nyx::Texture texture;
    texture.Index = slot;
    return texture.SampleLevel(nyx::LinearClamp, uv, 0.0);
}

TextureCube sourceCube()
{
    nyx::Cubemap cube;
    cube.Index = Dispatch.Source;
    return cube.get();
}

// What the air is like `height` up: what it scatters (all of it, the molecules', the haze's) and
// what it takes away
struct Medium
{
    float3 Scattering;
    float3 Rayleigh;
    float3 Mie;
    float3 Extinction;
}

Medium sampleMedium(float height)
{
    height = max(height, 0.0);
    const float rayleigh = exp(-height / Sky.RayleighScattering.w);
    const float mie = exp(-height / Sky.MieScattering.w);
    // OzoneCenterHeight, OzoneHalfWidth in SkyAtmosphere.h
    const float ozone = max(0.0, 1.0 - abs(height - 25.0) / 15.0);
    Medium medium;
    medium.Rayleigh = Sky.RayleighScattering.xyz * rayleigh;
    medium.Mie = Sky.MieScattering.xyz * mie;
    medium.Scattering = medium.Rayleigh + medium.Mie;
    medium.Extinction = medium.Rayleigh + Sky.MieExtinction.xyz * mie + Sky.OzoneAbsorption.xyz * ozone;
    return medium;
}

float rayleighPhase(float cosine)
{
    return 3.0 / (16.0 * nyx::SkyPi) * (1.0 + cosine * cosine);
}

float miePhase(float cosine, float g)
{
    const float g2 = g * g;
    const float denominator = max(1.0 + g2 - 2.0 * g * cosine, 1e-6);
    return (1.0 - g2) / (4.0 * nyx::SkyPi * denominator * sqrt(denominator));
}

// The light that gets through the air from `height` up, `cosZenith` from the up axis, to its top;
// none where the planet is in the way
float3 transmittanceTowards(float height, float cosZenith)
{
    const nyx::SkyPlanet p = planet();
    if (cosZenith < nyx::skyHorizonCosine(p, height))
    {
        return float3(0.0);
    }
    return sampleTable(Sky.TransmittanceLut, nyx::transmittanceUv(p, height, cosZenith)).rgb;
}

// The light scattered more than once that reaches a point `height` up, per unit of the sun's
float3 multipleScattering(float height, float sunCosZenith)
{
    return sampleTable(Sky.MultipleScatteringLut, nyx::multipleScatteringUv(planet(), height, sunCosZenith)).rgb;
}

// What a ray from `height` up gathers, in the frame of its start (y up): the light scattered towards
// its start, the light that gets through it, and (for the multiple scattering) what it would gather
// of a light of 1 from every way
struct Scattered
{
    float3 Light;
    float3 Transmittance;
    float3 FromAround;
}

// Along `direction` from `height` up (y up at the start), lit by a sun `towardsSun` of illuminance
// 1: by `steps` steps; with `bPhases` the molecules' and the haze's own phases, else the same every
// way; with `bMultiple` the light scattered more than once on top; with `bGround` the planet's lit
// ground where the ray ends on it
Scattered integrate(float height, float3 direction, float3 towardsSun, int steps, bool bPhases, bool bMultiple, bool bGround)
{
    const nyx::SkyPlanet p = planet();
    Scattered result;
    result.Light = float3(0.0);
    result.Transmittance = float3(1.0);
    result.FromAround = float3(0.0);

    const float cosZenith = direction.y;
    const float toGround = nyx::skyDistanceToGround(p, height, cosZenith);
    const bool bEndsOnGround = toGround >= 0.0;
    const float tMax = bEndsOnGround ? toGround : nyx::skyDistanceToTop(p, height, cosZenith);
    if (tMax <= 0.0)
    {
        return result;
    }

    const float cosToSun = dot(direction, towardsSun);
    const float rayleigh = rayleighPhase(cosToSun);
    const float mie = miePhase(cosToSun, Sky.MieExtinction.w);
    const float uniform = 1.0 / (4.0 * nyx::SkyPi);
    const float r = p.Radius + height;
    const float3 start = float3(0.0, r, 0.0);

    float t = 0.0;
    for (int step = 0; step < steps; ++step)
    {
        // A third of the way into each step, as Hillaire's; the exact step lengths
        const float next = tMax * (float(step) + 0.3) / float(steps);
        const float dt = step == 0 ? next : next - t;
        t = next;
        const float sampleHeight = nyx::skyHeightAlong(p, height, cosZenith, t);
        const float3 up = normalize(start + direction * t);
        const float sunCosZenith = dot(towardsSun, up);
        const Medium medium = sampleMedium(sampleHeight);
        const float3 stepTransmittance = exp(-medium.Extinction * dt);

        const float3 toSun = transmittanceTowards(sampleHeight, sunCosZenith);
        const float3 phaseScattering = bPhases ? medium.Rayleigh * rayleigh + medium.Mie * mie : medium.Scattering * uniform;
        const float3 multiple = bMultiple ? multipleScattering(sampleHeight, sunCosZenith) * medium.Scattering : float3(0.0);
        const float3 source = toSun * phaseScattering + multiple;

        // Each step integrated exactly for its own extinction (Hillaire's Frostbite volumetrics)
        const float3 extinction = max(medium.Extinction, 1e-9);
        result.Light += result.Transmittance * (source - source * stepTransmittance) / extinction;
        result.FromAround += result.Transmittance * (medium.Scattering - medium.Scattering * stepTransmittance) / extinction;
        result.Transmittance *= stepTransmittance;
    }

    // The ground, lit by the sun through the air, sends a part of it back
    if (bGround && bEndsOnGround)
    {
        const float3 up = normalize(start + direction * tMax);
        const float sunCosZenith = dot(towardsSun, up);
        result.Light += transmittanceTowards(0.0, sunCosZenith) * result.Transmittance * saturate(sunCosZenith) * Sky.GroundAlbedo.xyz / nyx::SkyPi;
    }
    return result;
}

// -------------------------------------------------------------------------
// The tables
// -------------------------------------------------------------------------

[shader("compute")]
[numthreads(8, 8, 1)]
void skyTransmittance(uint3 id : SV_DispatchThreadID)
{
    if (id.x >= Dispatch.Width || id.y >= Dispatch.Height)
    {
        return;
    }
    const nyx::SkyPlanet p = planet();
    float height;
    float cosZenith;
    nyx::transmittanceParams(p, (float2(id.xy) + 0.5) / float2(Dispatch.Width, Dispatch.Height), height, cosZenith);

    // To the top of the air, through the planet too: the table is read only for rays that miss it
    // (GetAtmosphereTransmittance() on the CPU)
    const float distance = nyx::skyDistanceToTop(p, height, cosZenith);
    const float step = distance / float(TransmittanceSteps);
    float3 depth = float3(0.0);
    for (int index = 0; index < TransmittanceSteps; ++index)
    {
        const float t = (float(index) + 0.5) * step;
        depth += sampleMedium(nyx::skyHeightAlong(p, height, cosZenith, t)).Extinction * step;
    }
    Target[uint3(id.xy, 0)] = float4(exp(-depth), 1.0);
}

groupshared float3 SharedFromAround[64];
groupshared float3 SharedLight[64];

// A texel a group, each thread one of 64 directions over the sphere: the light scattered twice
// (the light once scattered from every way, scattered again the same every way), and the part f of
// light that scatters again; all orders together are L / (1 - f) (Hillaire's equations 5 to 10)
[shader("compute")]
[numthreads(1, 1, 64)]
void skyMultipleScattering(uint3 group : SV_GroupID, uint3 thread : SV_GroupThreadID)
{
    const nyx::SkyPlanet p = planet();
    float2 uv = (float2(group.xy) + 0.5) / nyx::MultipleScatteringLutSize;
    uv = float2(nyx::subUvToUnit(uv.x, nyx::MultipleScatteringLutSize), nyx::subUvToUnit(uv.y, nyx::MultipleScatteringLutSize));
    const float sunCosZenith = uv.x * 2.0 - 1.0;
    const float3 towardsSun = float3(sqrt(saturate(1.0 - sunCosZenith * sunCosZenith)), sunCosZenith, 0.0);
    // A little above the ground, so that a ray along it isn't taken for one into it
    const float height = clamp(uv.y * (p.Top - p.Radius), 1e-4, p.Top - p.Radius - 1e-4);

    const uint index = thread.z;
    const float i = 0.5 + float(index / MultipleScatteringSqrtDirections);
    const float j = 0.5 + float(index % MultipleScatteringSqrtDirections);
    const float theta = 2.0 * nyx::SkyPi * i / float(MultipleScatteringSqrtDirections);
    const float cosPhi = 1.0 - 2.0 * j / float(MultipleScatteringSqrtDirections);
    const float sinPhi = sqrt(saturate(1.0 - cosPhi * cosPhi));
    const float3 direction = float3(cos(theta) * sinPhi, cosPhi, sin(theta) * sinPhi);
    const Scattered scattered = integrate(height, direction, towardsSun, MultipleScatteringSteps, false, false, true);

    const float solidAngle = 4.0 * nyx::SkyPi / float(MultipleScatteringSqrtDirections * MultipleScatteringSqrtDirections);
    SharedFromAround[index] = scattered.FromAround * solidAngle;
    SharedLight[index] = scattered.Light * solidAngle;
    GroupMemoryBarrierWithGroupSync();
    for (uint half = 32; half > 0; half >>= 1)
    {
        if (index < half)
        {
            SharedFromAround[index] += SharedFromAround[index + half];
            SharedLight[index] += SharedLight[index + half];
        }
        GroupMemoryBarrierWithGroupSync();
    }
    if (index != 0)
    {
        return;
    }
    const float uniform = 1.0 / (4.0 * nyx::SkyPi);
    const float3 again = SharedFromAround[0] * uniform;
    const float3 twice = SharedLight[0] * uniform;
    Target[uint3(group.xy, 0)] = float4(twice / max(1.0 - again, 1e-4), 1.0);
}

// The sky view's light along a direction from the camera, lit by the view's sun
float3 skyLight(float3 direction, float3 towardsSun, int steps)
{
    const Scattered scattered = integrate(Sky.TowardsSun.w, direction, towardsSun, steps, true, true, true);
    return scattered.Light * Sky.SunIlluminance.xyz * Sky.WarmthTint.xyz + Sky.NightLight.xyz;
}

[shader("compute")]
[numthreads(8, 8, 1)]
void skySkyView(uint3 id : SV_DispatchThreadID)
{
    if (id.x >= Dispatch.Width || id.y >= Dispatch.Height)
    {
        return;
    }
    const nyx::SkyPlanet p = planet();
    const float height = Sky.TowardsSun.w;
    float viewCosZenith;
    float lightViewCosine;
    nyx::skyViewParams(p, height, (float2(id.xy) + 0.5) / float2(Dispatch.Width, Dispatch.Height), viewCosZenith, lightViewCosine);

    // In the frame of the camera: the sun in the x-y plane, the direction its angle round from it
    const float sunCosZenith = clamp(Sky.TowardsSun.y, -1.0, 1.0);
    const float3 towardsSun = float3(sqrt(saturate(1.0 - sunCosZenith * sunCosZenith)), sunCosZenith, 0.0);
    const float viewSinZenith = sqrt(saturate(1.0 - viewCosZenith * viewCosZenith));
    const float3 direction = float3(viewSinZenith * lightViewCosine, viewCosZenith, viewSinZenith * sqrt(saturate(1.0 - lightViewCosine * lightViewCosine)));

    // More steps along the longer rays, the near-horizontal ones
    const float toGround = nyx::skyDistanceToGround(p, height, viewCosZenith);
    const float length = toGround >= 0.0 ? toGround : nyx::skyDistanceToTop(p, height, viewCosZenith);
    const int steps = int(lerp(float(SkyViewMinSteps), float(SkyViewMaxSteps), saturate(length / 100.0)));
    Target[uint3(id.xy, 0)] = float4(skyLight(direction, towardsSun, steps), 1.0);
}

// A cube face's texel as a direction (Vulkan's faces: +X, -X, +Y, -Y, +Z, -Z), as Environment.slang
float3 cubeDirection(uint3 texel, uint size)
{
    const float2 st = (float2(texel.xy) + 0.5) / float(size) * 2.0 - 1.0;
    const float s = st.x;
    const float t = st.y;
    float3 direction;
    switch (texel.z)
    {
    case 0: direction = float3(1.0, -t, -s); break;
    case 1: direction = float3(-1.0, -t, s); break;
    case 2: direction = float3(s, 1.0, t); break;
    case 3: direction = float3(s, -1.0, -t); break;
    case 4: direction = float3(s, -t, 1.0); break;
    default: direction = float3(-s, -t, -1.0); break;
    }
    return normalize(direction);
}

// The sky view in a direction of the world, as nyx::skyAtmosphereRadiance() reads it, from the
// constants rather than the view's
float3 skyViewIn(float3 direction)
{
    const nyx::SkyPlanet p = planet();
    const float height = Sky.TowardsSun.w;
    const bool bGround = direction.y < nyx::skyHorizonCosine(p, height);
    return sampleTable(Sky.SkyViewLut, nyx::skyViewUv(p, height, direction.y, nyx::skyAzimuthCosine(direction, Sky.TowardsSun.xyz), bGround)).rgb;
}

// What the cloud layer the cube draws is made from, by the constants: the camera's height the
// tables are made for, the sun at the top of the air, and the sky's light on the clouds from above
// and below, from the sky view (its mean over a few directions each way: the irradiance is made
// from this cube, after it)
nyx::CloudParams skyCloudParams()
{
    nyx::CloudParams p;
    p.Planet = planet();
    p.CameraHeight = Sky.TowardsSun.w;
    p.Shell = Sky.CloudShell;
    p.Shape = Sky.CloudShape;
    p.Wind = Sky.CloudWind;
    p.Origin = Sky.CloudOrigin;
    p.SunLight = Sky.SunIlluminance.rgb / nyx::SkyPi;
    p.TowardsSun = Sky.TowardsSun.xyz;
    p.Noise = Sky.CloudNoise;
    p.Transmittance = Sky.TransmittanceLut;
    const float level = 0.5;
    const float slope = 0.866;
    p.SkyAbove = (skyViewIn(float3(0.0, 1.0, 0.0)) + skyViewIn(float3(slope, level, 0.0)) + skyViewIn(float3(-slope, level, 0.0)) + skyViewIn(float3(0.0, level, slope))
        + skyViewIn(float3(0.0, level, -slope))) * 0.2;
    p.SkyBelow = (skyViewIn(float3(0.0, -1.0, 0.0)) + skyViewIn(float3(slope, -level, 0.0)) + skyViewIn(float3(-slope, -level, 0.0)) + skyViewIn(float3(0.0, -level, slope))
        + skyViewIn(float3(0.0, -level, -slope))) * 0.2;
    return p;
}

// The sky as a cube, without the sun's disk, which lights the world by itself; while the cloud
// layer lights the world, with its clouds over the sky as the camera sees them, by Light Strength
[shader("compute")]
[numthreads(8, 8, 1)]
void skyCube(uint3 id : SV_DispatchThreadID)
{
    if (id.x >= Dispatch.Width || id.y >= Dispatch.Width)
    {
        return;
    }
    const float3 direction = cubeDirection(id, Dispatch.Width);
    float3 sky = skyViewIn(direction);
    if (Sky.CloudLayer != 0)
    {
        // A texel's footprint: the ways to its neighbours
        const float3 towardsX = cubeDirection(id + uint3(1, 0, 0), Dispatch.Width) - direction;
        const float3 towardsY = cubeDirection(id + uint3(0, 1, 0), Dispatch.Width) - direction;
        const float4 clouds = nyx::cloudLayerAlong(skyCloudParams(), direction, towardsX, towardsY);
        sky = lerp(sky, sky * clouds.a + clouds.rgb, Sky.CloudStrength);
    }
    Target[id] = float4(sky, 1.0);
}

[shader("compute")]
[numthreads(8, 8, 1)]
void skyCubeDownsample(uint3 id : SV_DispatchThreadID)
{
    if (id.x >= Dispatch.Width || id.y >= Dispatch.Width)
    {
        return;
    }
    const uint3 corner = uint3(id.xy * 2, id.z);
    Target[id] = 0.25 * (Previous[corner] + Previous[corner + uint3(1, 0, 0)] + Previous[corner + uint3(0, 1, 0)] + Previous[corner + uint3(1, 1, 0)]);
}

// The diffuse light reaching a surface facing each texel's direction, over pi (what a white
// Lambertian surface shows): every texel of one of the cube's mips, by the cosine and the solid
// angle it covers
[shader("compute")]
[numthreads(8, 8, 1)]
void skyIrradiance(uint3 id : SV_DispatchThreadID)
{
    if (id.x >= Dispatch.Width || id.y >= Dispatch.Width)
    {
        return;
    }
    const float3 n = cubeDirection(id, Dispatch.Width);
    const uint size = uint(Dispatch.SourceSize);
    float3 sum = float3(0.0);
    for (uint face = 0; face < 6; ++face)
    {
        for (uint y = 0; y < size; ++y)
        {
            for (uint x = 0; x < size; ++x)
            {
                const uint3 texel = uint3(x, y, face);
                const float3 d = cubeDirection(texel, size);
                const float cosine = dot(n, d);
                if (cosine <= 0.0)
                {
                    continue;
                }
                const float2 st = (float2(texel.xy) + 0.5) / float(size) * 2.0 - 1.0;
                const float solidAngle = 4.0 / (float(size) * float(size) * pow(1.0 + dot(st, st), 1.5));
                sum += sourceCube().SampleLevel(nyx::LinearClamp, d, Dispatch.SourceLevel).rgb * cosine * solidAngle;
            }
        }
    }
    Target[id] = float4(sum / nyx::SkyPi, 1.0);
}

float2 hammersley(uint index, uint count)
{
    return float2(float(index) / float(count), float(reversebits(index)) * 2.3283064365386963e-10);
}

// A half vector around `n` drawn from GGX of `alpha` (roughness squared)
float3 sampleGgx(float2 xi, float3 n, float alpha)
{
    const float phi = 2.0 * nyx::SkyPi * xi.x;
    const float cosTheta = min(sqrt((1.0 - xi.y) / (1.0 + (alpha * alpha - 1.0) * xi.y)), 1.0);
    const float sinTheta = sqrt(max(1.0 - cosTheta * cosTheta, 0.0));
    const float3 h = float3(sinTheta * cos(phi), sinTheta * sin(phi), cosTheta);
    const float3 up = abs(n.z) < 0.999 ? float3(0.0, 0.0, 1.0) : float3(1.0, 0.0, 0.0);
    const float3 tangent = normalize(cross(up, n));
    const float3 bitangent = cross(n, tangent);
    return normalize(tangent * h.x + bitangent * h.y + n * h.z);
}

// One mip of the specular cube, as Environment.slang's environmentPrefilter (n = v = r), with
// fewer samples: the sky is smooth, and its sun is drawn apart
[shader("compute")]
[numthreads(8, 8, 1)]
void skySpecular(uint3 id : SV_DispatchThreadID)
{
    if (id.x >= Dispatch.Width || id.y >= Dispatch.Width)
    {
        return;
    }
    const float3 n = cubeDirection(id, Dispatch.Width);
    if (Dispatch.Roughness <= 0.0)
    {
        Target[id] = float4(sourceCube().SampleLevel(nyx::LinearClamp, n, Dispatch.SourceLevel).rgb, 1.0);
        return;
    }
    const float alpha = Dispatch.Roughness * Dispatch.Roughness;
    const uint count = 32;
    const float texelSolidAngle = 4.0 * nyx::SkyPi / (6.0 * Dispatch.SourceSize * Dispatch.SourceSize);
    float3 sum = float3(0.0);
    float weight = 0.0;
    for (uint index = 0; index < count; ++index)
    {
        const float3 h = sampleGgx(hammersley(index, count), n, alpha);
        const float nDotH = saturate(dot(n, h));
        const float3 l = normalize(2.0 * dot(n, h) * h - n);
        const float nDotL = dot(n, l);
        if (nDotL > 0.0)
        {
            const float a2 = alpha * alpha;
            const float d = nDotH * nDotH * (a2 - 1.0) + 1.0;
            const float pdf = a2 / (nyx::SkyPi * d * d) * 0.25;
            const float sampleSolidAngle = 1.0 / (float(count) * pdf + 0.0001);
            const float level = max(0.5 * log2(sampleSolidAngle / texelSolidAngle) + 1.0, 0.0);
            sum += sourceCube().SampleLevel(nyx::LinearClamp, l, level).rgb * nDotL;
            weight += nDotL;
        }
    }
    Target[id] = float4(sum / max(weight, 0.0001), 1.0);
}

// -------------------------------------------------------------------------
// The debug window's pictures
// -------------------------------------------------------------------------

float3 previewColor(float3 light)
{
    // Reinhard, then sRGB-encoded
    const float3 c = light * Dispatch.Exposure;
    const float3 mapped = c / (1.0 + c);
    return select(mapped <= 0.0031308, mapped * 12.92, 1.055 * pow(mapped, 1.0 / 2.4) - 0.055);
}

// A table as it is (mode 0, the transmittance, 0 to 1), exposed (1 and 2), or the cube unfolded
// as a cross, four faces wide and three high (3)
[shader("compute")]
[numthreads(8, 8, 1)]
void skyPreview(uint3 id : SV_DispatchThreadID)
{
    if (id.x >= Dispatch.Width || id.y >= Dispatch.Height)
    {
        return;
    }
    const float2 uv = (float2(id.xy) + 0.5) / float2(Dispatch.Width, Dispatch.Height);
    if (Dispatch.Mode == 0)
    {
        Preview[id.xy] = float4(saturate(sampleTable(Dispatch.Source, uv).rgb), 1.0);
        return;
    }
    if (Dispatch.Mode < 3)
    {
        Preview[id.xy] = float4(previewColor(sampleTable(Dispatch.Source, uv).rgb), 1.0);
        return;
    }

    // The cross: +Y above, -X, +Z, +X, -Z across the middle, -Y below
    const float2 cell = uv * float2(4.0, 3.0);
    const int2 face = int2(floor(cell));
    const float2 st = frac(cell) * 2.0 - 1.0;
    float3 direction;
    if (face.y == 1)
    {
        switch (face.x)
        {
        case 0: direction = float3(-1.0, -st.y, st.x); break;
        case 1: direction = float3(st.x, -st.y, 1.0); break;
        case 2: direction = float3(1.0, -st.y, -st.x); break;
        default: direction = float3(-st.x, -st.y, -1.0); break;
        }
    }
    else if (face.x == 1)
    {
        direction = face.y == 0 ? float3(st.x, 1.0, st.y) : float3(st.x, -1.0, -st.y);
    }
    else
    {
        Preview[id.xy] = float4(0.0, 0.0, 0.0, 1.0);
        return;
    }
    Preview[id.xy] = float4(previewColor(sourceCube().SampleLevel(nyx::LinearClamp, normalize(direction), 0.0).rgb), 1.0);
}
