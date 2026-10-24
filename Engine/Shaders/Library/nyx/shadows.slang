// The primary light's shadows at a point (VulkanShadows draws the maps): the cascade the point is
// in by its depth along the camera's view, the point moved towards the light and out along its
// normal by the light's biases, then the map's texels around it compared with it, filtered
// (PCF, each tap bilinear over four texels): 1 where the sun reaches it, 0 in shadow. Shadows
// fade out over the last tenth of the shadow distance.

import nyx.bindless;
import nyx.view;

namespace nyx
{
    public static const uint MaxShadowCascades = 4;

    // The cascade's tint in the shadow debug view: red, green, blue, yellow, nearest first
    public float3 shadowCascadeTint(uint cascade)
    {
        switch (cascade)
        {
        case 0:
            return float3(1.0, 0.35, 0.3);
        case 1:
            return float3(0.35, 1.0, 0.35);
        case 2:
            return float3(0.35, 0.5, 1.0);
        default:
            return float3(1.0, 0.9, 0.3);
        }
    }

    public Texture2DArray shadowMap()
    {
        return View.ShadowMap.get();
    }

    // Where a point of the world is drawn into the shadow map's layer of the draw's cascade
    // (Object.CustomData2.x): what casts, as Shadow.slang and a material's vertex function
    // (nyx.surface) draw it
    public float4 shadowCasterPosition(float4 positionWS)
    {
        const uint cascade = min(uint(Object.CustomData2.x), MaxShadowCascades - 1);
        return mul(View.ShadowViewProjection[cascade], positionWS);
    }

    // How far the point is along the camera's view, in metres
    float viewDepth(float3 positionWS)
    {
        return dot(positionWS - View.CameraPosition.xyz, View.CameraForward.xyz);
    }

    // The cascade the point takes its shadow from; ShadowCascadeCount beyond the last
    public uint shadowCascadeAt(float3 positionWS)
    {
        const float depth = viewDepth(positionWS);
        for (uint cascade = 0; cascade < View.ShadowCascadeCount; ++cascade)
        {
            if (depth <= View.ShadowSplits[cascade])
            {
                return cascade;
            }
        }
        return View.ShadowCascadeCount;
    }

    // How much of the four texels around `uv` the point at `depth` is lit by, weighed by how near
    // `uv` is to each, as a comparison sampler's linear filter weighs them. Depth is reversed: a
    // texel holds the depth nearest the light, and the point is lit where it is at least as near.
    // Only texels of the part of the layer the view drew are read: other views draw the rest.
    float compareBilinear(float2 uv, uint cascade, float depth)
    {
        const float size = 1.0 / View.ShadowMapTexel;
        const float2 texel = uv * size - 0.5;
        const float2 corner = floor(texel);
        const float2 weight = texel - corner;
        const int last = int(size * View.ShadowMapScale + 0.5) - 1;
        float lit[4];
        for (int index = 0; index < 4; ++index)
        {
            const int2 at = clamp(int2(corner) + int2(index & 1, index >> 1), int2(0), int2(last));
            lit[index] = depth >= View.ShadowMap.Load(int4(at, int(cascade), 0)).r ? 1.0 : 0.0;
        }
        const float near = lerp(lit[0], lit[1], weight.x);
        const float far = lerp(lit[2], lit[3], weight.x);
        return lerp(near, far, weight.y);
    }

    // How much of the sun reaches a point of a surface whose normal is `normalWS` (normalised,
    // its own, not a normal map's): 1 lit, 0 in shadow
    public float sunShadow(float3 positionWS, float3 normalWS)
    {
        if (View.ShadowCascadeCount == 0)
        {
            return 1.0;
        }
        const float3 l = normalize(View.LightDirection.xyz);
        // Turned away from the sun, it gets none of its light anyway
        if (dot(normalWS, l) <= 0.0)
        {
            return 1.0;
        }
        const uint cascade = shadowCascadeAt(positionWS);
        if (cascade >= View.ShadowCascadeCount)
        {
            return 1.0;
        }

        // Moved by the biases, in the cascade's texels: what shades a surface is a little above it.
        // Out along the normal the more the surface is turned from the light, and the further the
        // filter reaches, so that a surface the light grazes doesn't find itself in the texels
        // around it (acne).
        const int reach = int(clamp(round(View.ShadowSoftness), 0.0, 3.0));
        const float texelSize = View.ShadowTexelSizes[cascade];
        const float nDotL = saturate(dot(normalWS, l));
        const float turned = sqrt(1.0 - nDotL * nDotL);
        const float3 biased = positionWS + l * (texelSize * View.ShadowDepthBias) + normalWS * (texelSize * View.ShadowNormalBias * turned * float(1 + reach));
        const float4 clip = mul(View.ShadowViewProjection[cascade], float4(biased, 1.0));
        const float3 coords = clip.xyz / clip.w;
        const float2 uv = (coords.xy * 0.5 + 0.5) * View.ShadowMapScale;

        // The texels out to the softness on each side, each bilinearly
        float lit = 0.0;
        for (int y = -reach; y <= reach; ++y)
        {
            for (int x = -reach; x <= reach; ++x)
            {
                lit += compareBilinear(uv + float2(float(x), float(y)) * View.ShadowMapTexel, cascade, coords.z);
            }
        }
        const float taps = float((2 * reach + 1) * (2 * reach + 1));
        lit /= taps;

        // Faded out towards the shadow distance, so shadows don't end at a line
        const float fade = saturate((viewDepth(positionWS) - 0.9 * View.ShadowDistance) / (0.1 * View.ShadowDistance));
        return lerp(lit, 1.0, fade);
    }
}
