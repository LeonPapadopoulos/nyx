#pragma once

#include "ReflectionMacros.h"

#include <glm/glm.hpp>

#include <vector>

namespace Nyx
{
	// One set of grading values, as the picture is graded after the tonemap: its white balance, its
	// contrast, lift, gamma and gain, its shadows and highlights tinted apart (split toning) and its
	// saturation. The defaults leave the picture as it is.
	NYX_REFLECT()
	struct ColorGrade
	{
		NYX_PROPERTY(Edit, Undo, Serialize, meta = (DragSpeed = 0.01, Tooltip = "The white balance, -1 to 1: below 0 cooler (bluer), above 0 warmer (more orange)"))
		float Temperature = 0.0f;

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (DragSpeed = 0.01, Tooltip = "The white balance across, -1 to 1: below 0 greener, above 0 more magenta"))
		float Tint = 0.0f;

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (DragSpeed = 0.01, Tooltip = "0 to 4: above 1 darks darker and lights lighter round mid grey, below 1 flatter; black and white stay"))
		float Contrast = 1.0f;

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (DragSpeed = 0.002, Tooltip = "Added to the shadows, red, green and blue, -1 to 1: black becomes this, white stays; a colour lifts the shadows in it"))
		glm::vec3 Lift{ 0.0f, 0.0f, 0.0f };

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (DragSpeed = 0.005, Tooltip = "The midtones, red, green and blue, 0.1 to 10: above 1 lighter, below 1 darker; black and white stay"))
		glm::vec3 Gamma{ 1.0f, 1.0f, 1.0f };

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (DragSpeed = 0.005, Tooltip = "What the highlights are multiplied by, red, green and blue, 0 to 4: white becomes this, black stays"))
		glm::vec3 Gain{ 1.0f, 1.0f, 1.0f };

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (DragSpeed = 0.01, Tooltip = "0 to 4: 0 grey, 1 as it is, above 1 more colourful; the brightness stays"))
		float Saturation = 1.0f;

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (DisplayName = "Shadow Tint", UI = Color, Tooltip = "The hue the shadows take, by Shadow Tint Strength; its brightness doesn't count"))
		glm::vec3 ShadowTint{ 0.4f, 0.6f, 1.0f };

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (DisplayName = "Shadow Tint Strength", DragSpeed = 0.01, Tooltip = "How much the shadows take the shadow tint, 0 to 1"))
		float ShadowTintStrength = 0.0f;

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (DisplayName = "Highlight Tint", UI = Color, Tooltip = "The hue the highlights take, by Highlight Tint Strength; its brightness doesn't count"))
		glm::vec3 HighlightTint{ 1.0f, 0.8f, 0.5f };

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (DisplayName = "Highlight Tint Strength", DragSpeed = 0.01, Tooltip = "How much the highlights take the highlight tint, 0 to 1"))
		float HighlightTintStrength = 0.0f;

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (DisplayName = "Tint Balance", DragSpeed = 0.01, Tooltip = "Where the shadows' tint gives way to the highlights', -1 to 1: above 0 more of the picture counts as highlights"))
		float TintBalance = 0.0f;
	};

	// A grade and the sun's height it belongs to
	NYX_REFLECT()
	struct ColorGradingKey
	{
		NYX_PROPERTY(Edit, Undo, Serialize, meta = (DisplayName = "Sun Elevation", DragSpeed = 0.1, Tooltip = "The primary light's height above the horizon this grade belongs to, in degrees, -90 to 90: below 0 the sun has set"))
		float SunElevation = 0.0f;

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (Tooltip = "The grade at this height of the sun"))
		ColorGrade Grade;
	};

	// How every view of the world grades its picture: after the tonemap, through a lookup table
	// baked from the grade whenever it changes. With Use Keys, the grade follows the sun: its keys,
	// each a grade at a height of the primary light, blended by where the light is, so the golden
	// hour is graded warmer because the sun is low. The world's first such component counts; without
	// one, or with it off, the picture is as it is.
	NYX_REFLECT(Component, meta = (DisplayName = "Color Grading"))
	struct ColorGradingComponent
	{
		NYX_PROPERTY(Edit, Undo, Serialize, meta = (Category = "Color Grading", DisplayName = "Grading", Tooltip = "Whether the picture is graded"))
		bool bGrading = true;

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (Category = "Color Grading", DisplayName = "Use Keys", Tooltip = "Whether the grade follows the sun's height, blended between the keys; else Grade is used"))
		bool bUseKeys = false;

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (Category = "Color Grading", Tooltip = "The grade, while the keys aren't used or there are none"))
		ColorGrade Grade;

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (Category = "Color Grading", Tooltip = "Grades at heights of the sun, in any order: between two, blended by the sun's height; below the lowest or above the highest, that one"))
		std::vector<ColorGradingKey> Keys;
	};
}

#include "Generated/Runtime/ColorGradingComponent.reflect.h"
