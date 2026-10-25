#include "NyxPCH.h"
#include "ColorGrading.h"

#include "ColorGradingComponent.h"

#include <algorithm>
#include <cmath>

namespace Nyx
{
	namespace
	{
		// Rec. 709 / sRGB primaries
		constexpr glm::vec3 Luma(0.2126f, 0.7152f, 0.0722f);

		// A matrix from its rows (glm's are columns)
		glm::mat3 FromRows(const glm::vec3& row0, const glm::vec3& row1, const glm::vec3& row2)
		{
			return glm::transpose(glm::mat3(row0, row1, row2));
		}

		// Linear sRGB to CIE XYZ, white D65
		const glm::mat3 RgbToXyz = FromRows(glm::vec3(0.4124564f, 0.3575761f, 0.1804375f), glm::vec3(0.2126729f, 0.7151522f, 0.0721750f), glm::vec3(0.0193339f, 0.1191920f, 0.9503041f));
		// CIECAM02's cone responses (CAT02), for adapting from one white to another
		const glm::mat3 XyzToLms = FromRows(glm::vec3(0.7328f, 0.4296f, -0.1624f), glm::vec3(-0.7036f, 1.6975f, 0.0061f), glm::vec3(0.0030f, 0.0136f, 0.9834f));

		constexpr float D65X = 0.31271f;
		constexpr float D65Y = 0.32902f;

		// Where the CIE's daylight whites lie: y by x
		float DaylightY(float x)
		{
			return -3.0f * x * x + 2.87f * x - 0.275f;
		}

		glm::vec3 XyzOf(float x, float y)
		{
			return glm::vec3(x / y, 1.0f, (1.0f - x - y) / y);
		}

		// The picture adapted, as an eye adapts, from a white along the daylight whites (temperature)
		// and across them (tint) to D65: the light of a bluer white made warmer, of a greener one more
		// magenta. White keeps its luminance. 0 and 0: none.
		glm::mat3 MakeWhiteBalance(float temperature, float tint)
		{
			if (temperature == 0.0f && tint == 0.0f)
			{
				return glm::mat3(1.0f);
			}
			// From about 25 000 K (warmest) to 4 300 K (coolest); y kept relative to D65's
			const float x = D65X - temperature * 0.06f;
			const float y = DaylightY(x) - DaylightY(D65X) + D65Y + tint * 0.04f;
			const glm::vec3 white = XyzToLms * XyzOf(D65X, D65Y);
			const glm::vec3 adapted = XyzToLms * XyzOf(x, y);
			glm::mat3 scale(1.0f);
			for (int cone = 0; cone < 3; ++cone)
			{
				scale[cone][cone] = white[cone] / adapted[cone];
			}
			const glm::mat3 balance = glm::inverse(RgbToXyz) * glm::inverse(XyzToLms) * scale * XyzToLms * RgbToXyz;
			return balance / glm::dot(Luma, balance * glm::vec3(1.0f));
		}

		// The tint's hue at luminance 1, less 1, by its strength: what light is multiplied by, less 1
		glm::vec3 MakeTint(const glm::vec3& tint, float strength)
		{
			const glm::vec3 color = glm::max(tint, glm::vec3(0.0f));
			const float luminance = glm::dot(Luma, color);
			const glm::vec3 hue = luminance > 1e-4f ? color / luminance : glm::vec3(1.0f);
			return std::clamp(strength, 0.0f, 1.0f) * (hue - 1.0f);
		}

		float LinearToSrgb(float linear)
		{
			const float c = std::clamp(linear, 0.0f, 1.0f);
			return c <= 0.0031308f ? c * 12.92f : 1.055f * std::pow(c, 1.0f / 2.4f) - 0.055f;
		}

		float SrgbToLinear(float encoded)
		{
			return encoded <= 0.04045f ? encoded / 12.92f : std::pow((encoded + 0.055f) / 1.055f, 2.4f);
		}

		// Darks darker and lights lighter round mid grey by `contrast`, 0 and 1 kept: a power curve
		// each side, as steep as `contrast` at the pivot from both
		float Contrast(float shown, float contrast)
		{
			constexpr float Pivot = ColorGradingContrastPivot;
			return shown < Pivot ? Pivot * std::pow(shown / Pivot, contrast) : 1.0f - (1.0f - Pivot) * std::pow((1.0f - shown) / (1.0f - Pivot), contrast);
		}
	}

	float GetSunElevation(const glm::vec3& towardsLight)
	{
		const float length = glm::length(towardsLight);
		if (!(length > 0.0f))
		{
			return 0.0f;
		}
		return glm::degrees(std::asin(std::clamp(towardsLight.y / length, -1.0f, 1.0f)));
	}

	ColorGrade MixColorGrades(const ColorGrade& from, const ColorGrade& to, float amount)
	{
		ColorGrade mixed;
		mixed.Temperature = glm::mix(from.Temperature, to.Temperature, amount);
		mixed.Tint = glm::mix(from.Tint, to.Tint, amount);
		mixed.Contrast = glm::mix(from.Contrast, to.Contrast, amount);
		mixed.Lift = glm::mix(from.Lift, to.Lift, amount);
		mixed.Gamma = glm::mix(from.Gamma, to.Gamma, amount);
		mixed.Gain = glm::mix(from.Gain, to.Gain, amount);
		mixed.Saturation = glm::mix(from.Saturation, to.Saturation, amount);
		mixed.ShadowTint = glm::mix(from.ShadowTint, to.ShadowTint, amount);
		mixed.ShadowTintStrength = glm::mix(from.ShadowTintStrength, to.ShadowTintStrength, amount);
		mixed.HighlightTint = glm::mix(from.HighlightTint, to.HighlightTint, amount);
		mixed.HighlightTintStrength = glm::mix(from.HighlightTintStrength, to.HighlightTintStrength, amount);
		mixed.TintBalance = glm::mix(from.TintBalance, to.TintBalance, amount);
		return mixed;
	}

	ColorGrade BlendColorGradingKeys(std::span<const ColorGradingKey> keys, float sunElevation)
	{
		if (keys.empty())
		{
			return ColorGrade{};
		}
		// The nearest key at or below the sun, and at or above it; the first of keys at one height
		const ColorGradingKey* below = nullptr;
		const ColorGradingKey* above = nullptr;
		for (const ColorGradingKey& key : keys)
		{
			if (key.SunElevation <= sunElevation && (!below || key.SunElevation > below->SunElevation))
			{
				below = &key;
			}
			if (key.SunElevation >= sunElevation && (!above || key.SunElevation < above->SunElevation))
			{
				above = &key;
			}
		}
		if (!below && !above)
		{
			// Not a number
			return keys.front().Grade;
		}
		if (!below)
		{
			return above->Grade;
		}
		if (!above || above->SunElevation <= below->SunElevation)
		{
			return below->Grade;
		}
		const float amount = (sunElevation - below->SunElevation) / (above->SunElevation - below->SunElevation);
		return MixColorGrades(below->Grade, above->Grade, amount);
	}

	ColorGrade GetColorGrade(const ColorGradingComponent& grading, float sunElevation)
	{
		if (grading.bUseKeys && !grading.Keys.empty())
		{
			return BlendColorGradingKeys(grading.Keys, sunElevation);
		}
		return grading.Grade;
	}

	ColorGradingConstants MakeColorGradingConstants(const ColorGrade& grade)
	{
		ColorGradingConstants constants;
		const glm::mat3 balance = MakeWhiteBalance(std::clamp(grade.Temperature, -1.0f, 1.0f), std::clamp(grade.Tint, -1.0f, 1.0f));
		for (int row = 0; row < 3; ++row)
		{
			constants.WhiteBalance[row] = glm::vec4(balance[0][row], balance[1][row], balance[2][row], 0.0f);
		}
		// A contrast of 0 would be 0 to the power 0 at black and white
		constants.LiftContrast = glm::vec4(glm::clamp(grade.Lift, glm::vec3(-1.0f), glm::vec3(1.0f)), std::clamp(grade.Contrast, 0.01f, 4.0f));
		constants.InverseGammaSaturation = glm::vec4(1.0f / glm::clamp(grade.Gamma, glm::vec3(0.1f), glm::vec3(10.0f)), std::clamp(grade.Saturation, 0.0f, 4.0f));
		constants.GainBalance = glm::vec4(glm::clamp(grade.Gain, glm::vec3(0.0f), glm::vec3(4.0f)), std::clamp(grade.TintBalance, -1.0f, 1.0f));
		constants.ShadowTint = glm::vec4(MakeTint(grade.ShadowTint, grade.ShadowTintStrength), 0.0f);
		constants.HighlightTint = glm::vec4(MakeTint(grade.HighlightTint, grade.HighlightTintStrength), 0.0f);
		return constants;
	}

	SceneViewColorGradingSettings MakeSceneViewColorGrading(const ColorGradingComponent& grading, const glm::vec3& towardsLight)
	{
		SceneViewColorGradingSettings settings;
		settings.bColorGrading = grading.bGrading;
		settings.Constants = MakeColorGradingConstants(GetColorGrade(grading, GetSunElevation(towardsLight)));
		return settings;
	}

	bool IsColorGradingBakeNeeded(const ColorGradingConstants& baked, const ColorGradingConstants& wanted)
	{
		const auto* from = reinterpret_cast<const float*>(&baked);
		const auto* to = reinterpret_cast<const float*>(&wanted);
		for (size_t value = 0; value < sizeof(ColorGradingConstants) / sizeof(float); ++value)
		{
			if (!(std::abs(from[value] - to[value]) <= ColorGradingBakeTolerance))
			{
				return true;
			}
		}
		return false;
	}

	glm::vec3 GradeColor(const ColorGradingConstants& constants, const glm::vec3& encoded)
	{
		// The white balance, on light
		glm::vec3 light = SrgbToLinear(glm::clamp(encoded, glm::vec3(0.0f), glm::vec3(1.0f)));
		light = glm::max(glm::vec3(glm::dot(glm::vec3(constants.WhiteBalance[0]), light), glm::dot(glm::vec3(constants.WhiteBalance[1]), light), glm::dot(glm::vec3(constants.WhiteBalance[2]), light)), glm::vec3(0.0f));

		// Contrast, lift, gamma and gain, on what is shown
		glm::vec3 shown = LinearToSrgb(light);
		for (int channel = 0; channel < 3; ++channel)
		{
			shown[channel] = Contrast(shown[channel], constants.LiftContrast.w);
		}
		const glm::vec3 lift(constants.LiftContrast);
		shown = glm::vec3(constants.GainBalance) * (shown + lift * (1.0f - shown));
		shown = glm::pow(glm::max(shown, glm::vec3(0.0f)), glm::vec3(constants.InverseGammaSaturation));

		// The shadows and the highlights tinted apart, by how bright each is shown
		light = SrgbToLinear(shown);
		const float bright = std::clamp(LinearToSrgb(glm::dot(Luma, light)) + 0.5f * constants.GainBalance.w, 0.0f, 1.0f);
		const float highlights = bright * bright * (3.0f - 2.0f * bright);
		light *= (1.0f + glm::vec3(constants.ShadowTint) * (1.0f - highlights)) * (1.0f + glm::vec3(constants.HighlightTint) * highlights);

		// Saturation, the luminance kept
		const float luminance = glm::dot(Luma, light);
		light = luminance + constants.InverseGammaSaturation.w * (light - luminance);
		return LinearToSrgb(glm::max(light, glm::vec3(0.0f)));
	}

	glm::vec3 GetColorGradingTableInput(const glm::uvec3& texel)
	{
		return glm::vec3(texel) / static_cast<float>(ColorGradingTableSize - 1);
	}

	glm::vec3 TonemapPbrNeutral(glm::vec3 color)
	{
		constexpr float StartCompression = 0.8f - 0.04f;
		constexpr float Desaturation = 0.15f;
		const float x = std::min(color.r, std::min(color.g, color.b));
		const float offset = x < 0.08f ? x - 6.25f * x * x : 0.04f;
		color -= offset;
		const float peak = std::max(color.r, std::max(color.g, color.b));
		if (peak < StartCompression)
		{
			return color;
		}
		constexpr float D = 1.0f - StartCompression;
		const float newPeak = 1.0f - D * D / (peak + D - StartCompression);
		color *= newPeak / peak;
		const float g = 1.0f - 1.0f / (Desaturation * (peak - newPeak) + 1.0f);
		return glm::mix(color, glm::vec3(newPeak), g);
	}

	glm::vec3 LinearToSrgb(const glm::vec3& linear)
	{
		return glm::vec3(LinearToSrgb(linear.r), LinearToSrgb(linear.g), LinearToSrgb(linear.b));
	}

	glm::vec3 SrgbToLinear(const glm::vec3& encoded)
	{
		return glm::vec3(SrgbToLinear(encoded.r), SrgbToLinear(encoded.g), SrgbToLinear(encoded.b));
	}
}
