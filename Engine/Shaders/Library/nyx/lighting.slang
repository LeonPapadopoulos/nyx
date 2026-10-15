// Physically based light, glTF's metallic-roughness model: GGX distribution, height-correlated
// Smith visibility, Schlick Fresnel and Lambert diffuse for the sun; for the sky, its spherical
// harmonics (diffuse) and its prefiltered cube with the split-sum table (specular), from
// nyx.environment.

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

    // The light a surface sends towards `v` (normalised, towards the camera), from the sun and the
    // sky. `roughness` is perceptual, as materials give it; `occlusion` dims the sky's light alone.
    // The sun's colour is what a white matte surface facing it shows: its irradiance is pi times
    // that.
    public float3 lightSurface(float3 baseColor, float metallic, float roughness, float occlusion, float3 n, float3 v)
    {
        const float perceptual = clamp(roughness, 0.045, 1.0);
        const float alpha = perceptual * perceptual;
        const float3 f0 = lerp(float3(0.04), baseColor, metallic);
        const float3 diffuseColor = baseColor * (1.0 - metallic);
        const float nDotV = max(dot(n, v), 1e-4);

        // The sun
        const float3 l = normalize(View.LightDirection.xyz);
        const float3 h = normalize(v + l);
        const float nDotL = saturate(dot(n, l));
        const float nDotH = saturate(dot(n, h));
        const float lDotH = saturate(dot(l, h));
        const float3 specular = distributionGgx(nDotH, alpha) * visibilitySmithGgx(nDotV, nDotL, alpha) * fresnelSchlick(f0, lDotH);
        const float3 sun = (diffuseColor + Pi * specular) * View.LightColor.rgb * nDotL;

        // The sky. The diffuse part gets what the specular part leaves, so that a white
        // dielectric under a uniform sky shows the sky's colour (the furnace test).
        const float2 brdf = environmentBrdf(nDotV, perceptual);
        const float3 specularShare = f0 * brdf.x + brdf.y;
        const float3 skySpecular = environmentSpecular(reflect(-v, n), perceptual) * specularShare;
        const float3 skyDiffuse = diffuseColor * environmentDiffuse(n) * (1.0 - specularShare);
        return sun + (skyDiffuse + skySpecular) * occlusion;
    }
}
