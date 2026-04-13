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

layout(location = 0) in vec3 vColor;
layout(location = 1) in vec2 vUV;
layout(location = 2) in vec3 vNormalWS;

layout(location = 0) out vec4 outColor;

void main()
{
    vec4 albedo = texture(uTexture, vUV);

    vec3 N = normalize(vNormalWS);
    vec3 L = normalize(uLightDirectionWS.xyz);

    float NdotL = max(dot(N, L), 0.0);
    float ambient = uLightColor.a;

    vec3 lighting = ambient + (NdotL * uLightColor.rgb);
    vec3 finalColor = albedo.rgb * vColor * lighting;

    outColor = vec4(finalColor, albedo.a);
}