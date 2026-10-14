// The scene's meshes until materials come (Design/shader-materials.md, stage 1, change 6): one
// program for the three looks in code, its values loaded from the material buffer at the draw's
// offset. Ported from DefaultShader.vert and .frag without changing the picture.

import nyx.bindless;
import nyx.view;

// SceneMaterialValues in VulkanRenderer.cpp
struct SceneMaterial
{
    float Reflectivity;
    // As in DefaultShader.frag, neither of these two reaches the colour: the lit colour is the
    // texture's, whatever they say. Kept so the picture stays the same; materials replace it all.
    float UseTexture;
    float3 Tint;
    nyx::Texture Albedo;
}

struct VertexInput
{
    float3 Position : POSITION;
    float3 Color : COLOR;
    float2 UV : TEXCOORD0;
    float3 Normal : NORMAL;
}

struct VertexOutput
{
    float4 Position : SV_Position;
    float3 Color : COLOR;
    float2 UV : TEXCOORD0;
    float3 NormalWS : NORMAL;
    float3 PositionWS : POSITION_WS;
}

// The inverse of the transpose, which carries normals: made from the rows' cross products
float3x3 normalMatrix(float4x4 model)
{
    const float3 r0 = model[0].xyz;
    const float3 r1 = model[1].xyz;
    const float3 r2 = model[2].xyz;
    const float3x3 cofactors = float3x3(cross(r1, r2), cross(r2, r0), cross(r0, r1));
    return cofactors / dot(r0, cross(r1, r2));
}

[shader("vertex")]
VertexOutput sceneVertex(VertexInput input)
{
    const float4 positionWS = mul(Object.Model, float4(input.Position, 1.0));

    VertexOutput output;
    output.Position = mul(View.ViewProjection, positionWS);
    output.Color = input.Color;
    output.UV = input.UV;
    output.NormalWS = normalize(mul(normalMatrix(Object.Model), input.Normal));
    output.PositionWS = positionWS.xyz;
    return output;
}

[shader("fragment")]
float4 sceneFragment(VertexOutput input) : SV_Target
{
    const SceneMaterial material = MaterialBuffer.Load<SceneMaterial>(Object.MaterialOffset);

    const float3 N = normalize(input.NormalWS);
    const float3 V = normalize(View.CameraPosition.xyz - input.PositionWS);
    const float3 L = normalize(View.LightDirection.xyz);

    const float4 albedo = material.Albedo.Sample(nyx::LinearWrap, input.UV);

    const float NdotL = max(dot(N, L), 0.0);
    const float ambient = View.LightColor.a;
    const float3 lighting = ambient + NdotL * View.LightColor.rgb;
    const float3 litBaseColor = albedo.rgb * input.Color * lighting;

    // Simple reflection of the sky
    const float3 R = reflect(-V, N);
    const float3 reflectedColor = View.Environment.Sample(nyx::LinearClamp, R).rgb;

    const float fresnel = pow(1.0 - max(dot(N, V), 0.0), 5.0);
    const float reflectivity = clamp(material.Reflectivity + fresnel * 0.5, 0.0, 1.0);

    return float4(lerp(litBaseColor, reflectedColor, reflectivity), albedo.a);
}
