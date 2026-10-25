// Colour grading without a GPU (ColorGrading.h): a grade at its defaults leaves every texel of the
// table as it is; each control moves colours the way it says (warmer, cooler, greener, more
// magenta; more and less saturated, the brightness kept; contrast round mid grey; lifted shadows,
// lighter midtones, dimmer highlights; shadows and highlights tinted apart), black and white kept
// where they should be; values out of range stay in range. The keys: halfway between two keys'
// heights of the sun, halfway values; outside them, the nearest; in any order. The sun's height
// from the light's direction; a table baked again only when its grade moved noticeably, never
// further than that from what is wanted; and the component saved in a scene and read back.
#include "ColorGrading.h"
#include "ColorGradingComponent.h"
#include "ComponentRegistration.h"
#include "Entity.h"
#include "GuidComponent.h"
#include "Log.h"
#include "SceneSerializer.h"
#include "TestSupport.h"

#include <glm/glm.hpp>

#include <cmath>
#include <iostream>
#include <limits>
#include <string>
#include <vector>

namespace
{
	using namespace Nyx;
	using Nyx::Tests::Require;

	std::string Text(const glm::vec3& v)
	{
		return std::to_string(v.x) + " " + std::to_string(v.y) + " " + std::to_string(v.z);
	}

	float Luminance(const glm::vec3& linear)
	{
		return glm::dot(linear, glm::vec3(0.2126f, 0.7152f, 0.0722f));
	}

	// The brightness of an encoded colour, as light
	float LuminanceOf(const glm::vec3& encoded)
	{
		return Luminance(SrgbToLinear(encoded));
	}

	glm::vec3 Graded(const ColorGrade& grade, const glm::vec3& encoded)
	{
		return GradeColor(MakeColorGradingConstants(grade), encoded);
	}

	bool Near(float a, float b, float tolerance)
	{
		return std::abs(a - b) <= tolerance;
	}

	bool Near(const glm::vec3& a, const glm::vec3& b, float tolerance)
	{
		return glm::all(glm::lessThanEqual(glm::abs(a - b), glm::vec3(tolerance)));
	}

	const glm::vec3 Grey(0.5f);

	// The defaults change nothing: every texel of the table is the colour it stands for
	void TestIdentity()
	{
		const ColorGradingConstants constants = MakeColorGradingConstants(ColorGrade{});
		Require(!IsColorGradingBakeNeeded(ColorGradingConstants{}, constants), "A grade at its defaults isn't the identity's constants");
		Require(GetColorGradingTableInput(glm::uvec3(0)) == glm::vec3(0.0f) && GetColorGradingTableInput(glm::uvec3(ColorGradingTableSize - 1)) == glm::vec3(1.0f),
			"The table doesn't span 0 to 1");
		float largest = 0.0f;
		for (uint32_t z = 0; z < ColorGradingTableSize; ++z)
		{
			for (uint32_t y = 0; y < ColorGradingTableSize; ++y)
			{
				for (uint32_t x = 0; x < ColorGradingTableSize; ++x)
				{
					const glm::vec3 input = GetColorGradingTableInput(glm::uvec3(x, y, z));
					const glm::vec3 difference = glm::abs(GradeColor(constants, input) - input);
					largest = std::max(largest, std::max(difference.x, std::max(difference.y, difference.z)));
				}
			}
		}
		Require(largest < 1e-4f, "The identity's table moves a colour by " + std::to_string(largest));
	}

	void TestWhiteBalance()
	{
		ColorGrade grade;
		grade.Temperature = 0.5f;
		const glm::vec3 warm = Graded(grade, Grey);
		Require(warm.r > Grey.r + 0.02f && warm.b < Grey.b - 0.02f, "A warmer white balance doesn't warm grey: " + Text(warm));
		Require(Near(LuminanceOf(warm), LuminanceOf(Grey), 0.01f), "A warmer white balance changes grey's brightness: " + Text(warm));
		grade.Temperature = -0.5f;
		const glm::vec3 cool = Graded(grade, Grey);
		Require(cool.b > Grey.b + 0.02f && cool.r < Grey.r - 0.02f, "A cooler white balance doesn't cool grey: " + Text(cool));

		grade.Temperature = 0.0f;
		grade.Tint = 0.5f;
		const glm::vec3 magenta = Graded(grade, Grey);
		Require(magenta.g < magenta.r && magenta.g < magenta.b && magenta.g < Grey.g, "A tint above 0 isn't more magenta: " + Text(magenta));
		grade.Tint = -0.5f;
		const glm::vec3 green = Graded(grade, Grey);
		Require(green.g > green.r && green.g > green.b && green.g > Grey.g, "A tint below 0 isn't greener: " + Text(green));
		Require(Graded(grade, glm::vec3(0.0f)) == glm::vec3(0.0f), "The white balance lightens black");
	}

	void TestSaturation()
	{
		const glm::vec3 color(0.7f, 0.45f, 0.3f);
		const float spread = color.r - color.b;
		ColorGrade grade;
		grade.Saturation = 1.5f;
		const glm::vec3 vivid = Graded(grade, color);
		Require(vivid.r - vivid.b > spread + 0.05f, "More saturation doesn't spread the colour: " + Text(vivid));
		Require(Near(LuminanceOf(vivid), LuminanceOf(color), 0.01f), "Saturation changes the brightness: " + Text(vivid));
		grade.Saturation = 0.0f;
		const glm::vec3 grey = Graded(grade, color);
		Require(Near(grey.r, grey.g, 1e-4f) && Near(grey.g, grey.b, 1e-4f), "No saturation isn't grey: " + Text(grey));
		Require(Near(LuminanceOf(grey), LuminanceOf(color), 0.01f), "No saturation changes the brightness: " + Text(grey));
	}

	void TestContrast()
	{
		ColorGrade grade;
		grade.Contrast = 1.5f;
		const glm::vec3 dark = Graded(grade, glm::vec3(0.2f));
		const glm::vec3 light = Graded(grade, glm::vec3(0.8f));
		Require(dark.r < 0.2f - 0.02f && light.r > 0.8f + 0.02f, "More contrast doesn't darken the darks and lighten the lights: " + Text(dark) + ", " + Text(light));
		Require(Near(Graded(grade, glm::vec3(ColorGradingContrastPivot)), glm::vec3(ColorGradingContrastPivot), 1e-4f), "Contrast moves mid grey");
		Require(Near(Graded(grade, glm::vec3(0.0f)), glm::vec3(0.0f), 1e-5f) && Near(Graded(grade, glm::vec3(1.0f)), glm::vec3(1.0f), 1e-5f), "Contrast moves black or white");
		grade.Contrast = 0.6f;
		Require(Graded(grade, glm::vec3(0.2f)).r > 0.2f + 0.02f, "Less contrast doesn't lighten the darks");
	}

	void TestLiftGammaGain()
	{
		ColorGrade lifted;
		lifted.Lift = glm::vec3(0.1f);
		Require(Near(Graded(lifted, glm::vec3(0.0f)), glm::vec3(0.1f), 1e-4f), "A lift of 0.1 doesn't lift black to 0.1: " + Text(Graded(lifted, glm::vec3(0.0f))));
		Require(Near(Graded(lifted, glm::vec3(1.0f)), glm::vec3(1.0f), 1e-5f), "The lift moves white");
		const float shadows = Graded(lifted, glm::vec3(0.2f)).r - 0.2f;
		const float highlights = Graded(lifted, glm::vec3(0.8f)).r - 0.8f;
		Require(shadows > 0.05f && shadows > highlights * 3.0f, "The lift doesn't lift the shadows most: " + std::to_string(shadows) + " against " + std::to_string(highlights));
		// A colour lifts the shadows in it, and only it
		lifted.Lift = glm::vec3(0.0f, 0.0f, 0.1f);
		const glm::vec3 blueShadows = Graded(lifted, glm::vec3(0.1f));
		Require(blueShadows.b > 0.15f && Near(blueShadows.r, 0.1f, 1e-4f), "A blue lift doesn't lift the shadows' blue alone: " + Text(blueShadows));

		ColorGrade gamma;
		gamma.Gamma = glm::vec3(1.5f);
		Require(Near(Graded(gamma, Grey).r, std::pow(0.5f, 1.0f / 1.5f), 1e-4f), "A gamma of 1.5 doesn't lighten the midtones as it says");
		Require(Near(Graded(gamma, glm::vec3(0.0f)), glm::vec3(0.0f), 1e-6f) && Near(Graded(gamma, glm::vec3(1.0f)), glm::vec3(1.0f), 1e-5f), "The gamma moves black or white");

		ColorGrade gain;
		gain.Gain = glm::vec3(0.8f);
		Require(Near(Graded(gain, glm::vec3(1.0f)), glm::vec3(0.8f), 1e-4f), "A gain of 0.8 doesn't dim white to 0.8");
		Require(Graded(gain, glm::vec3(0.0f)) == glm::vec3(0.0f), "The gain moves black");
	}

	void TestSplitToning()
	{
		ColorGrade grade;
		grade.ShadowTint = glm::vec3(0.2f, 0.4f, 1.0f);
		grade.ShadowTintStrength = 0.6f;
		grade.HighlightTint = glm::vec3(1.0f, 0.7f, 0.3f);
		grade.HighlightTintStrength = 0.6f;
		const glm::vec3 shadow = Graded(grade, glm::vec3(0.15f));
		const glm::vec3 highlight = Graded(grade, glm::vec3(0.85f));
		Require(shadow.b > shadow.r + 0.02f, "The shadows don't take the shadow tint: " + Text(shadow));
		Require(highlight.r > highlight.b + 0.02f, "The highlights don't take the highlight tint: " + Text(highlight));
		Require(Near(LuminanceOf(shadow), LuminanceOf(glm::vec3(0.15f)), 0.01f) && Near(LuminanceOf(Graded(grade, Grey)), LuminanceOf(Grey), 0.01f),
			"Split toning changes grey's brightness");
		Require(Graded(grade, glm::vec3(0.0f)) == glm::vec3(0.0f), "Split toning lightens black");

		// The balance towards the highlights: mid grey takes more of the highlights' tint
		const glm::vec3 even = Graded(grade, Grey);
		grade.TintBalance = 0.8f;
		const glm::vec3 towardsHighlights = Graded(grade, Grey);
		Require(towardsHighlights.r - towardsHighlights.b > even.r - even.b + 0.02f, "The balance doesn't give mid grey more of the highlights' tint: " + Text(towardsHighlights) + " against " + Text(even));

		// No strength, no tint, whatever the colours
		grade.ShadowTintStrength = 0.0f;
		grade.HighlightTintStrength = 0.0f;
		Require(Near(Graded(grade, glm::vec3(0.3f, 0.5f, 0.7f)), glm::vec3(0.3f, 0.5f, 0.7f), 1e-4f), "Tints without strength change the picture");
	}

	// Values far out of range give a picture in range, never NaN
	void TestOutOfRange()
	{
		ColorGrade grade;
		grade.Temperature = 9.0f;
		grade.Tint = -9.0f;
		grade.Contrast = -3.0f;
		grade.Lift = glm::vec3(-4.0f, 4.0f, 0.5f);
		grade.Gamma = glm::vec3(0.0f, -1.0f, 100.0f);
		grade.Gain = glm::vec3(-1.0f, 10.0f, 2.0f);
		grade.Saturation = -2.0f;
		grade.ShadowTint = glm::vec3(-1.0f, 0.0f, 0.0f);
		grade.ShadowTintStrength = 5.0f;
		grade.HighlightTint = glm::vec3(0.0f);
		grade.HighlightTintStrength = -5.0f;
		grade.TintBalance = 7.0f;
		const ColorGradingConstants constants = MakeColorGradingConstants(grade);
		for (const glm::vec3 input : { glm::vec3(0.0f), glm::vec3(1.0f), glm::vec3(0.3f, 0.6f, 0.9f), glm::vec3(0.999f, 0.001f, 0.5f) })
		{
			const glm::vec3 output = GradeColor(constants, input);
			Require(glm::all(glm::greaterThanEqual(output, glm::vec3(0.0f))) && glm::all(glm::lessThanEqual(output, glm::vec3(1.0f))), "A grade out of range gives " + Text(output));
		}
	}

	ColorGrade Warm(float temperature)
	{
		ColorGrade grade;
		grade.Temperature = temperature;
		grade.Lift = glm::vec3(temperature * 0.1f);
		grade.HighlightTint = glm::vec3(1.0f, temperature, 0.0f);
		grade.HighlightTintStrength = temperature;
		return grade;
	}

	ColorGradingKey Key(float sunElevation, const ColorGrade& grade)
	{
		return ColorGradingKey{ .SunElevation = sunElevation, .Grade = grade };
	}

	// Halfway between two keys, halfway values; outside them, the nearest; in any order
	void TestKeys()
	{
		const std::vector<ColorGradingKey> keys = { Key(30.0f, Warm(0.2f)), Key(-10.0f, Warm(-0.6f)), Key(10.0f, Warm(0.8f)) };
		const ColorGrade halfway = BlendColorGradingKeys(keys, 20.0f);
		Require(Near(halfway.Temperature, 0.5f, 1e-6f) && Near(halfway.Lift, glm::vec3(0.05f), 1e-6f) && Near(halfway.HighlightTint, glm::vec3(1.0f, 0.5f, 0.0f), 1e-6f) &&
				Near(halfway.HighlightTintStrength, 0.5f, 1e-6f),
			"Halfway between the keys at 10 and 30 degrees isn't halfway: temperature " + std::to_string(halfway.Temperature));
		Require(Near(BlendColorGradingKeys(keys, 5.0f).Temperature, 0.45f, 1e-6f), "Three quarters of the way up from -10 to 10 degrees isn't three quarters of the way");
		Require(BlendColorGradingKeys(keys, 10.0f).Temperature == 0.8f, "On a key, its grade");
		Require(BlendColorGradingKeys(keys, -40.0f).Temperature == -0.6f, "Below the lowest key, the lowest");
		Require(BlendColorGradingKeys(keys, 85.0f).Temperature == 0.2f, "Above the highest key, the highest");
		Require(BlendColorGradingKeys(std::vector{ Key(5.0f, Warm(0.3f)) }, -50.0f).Temperature == 0.3f, "One key isn't used at every height");
		Require(!IsColorGradingBakeNeeded(ColorGradingConstants{}, MakeColorGradingConstants(BlendColorGradingKeys({}, 10.0f))), "No keys don't leave the picture as it is");
		Require(BlendColorGradingKeys(keys, std::numeric_limits<float>::quiet_NaN()).Temperature == 0.2f, "Not a number isn't the first key");

		// The component: its keys with Use Keys, else its grade
		ColorGradingComponent grading;
		grading.Grade = Warm(0.1f);
		grading.Keys = keys;
		Require(GetColorGrade(grading, 20.0f).Temperature == 0.1f, "Without Use Keys, the keys are used");
		grading.bUseKeys = true;
		Require(Near(GetColorGrade(grading, 20.0f).Temperature, 0.5f, 1e-6f), "With Use Keys, the keys aren't used");
		grading.Keys.clear();
		Require(GetColorGrade(grading, 20.0f).Temperature == 0.1f, "With Use Keys and no keys, the grade isn't used");
	}

	void TestSunElevation()
	{
		Require(Near(GetSunElevation(glm::vec3(0.0f, 1.0f, 0.0f)), 90.0f, 1e-4f) && Near(GetSunElevation(glm::vec3(1.0f, 0.0f, 0.0f)), 0.0f, 1e-4f) &&
				Near(GetSunElevation(glm::vec3(0.0f, -2.0f, 0.0f)), -90.0f, 1e-4f),
			"The sun's height straight up, on the horizon or straight down");
		const float up = std::sin(glm::radians(12.0f));
		const float across = std::cos(glm::radians(12.0f));
		Require(Near(GetSunElevation(glm::vec3(across * 0.6f, up, across * 0.8f) * 5.0f), 12.0f, 1e-3f), "The sun 12 degrees up, its direction not of length 1");
		Require(GetSunElevation(glm::vec3(0.0f)) == 0.0f, "No direction isn't on the horizon");

		// The view's grading follows the light's height only
		ColorGradingComponent grading;
		grading.bUseKeys = true;
		grading.Keys = { Key(0.0f, Warm(0.0f)), Key(20.0f, Warm(1.0f)) };
		const SceneViewColorGradingSettings settings = MakeSceneViewColorGrading(grading, glm::vec3(across, up, 0.0f));
		Require(settings.bColorGrading, "The view doesn't grade");
		Require(!IsColorGradingBakeNeeded(settings.Constants, MakeColorGradingConstants(Warm(0.6f))), "The sun 12 degrees up isn't graded 60 % of the way from the key at 0 to the one at 20");
		grading.bGrading = false;
		Require(!MakeSceneViewColorGrading(grading, glm::vec3(across, up, 0.0f)).bColorGrading, "Grading off still grades");
	}

	// The table is baked again only when its grade moved noticeably, and the grade it shows is never
	// further than that from the one wanted, however slowly the sun moves
	void TestBakedWhenChanged()
	{
		const ColorGradingConstants still = MakeColorGradingConstants(Warm(0.5f));
		Require(!IsColorGradingBakeNeeded(still, MakeColorGradingConstants(Warm(0.5f))), "The same grade is baked again");
		ColorGrade nudged = Warm(0.5f);
		nudged.Saturation += ColorGradingBakeTolerance * 0.5f;
		Require(!IsColorGradingBakeNeeded(still, MakeColorGradingConstants(nudged)), "A change smaller than the tolerance is baked again");
		nudged.Saturation += ColorGradingBakeTolerance;
		Require(IsColorGradingBakeNeeded(still, MakeColorGradingConstants(nudged)), "A change larger than the tolerance isn't baked");

		// The sun rising from 0 to 20 degrees in 20 000 frames, from a warm, lifted, saturated grade
		// to none
		ColorGrade low;
		low.Temperature = 0.6f;
		low.Lift = glm::vec3(0.02f, 0.01f, 0.0f);
		low.Saturation = 1.2f;
		low.HighlightTintStrength = 0.3f;
		const std::vector<ColorGradingKey> keys = { Key(0.0f, low), Key(20.0f, ColorGrade{}) };
		ColorGradingConstants baked = MakeColorGradingConstants(BlendColorGradingKeys(keys, 0.0f));
		int bakes = 0;
		constexpr int Frames = 20000;
		for (int frame = 1; frame <= Frames; ++frame)
		{
			const ColorGradingConstants wanted = MakeColorGradingConstants(BlendColorGradingKeys(keys, 20.0f * frame / Frames));
			if (IsColorGradingBakeNeeded(baked, wanted))
			{
				baked = wanted;
				++bakes;
			}
		}
		std::cout << "The sun rising 20 degrees in " << Frames << " frames: baked " << bakes << " times\n";
		Require(bakes > 10 && bakes < Frames / 10, "A slowly rising sun is baked " + std::to_string(bakes) + " times in " + std::to_string(Frames) + " frames");
		Require(!IsColorGradingBakeNeeded(baked, MakeColorGradingConstants(ColorGrade{})), "The last grade baked is further than the tolerance from the one wanted");
	}

	void TestSavedWithTheScene()
	{
		Registry world;
		const Entity entity = world.CreateEntity();
		ColorGradingComponent grading;
		grading.bUseKeys = true;
		grading.Grade.Saturation = 1.25f;
		grading.Keys = { Key(-6.0f, Warm(-0.4f)), Key(12.0f, Warm(0.7f)) };
		world.Add<ColorGradingComponent>(entity, grading);
		world.Add<GuidComponent>(entity, GuidComponent{ EntityGuid{ 0xC01 } });

		Nyx::Tests::TestDirectory directory("NyxColorGradingTests");
		const std::filesystem::path path = directory.Path / "ColorGrading.nyxscene";
		Require(SceneSerializer::SaveToFile(world, path), "The scene can't be saved");
		Registry loaded;
		ScenePostLoadContext context{};
		Require(SceneSerializer::LoadFromFile(path, loaded, context), "The scene can't be loaded");
		bool bFound = false;
		loaded.Each<ColorGradingComponent>([&](Entity, const ColorGradingComponent& read)
			{
				bFound = true;
				Require(read.bGrading && read.bUseKeys && read.Grade.Saturation == 1.25f && read.Keys.size() == 2, "The grading isn't read back as saved");
				Require(read.Keys[1].SunElevation == 12.0f && read.Keys[1].Grade.Temperature == 0.7f && read.Keys[1].Grade.HighlightTint == glm::vec3(1.0f, 0.7f, 0.0f) &&
						read.Keys[0].Grade.Lift == glm::vec3(-0.4f * 0.1f),
					"The keys aren't read back as saved");
			});
		Require(bFound, "The grading isn't saved");
	}
}

int main()
{
	try
	{
		Nyx::Logger::Get().Init();
		Nyx::RegisterComponentTypes();
		TestIdentity();
		TestWhiteBalance();
		TestSaturation();
		TestContrast();
		TestLiftGammaGain();
		TestSplitToning();
		TestOutOfRange();
		TestKeys();
		TestSunElevation();
		TestBakedWhenChanged();
		TestSavedWithTheScene();
	}
	catch (const std::exception& error)
	{
		std::cerr << "FAILED: " << error.what() << "\n";
		return 1;
	}

	std::cout << "All colour grading tests passed\n";
	return 0;
}
