// The bindings every scene program shares: set 0 is global (VulkanBindless), push constants
// belong to the draw. The view's set is nyx.view.

// -------------------------------------------------------------------------
// Set 0: global
// -------------------------------------------------------------------------

// Every sampled image, 2D or cube, in one array; a texture is an index into it. Slots 0 to 3 are
// white, black, a flat normal and a magenta checker (EBindlessTexture).
[vk::binding(0, 0)] __DynamicResource<__DynamicResourceKind.General> TextureHeap[];
[vk::binding(1, 0)] SamplerState FixedSamplers[4];
// Every material's values, each at its own offset, in scalar layout
[vk::binding(2, 0)] public ByteAddressBuffer MaterialBuffer;

// -------------------------------------------------------------------------
// Push constants: one draw. ObjectConstants in VulkanRenderer.h.
// -------------------------------------------------------------------------

public struct ObjectConstants
{
    public float4x4 Model;
    // Where the material's values start in MaterialBuffer
    public uint MaterialOffset;
    // The entity drawn, as the picking pass writes it
    public uint EntityId;
    // Two more values set per object by code: a decal's reveal and pulse (DecalInput, nyx.surface)
    public float2 CustomData2;
    // Set per object by code (zero until materials are steered): a decal's colour and intensity
    public float4 CustomData;
}
[vk::push_constant] public ConstantBuffer<ObjectConstants> Object;

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
    // 8 bytes, a handle is loaded from the wrong offset.
    public struct Texture
    {
        public uint Index = 0;

        public Texture2D get() { return TextureHeap[Index].asOpaqueDescriptor<Texture2D>(); }
        public float4 Sample(SamplerState sampler, float2 uv) { return get().Sample(sampler, uv); }
        public float4 SampleLevel(SamplerState sampler, float2 uv, float level) { return get().SampleLevel(sampler, uv, level); }
    }

    // The same for a cube map, e.g. the sky: sampled by direction
    public struct Cubemap
    {
        public uint Index = 0;

        public TextureCube get() { return TextureHeap[Index].asOpaqueDescriptor<TextureCube>(); }
        public float4 Sample(SamplerState sampler, float3 direction) { return get().Sample(sampler, direction); }
        public float4 SampleLevel(SamplerState sampler, float3 direction, float level) { return get().SampleLevel(sampler, direction, level); }
    }

    // The same for a 2D texture array, e.g. the shadow map: sampled by UV and layer
    public struct TextureArray
    {
        public uint Index = 0;

        public Texture2DArray get() { return TextureHeap[Index].asOpaqueDescriptor<Texture2DArray>(); }
        public float4 SampleLevel(SamplerState sampler, float3 uvLayer, float level) { return get().SampleLevel(sampler, uvLayer, level); }
        // A texel as it is: x and y, the layer, the mip
        public float4 Load(int4 texelLayerMip) { return get().Load(texelLayerMip); }
    }

    // FixedSamplers, in EFixedSampler's order
    public property SamplerState LinearWrap { get { return FixedSamplers[0]; } }
    public property SamplerState LinearClamp { get { return FixedSamplers[1]; } }
    public property SamplerState PointClamp { get { return FixedSamplers[2]; } }
    public property SamplerState Anisotropic { get { return FixedSamplers[3]; } }
}
