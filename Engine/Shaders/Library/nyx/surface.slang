// The surface domain (Design/shader-materials.md): a material for meshes implements ISurface,
// and the engine's entry points below are generic over it. ShaderCompiler::CompileMaterial()
// specialises them with the material's struct.
//
// Bindings follow the bindless plan: set 0 is global, set 1 belongs to the view, push constants
// to the draw. The lighting is a stand-in until nyx.lighting exists.

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
// Bindings
// -------------------------------------------------------------------------

// Every sampled image, 2D or cube, in one array; a texture is an index into it
[vk::binding(0, 0)] __DynamicResource<__DynamicResourceKind.General> TextureHeap[];
[vk::binding(1, 0)] SamplerState FixedSamplers[4];
// Every material's values, each at its own offset, in scalar layout
[vk::binding(2, 0)] public ByteAddressBuffer MaterialBuffer;

public struct ObjectConstants
{
    public float4x4 Model;
    public uint MaterialOffset;
    public uint EntityId;
    public uint2 _Padding;
    public float4 CustomData;
}
[vk::push_constant] public ConstantBuffer<ObjectConstants> Object;

public struct ViewConstants
{
    public float4x4 ViewProjection;
    public float4 CameraPosition;
    public float4 LightDirection;
    public float4 LightColor;
}
[vk::binding(0, 1)] public ConstantBuffer<ViewConstants> View;

// Slang's own DescriptorHandles, wherever they are used, find their descriptor in TextureHeap too
export T getDescriptorFromHandle<T>(DescriptorHandle<T> handle) where T : IOpaqueDescriptor
{
    __target_switch
    {
    case spirv:
        return TextureHeap[((uint2)handle).x].asOpaqueDescriptor<T>();
    default:
        return defaultGetDescriptorFromHandle(handle);
    }
}

namespace nyx
{
    // A texture field of a material: an index into TextureHeap, unset slot 0. A plain uint rather
    // than Slang's DescriptorHandle<Texture2D>: a handle has no default, and a material must be
    // constructible with its defaults; and inside a struct, at an offset that isn't a multiple of
    // 8 bytes, a handle is loaded from the wrong offset (shader-materials.md, as built, change 1).
    public struct Texture
    {
        public uint Index = 0;

        public Texture2D get() { return TextureHeap[Index].asOpaqueDescriptor<Texture2D>(); }
        public float4 Sample(SamplerState sampler, float2 uv) { return get().Sample(sampler, uv); }
        public float4 SampleLevel(SamplerState sampler, float2 uv, float level) { return get().SampleLevel(sampler, uv, level); }
    }

    public property SamplerState LinearWrap { get { return FixedSamplers[0]; } }
    public property SamplerState LinearClamp { get { return FixedSamplers[1]; } }
    public property SamplerState PointClamp { get { return FixedSamplers[2]; } }
    public property SamplerState Anisotropic { get { return FixedSamplers[3]; } }
}

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

[shader("vertex")]
public VertexOutput surfaceVertex(VertexInput input)
{
    float4 positionWS = mul(Object.Model, float4(input.Position, 1.0));
    VertexOutput output;
    output.Position = mul(View.ViewProjection, positionWS);
    output.UV = input.UV;
    output.NormalWS = mul((float3x3)Object.Model, input.Normal);
    output.PositionWS = positionWS.xyz;
    return output;
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
