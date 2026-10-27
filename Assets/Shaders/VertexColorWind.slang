// Vertex colours that sway in the wind, for grass, flowers and leaves: VertexColor's look, with a
// vertex function (ISurfaceVertex). The wind is the world's (WindComponent) with a game's local air
// on top (nyx::windAt()), felt at the instance's origin, a little above it, so a whole plant leans
// as one, gust fronts crossing the ground lean neighbours together and a field ripples. The
// material keeps only how it answers: how far it bends, how stiff it is, how it flutters.
//
// Each vertex bends as far as its mesh's Custom.x says, 0 where it holds still (a blade's foot) to
// 1 where it bends most (its tip); each instance flutters with its own phase, its InstanceCustom.x
// from 0 to 1, which a scatter gives every copy. A vertex of weight 1 leans by less than
// Strength × (1 + Flutter) metres in any wind; keep that within the material's Max Vertex Offset,
// which the renderer culls by.

import nyx.surface;
import nyx.worldwind;

static const float Tau = 6.28318530718;

struct VertexColorWind : ISurface, ISurfaceVertex
{
    [Category("Surface"), Color] float3 Tint = float3(1.0, 1.0, 1.0);
    [Category("Surface"), Range(0, 1)] float Metallic = 0.0;
    [Category("Surface"), Range(0, 1)] float Roughness = 0.9;

    [Category("Wind"), Range(0, 1), Tooltip("How much the wind moves it: 0 holds still in any wind, 1 bends as Strength and Stiffness say")]
    float Response = 1.0;
    [Category("Wind"), DragSpeed(0.005), Tooltip("How far, in metres, a tip leans at most, in the strongest wind")]
    float Strength = 0.15;
    [Category("Wind"), DragSpeed(0.05), Tooltip("How stiff it is: the wind in metres a second that leans a tip about 70% of Strength; a breeze bends it about in proportion below that")]
    float Stiffness = 6.0;
    [Category("Wind"), Range(0, 1), Tooltip("The quick sway back and forth on top of the lean, as a part of it; rising air (a fire's updraft) shakes it too")]
    float Flutter = 0.35;
    [Category("Wind"), DragSpeed(0.01), Tooltip("Sways a second")]
    float FlutterFrequency = 1.3;
    [Category("Wind"), DragSpeed(0.01), Tooltip("How high above its origin, in metres, a plant feels the wind")]
    float WindHeight = 0.3;

    SurfaceOutput shade(SurfaceInput input)
    {
        SurfaceOutput o = SurfaceOutput::defaults(input);
        o.BaseColor = input.Color * Tint;
        o.Metallic = Metallic;
        o.Roughness = Roughness;
        return o;
    }

    void displace(inout SurfaceVertex vertex)
    {
        const float weight = saturate(vertex.Custom.x) * Response;
        // Where the instance stands: its origin in the world, so its whole plant sways as one
        const float3 origin = float3(vertex.ObjectToWorld[0][3], vertex.ObjectToWorld[1][3], vertex.ObjectToWorld[2][3]);
        const float3 air = nyx::windAt(origin + float3(0.0, WindHeight, 0.0), vertex.Time);

        // The lean, along the wind over the ground, about in proportion to it below Stiffness and
        // never past Strength
        const float speed = length(air.xz);
        const float x = speed / max(Stiffness, 0.01);
        const float lean = Strength * x * rsqrt(1.0 + x * x);
        // The quick sway, each instance at its own phase, as strong as all the air moving it, so
        // rising air shakes it too; along the lean, or its own way in still air
        const float phase = vertex.InstanceCustom.x * Tau;
        const float stirred = length(air) / max(Stiffness, 0.01);
        const float shake = Strength * stirred * rsqrt(1.0 + stirred * stirred);
        const float2 own = float2(cos(phase), sin(phase));
        const float2 along = speed > 0.05 ? air.xz / speed : own;
        const float flutter = Flutter * shake * sin(vertex.Time * FlutterFrequency * Tau + phase);

        const float2 bend = (along * (lean + flutter)) * weight;
        vertex.OffsetWS = float3(bend.x, 0.0, bend.y);
    }
}
