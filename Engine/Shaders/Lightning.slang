// Lightning (VulkanLightning; Docs/Design/lightning.md): each bolt's paths as strips facing the
// camera, built on the CPU per view (BuildLightningStrips()), drawn after the view's ambient
// occlusion and before its particles into the view's lightning picture, each pixel the brightest of
// the strips over it, then that picture added to the scene in HDR; with no depth buffer: each pixel is
// tested against the scene's depth, which it reads (View.SceneDepth), and fades out over the soft
// distance in front of what is behind it. A bolt past the camera's far plane is held at it and
// shows against the sky, which is as far as can be. Its light is dimmed by the fog and by the clouds before it,
// so a bolt's top inside the clouds is hidden. Across a strip: a narrow white core, the
// coloured body round it, and a glow falling off to the strip's edges, by each pixel's distance to
// its stretch of path; a round sprite the same from its middle out. The draw's push constants hold
// the bolt's look (LightningLook), in the columns of Object.Model:
//   - 0: the core's colour times its brightness, and its half width against the glow's (w);
//   - 1: the body's, and its half width against the glow's (w);
//   - 2: the glow's, and the soft distance in metres (w);
//   - 3: x the power of the clouds' transmittance its light is dimmed by (Cloud Hiding), y how hard
//     the core's and the body's edges are (Sharpness, 0 soft peaks to 1 a step).

import nyx.bindless;
import nyx.view;
import nyx.atmosphere;
import nyx.clouds;
import nyx.environment;

// One vertex (LightningVertex in Lightning.h): in the world, and the stretch of path its quad draws,
// the same for each of the quad's vertices: its ends, the glow's half width and the brightness at
// each
struct LightningVertex
{
    float4 Position : LIGHTNING_POSITION;
    float4 Start : LIGHTNING_START;
    float4 End : LIGHTNING_END;
    float4 Intensity : LIGHTNING_INTENSITY;
}

struct LightningVaryings
{
    float4 Position : SV_Position;
    float3 PositionWS : POSITION_WS;
    nointerpolation float4 Start : START;
    nointerpolation float4 End : END;
    nointerpolation float4 Intensity : INTENSITY;
}

// A column of the look (Object.Model's, as the C++ side wrote it)
float4 lookColumn(uint column)
{
    float4 unit = float4(0.0);
    unit[column] = 1.0;
    return mul(Object.Model, unit);
}

[shader("vertex")]
LightningVaryings lightningVertex(LightningVertex input)
{
    LightningVaryings output;
    output.Position = mul(View.ViewProjection, float4(input.Position.xyz, 1.0));
    // Past the far plane (0, reversed), held at it rather than cut off
    output.Position.z = max(output.Position.z, 0.0);
    output.PositionWS = input.Position.xyz;
    output.Start = input.Start;
    output.End = input.End;
    output.Intensity = input.Intensity;
    return output;
}

// A depth of the depth buffer as metres along the view (reversed: MakePerspectiveProjection())
float linearDepth(float depth)
{
    return View.ProjectionInfo.w / max(depth + View.ProjectionInfo.z, 1e-7);
}

[shader("fragment")]
float4 lightningFragment(LightningVaryings input) : SV_Target
{
    // How far across the glow the pixel is: its distance to the stretch (to its nearer end past
    // them, so joins and ends are round) over the glow's half width there
    const float3 along = input.End.xyz - input.Start.xyz;
    const float part = saturate(dot(input.PositionWS - input.Start.xyz, along) / max(dot(along, along), 1e-8));
    const float halfWidth = lerp(input.Start.w, input.End.w, part);
    const float intensity = lerp(input.Intensity.x, input.Intensity.y, part);
    const float across = length(input.PositionWS - (input.Start.xyz + along * part)) / max(halfWidth, 1e-6);
    if (across >= 1.0 || intensity <= 0.0)
    {
        discard;
    }
    const float4 core = lookColumn(0);
    const float4 body = lookColumn(1);
    const float4 glow = lookColumn(2);
    const float cloudHiding = lookColumn(3).x;
    const float sharpness = lookColumn(3).y;

    // Behind what the scene drew there, nothing; just in front of it, fading in over the soft
    // distance; over the sky (depth at the far plane), all of it, however far
    float alpha = 1.0;
    const float depth = View.SceneDepth.get().Load(int3(int2(input.Position.xy), 0)).r;
    if (depth > 0.0)
    {
        const float gap = linearDepth(depth) - dot(input.PositionWS - View.CameraPosition.xyz, View.CameraForward.xyz);
        if (gap <= 0.0)
        {
            discard;
        }
        if (glow.w > 0.0)
        {
            alpha = saturate(gap / glow.w);
        }
    }

    // The core and the body: soft peaks of their half widths, or as hard as Sharpness says, a step at
    // their edge a pixel wide (as an ink line is drawn); the glow falling off softly to the strip's
    // edge, as much of it as the stretch has
    const float pixel = max(fwidth(across), 1e-4);
    const float coreSoft = exp(-2.0 * (across / max(core.w, 1e-4)) * (across / max(core.w, 1e-4)));
    const float bodySoft = exp(-2.0 * (across / max(body.w, 1e-4)) * (across / max(body.w, 1e-4)));
    const float coreHard = 1.0 - smoothstep(core.w - pixel, core.w + pixel, across);
    const float bodyHard = 1.0 - smoothstep(body.w - pixel, body.w + pixel, across);
    const float coreShape = lerp(coreSoft, coreHard, sharpness);
    const float bodyShape = lerp(bodySoft, bodyHard, sharpness);
    const float edge = 1.0 - across;
    const float glowShape = edge * edge * exp(-3.0 * across) * lerp(input.Intensity.z, input.Intensity.w, part);
    const float3 light = (core.rgb * coreShape + body.rgb * bodyShape + glow.rgb * glowShape) * intensity;

    // Through the fog, and the clouds where they lie between, which hide its line sooner than what is
    // behind them (most of its light that gets through them is scattered off its line: their glow)
    const nyx::Fog fog = nyx::heightFog(input.PositionWS);
    const nyx::CloudCover clouds = nyx::cloudCoverBefore(input.PositionWS, input.Position.xy);
    return float4(light * (alpha * fog.Transmittance * pow(saturate(clouds.Transmittance), cloudHiding)), 0.0);
}

// The view's lightning picture over the scene: one triangle, added; the picture's slot in the
// push constants' MaterialOffset
[shader("vertex")]
float4 lightningCompositeVertex(uint vertex : SV_VulkanVertexID) : SV_Position
{
    const float2 corner = float2((vertex << 1) & 2, vertex & 2);
    return float4(corner * 2.0 - 1.0, 0.0, 1.0);
}

[shader("fragment")]
float4 lightningCompositeFragment(float4 position : SV_Position) : SV_Target
{
    nyx::Texture picture;
    picture.Index = Object.MaterialOffset;
    return float4(picture.get().Load(int3(int2(position.xy), 0)).rgb, 0.0);
}
