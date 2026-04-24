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

layout(location = 0) in vec3 aPosition;
layout(location = 1) in vec3 aColor;
layout(location = 2) in vec2 aUV;
layout(location = 3) in vec3 aNormal;

layout(location = 0) out vec3 vColor;
layout(location = 1) out vec2 vUV;
layout(location = 2) out vec3 vNormalWS;
layout(location = 3) out vec3 vWorldPos;

void main()
{
    vec4 worldPos = uModel * vec4(aPosition, 1.0);
    gl_Position = uViewProj * worldPos;

    vColor = aColor;
    vUV = aUV;

    mat3 normalMatrix = mat3(transpose(inverse(uModel)));
    vNormalWS = normalize(normalMatrix * aNormal);

    vWorldPos = worldPos.xyz;
}