// The surface domain (Design/shader-materials.md): a material for meshes implements ISurface,
// and the engine's entry points below are generic over it. ShaderCompiler::CompileMaterial()
// specialises them with the material's struct.
//
// Bindings follow the bindless plan: set 0 is global and push constants belong to the draw
// (nyx.bindless), set 1 belongs to the view (nyx.view); a material sees both. The lighting is a
// stand-in until nyx.lighting exists.

__exported import nyx.bindless;
__exported import nyx.view;

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

// -------------------------------------------------------------------------
// What a material reads and writes
// -------------------------------------------------------------------------

public struct SurfaceInput
{
    public float2 UV;
    public float3 NormalWS;
    public float3 PositionWS;
    public float4 CustomData;
}

public struct SurfaceOutput
{
    public float3 BaseColor;
    public float Metallic;
    public float Roughness;
    public float3 Normal;
    public float3 Emission;
    public float Opacity;

    public static SurfaceOutput defaults(SurfaceInput input)
    {
        SurfaceOutput o;
        o.BaseColor = float3(1.0);
        o.Metallic = 0.0;
        o.Roughness = 0.5;
        o.Normal = input.NormalWS;
        o.Emission = float3(0.0);
        o.Opacity = 1.0;
        return o;
    }
}

public interface ISurface
{
    SurfaceOutput shade(SurfaceInput input);
}

// -------------------------------------------------------------------------
// Entry points
// -------------------------------------------------------------------------

public struct VertexInput
{
    public float3 Position : POSITION;
    public float3 Color : COLOR;
    public float2 UV : TEXCOORD0;
    public float3 Normal : NORMAL;
}

public struct VertexOutput
{
    public float4 Position : SV_Position;
    public float2 UV : TEXCOORD0;
    public float3 NormalWS : NORMAL;
    public float3 PositionWS : POSITION_WS;
}

// What surfaceVertex does, for other entry points that place meshes the same way. (Called from
// another entry point, an entry point crashes Slang 2026.1 when compiled through its API.)
public VertexOutput transformSurfaceVertex(VertexInput input)
{
    float4 positionWS = mul(Object.Model, float4(input.Position, 1.0));
    VertexOutput output;
    output.Position = mul(View.ViewProjection, positionWS);
    output.UV = input.UV;
    output.NormalWS = mul((float3x3)Object.Model, input.Normal);
    output.PositionWS = positionWS.xyz;
    return output;
}

[shader("vertex")]
public VertexOutput surfaceVertex(VertexInput input)
{
    return transformSurfaceVertex(input);
}

// The material is loaded from the material buffer at the draw's offset, then shades the pixel
[shader("fragment")]
public float4 surfaceFragment<S : ISurface>(VertexOutput vertex) : SV_Target
{
    S material = MaterialBuffer.Load<S>(Object.MaterialOffset);

    SurfaceInput input;
    input.UV = vertex.UV;
    input.NormalWS = normalize(vertex.NormalWS);
    input.PositionWS = vertex.PositionWS;
    input.CustomData = Object.CustomData;

    SurfaceOutput o = material.shade(input);
    float diffuse = max(dot(normalize(o.Normal), normalize(View.LightDirection.xyz)), 0.0);
    float3 lit = o.BaseColor * (View.LightColor.a + diffuse * View.LightColor.rgb) + o.Emission;
    return float4(lit, o.Opacity);
}
