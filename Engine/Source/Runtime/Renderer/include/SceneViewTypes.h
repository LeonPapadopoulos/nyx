#pragma once

#include <cstdint>
#include <glm/glm.hpp>

namespace Nyx
{
	enum class EViewportCameraMode : uint8_t
	{
		EditorFreeCamera,
		ScenePrimaryCamera
	};

	// The sky a scene view shows and is lit by (IRenderer::SetSceneViewSky())
	enum class ESceneViewSky : uint8_t
	{
		// The scene's sky
		Scene,
		// A grey room with soft lights, for looking at materials
		Studio,
		// Nothing: only the sun lights
		Black,
	};

	// What a scene view shows (IRenderer::SetSceneViewDebug()): the lit scene, one output of its
	// surface materials, or where the light before tonemapping is wrong. The values are
	// nyx/view.slang's Debug* constants.
	enum class ESceneViewDebug : uint8_t
	{
		Lit,
		BaseColor,
		// The normal, 0 to 1 per axis: facing the camera is light blue
		Normal,
		Roughness,
		Metallic,
		Occlusion,
		Emission,
		Opacity,
		// A checker coloured by the UV: red along u, green along v
		UvChecker,
		// Pixels that aren't numbers or are infinite, bright magenta over the scene in grey
		NanInf,
		// Light above 1 before tonemapping in red, below 0 in blue, over the scene in grey
		OutOfRange,
	};

	struct ExtractedSceneGlobals
	{
		glm::mat4 View{ 1.0f };
		glm::mat4 Projection{ 1.0f };
		glm::mat4 ViewProjection{ 1.0f };

		glm::vec3 CameraWorldPos{ 0.0f, 0.0f, 3.0f };
		glm::vec3 LightDirectionWS{ glm::normalize(glm::vec3(0.4f, 1.0f, 0.2f)) };
		glm::vec3 LightColor{ 1.0f, 0.98f, 0.95f };
		float Ambient = 0.18f;

		bool bHasCamera = false;
		bool bHasDirectionalLight = false;
	};
}