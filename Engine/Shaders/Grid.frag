#version 450

layout(binding = 0) uniform SceneUBO
{
    mat4 uViewProj;
    mat4 uInvViewProj;
    mat4 uModel;
    vec2 uViewportSize;
    vec2 _Padding;
};

layout(location = 0) in vec2 vNdc; // varying Normalized Device Coordinates
layout(location = 0) out vec4 outColor;

vec2 WorldFromNdc(vec2 ndc)
{
    vec4 world = uInvViewProj * vec4(ndc, 0.0, 1.0);
    return world.xy / world.w;
}

float GridLine(vec2 worldPos, float spacing, float lineWidthPx)
{
    vec2 coord = worldPos / spacing;

    vec2 deriv = fwidth(coord);
    vec2 grid = abs(fract(coord - 0.5) - 0.5) / max(deriv, vec2(1e-6));

    float line = min(grid.x, grid.y);
    return 1.0 - clamp(line / lineWidthPx, 0.0, 1.0);
}

void main()
{
    vec2 worldPos = WorldFromNdc(vNdc);

    float minor = GridLine(worldPos, 0.5, 1.0);
    float major = GridLine(worldPos, 2.5, 1.2);

    float axisX = 1.0 - clamp(abs(worldPos.y) / max(fwidth(worldPos.y), 1e-6), 0.0, 1.0);
    float axisY = 1.0 - clamp(abs(worldPos.x) / max(fwidth(worldPos.x), 1e-6), 0.0, 1.0);

    vec3 color = vec3(0.0);
    color += vec3(0.16) * minor;
    color += vec3(0.28) * major;
    color = mix(color, vec3(0.65, 0.15, 0.15), axisX);
    color = mix(color, vec3(0.15, 0.65, 0.15), axisY);

    outColor = vec4(color, 1.0);
}