// Physically based light, glTF's metallic-roughness model: GGX distribution, height-correlated
// Smith visibility, Schlick Fresnel and Lambert diffuse for the sun and the view's point lights; for
// the sky, its spherical harmonics (diffuse) and its prefiltered cube with the split-sum table
// (specular), from nyx.environment.

import nyx.bindless;
import nyx.environment;
import nyx.view;

namespace nyx
{
    static const float Pi = 3.14159265358979;

    public float distributionGgx(float nDotH, float alpha)
    {
        const float a2 = alpha * alpha;
        const float d = nDotH * nDotH * (a2 - 1.0) + 1.0;
        return a2 / (Pi * d * d);
    }

    // G / (4 n·l n·v), height-correlated (Heitz)
    public float visibilitySmithGgx(float nDotV, float nDotL, float alpha)
    {
        const float a2 = alpha * alpha;
        const float lambdaV = nDotL * sqrt(nDotV * nDotV * (1.0 - a2) + a2);
        const float lambdaL = nDotV * sqrt(nDotL * nDotL * (1.0 - a2) + a2);
        return 0.5 / max(lambdaV + lambdaL, 1e-6);
    }

    public float3 fresnelSchlick(float3 f0, float cosine)
    {
        return f0 + (1.0 - f0) * pow(1.0 - cosine, 5.0);
    }

    // How far a point light's light reaches, as a part of its intensity, `distanceSquared` from it
    // for a light of 1 / range² `inverseRangeSquared` (GetPointLightFalloff() in PointLights.h): the
    // inverse square of the distance, no nearer than a small bulb's 0.1 m, times a window that
    // brings it softly to 0 at the range
    public float pointLightFalloff(float distanceSquared, float inverseRangeSquared)
    {
        const float ratio2 = distanceSquared * inverseRangeSquared;
        const float window = saturate(1.0 - ratio2 * ratio2);
        return window * window / max(distanceSquared, 0.01);
    }

    // What one light from direction `l` (normalised, towards it), of colour `radiance` at the
    // surface, sends towards `v`: the GGX specular lobe and Lambert diffuse, times n·l
    float3 directLight(float3 diffuseColor, float3 f0, float alpha, float3 n, float3 v, float nDotV, float3 l, float3 radiance)
    {
        const float3 h = normalize(v + l);
        const float nDotL = saturate(dot(n, l));
        const float nDotH = saturate(dot(n, h));
        const float lDotH = saturate(dot(l, h));
        const float3 specular = distributionGgx(nDotH, alpha) * visibilitySmithGgx(nDotV, nDotL, alpha) * fresnelSchlick(f0, lDotH);
        return (diffuseColor + Pi * specular) * radiance * nDotL;
    }

    // The light a surface sends back, apart: the sun's and the point lights', and the sky's (what
    // ambient occlusion dims)
    public struct SurfaceLight
    {
        public float3 Direct;
        public float3 Ambient;
    }

    // The light a surface at `positionWS` sends towards `v` (normalised, towards the camera), from the
    // sun, the view's point lights and the sky. `roughness` is perceptual, as materials give it;
    // `occlusion` dims the sky's light alone, `sunShadow` the sun's alone (nyx::sunShadow(): 1 where
    // the sun reaches, 0 in shadow). The sun's colour is what a white matte surface facing it shows:
    // its irradiance is pi times that; a point light's the same from a metre away.
    public float3 lightSurface(float3 baseColor, float metallic, float roughness, float occlusion, float3 n, float3 v, float3 positionWS, float sunShadow = 1.0)
    {
        const SurfaceLight light = lightSurfaceApart(baseColor, metallic, roughness, occlusion, n, v, positionWS, sunShadow);
        return light.Direct + light.Ambient;
    }

    // lightSurface(), the direct light (the sun's and the point lights') and the sky's apart
    public SurfaceLight lightSurfaceApart(float3 baseColor, float metallic, float roughness, float occlusion, float3 n, float3 v, float3 positionWS, float sunShadow = 1.0)
    {
        const float perceptual = clamp(roughness, 0.045, 1.0);
        const float alpha = perceptual * perceptual;
        const float3 f0 = lerp(float3(0.04), baseColor, metallic);
        const float3 diffuseColor = baseColor * (1.0 - metallic);
        const float nDotV = max(dot(n, v), 1e-4);

        // The sun
        float3 direct = directLight(diffuseColor, f0, alpha, n, v, nDotV, normalize(View.LightDirection.xyz), View.LightColor.rgb) * sunShadow;

        // The point lights that reach the view, each skipped where the pixel is past its range
        for (uint index = 0; index < View.PointLightCount; ++index)
        {
            const PointLight light = PointLights[index];
            const float3 toLight = light.Position - positionWS;
            const float distanceSquared = dot(toLight, toLight);
            if (distanceSquared * light.InverseRangeSquared >= 1.0)
            {
                continue;
            }
            const float3 l = toLight * rsqrt(max(distanceSquared, 1e-12));
            direct += directLight(diffuseColor, f0, alpha, n, v, nDotV, l, light.Color * pointLightFalloff(distanceSquared, light.InverseRangeSquared));
        }

        // The sky. The diffuse part gets what the specular part leaves, so that a white
        // dielectric under a uniform sky shows the sky's colour (the furnace test).
        const float2 brdf = environmentBrdf(nDotV, perceptual);
        const float3 specularShare = f0 * brdf.x + brdf.y;
        const float3 skySpecular = environmentSpecular(reflect(-v, n), perceptual) * specularShare;
        const float3 skyDiffuse = diffuseColor * environmentDiffuse(n) * (1.0 - specularShare);
        SurfaceLight light;
        light.Direct = direct;
        light.Ambient = (skyDiffuse + skySpecular) * occlusion;
        return light;
    }
}
