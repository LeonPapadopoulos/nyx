#version 450

layout(binding = 0) uniform SceneUBO
{
    mat4 uViewProj;
    mat4 uInvViewProj;
    mat4 uModel;
    vec2 uViewportSize;
    vec2 _Padding0;
    vec4 uCameraWorldPos;
    vec4 uLightDirectionWS;
    vec4 uLightColor;
};

layout(location = 0) noperspective in vec2 vNdc;
layout(location = 0) out vec4 outColor;

vec3 Unproject(vec2 ndc, float depth01)
{
    vec4 p = uInvViewProj * vec4(ndc, depth01, 1.0);
    return p.xyz / p.w;
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
    // For Vulkan-style depth with GLM_FORCE_DEPTH_ZERO_TO_ONE:
    // near = 0, far = 1
    vec3 nearPoint = Unproject(vNdc, 0.0);
    vec3 farPoint  = Unproject(vNdc, 1.0);

    vec3 rayDir = farPoint - nearPoint;

    // Intersect view ray with plane y = 0
    if (abs(rayDir.y) < 1e-6)
    {
        discard;
    }

    float t = -nearPoint.y / rayDir.y;
    if (t <= 0.0)
    {
        // Prevents multiple grids from appearing above and beyond the horizon 
        discard;
    }

    vec3 worldPos = nearPoint + rayDir * t;

    // Write grid plane depth so it correctly intersects other objects in the scene
    vec4 clipPos = uViewProj * vec4(worldPos, 1.0);
    gl_FragDepth = clipPos.z / clipPos.w;

    float distToCamera = length(worldPos - uCameraWorldPos.xyz);

    const float fadeStartDistance = 5.0;
    const float fadeEndDistance = 50.0;
    const float fadeStrength = 0.8;
    float fade = 1.0 - smoothstep(fadeStartDistance, fadeEndDistance, distToCamera) * fadeStrength;

    // Ground plane uses XZ
    vec2 gridPos = worldPos.xz;

    float minor = GridLine(gridPos, 0.5, 1.0);
    float major = GridLine(gridPos, 2.5, 1.2);

    float axisX = 1.0 - clamp(abs(worldPos.z) / max(fwidth(worldPos.z), 1e-6), 0.0, 1.0);
    float axisZ = 1.0 - clamp(abs(worldPos.x) / max(fwidth(worldPos.x), 1e-6), 0.0, 1.0);

    vec3 color = vec3(0.0);
    color += vec3(0.16) * minor;
    color += vec3(0.28) * major;

    // z == 0 line
    color = mix(color, vec3(0.65, 0.15, 0.15), axisX);

    // x == 0 line
    color = mix(color, vec3(0.15, 0.65, 0.15), axisZ);

    color *= fade;

    // we only want to see the grid lines, so we make the area inbetween transparent
    float lineAlpha = max(minor, major);
    lineAlpha = max(lineAlpha, axisX);
    lineAlpha = max(lineAlpha, axisZ);
    lineAlpha *= fade;

    // remove nearly invisible fragments completely
    if (lineAlpha <= 0.001)
    {
        discard;
    }

    outColor = vec4(color, lineAlpha);
}