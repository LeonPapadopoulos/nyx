#version 450

layout(binding = 0) uniform SceneUBO
{
    mat4 uViewProj;
    mat4 uInvViewProj;
    vec2 uViewportSize;
    vec2 _Padding0;
    vec4 uCameraWorldPos;
    vec4 uLightDirectionWS;
    vec4 uLightColor;
};

layout(binding = 1) uniform sampler2D uTexture;
layout(binding = 2) uniform samplerCube uSkybox;

layout(push_constant) uniform ObjectPushConstants
{
    mat4 uModel;
    vec4 uParams; // x = reflectivity, y = useTexture
    vec4 uTint;   // rgb = tint
} PC;

layout(location = 0) in vec3 vColor;
layout(location = 1) in vec2 vUV;
layout(location = 2) in vec3 vNormalWS;
layout(location = 3) in vec3 vWorldPos;

layout(location = 0) out vec4 outColor;

void main()
{
    vec3 N = normalize(vNormalWS);
    vec3 V = normalize(uCameraWorldPos.xyz - vWorldPos);
    vec3 L = normalize(uLightDirectionWS.xyz);

    vec4 albedo = texture(uTexture, vUV);

    float useTexture = PC.uParams.y;
    vec3 baseTex = mix(vec3(1.0), albedo.rgb, useTexture);

    float NdotL = max(dot(N, L), 0.0);
    float ambient = uLightColor.a;

    vec3 lighting = ambient + (NdotL * uLightColor.rgb);
    vec3 litBaseColor = albedo.rgb * vColor * lighting;

    // Simple Skybox reflection
    vec3 R = reflect(-V, N);
    vec3 reflectedColor = texture(uSkybox, R).rgb;

    float fresnel = pow(1.0 - max(dot(N, V), 0.0), 5.0);
    float reflectivity = clamp(PC.uParams.x + fresnel * 0.50, 0.0, 1.0);

    vec3 finalColor = mix(litBaseColor, reflectedColor, reflectivity);

    outColor = vec4(finalColor, albedo.a);

    // use to debug reflections
    //outColor = vec4(reflectedColor, 1.0);
}