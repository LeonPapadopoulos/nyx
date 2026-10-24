#pragma once

#include "ReflectionMacros.h"

#include <glm/glm.hpp>

#include <cstdint>

namespace Nyx
{
	// Light from a far-away source like the sun, shining along the entity's forward axis (-Z).
	// The scene is lit by the first light marked as primary, which casts the scene's shadows:
	// cascaded shadow maps, each cascade a slice of the camera's view out to the shadow distance,
	// nearer slices finer.
	NYX_REFLECT(Component, meta = (DisplayName = "Directional Light Component"))
	struct DirectionalLightComponent
	{
		NYX_PROPERTY(Edit, Undo, Serialize, meta = (Category = "Light", UI = Color))
		glm::vec3 Color{ 1.0f, 1.0f, 1.0f };

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (Category = "Light", DragSpeed = 0.01))
		float Intensity = 1.0f;

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (Category = "Light", DragSpeed = 0.01, Tooltip = "Light that reaches every surface, including those facing away"))
		float Ambient = 0.18f;

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (Category = "Light", DisplayName = "Primary", Tooltip = "Whether this light lights the scene"))
		bool bPrimary = true;

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (Category = "Shadows", DisplayName = "Cast Shadows", Tooltip = "Whether what the light shines on casts shadows; only the primary light's are drawn"))
		bool bCastShadows = true;

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (Category = "Shadows", DisplayName = "Cascades", Tooltip = "How many slices of the view the shadows are drawn in, 1 to 4: more keep near shadows sharp further out"))
		int32_t CascadeCount = 4;

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (Category = "Shadows", DisplayName = "Shadow Distance", DragSpeed = 0.5, Tooltip = "How far from the camera shadows are drawn, in metres; they fade out over the last tenth"))
		float ShadowDistance = 80.0f;

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (Category = "Shadows", DisplayName = "Split Blend", DragSpeed = 0.01, Tooltip = "Where the cascades end, 0 to 1: 0 evenly apart, 1 each a few times the one before, so near cascades are small and sharp"))
		float SplitBlend = 0.8f;

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (Category = "Shadows", DisplayName = "Depth Bias", DragSpeed = 0.05, Tooltip = "How far a surface is moved towards the light before its shadow is looked up, in texels of its cascade: more takes acne away, too much lifts shadows off their casters"))
		float DepthBias = 1.0f;

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (Category = "Shadows", DisplayName = "Normal Bias", DragSpeed = 0.05, Tooltip = "How far a surface is moved out along its normal before its shadow is looked up, in texels of its cascade: takes acne away on surfaces turned from the light"))
		float NormalBias = 1.5f;

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (Category = "Shadows", DisplayName = "Softness", DragSpeed = 0.05, Tooltip = "How soft shadows' edges are, 0 to 3: the texels around a point filtered together (PCF), out to this many on each side"))
		float FilterSoftness = 1.0f;

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (Category = "Shadows", DisplayName = "Resolution", Tooltip = "Texels across each cascade's shadow map, 256 to 4096, rounded to a power of two"))
		int32_t ShadowMapResolution = 2048;
	};
}

#include "Generated/Runtime/DirectionalLightComponent.reflect.h"
