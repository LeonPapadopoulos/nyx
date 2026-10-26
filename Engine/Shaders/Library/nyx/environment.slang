// The environment's light, as VulkanEnvironment makes it from the sky: diffuse light from nine
// spherical harmonics, specular light from the prefiltered cube, and the split-sum table. Both
// lights are tinted as the sky is (nyx::skyTint(): white without an environment). While the view
// draws the sky made by the air's scattering, both are that sky's instead, at the camera's height
// (VulkanSkyAtmosphere's irradiance and specular cubes), times its Sky Light Intensity.

import nyx.atmosphere;
import nyx.bindless;
import nyx.sky;
import nyx.view;

namespace nyx
{
    // The diffuse light reaching a surface facing `n` (normalised), over pi: what a white
    // Lambertian surface shows
    public float3 environmentDiffuse(float3 worldNormal)
    {
        if (hasSkyAtmosphere())
        {
            return View.SkyIrradiance.SampleLevel(LinearClamp, worldNormal, 0.0).rgb * View.SkyTint.a;
        }
        const float3 n = skyDirection(worldNormal);
        const float4 c[9] = View.Irradiance;
        return max(c[0].rgb * 0.282095
                + c[1].rgb * 0.488603 * n.y
                + c[2].rgb * 0.488603 * n.z
                + c[3].rgb * 0.488603 * n.x
                + c[4].rgb * 1.092548 * n.x * n.y
                + c[5].rgb * 1.092548 * n.y * n.z
                + c[6].rgb * 0.315392 * (3.0 * n.z * n.z - 1.0)
                + c[7].rgb * 1.092548 * n.x * n.z
                + c[8].rgb * 0.546274 * (n.x * n.x - n.y * n.y),
            0.0) * skyTint();
    }

    // The light a surface of `roughness` (0 to 1) reflects from direction `r`
    public float3 environmentSpecular(float3 r, float roughness)
    {
        if (hasSkyAtmosphere())
        {
            return View.SkySpecular.SampleLevel(LinearClamp, r, saturate(roughness) * float(View.SkySpecularLevels - 1)).rgb * View.SkyTint.a;
        }
        const float level = saturate(roughness) * float(View.SpecularLevels - 1);
        return View.Specular.SampleLevel(LinearClamp, skyDirection(r), level).rgb * skyTint();
    }

    // The split-sum's scale (x) and bias (y) of F0, for n·v and roughness
    public float2 environmentBrdf(float nDotV, float roughness)
    {
        return View.BrdfLookup.SampleLevel(LinearClamp, float2(saturate(nDotV), saturate(roughness)), 0.0).xy;
    }
}
