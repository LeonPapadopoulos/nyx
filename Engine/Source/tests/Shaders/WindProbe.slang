// The wind's contract on the GPU, for NyxWindGpuTests: a WindState, then one probe a float4 (a point
// of the world and a time); for each, nyx::sampleWind(), nyx::sampleWindGust() and
// nyx::windHeightFactor() into the output, two float4s a probe, so the test can hold them to
// Wind.cpp's to the bit.
import nyx.wind;

[vk::binding(2, 0)] StructuredBuffer<WindState> WindInput;
[vk::binding(3, 0)] RWStructuredBuffer<float4> WindOutput;
[vk::binding(4, 0)] StructuredBuffer<float4> WindProbes;

[shader("compute")]
[numthreads(64, 1, 1)]
void probeWind(uint3 id : SV_DispatchThreadID)
{
    const WindState wind = WindInput[0];
    const float4 probe = WindProbes[id.x];
    const float3 velocity = nyx::sampleWind(wind, probe.xyz, probe.w);
    const float gust = nyx::sampleWindGust(wind, probe.xyz, probe.w);
    const float factor = nyx::windHeightFactor(wind, probe.y);
    WindOutput[id.x * 2] = float4(velocity, gust);
    WindOutput[id.x * 2 + 1] = float4(factor, 0.0, 0.0, 0.0);
}
