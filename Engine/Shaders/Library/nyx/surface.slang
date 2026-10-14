// The surface domain (Design/shader-materials.md): a material for meshes implements ISurface,
// and the engine's entry points below are generic over it. ShaderCompiler::CompileMaterial()
// specialises them with the material's struct.
//
// Bindings follow the bindless plan: set 0 is global and push constants belong to the draw
// (nyx.bindless), set 1 belongs to the view (nyx.view); a material sees both. The pixel is lit by
// nyx.lighting: the sun and the sky's light, physically based (metallic-roughness).

__exported import nyx.bindless;
__exported import nyx.view;
__exported import nyx.environment;
import nyx.lighting;

// -------------------------------------------------------------------------
// Attributes a material's fields can carry. The editor reads them through reflection
// (ShaderLayout); the names follow NYX_PROPERTY's meta where they mean the same.
// -------------------------------------------------------------------------

[__AttributeUsage(_AttributeTargets.Var)] public struct CategoryAttribute { public string Name; };
[__AttributeUsage(_AttributeTargets.Var)] public struct TooltipAttribute { public string Text; };
[__AttributeUsage(_AttributeTargets.Var)] public struct DisplayNameAttribute { public string Name; };
[__AttributeUsage(_AttributeTargets.Var)] public struct DragSpeedAttribute { public float Speed; };
[__AttributeUsage(_AttributeTargets.Var)] public struct RangeAttribute { public float Min; public float Max; };
[__AttributeUsage(_AttributeTargets.Var)] public struct ColorAttribute {};
[__AttributeUsage(_AttributeTargets.Var)] public struct HDRAttribute {};
[__AttributeUsage(_AttributeTargets.Var)] public struct HiddenAttribute {};
// For textures: the texture an unset field shows, e.g. "White" or "FlatNormal"
[__AttributeUsage(_AttributeTargets.Var)] public struct DefaultAttribute { public string Texture; };
// For textures: data, not colour (normals, roughness, occlusion), read as stored rather than
// decoded from sRGB
[__AttributeUsage(_AttributeTargets.Var)] public struct LinearAttribute {};
// For bools that turn a part of the shader on or off. A plain bool in the material buffer for now;
// specialisation constants later (Design/shader-materials.md, Left for later).
[__AttributeUsage(_AttributeTargets.Var)] public struct SwitchAttribute {};

// -------------------------------------------------------------------------
// What a material reads and writes
// -------------------------------------------------------------------------

public struct SurfaceInput
{
    public float2 UV;
    // Normalised
    public float3 NormalWS;
    // Along u in xyz; w the side of the bitangent (normalFromMap())
    public float4 TangentWS;
    public float3 PositionWS;
    // Towards the camera, normalised
    public float3 ViewWS;
    public float4 CustomData;
}

public struct SurfaceOutput
{
    public float3 BaseColor;
    public float Metallic;
    // Perceptual: 0 a mirror, 1 fully rough
    public float Roughness;
    // In world space
    public float3 Normal;
    // How much of the sky's light reaches the pixel, 0 to 1
    public float Occlusion;
    public float3 Emission;
    public float Opacity;
    // Not lit: the base colour as it is, plus the emission
    public bool Unlit;

    public static SurfaceOutput defaults(SurfaceInput input)
    {
        SurfaceOutput o;
        o.BaseColor = float3(1.0);
        o.Metallic = 0.0;
        o.Roughness = 0.5;
        o.Normal = input.NormalWS;
        o.Occlusion = 1.0;
        o.Emission = float3(0.0);
        o.Opacity = 1.0;
        o.Unlit = false;
        return o;
    }
}

public interface ISurface
{
    SurfaceOutput shade(SurfaceInput input);
}

namespace nyx
{
    // A normal map's sample (green up, as glTF's are) as a normal in world space; `scale` makes
    // its bumps flatter or deeper
    public float3 normalFromMap(float4 sample, SurfaceInput input, float scale = 1.0)
    {
        float3 local = sample.xyz * 2.0 - 1.0;
        local.xy *= scale;
        const float3 n = input.NormalWS;
        const float3 t = normalize(input.TangentWS.xyz - n * dot(n, input.TangentWS.xyz));
        const float3 b = cross(n, t) * input.TangentWS.w;
        return normalize(t * local.x + b * local.y + n * local.z);
    }
}

// -------------------------------------------------------------------------
// Entry points
// -------------------------------------------------------------------------

// Nyx::Vertex
public struct VertexInput
{
    public float3 Position : POSITION;
    public float3 Color : COLOR;
    public float2 UV : TEXCOORD0;
    public float3 Normal : NORMAL;
    public float4 Tangent : TANGENT;
}

public struct VertexOutput
{
    public float4 Position : SV_Position;
    public float2 UV : TEXCOORD0;
    public float3 NormalWS : NORMAL;
    public float4 TangentWS : TANGENT;
    public float3 PositionWS : POSITION_WS;
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

// What surfaceVertex does, for other entry points that place meshes the same way. (Called from
// another entry point, an entry point crashes Slang 2026.1 when compiled through its API.)
public VertexOutput transformSurfaceVertex(VertexInput input)
{
    float4 positionWS = mul(Object.Model, float4(input.Position, 1.0));
    VertexOutput output;
    output.Position = mul(View.ViewProjection, positionWS);
    output.UV = input.UV;
    output.NormalWS = mul(normalMatrix(Object.Model), input.Normal);
    // Tangents lie along the surface, so they follow the model itself
    output.TangentWS = float4(mul((float3x3)Object.Model, input.Tangent.xyz), input.Tangent.w);
    output.PositionWS = positionWS.xyz;
    return output;
}

[shader("vertex")]
public VertexOutput surfaceVertex(VertexInput input)
{
    return transformSurfaceVertex(input);
}

// The material is loaded from the material buffer at the draw's offset, shades the pixel, and
// nyx.lighting lights what it gave
[shader("fragment")]
public float4 surfaceFragment<S : ISurface>(VertexOutput vertex) : SV_Target
{
    S material = MaterialBuffer.Load<S>(Object.MaterialOffset);

    SurfaceInput input;
    input.UV = vertex.UV;
    input.NormalWS = normalize(vertex.NormalWS);
    input.TangentWS = float4(normalize(vertex.TangentWS.xyz), vertex.TangentWS.w);
    input.PositionWS = vertex.PositionWS;
    input.ViewWS = normalize(View.CameraPosition.xyz - vertex.PositionWS);
    input.CustomData = Object.CustomData;

    SurfaceOutput o = material.shade(input);

    // A debug view: one output of the material, as it is (IRenderer::SetSceneViewDebug())
    if (nyx::isMaterialDebugView())
    {
        switch (View.DebugView)
        {
        case nyx::DebugBaseColor:
            return float4(o.BaseColor, 1.0);
        case nyx::DebugNormal:
            return float4(normalize(o.Normal) * 0.5 + 0.5, 1.0);
        case nyx::DebugRoughness:
            return float4(o.Roughness.xxx, 1.0);
        case nyx::DebugMetallic:
            return float4(o.Metallic.xxx, 1.0);
        case nyx::DebugOcclusion:
            return float4(o.Occlusion.xxx, 1.0);
        case nyx::DebugEmission:
            return float4(o.Emission, 1.0);
        case nyx::DebugOpacity:
            return float4(o.Opacity.xxx, 1.0);
        default:
        {
            // Eight cells across the UV square, light and dark, tinted red along u and green along v
            const float2 cell = floor(input.UV * 8.0);
            const float shade = frac((cell.x + cell.y) * 0.5) < 0.25 ? 0.9 : 0.45;
            return float4(float3(frac(input.UV), 0.25) * shade, 1.0);
        }
        }
    }

    if (o.Unlit)
    {
        return float4(o.BaseColor + o.Emission, o.Opacity);
    }
    const float3 lit = nyx::lightSurface(o.BaseColor, o.Metallic, o.Roughness, o.Occlusion, normalize(o.Normal), input.ViewWS);
    return float4(lit + o.Emission, o.Opacity);
}
