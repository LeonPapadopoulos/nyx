// The surface domain: a material for meshes implements ISurface, and the engine's entry points
// below are generic over it. ShaderCompiler::CompileMaterial() specialises them with the
// material's struct.
//
// Bindings follow the bindless plan: set 0 is global and push constants belong to the draw
// (nyx.bindless), set 1 belongs to the view (nyx.view); a material sees both. The pixel is lit by
// nyx.lighting: the sun and the sky's light, physically based (metallic-roughness), the sun's
// shadowed by nyx.shadows; then seen through the world's height fog (nyx.atmosphere).

__exported import nyx.atmosphere;
__exported import nyx.bindless;
__exported import nyx.view;
__exported import nyx.environment;
__exported import nyx.shadows;
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
// specialisation constants later.
[__AttributeUsage(_AttributeTargets.Var)] public struct SwitchAttribute {};
// For whole numbers that choose one of a few ways: the choices' names, by comma, from 0 up
[__AttributeUsage(_AttributeTargets.Var)] public struct EnumAttribute { public string Items; };

// -------------------------------------------------------------------------
// What a material reads and writes
// -------------------------------------------------------------------------

// What a decal (SurfaceDecalComponent) draws its material with, from the draw's push constants:
// CustomData holds the colour and the intensity, CustomData2 the reveal and the pulse. A mesh
// drawn with the material gets the defaults.
public struct DecalInput
{
    public float3 Color;
    // What the colour is multiplied by; above 1, it glows
    public float Intensity;
    // 0 to 1: how much of the decal is drawn
    public float Reveal;
    // 0 to 1: how strongly it throbs
    public float Pulse;
    // Whether a decal draws the material, rather than a mesh
    public bool IsDecal;

    // White, at intensity 1, wholly revealed, not throbbing
    public static DecalInput defaults()
    {
        DecalInput decal;
        decal.Color = float3(1.0);
        decal.Intensity = 1.0;
        decal.Reveal = 1.0;
        decal.Pulse = 0.0;
        decal.IsDecal = false;
        return decal;
    }
}

public struct SurfaceInput
{
    public float2 UV;
    // The mesh's vertex colour, linear; white for a mesh that has none, as the built-in meshes
    public float3 Color;
    // Normalised
    public float3 NormalWS;
    // Along u in xyz; w the side of the bitangent (normalFromMap())
    public float4 TangentWS;
    public float3 PositionWS;
    // Towards the camera, normalised
    public float3 ViewWS;
    public float4 CustomData;
    public DecalInput Decal;
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
    // For a decal: how much of it is at the pixel, 0 to 1, as a click in the editor finds it
    // (decalPickingFragment), so that a click on a mask's empty parts goes to what is beneath. All
    // of the quad unless the material says less.
    public float Coverage;

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
        o.Coverage = 1.0;
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

// Nyx::Vertex, then the instance's transform from the second stream, one mat4 per instance
// (GetInstanceAttributeDescriptions()), relative to Object.Model: a MeshInstancesComponent's, or
// the identity for an entity drawn once. Its four columns, as glm keeps a mat4.
public struct VertexInput
{
    public float3 Position : POSITION;
    public float3 Color : COLOR;
    public float2 UV : TEXCOORD0;
    public float3 Normal : NORMAL;
    public float4 Tangent : TANGENT;
    public float4 Instance0 : INSTANCE0;
    public float4 Instance1 : INSTANCE1;
    public float4 Instance2 : INSTANCE2;
    public float4 Instance3 : INSTANCE3;
}

public struct VertexOutput
{
    public float4 Position : SV_Position;
    public float3 Color : COLOR;
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

// The instance's transform: its columns made a matrix that mul() applies as Object.Model is
float4x4 instanceTransform(VertexInput input)
{
    return transpose(float4x4(input.Instance0, input.Instance1, input.Instance2, input.Instance3));
}

// What surfaceVertex does, for other entry points that place meshes the same way. (Called from
// another entry point, an entry point crashes Slang 2026.1 when compiled through its API.)
public VertexOutput transformSurfaceVertex(VertexInput input)
{
    // The draw's transform after the instance's; an entity drawn once has the identity, which
    // leaves the draw's as it is, exactly
    const float4x4 model = mul(Object.Model, instanceTransform(input));
    float4 positionWS = mul(model, float4(input.Position, 1.0));
    VertexOutput output;
    output.Position = mul(View.ViewProjection, positionWS);
    output.Color = input.Color;
    output.UV = input.UV;
    output.NormalWS = mul(normalMatrix(model), input.Normal);
    // Tangents lie along the surface, so they follow the model itself
    output.TangentWS = float4(mul((float3x3)model, input.Tangent.xyz), input.Tangent.w);
    output.PositionWS = positionWS.xyz;
    return output;
}

[shader("vertex")]
public VertexOutput surfaceVertex(VertexInput input)
{
    return transformSurfaceVertex(input);
}

// A pixel as its material and the light make it: the light it sends back, the light it gives off
// and how much it covers
struct ShadedPixel
{
    float3 Reflected;
    float3 Emission;
    float Opacity;
}

// What the material is given at the pixel. On a triangle's back, which only a two-sided material
// draws (MaterialOptions), the normal is turned round, and the bitangent's side with it, so that
// the bitangent still points up the image: the back is lit as a front, a normal map's bumps
// mirrored through the sheet.
SurfaceInput makeSurfaceInput(VertexOutput vertex, DecalInput decal, bool bFrontFace)
{
    const float side = bFrontFace ? 1.0 : -1.0;
    SurfaceInput input;
    input.UV = vertex.UV;
    input.Color = vertex.Color;
    input.NormalWS = normalize(vertex.NormalWS) * side;
    input.TangentWS = float4(normalize(vertex.TangentWS.xyz), vertex.TangentWS.w * side);
    input.PositionWS = vertex.PositionWS;
    input.ViewWS = normalize(View.CameraPosition.xyz - vertex.PositionWS);
    input.CustomData = Object.CustomData;
    input.Decal = decal;
    return input;
}

// The decal's values, from the draw's push constants
DecalInput objectDecalInput()
{
    DecalInput decal;
    decal.Color = Object.CustomData.rgb;
    decal.Intensity = Object.CustomData.a;
    decal.Reveal = Object.CustomData2.x;
    decal.Pulse = Object.CustomData2.y;
    decal.IsDecal = true;
    return decal;
}

// The material is loaded from the material buffer at the draw's offset, shades the pixel, and
// nyx.lighting lights what it gave. In a debug view, the pixel is the output shown, opaque.
ShadedPixel shadeSurfacePixel<S : ISurface>(VertexOutput vertex, DecalInput decal, bool bFrontFace)
{
    S material = MaterialBuffer.Load<S>(Object.MaterialOffset);
    const SurfaceInput input = makeSurfaceInput(vertex, decal, bFrontFace);
    SurfaceOutput o = material.shade(input);

    ShadedPixel pixel;
    pixel.Emission = float3(0.0);
    pixel.Opacity = 1.0;

    // A debug view: one output of the material, as it is (IRenderer::SetSceneViewDebug())
    if (nyx::isMaterialDebugView())
    {
        switch (View.DebugView)
        {
        case nyx::DebugBaseColor:
            pixel.Reflected = o.BaseColor;
            break;
        case nyx::DebugNormal:
            pixel.Reflected = normalize(o.Normal) * 0.5 + 0.5;
            break;
        case nyx::DebugRoughness:
            pixel.Reflected = o.Roughness.xxx;
            break;
        case nyx::DebugMetallic:
            pixel.Reflected = o.Metallic.xxx;
            break;
        case nyx::DebugOcclusion:
            pixel.Reflected = o.Occlusion.xxx;
            break;
        case nyx::DebugEmission:
            pixel.Reflected = o.Emission;
            break;
        case nyx::DebugOpacity:
            pixel.Reflected = o.Opacity.xxx;
            break;
        default:
        {
            // Eight cells across the UV square, light and dark, tinted red along u and green along v
            const float2 cell = floor(input.UV * 8.0);
            const float shade = frac((cell.x + cell.y) * 0.5) < 0.25 ? 0.9 : 0.45;
            pixel.Reflected = float3(frac(input.UV), 0.25) * shade;
            break;
        }
        }
        return pixel;
    }

    // The sun's shadow by the surface's own normal, not a normal map's: the map's bumps don't move
    // where the shadow falls. A decal is shadowed as what it lies on is.
    pixel.Reflected = o.Unlit ? o.BaseColor
                              : nyx::lightSurface(o.BaseColor, o.Metallic, o.Roughness, o.Occlusion, normalize(o.Normal), input.ViewWS,
                                    nyx::sunShadow(input.PositionWS, input.NormalWS));
    pixel.Emission = o.Emission;
    pixel.Opacity = o.Opacity;

    // The shadow debug view: tinted by the cascade the shadow comes from
    if (View.ShadowDebug != 0 && !decal.IsDecal)
    {
        const uint cascade = nyx::shadowCascadeAt(input.PositionWS);
        if (cascade < View.ShadowCascadeCount)
        {
            pixel.Reflected *= nyx::shadowCascadeTint(cascade);
        }
    }
    return pixel;
}

// The fog between the camera and the pixel; none in a debug view of the materials
nyx::Fog surfaceFog(VertexOutput vertex)
{
    if (nyx::isMaterialDebugView())
    {
        return nyx::heightFogAlong(float3(0.0, 0.0, -1.0), 0.0);
    }
    return nyx::heightFog(vertex.PositionWS);
}

[shader("fragment")]
public float4 surfaceFragment<S : ISurface>(VertexOutput vertex, bool bFrontFace : SV_IsFrontFace) : SV_Target
{
    const ShadedPixel pixel = shadeSurfacePixel<S>(vertex, DecalInput::defaults(), bFrontFace);
    return float4(surfaceFog(vertex).apply(pixel.Reflected + pixel.Emission), pixel.Opacity);
}

// The material drawn as a decal, over what is drawn already, in premultiplied alpha, which the
// decal pipeline blends with (VulkanMaterials): its colour covers what is beneath by its opacity,
// and its emission adds to both, so it can glow without covering anything. What is beneath is
// fogged already: the decal's light is dimmed by the fog, and the fog's own added by its opacity
// alone, for the part of it the decal covers.
[shader("fragment")]
public float4 decalFragment<S : ISurface>(VertexOutput vertex, bool bFrontFace : SV_IsFrontFace) : SV_Target
{
    const ShadedPixel pixel = shadeSurfacePixel<S>(vertex, objectDecalInput(), bFrontFace);
    return float4(surfaceFog(vertex).applyPremultiplied(pixel.Reflected * pixel.Opacity + pixel.Emission, pixel.Opacity), pixel.Opacity);
}

// How much of a decal must be at a pixel for a click there to pick it
static const float DecalPickingCoverage = 0.05;

// The material drawn as a decal in the editor's picking pass (VulkanEditorOverlays): the decal's
// entity where the material says the decal is (SurfaceOutput.Coverage), not pulsing, so a click
// finds it at any moment. Elsewhere, such as in a ring's middle, the click goes to what is beneath.
[shader("fragment")]
public uint decalPickingFragment<S : ISurface>(VertexOutput vertex, bool bFrontFace : SV_IsFrontFace) : SV_Target
{
    DecalInput decal = objectDecalInput();
    decal.Pulse = 0.0;
    S material = MaterialBuffer.Load<S>(Object.MaterialOffset);
    const SurfaceOutput o = material.shade(makeSurfaceInput(vertex, decal, bFrontFace));
    if (o.Coverage < DecalPickingCoverage)
    {
        discard;
    }
    return Object.EntityId;
}
