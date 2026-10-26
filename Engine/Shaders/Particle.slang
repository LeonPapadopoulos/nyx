// Particles (VulkanParticles): each a soft round quad facing the camera, or a small mesh, drawn
// instanced after the view's ambient occlusion, over the scene in HDR, with no depth buffer: each
// pixel is tested against the scene's depth, which it reads (View.SceneDepth), and fades out over
// the soft distance in front of what is behind it, so a particle meeting the ground shows no hard
// edge. Additive particles add their light, dimmed by the fog; alpha ones cover what is behind,
// fogged as a decal is, in premultiplied alpha. The draw's push constants hold the emitter's:
//   - CustomData.x: the soft distance, in metres (0: a hard edge);
//   - CustomData.y: 1 if the sun, the sky and the point lights light it;
//   - CustomData.z: 1 if alpha blended, 0 if additive.

import nyx.bindless;
import nyx.view;
import nyx.atmosphere;
import nyx.clouds;
import nyx.environment;
import nyx.lighting;

// One particle (ParticleInstanceData in Particles.h), the instance stream: in the world, its size in
// metres, its colour times its emissive intensity and its alpha, its turn (a billboard's angle in
// x, a mesh's quaternion). Its locations follow the vertex's own inputs, in their order: 0 to 2 for
// a billboard, which has none, 2 to 4 after a mesh's position and normal (VulkanParticles).
struct ParticleInstance
{
    float4 PositionSize : PARTICLE_POSITION;
    float4 Color : PARTICLE_COLOR;
    float4 Rotation : PARTICLE_ROTATION;
}

struct ParticleVaryings
{
    float4 Position : SV_Position;
    // Its light, and its alpha
    float4 Color : COLOR;
    // Across a billboard, -1 to 1 both ways; 0 on a mesh, which isn't faded round its edge
    float2 Corner : TEXCOORD0;
    float3 PositionWS : POSITION_WS;
}

// The light a lit particle at `positionWS` shows for each unit of its colour: the sun's, the sky's
// from above and the point lights', all of it whichever way they come from, as a puff of smoke
// scatters light; else 1, its colour as it is
float3 particleLight(float3 positionWS)
{
    if (Object.CustomData.y < 0.5)
    {
        return float3(1.0);
    }
    float3 light = View.LightColor.rgb + nyx::environmentDiffuse(float3(0.0, 1.0, 0.0));
    for (uint index = 0; index < View.PointLightCount; ++index)
    {
        const PointLight point = PointLights[index];
        const float3 toLight = point.Position - positionWS;
        light += point.Color * nyx::pointLightFalloff(dot(toLight, toLight), point.InverseRangeSquared);
    }
    return light;
}

ParticleVaryings placeParticle(float3 positionWS, ParticleInstance instance, float2 corner, float shade)
{
    ParticleVaryings output;
    output.Position = mul(View.ViewProjection, float4(positionWS, 1.0));
    output.Color = float4(instance.Color.rgb * particleLight(instance.PositionSize.xyz) * shade, instance.Color.a);
    output.Corner = corner;
    output.PositionWS = positionWS;
    return output;
}

// Two triangles a particle, its corners by the vertex's index, across the camera's view
[shader("vertex")]
ParticleVaryings particleBillboardVertex(uint vertex : SV_VulkanVertexID, ParticleInstance instance)
{
    static const float2 Corners[6] = { float2(-1.0, -1.0), float2(1.0, -1.0), float2(1.0, 1.0), float2(-1.0, -1.0), float2(1.0, 1.0), float2(-1.0, 1.0) };
    const float2 corner = Corners[vertex % 6];

    // The camera's right and up, its turn about the view kept out of it
    const float3 forward = View.CameraForward.xyz;
    const float3 helper = abs(forward.y) < 0.999 ? float3(0.0, 1.0, 0.0) : float3(0.0, 0.0, -1.0);
    const float3 right = normalize(cross(forward, helper));
    const float3 up = cross(right, forward);

    float sine;
    float cosine;
    sincos(instance.Rotation.x, sine, cosine);
    const float2 turned = float2(corner.x * cosine - corner.y * sine, corner.x * sine + corner.y * cosine);
    const float3 positionWS = instance.PositionSize.xyz + (right * turned.x + up * turned.y) * (instance.PositionSize.w * 0.5);
    return placeParticle(positionWS, instance, corner, 1.0);
}

// A point turned by a quaternion (x, y, z, w)
float3 rotateByQuaternion(float4 q, float3 v)
{
    const float3 t = 2.0 * cross(q.xyz, v);
    return v + q.w * t + cross(q.xyz, t);
}

// A small mesh a particle, its vertices from the mesh (GpuVertex: its position and normal), turned
// and scaled to the particle's size; shaded a little by how it faces the camera, so its shape shows
[shader("vertex")]
ParticleVaryings particleMeshVertex(float3 position : POSITION, float3 normal : NORMAL, ParticleInstance instance)
{
    const float3 positionWS = instance.PositionSize.xyz + rotateByQuaternion(instance.Rotation, position) * instance.PositionSize.w;
    const float3 normalWS = rotateByQuaternion(instance.Rotation, normal);
    const float facing = saturate(dot(normalize(normalWS), normalize(View.CameraPosition.xyz - positionWS)));
    return placeParticle(positionWS, instance, float2(0.0), 0.55 + 0.45 * facing);
}

// A depth of the depth buffer as metres along the view (reversed: MakePerspectiveProjection())
float linearDepth(float depth)
{
    return View.ProjectionInfo.w / max(depth + View.ProjectionInfo.z, 1e-7);
}

[shader("fragment")]
float4 particleFragment(ParticleVaryings input) : SV_Target
{
    // A billboard round and soft: its alpha falling to 0 at its rim
    float alpha = input.Color.a;
    const float rim = dot(input.Corner, input.Corner);
    if (rim >= 1.0)
    {
        discard;
    }
    alpha *= (1.0 - rim) * (1.0 - rim);

    // Behind what the scene drew there, nothing; just in front of it, fading in over the soft
    // distance
    const float sceneDepth = linearDepth(View.SceneDepth.get().Load(int3(int2(input.Position.xy), 0)).r);
    const float gap = sceneDepth - linearDepth(input.Position.z);
    if (gap <= 0.0)
    {
        discard;
    }
    const float soft = Object.CustomData.x;
    if (soft > 0.0)
    {
        alpha *= saturate(gap / soft);
    }

    // Through the clouds, where they lie between, and the fog
    const nyx::Fog fog = nyx::heightFog(input.PositionWS);
    const nyx::CloudCover clouds = nyx::cloudCoverBefore(input.PositionWS, input.Position.xy);
    if (Object.CustomData.z < 0.5)
    {
        return float4(input.Color.rgb * (alpha * (fog.Transmittance * clouds.Transmittance)), 0.0);
    }
    return float4(fog.applyPremultiplied(clouds.applyPremultiplied(input.Color.rgb * alpha, alpha), alpha), alpha);
}
