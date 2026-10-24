// Vertex colours that sway in the wind, for grass, flowers and leaves: VertexColor's look, with a
// vertex function (ISurfaceVertex). Each vertex bends as far as its mesh's Custom.x says, 0 where
// it holds still (a blade's foot) to 1 where it bends most (its tip); each instance sways with its
// own phase, its InstanceCustom.x from 0 to 1, which a scatter gives every copy. Gusts cross the
// ground along the wind, so neighbours lean together and a field ripples.
//
// The wind leans a vertex of weight 1 by up to Strength × (1 + Flutter) metres; keep that within
// the material's Max Vertex Offset, which the renderer culls by.

import nyx.surface;

static const float Tau = 6.28318530718;

struct VertexColorWind : ISurface, ISurfaceVertex
{
    [Category("Surface"), Color] float3 Tint = float3(1.0, 1.0, 1.0);
    [Category("Surface"), Range(0, 1)] float Metallic = 0.0;
    [Category("Surface"), Range(0, 1)] float Roughness = 0.9;

    [Category("Wind"), DragSpeed(0.01), Tooltip("Where the wind blows to, along the ground: x and z of the world")]
    float2 Direction = float2(1.0, 0.35);
    [Category("Wind"), DragSpeed(0.005), Tooltip("How far, in metres, a tip leans at a gust's peak")]
    float Strength = 0.12;
    [Category("Wind"), Range(0, 1), Tooltip("How much of the lean comes and goes with the gusts: 0 a steady wind, 1 calm between gusts")]
    float Gustiness = 0.6;
    [Category("Wind"), DragSpeed(0.01), Tooltip("Gusts a second that reach a place")]
    float GustFrequency = 0.25;
    [Category("Wind"), DragSpeed(0.1), Tooltip("Metres from one gust to the next, along the wind")]
    float GustLength = 6.0;
    [Category("Wind"), Range(0, 1), Tooltip("The quick sway back and forth on top of the lean, as a part of the strength")]
    float Flutter = 0.35;
    [Category("Wind"), DragSpeed(0.01), Tooltip("Sways a second")]
    float FlutterFrequency = 1.3;

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
        const float weight = saturate(vertex.Custom.x);
        const float2 along = length(Direction) > 1e-4 ? normalize(Direction) : float2(1.0, 0.0);
        // Where the instance stands: its origin in the world, so its whole plant sways as one
        const float2 origin = float2(vertex.ObjectToWorld[0][3], vertex.ObjectToWorld[2][3]);

        // A gust's front travelling along the wind: 0 between gusts, 1 at its peak
        const float travelled = dot(origin, along) / max(GustLength, 0.01) - vertex.Time * GustFrequency;
        const float gust = 0.5 - 0.5 * cos(travelled * Tau);
        const float lean = 1.0 - Gustiness * (1.0 - gust);
        // The quick sway, each instance at its own phase
        const float phase = vertex.InstanceCustom.x * Tau;
        const float flutter = Flutter * sin(vertex.Time * FlutterFrequency * Tau + phase);

        const float bend = Strength * weight * (lean + flutter);
        vertex.OffsetWS = float3(along.x, 0.0, along.y) * bend;
    }
}
