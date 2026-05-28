#version 450

layout(binding = 0) uniform sampler2D uVisibleSelectionMask;
layout(binding = 1) uniform sampler2D uFullSelectionMask;

layout(push_constant) uniform OutlineCompositePushConstants
{
    vec4 uParams0;              // x=TexelSizeX, y=TexelSizeY, z=Thickness, w=Mode
    vec4 uVisibleOutlineColor;
    vec4 uOccludedOutlineColor;
    vec4 uHatchColor;
    vec4 uHatchParams;          // x=spacing, y=thickness, z=dashLen, w=dashGap
};

layout(location = 0) in vec2 vUV;
layout(location = 0) out vec4 outColor;

float SampleVisible(vec2 uv)
{
    return texture(uVisibleSelectionMask, uv).r;
}

float SampleFull(vec2 uv)
{
    return texture(uFullSelectionMask, uv).r;
}

float ComputeEdgeVisible(vec2 uv, vec2 texelSize, float thickness)
{
    float center = SampleVisible(uv);
    float maxNeighbor = 0.0;

    for (int y = -1; y <= 1; ++y)
    {
        for (int x = -1; x <= 1; ++x)
        {
            if (x == 0 && y == 0)
            {
                continue;
            }

            vec2 offset = vec2(float(x), float(y)) * texelSize * thickness;
            maxNeighbor = max(maxNeighbor, SampleVisible(uv + offset));
        }
    }

    return maxNeighbor - center;
}

float ComputeEdgeFull(vec2 uv, vec2 texelSize, float thickness)
{
    float center = SampleFull(uv);
    float maxNeighbor = 0.0;

    for (int y = -1; y <= 1; ++y)
    {
        for (int x = -1; x <= 1; ++x)
        {
            if (x == 0 && y == 0)
            {
                continue;
            }

            vec2 offset = vec2(float(x), float(y)) * texelSize * thickness;
            maxNeighbor = max(maxNeighbor, SampleFull(uv + offset));
        }
    }

    return maxNeighbor - center;
}

float StripeDash(float lineCoord, float alongCoord, float spacing, float thickness, float dashLen, float dashGap)
{
    float stripePhase = abs(fract(lineCoord / spacing) - 0.5);
    float stripe = step(stripePhase, (thickness / spacing) * 0.5);

    float dashPeriod = dashLen + dashGap;
    float dashPhase = fract(abs(alongCoord) / dashPeriod);
    float dash = step(dashPhase, dashLen / dashPeriod);

    return stripe * dash;
}

void main()
{
    vec2 texelSize = uParams0.xy;
    float thickness = uParams0.z;
    float mode = uParams0.w;

    float visibleMask = SampleVisible(vUV);
    float fullMask = SampleFull(vUV);
    float occludedMask = max(fullMask - visibleMask, 0.0);

    float visibleEdge = ComputeEdgeVisible(vUV, texelSize, thickness);
    float fullEdge = ComputeEdgeFull(vUV, texelSize, thickness);
    float occludedEdge = max(fullEdge - visibleEdge, 0.0);

    // Mode 0: VisibleOnly
    if (mode < 0.5)
    {
        if (visibleEdge <= 0.001)
        {
            discard;
        }

        outColor = uVisibleOutlineColor;
        return;
    }

    // Mode 1: FullSilhouette
    if (mode < 1.5)
    {
        if (fullEdge <= 0.001)
        {
            discard;
        }

        outColor = uVisibleOutlineColor;
        return;
    }

    // Mode 2: VisibleOccludedHatched
    if (visibleEdge > 0.001)
    {
        outColor = uVisibleOutlineColor;
        return;
    }

    if (occludedEdge > 0.001)
    {
        outColor = uOccludedOutlineColor;
        return;
    }

    if (occludedMask > 0.001)
    {
        vec2 p = gl_FragCoord.xy;

        float spacing = uHatchParams.x;
        float lineThickness = uHatchParams.y;
        float dashLen = uHatchParams.z;
        float dashGap = uHatchParams.w;

        float patternA = StripeDash(
            p.x + p.y,
            p.x - p.y,
            spacing,
            lineThickness,
            dashLen,
            dashGap
        );

        float patternB = StripeDash(
            p.x - p.y,
            p.x + p.y,
            spacing,
            lineThickness,
            dashLen,
            dashGap
        );

        float hatch = max(patternA, patternB);

        if (hatch <= 0.001)
        {
            discard;
        }

        outColor = uHatchColor;
        return;
    }

    discard;
}