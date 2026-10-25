#pragma once

#include <glm/glm.hpp>

#include <cstdint>
#include <span>

namespace Nyx
{
	struct ColorGrade;
	struct ColorGradingComponent;
	struct ColorGradingKey;

	// Colour grading as plain math (Docs/Design/atmosphere.md): the grade the sun's height gives,
	// what the bake makes of it, and what a colour of the tonemapped picture becomes. No GPU, so
	// tests check it as the renderer uses it (VulkanColorGrading, ColorGrading.slang).

	// The lookup table's texels a side: the tonemapped picture, sRGB-encoded, 0 to 1 each way
	inline constexpr uint32_t ColorGradingTableSize = 32;
	// How far a value the bake uses may move before the table is baked again: well under what an
	// 8-bit picture shows
	inline constexpr float ColorGradingBakeTolerance = 1.0f / 1024.0f;
	// Mid grey, sRGB-encoded (0.18 linear): what contrast turns round
	inline constexpr float ColorGradingContrastPivot = 0.46135612f;

	// A grade in range, ready for the bake: ColorGradingConstants in ColorGrading.slang, its push
	// constants (128 bytes, the most every device takes)
	struct ColorGradingConstants
	{
		// The white balance, rows of a matrix on linear light
		glm::vec4 WhiteBalance[3] = { glm::vec4(1.0f, 0.0f, 0.0f, 0.0f), glm::vec4(0.0f, 1.0f, 0.0f, 0.0f), glm::vec4(0.0f, 0.0f, 1.0f, 0.0f) };
		// xyz: the lift; w: the contrast
		glm::vec4 LiftContrast{ 0.0f, 0.0f, 0.0f, 1.0f };
		// xyz: 1 / the gamma; w: the saturation
		glm::vec4 InverseGammaSaturation{ 1.0f, 1.0f, 1.0f, 1.0f };
		// xyz: the gain; w: the tint balance
		glm::vec4 GainBalance{ 1.0f, 1.0f, 1.0f, 0.0f };
		// xyz: what the shadows' and the highlights' light is multiplied by at most, less 1 (their
		// tint's hue at luminance 1, less 1, by its strength); w unused
		glm::vec4 ShadowTint{ 0.0f };
		glm::vec4 HighlightTint{ 0.0f };
	};
	static_assert(sizeof(ColorGradingConstants) == 128, "ColorGradingConstants must match ColorGrading.slang");

	// The view's grading this frame (ColorGradingComponent, under the view's light:
	// MakeSceneViewColorGrading()); none without the component, or with it off
	struct SceneViewColorGradingSettings
	{
		bool bColorGrading = false;
		ColorGradingConstants Constants;
	};

	// The primary light's height above the horizon, in degrees, from the way towards it
	float GetSunElevation(const glm::vec3& towardsLight);

	// Each value of the two grades mixed, 0 all of `from`, 1 all of `to`
	ColorGrade MixColorGrades(const ColorGrade& from, const ColorGrade& to, float amount);
	// The grade of the keys, in any order, at this height of the sun: between the two keys round it,
	// mixed by where it is between them; below the lowest or above the highest, that one; with none,
	// a grade that leaves the picture as it is
	ColorGrade BlendColorGradingKeys(std::span<const ColorGradingKey> keys, float sunElevation);
	// The component's grade with the sun at this height: its keys' blend with Use Keys and any keys,
	// else its own
	ColorGrade GetColorGrade(const ColorGradingComponent& grading, float sunElevation);

	// The grade's values in range, ready for the bake
	ColorGradingConstants MakeColorGradingConstants(const ColorGrade& grade);
	// The view's grading from the world's component, with the primary light the way `towardsLight`
	// says: only the light's height counts, not where the camera is
	SceneViewColorGradingSettings MakeSceneViewColorGrading(const ColorGradingComponent& grading, const glm::vec3& towardsLight);
	// Whether a table baked from `baked` would differ noticeably from one baked from `wanted`: any
	// value moved by more than ColorGradingBakeTolerance
	bool IsColorGradingBakeNeeded(const ColorGradingConstants& baked, const ColorGradingConstants& wanted);

	// What the grade makes of a colour of the tonemapped picture, sRGB-encoded, 0 to 1, in and out:
	// white balance, contrast, lift, gamma and gain, split toning, saturation (ColorGrading.slang's
	// gradeColor())
	glm::vec3 GradeColor(const ColorGradingConstants& constants, const glm::vec3& encoded);
	// The colour a texel of the table stands for: x red, y green, z blue, 0 to 1 over the table
	glm::vec3 GetColorGradingTableInput(const glm::uvec3& texel);

	// The tonemap's own math (nyx/color.slang), for what is graded: Khronos PBR Neutral, and the
	// sRGB encoding, both ways
	glm::vec3 TonemapPbrNeutral(glm::vec3 color);
	glm::vec3 LinearToSrgb(const glm::vec3& linear);
	glm::vec3 SrgbToLinear(const glm::vec3& encoded);
}
