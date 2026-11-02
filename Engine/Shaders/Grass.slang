// The grass of a view, each frame (VulkanGrass): grassBlades goes over the ground points of the tiles
// in view and keeps the blades that are in view and near enough, thinning them with the distance;
// grassVertex builds each from its numbers, a curve up from its foot, of fewer segments far away;
// grassFragment lights it.

import nyx.atmosphere;
import nyx.bindless;
import nyx.clouds;
import nyx.grass;
import nyx.lighting;
import nyx.shadows;
import nyx.terrainocclusion;
import nyx.view;
import nyx.worldwind;

[vk::binding(0, 2)] StructuredBuffer<nyx::GrassPoint> Points;
[vk::binding(1, 2)] StructuredBuffer<nyx::GrassKind> Kinds;
[vk::binding(2, 2)] RWStructuredBuffer<nyx::GrassBlade> BladesWritten;
// Two draws' arguments: the near blades', then the far ones'
[vk::binding(3, 2)] RWStructuredBuffer<uint> DrawArguments;
[vk::binding(4, 2)] StructuredBuffer<nyx::GrassBlade> Blades;
[vk::binding(5, 2)] StructuredBuffer<nyx::GrassTileCount> Tiles;

struct Constants
{
    // The view's frustum's sides, facing inwards, of unit normal
    float4 Planes[4];
    float2 Corner;
    float PlaceUnit;
    uint FirstPoint;
    uint Slot;
    uint Kind;
    // The ground's slide with the terrain's level of detail: from how far, and 1 / over how far
    float2 Morph;
    // Where the near and the far blades are in the buffer, and how many of each it can hold
    uint NearCapacity;
    uint FarCapacity;
    // For the draws: the blades' first, and a blade's segments
    uint FirstBlade;
    uint Segments;
}
[vk::push_constant] ConstantBuffer<Constants> Tile;

// Blades nearer than this have the near shape
static const float NearShapeDistance = 15.0;
// The tip's offset is kept to this much of its height, so a blade is never quite flat
static const float MaxLeanShare = 0.97;

[shader("compute")]
[numthreads(64, 1, 1)]
void grassBlades(uint3 id : SV_DispatchThreadID)
{
    if (id.x >= Tiles[Tile.Slot].Count)
    {
        return;
    }
    const nyx::GrassPoint point = Points[Tile.FirstPoint + id.x];
    const nyx::GrassKind kind = Kinds[Tile.Kind];

    float3 base = float3(Tile.Corner.x + float(point.Place & 65535u) * Tile.PlaceUnit, point.Y, Tile.Corner.y + float(point.Place >> 16) * Tile.PlaceUnit);
    const float3 toBlade = base - View.CameraPosition.xyz;
    const float distance = length(toBlade);
    if (distance >= kind.Range)
    {
        return;
    }
    if (Tile.Morph.y > 0.0)
    {
        base.y += (float(point.RankClump >> 20) - 2048.0) * nyx::GrassMorphUnit * saturate((distance - Tile.Morph.x) * Tile.Morph.y);
    }

    // Thinned with the distance: a blade grows from nothing as the share drawn comes to its rank, so
    // none pops
    const float share = nyx::grassDrawnShare(kind.FullDensityDistance, kind.Range, distance);
    const float rank = float(point.RankClump & 65535u) / 65535.0;
    if (rank >= share)
    {
        return;
    }
    const float growth = saturate((share - rank) / (share * 0.25));

    const uint random = point.NormalRandom >> 16;
    const float random0 = float(random & 255u) / 255.0;
    const float random1 = float(random >> 8) / 255.0;
    const uint hash = nyx::grassHash(random, point.Place, kind.Seed + 3u);

    // Its clump: the nearest found when the tile was made, one of the nine round the blade
    const int2 clumpCell = int2(floor(base.xz / kind.ClumpSize)) + int2(int((point.RankClump >> 16) & 3u) - 1, int((point.RankClump >> 18) & 3u) - 1);
    const uint clumpPlace = nyx::grassHash(uint(clumpCell.x), uint(clumpCell.y), kind.Seed + 1u);
    const uint clumpValues = nyx::grassHash(uint(clumpCell.x), uint(clumpCell.y), kind.Seed + 4u);
    const float2 clumpCentre = (float2(clumpCell) + float2(nyx::grassRandom(clumpPlace), nyx::grassRandom(clumpPlace, 16))) * kind.ClumpSize;
    const float clumpHeight = float(clumpValues & 255u) / 255.0;
    const float clumpFacing = float((clumpValues >> 8) & 255u) / 255.0 * 6.2831853;
    const float clumpTone = float((clumpValues >> 16) & 255u) / 255.0;
    const float clumpDry = float(clumpValues >> 24) / 255.0;

    // The patches: taller and drier here and there, over more than a clump
    const float patch = nyx::grassPatch(base.xz / kind.PatchSize, kind.Seed + 5u);
    const float patchDry = nyx::grassPatch(base.xz / (kind.PatchSize * 0.7) + 31.7, kind.Seed + 6u);

    const float heightShare = lerp(random0, clumpHeight, kind.ClumpHeight);
    const float height = kind.Height * (1.0 + kind.HeightVariance * (heightShare * 2.0 - 1.0)) * lerp(1.0, patch * 2.0, kind.PatchHeight) * growth;

    // In view, with room for how far it can reach
    const float3 middle = base + float3(0.0, height * 0.5, 0.0);
    for (uint plane = 0; plane < 4; ++plane)
    {
        if (dot(Tile.Planes[plane].xyz, middle) + Tile.Planes[plane].w < -height)
        {
            return;
        }
    }

    // Past the blades' full density each stands for more of them, so grows wider, up to a point
    const float widening = min(sqrt(1.0 / max(share, 0.0625)), 4.0);
    const float width = kind.Width * (1.0 + kind.WidthVariance * (nyx::grassRandom(hash) * 2.0 - 1.0)) * widening;

    // Its own way, its clump's, and towards the clump's middle
    const float facing = random1 * 6.2831853;
    const float2 towardsCentre = clumpCentre - base.xz;
    const float fromCentre = length(towardsCentre);
    float2 way = lerp(float2(cos(facing), sin(facing)), float2(cos(clumpFacing), sin(clumpFacing)), kind.ClumpFacing);
    way += towardsCentre / max(fromCentre, 0.001) * kind.ClumpPull * 2.0;
    way /= max(length(way), 0.0001);

    const float lean = kind.Lean + kind.LeanVariance * (nyx::grassRandom(hash, 16) * 2.0 - 1.0);
    float2 tip = way * sin(clamp(lean, 0.0, 1.5)) * height;

    // The wind where most of it is, over all it leans by: gusts cross the field as bands of blades
    // bowing, each as the air reaches it. How far it bends with the speed: less and less, up to a most.
    const float3 wind = nyx::windAt(base + float3(0.0, height * 0.7, 0.0), View.Time);
    const float windSpeed = length(wind.xz);
    const float windShare = windSpeed / kind.Stiffness;
    const float windBend = kind.WindResponse * windShare / sqrt(1.0 + windShare * windShare);
    if (windSpeed > 0.001)
    {
        tip += wind.xz / windSpeed * windBend * height;
    }
    tip *= min(MaxLeanShare * height / max(length(tip), 0.0001), 1.0);

    // The clump's tone and how dry the tips are; the blade's own brightness
    const float dryness = saturate(smoothstep(0.45, 0.85, patchDry) * kind.PatchDryness + (clumpDry - 0.5) * kind.ClumpColor * 0.5);
    const float brightness = (1.0 + (clumpTone - 0.5) * kind.ClumpColor) * (1.0 + (float(hash >> 16 & 255u) / 255.0 - 0.5) * 2.0 * kind.ColorVariance);
    const float3 bladeTip = lerp(kind.TipColor, kind.DryColor, dryness) * brightness;
    const float3 bladeRoot = kind.RootColor * brightness;
    // Darker towards the clump's middle, where less light gets down
    const float clumpShade = saturate(1.0 - fromCentre / kind.ClumpSize);

    // Far blades take the ground's colour, last of all
    const float towardsGround = smoothstep(kind.Range * 0.55, kind.Range * 0.9, distance);
    const float3 ground = nyx::unpackGrassColor(point.Ground);
    const float3 tipColor = lerp(bladeTip, ground, towardsGround);
    const float3 rootColor = lerp(bladeRoot, ground, towardsGround);

    const bool bNear = distance < NearShapeDistance;
    const uint capacity = bNear ? Tile.NearCapacity : Tile.FarCapacity;
    uint index = 0;
    InterlockedAdd(DrawArguments[bNear ? 1 : 5], 1u, index);
    if (index >= capacity)
    {
        return;
    }

    nyx::GrassBlade blade;
    blade.Base = base;
    blade.Size = uint(saturate(height / (nyx::GrassBladeHeightUnit * 65535.0)) * 65535.0) | (uint(saturate(width / (nyx::GrassBladeWidthUnit * 65535.0)) * 65535.0) << 16);
    const float2 tipPacked = clamp(tip / nyx::GrassBladeTipUnit + 32768.0, 0.0, 65535.0);
    blade.Tip = uint(tipPacked.x) | (uint(tipPacked.y) << 16);
    const uint towardsFar = uint(smoothstep(NearShapeDistance * 0.75, NearShapeDistance, distance) * 255.0);
    blade.Shape = uint(saturate(kind.Bend + windBend * 0.4) * 255.0) | ((hash >> 24) << 8) | (towardsFar << 16) | (uint(clumpShade * 255.0) << 24);
    blade.TipColor = nyx::packGrassColor(tipColor);
    blade.RootColor = nyx::packGrassColor(rootColor);
    blade.Extras = (point.NormalRandom & 65535u) | (Tile.Kind << 16) | (uint(saturate(windBend) * 255.0) << 24);
    BladesWritten[(bNear ? 0u : Tile.NearCapacity) + index] = blade;
}

// After the frame's tiles: the draws kept to what the buffer holds
[shader("compute")]
[numthreads(1, 1, 1)]
void grassBladesDone()
{
    DrawArguments[1] = min(DrawArguments[1], Tile.NearCapacity);
    DrawArguments[5] = min(DrawArguments[5], Tile.FarCapacity);
}

struct VertexOutput
{
    float4 Position : SV_Position;
    float3 Color : COLOR;
    float3 NormalWS : NORMAL;
    float3 PositionWS : POSITION_WS;
    // Across the blade, and which side of it: -1 to 1
    float3 AcrossWS : ACROSS;
    float Side : SIDE;
    // Up the blade, 0 to 1
    float Along : ALONG;
    nointerpolation float3 GroundNormal : GROUND_NORMAL;
    nointerpolation float ClumpShade : CLUMP_SHADE;
    nointerpolation uint Kind : KIND;
}

// A point of the curve from its foot to its tip with four control points
float3 bezier(float3 p1, float3 p2, float3 p3, float t)
{
    const float u = 1.0 - t;
    return 3.0 * u * u * t * p1 + 3.0 * u * t * t * p2 + t * t * t * p3;
}

// The curve's direction there
float3 bezierTangent(float3 p1, float3 p2, float3 p3, float t)
{
    const float u = 1.0 - t;
    return 3.0 * u * u * p1 + 6.0 * u * t * (p2 - p1) + 3.0 * t * t * (p3 - p2);
}

[shader("vertex")]
VertexOutput grassVertex(uint vertexId : SV_VulkanVertexID, uint instanceId : SV_VulkanInstanceID)
{
    const nyx::GrassBlade blade = Blades[Tile.FirstBlade + instanceId];

    // A strip: two vertices a ring from the foot, and one at the tip
    const uint segments = Tile.Segments;
    const uint ring = vertexId / 2;
    float t = float(ring) / float(segments);
    const float side = ring == segments ? 0.0 : (vertexId & 1u) == 0u ? -1.0 : 1.0;
    // The near shape's vertices slide to where the far shape has its before it takes over
    if (segments > 3u)
    {
        const float towardsFar = float((blade.Shape >> 16) & 255u) / 255.0;
        t = lerp(t, round(t * 3.0) / 3.0, towardsFar);
    }

    const float height = float(blade.Size & 65535u) * nyx::GrassBladeHeightUnit;
    const float width = float(blade.Size >> 16) * nyx::GrassBladeWidthUnit;
    const float2 tipAcross = (float2(float(blade.Tip & 65535u), float(blade.Tip >> 16)) - 32768.0) * nyx::GrassBladeTipUnit;
    const float tipUp = sqrt(max(height * height - dot(tipAcross, tipAcross), 0.0001));
    const float3 tip = float3(tipAcross.x, tipUp, tipAcross.y);
    const float bend = float(blade.Shape & 255u) / 255.0;

    // Straight without bend; with it, up first and over at the end
    const float3 up = float3(0.0, height, 0.0);
    const float3 p1 = lerp(tip / 3.0, up * 0.6, bend);
    const float3 p2 = lerp(tip * 2.0 / 3.0, tip + up * 0.3, bend);

    const float3 facing = length(tipAcross) > 0.0001 ? float3(tipAcross.x, 0.0, tipAcross.y) / length(tipAcross) : float3(1.0, 0.0, 0.0);
    const float3 across = float3(-facing.z, 0.0, facing.x);
    const float3 tangent = normalize(bezierTangent(p1, p2, tip, t));
    const float3 normal = normalize(cross(across, tangent));

    // The flutter: each blade at its own phase, the tip most, more the harder it blows
    const nyx::GrassKind kind = Kinds[(blade.Extras >> 16) & 255u];
    const float windShare = float(blade.Extras >> 24) / 255.0;
    const float phase = float((blade.Shape >> 8) & 255u) / 255.0 * 6.2831853;
    const float flutter = sin(View.Time * (5.0 + phase) + phase + t * 3.0) * kind.Flutter * windShare * t * t * height * 0.12;

    // Seen edge on a blade would be a line: it is widened there, so the field stays as full from any side
    const float3 curve = blade.Base + bezier(p1, p2, tip, t);
    const float edgeOn = 1.0 - abs(dot(normal, normalize(View.CameraPosition.xyz - curve)));
    const float thickened = width * (1.0 + 1.5 * edgeOn * edgeOn * edgeOn);

    const float3 positionWS = curve + across * side * thickened * 0.5 * (1.0 - t * t) + normal * flutter;

    VertexOutput output;
    output.Position = mul(View.ViewProjection, float4(positionWS, 1.0));
    output.Color = lerp(nyx::unpackGrassColor(blade.RootColor), nyx::unpackGrassColor(blade.TipColor), t);
    output.NormalWS = normal;
    output.PositionWS = positionWS;
    output.AcrossWS = across;
    output.Side = side;
    output.Along = t;
    output.GroundNormal = nyx::unpackGrassNormal(blade.Extras & 65535u);
    output.ClumpShade = float(blade.Shape >> 24) / 255.0;
    output.Kind = (blade.Extras >> 16) & 255u;
    return output;
}

struct Targets
{
    float4 Color : SV_Target0;
    float4 Ambient : SV_Target1;
}

[shader("fragment")]
Targets grassFragment(VertexOutput input, bool bFrontFace : SV_IsFrontFace)
{
    const nyx::GrassKind kind = Kinds[input.Kind];
    const float3 toCamera = View.CameraPosition.xyz - input.PositionWS;
    const float distance = length(toCamera);
    const float3 view = toCamera / max(distance, 0.001);
    // Its normal turned out towards each edge, as if the flat blade were rounded; far away the
    // ground's, so the field doesn't sparkle where its blades are smaller than a pixel
    const float3 flat = normalize(input.NormalWS) * (bFrontFace ? 1.0 : -1.0);
    const float3 rounded = normalize(flat + input.AcrossWS * input.Side * 0.9);
    const float3 near = normalize(lerp(flat, rounded, kind.NormalRounding));
    const float3 normal = normalize(lerp(near, input.GroundNormal, smoothstep(kind.Range * 0.25, kind.Range * 0.7, distance)));
    // Dark at its foot, where little light gets down, and in the middle of its clump
    const float low = 1.0 - saturate(input.Along);
    const float shade = (1.0 - kind.SelfShadow * low * low) * (1.0 - kind.SelfShadow * 0.5 * input.ClumpShade * low);
    const float3 baseColor = input.Color * shade;
    Targets targets;
    if (nyx::isMaterialDebugView())
    {
        targets.Color = float4(View.DebugView == nyx::DebugNormal ? normal * 0.5 + 0.5 : baseColor, 1.0);
        targets.Ambient = float4(0.0, 0.0, 0.0, 1.0);
        return targets;
    }
    const float occlusion = nyx::terrainSkyVisibility(input.PositionWS, input.GroundNormal) * shade;
    // The sun's shadow by the ground's normal: one that lights a blade from behind falls on it as well
    const float sun = nyx::sunShadow(input.PositionWS, input.GroundNormal);
    nyx::SurfaceLight light = nyx::lightSurfaceApart(baseColor, 0.0, kind.Roughness, occlusion, normal, view, input.PositionWS, sun);
    // The sun through the blade: on its side away from the sun, brightest looking towards it, the
    // light taking the colour of what it passed through
    const float3 toSun = normalize(View.LightDirection.xyz);
    const float through = saturate(dot(-normal, toSun)) * 0.4 + pow(saturate(dot(-view, toSun)), 3.0) * 0.6;
    light.Direct += baseColor * sqrt(max(baseColor, 0.0)) * View.LightColor.rgb * sun * kind.Translucency * through * saturate(input.Along * 1.5);
    const nyx::Fog fog = nyx::heightFog(input.PositionWS);
    const nyx::CloudCover clouds = nyx::cloudCoverBefore(input.PositionWS, input.Position.xy);
    if (View.AmbientOcclusion != 0)
    {
        targets.Color = float4(fog.apply(clouds.apply(light.Direct)), 1.0);
        targets.Ambient = float4(light.Ambient * (fog.Transmittance * clouds.Transmittance), 1.0);
    }
    else
    {
        targets.Color = float4(fog.apply(clouds.apply(light.Direct + light.Ambient)), 1.0);
        targets.Ambient = float4(0.0, 0.0, 0.0, 1.0);
    }
    return targets;
}
