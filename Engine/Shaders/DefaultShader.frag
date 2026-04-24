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

layout(binding = 1) uniform sampler2D uTexture;
layout(binding = 2) uniform samplerCube uSkybox;

layout(location = 0) in vec3 vColor;
layout(location = 1) in vec2 vUV;
layout(location = 2) in vec3 vNormalWS;
layout(location = 3) in vec3 vWorldPos;

layout(location = 0) out vec4 outColor;

void main()
{
    vec4 albedo = texture(uTexture, vUV);

    vec3 N = normalize(vNormalWS);
    vec3 V = normalize(uCameraWorldPos.xyz - vWorldPos);
    vec3 L = normalize(uLightDirectionWS.xyz);

    float NdotL = max(dot(N, L), 0.0);
    float ambient = uLightColor.a;

    vec3 lighting = ambient + (NdotL * uLightColor.rgb);
    vec3 litBaseColor = albedo.rgb * vColor * lighting;

    // Simple Skybox reflection
    vec3 R = reflect(-V, N);
    vec3 reflectedColor = texture(uSkybox, R).rgb;

    float fresnel = pow(1.0 - max(dot(N, V), 0.0), 5.0);
    float reflectivity = 0.35 + fresnel * 0.65;
    reflectivity = clamp(reflectivity, 0.0, 1.0);

    vec3 finalColor = litBaseColor + reflectedColor * reflectivity * 0.75;
    finalColor = min(finalColor, vec3(1.0));

    outColor = vec4(finalColor, albedo.a);

    // use to debug reflections
    //outColor = vec4(reflectedColor, 1.0);
}