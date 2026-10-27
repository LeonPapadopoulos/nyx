// The volumetric clouds without a GPU (VolumetricClouds.h): where rays from the camera are inside
// their band, from the ground, below, inside and above it, close to its shells, each end on a shell
// and the middle inside, in doubles; none past the ground; the pixel of each 2 x 2 or 4 x 4 a frame
// marches, every one in 4 or 16 frames, smaller squares while the picture moves fast, and how far a
// turn and a move move it; the settings in range, off with the component; the noises anchored in the
// world, carried by the wind with the world's time, their origins kept small; when everything must be
// marched again; the presets setting the values once, at a game's scale; older scenes' Temporal; and
// the component saved in a scene and read back.
#include "ComponentRegistration.h"
#include "Entity.h"
#include "GuidComponent.h"
#include "Log.h"
#include "SceneSerializer.h"
#include "TestSupport.h"
#include "VolumetricClouds.h"
#include "VolumetricCloudsComponent.h"

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <array>
#include <cmath>
#include <iostream>
#include <set>
#include <string>
#include <utility>

namespace
{
	using namespace Nyx;
	using Nyx::Tests::Require;

	constexpr float PlanetRadius = 6360.0f;
	constexpr float Bottom = 1.5f;
	constexpr float Top = 5.0f;

	bool Near(double a, double b, double tolerance)
	{
		return std::abs(a - b) <= tolerance;
	}

	// The height above the ground `t` kilometres along a ray, in doubles
	double HeightAt(double height, double cosZenith, double t)
	{
		const double sine = std::sqrt(std::max(1.0 - cosZenith * cosZenith, 0.0));
		const double x = t * sine;
		const double y = PlanetRadius + height + t * cosZenith;
		return std::sqrt(x * x + y * y) - PlanetRadius;
	}

	bool IsInside(double height)
	{
		return height > Bottom && height < Top;
	}

	// The lowest height along a ray between two distances, in steps of 100 m, and the end
	double LowestBetween(double height, double cosZenith, double from, double to)
	{
		double lowest = HeightAt(height, cosZenith, to);
		for (double t = from; t < to; t += 0.1)
		{
			lowest = std::min(lowest, HeightAt(height, cosZenith, t));
		}
		return lowest;
	}

	// From the ground, below the band, inside it, close to its shells and above it, every way from
	// straight up to straight down: where the band is entered (or at the camera inside it) and left,
	// the ray is on a shell, between them inside; the whole span above the ground; and a ray that
	// misses has nothing of the band before the ground
	void TestBandSpans()
	{
		const std::array<float, 8> heights = { 0.002f, 0.8f, Bottom - 0.01f, 2.5f, Top - 0.01f, Top + 0.01f, 7.0f, 20.0f };
		const double margin = 0.001;
		int spans = 0;
		for (const float height : heights)
		{
			for (int index = 0; index <= 400; ++index)
			{
				// Denser round the horizon, where the spans are longest
				const double u = index / 200.0 - 1.0;
				const double cosZenith = u * u * u;
				const CloudBandSpan span = GetCloudBandSpan(PlanetRadius, height, static_cast<float>(cosZenith), Bottom, Top);
				const std::string where = "From " + std::to_string(height) + " km, cos " + std::to_string(cosZenith);
				if (!span.bHit)
				{
					// Up to the ground or 600 km, nothing of the band
					for (double t = 0.0; t < 600.0; t += 0.25)
					{
						const double at = HeightAt(height, cosZenith, t);
						if (at < 0.0)
						{
							break;
						}
						Require(!IsInside(at), where + ": a ray found to miss the band goes through it at " + std::to_string(t) + " km");
					}
					continue;
				}
				++spans;
				Require(span.Enter >= 0.0f && span.Exit > span.Enter, where + ": the span is backwards");
				// Each end on a shell, within a metre (a part in 10^5 of a few hundred kilometres)
				const auto onShell = [&](double t)
				{
					const double at = HeightAt(height, cosZenith, t);
					return Near(at, Bottom, margin) || Near(at, Top, margin);
				};
				if (height > Bottom && height < Top)
				{
					Require(span.Enter == 0.0f, where + ": inside the band, the span doesn't start at the camera");
				}
				else
				{
					Require(onShell(span.Enter), where + ": the band isn't entered at " + std::to_string(span.Enter) + " km");
				}
				Require(onShell(span.Exit), where + ": the band isn't left at " + std::to_string(span.Exit) + " km");
				Require(IsInside(HeightAt(height, cosZenith, 0.5 * (span.Enter + span.Exit))), where + ": the span's middle isn't inside the band");
				Require(LowestBetween(height, cosZenith, span.Enter, span.Exit) > 0.0, where + ": the span goes below the ground");
			}
		}
		Require(spans > 1000, "Too few rays meet the band: " + std::to_string(spans));

		// From the ground: straight up through the whole band, the horizon's span hundreds of
		// kilometres long; straight down from above, the band's thickness
		const CloudBandSpan up = GetCloudBandSpan(PlanetRadius, 0.002f, 1.0f, Bottom, Top);
		Require(up.bHit && Near(up.Enter, Bottom - 0.002, 1e-4) && Near(up.Exit, Top - 0.002, 1e-4), "From the ground, straight up isn't from the bottom to the top");
		const CloudBandSpan level = GetCloudBandSpan(PlanetRadius, 0.002f, 0.0f, Bottom, Top);
		Require(level.bHit && level.Enter > 130.0f && level.Exit > 250.0f, "From the ground, the level ray's span isn't far off and long");
		const CloudBandSpan down = GetCloudBandSpan(PlanetRadius, 7.0f, -1.0f, Bottom, Top);
		Require(down.bHit && Near(down.Enter, 7.0 - Top, 1e-4) && Near(down.Exit, 7.0 - Bottom, 1e-4), "From above, straight down isn't from the top to the bottom");
		const CloudBandSpan upFromAbove = GetCloudBandSpan(PlanetRadius, 7.0f, 0.5f, Bottom, Top);
		Require(!upFromAbove.bHit, "From above, a ray up meets the band");
	}

	// Every pixel of a 4 x 4 square once in 16 frames, of a 2 x 2 in 4, then again in the same order;
	// neighbouring frames far apart; every pixel every frame for squares of 1. While the picture moves
	// fast, smaller squares; how far it moves under a turn and a move.
	void TestUpdateOrder()
	{
		for (const uint32_t size : { 2u, 4u })
		{
			std::set<std::pair<uint32_t, uint32_t>> seen;
			for (uint32_t frame = 0; frame < size * size; ++frame)
			{
				const glm::uvec2 offset = GetVolumetricCloudUpdateOffset(frame, size);
				Require(offset.x < size && offset.y < size, "An update offset is outside the square");
				seen.insert({ offset.x, offset.y });
				Require(GetVolumetricCloudUpdateOffset(frame + size * size, size) == offset, "The update order doesn't repeat");
				const glm::ivec2 next = glm::ivec2(GetVolumetricCloudUpdateOffset(frame + 1, size));
				const glm::ivec2 step = glm::abs(next - glm::ivec2(offset));
				Require(step.x + step.y >= (size == 4 ? 2 : 1), "Neighbouring frames march neighbouring pixels");
			}
			Require(seen.size() == size * size, "A square's frames don't march every pixel of it");
		}
		Require(GetVolumetricCloudUpdateOffset(5, 1) == glm::uvec2(0), "Every pixel marched has an offset");

		Require(GetVolumetricCloudBlockSize(4, 0.0f) == 4 && GetVolumetricCloudBlockSize(2, 0.0f) == 2 && GetVolumetricCloudBlockSize(1, 0.0f) == 1, "A still picture doesn't march as asked");
		Require(GetVolumetricCloudBlockSize(4, VolumetricCloudFastShift * 1.5f) == 2 && GetVolumetricCloudBlockSize(2, VolumetricCloudFastShift * 1.5f) == 2,
			"A fast picture doesn't march squares of 2");
		Require(GetVolumetricCloudBlockSize(4, VolumetricCloudFasterShift * 1.5f) == 1, "A faster picture doesn't march every pixel");

		// A 60-degree-high view, 540 pixels: still, nothing; turned 1 degree, 9 pixels in its middle
		const glm::mat4 projection = glm::perspective(glm::radians(60.0f), 16.0f / 9.0f, 0.1f, 1000.0f);
		const glm::mat4 still = projection;
		Require(GetVolumetricCloudScreenShift(still, still, 540.0f, glm::vec3(0.0f), 1.0f) < 1e-3f, "A still camera moves the picture");
		const glm::mat4 turned = projection * glm::rotate(glm::mat4(1.0f), glm::radians(1.0f), glm::vec3(0.0f, 1.0f, 0.0f));
		const float shift = GetVolumetricCloudScreenShift(still, turned, 540.0f, glm::vec3(0.0f), 1.0f);
		Require(shift > 8.0f && shift < 14.0f, "A degree's turn doesn't move the picture about 9 pixels: " + std::to_string(shift));
		// 100 m across at 1 km: 0.1 rad, at 270 pixels to 30 degrees about 52 pixels
		const float moved = GetVolumetricCloudScreenShift(still, still, 540.0f, glm::vec3(0.1f, 0.0f, 0.0f), 1.0f);
		Require(moved > 47.0f && moved < 57.0f, "A move doesn't move the picture as seen at the clouds' distance: " + std::to_string(moved));
	}

	// Values brought into range, in kilometres; off with the component
	void TestSettings()
	{
		VolumetricCloudsComponent clouds;
		clouds.BottomAltitude = -50.0f;
		clouds.TopAltitude = 20.0f;
		clouds.Coverage = 2.0f;
		clouds.Density = -1.0f;
		clouds.EdgeSoftness = 0.0f;
		clouds.CloudSize = 1.0f;
		clouds.Steps = 2;
		clouds.LightSteps = 100;
		clouds.ShadowTint = glm::vec3(5.0f, -1.0f, 0.5f);
		const SceneViewVolumetricCloudsSettings band = MakeSceneViewVolumetricCloudsSettings(clouds, glm::vec3(0.0f), 0.0);
		Require(band.bClouds && band.Bottom == 0.0f && Near(band.Top, 0.1, 1e-6), "The band isn't held in range, its top above its bottom");
		Require(band.Coverage == 1.0f && band.Density == 0.0f && band.EdgeSoftness == 0.01f, "The shape's values aren't held in range");
		Require(Near(band.ShapeRepeat, 0.1 * VolumetricCloudNoiseCells, 1e-6), "The cloud size isn't held in range");
		Require(band.Steps == 16 && band.LightSteps == 16, "The steps aren't held in range");
		Require(band.ShadowTint == glm::vec3(4.0f, 0.0f, 0.5f), "The shadow tint isn't held in range (0 to 4: it may brighten too)");

		const SceneViewVolumetricCloudsSettings defaults = MakeSceneViewVolumetricCloudsSettings(VolumetricCloudsComponent{}, glm::vec3(0.0f), 0.0);
		Require(Near(defaults.Bottom, 0.4, 1e-6) && Near(defaults.Top, 1.8, 1e-6) && Near(defaults.MaxDistance, 80.0, 1e-4), "The defaults aren't in kilometres");

		clouds.bClouds = false;
		Require(!MakeSceneViewVolumetricCloudsSettings(clouds, glm::vec3(0.0f), 0.0).bClouds, "Clouds that are off are drawn");
	}

	// Anchored in the world, carried by the wind: a camera moved downwind by as far as the wind blew
	// sees the same; the origins between 0 and 1, as exact after 100 hours as at the start
	void TestWind()
	{
		VolumetricCloudsComponent clouds;
		clouds.WindDirectionDegrees = 0.0f;
		clouds.WindSpeed = 20.0f;
		const SceneViewVolumetricCloudsSettings start = MakeSceneViewVolumetricCloudsSettings(clouds, glm::vec3(0.0f, 10.0f, 0.0f), 1000.0);
		const SceneViewVolumetricCloudsSettings later = MakeSceneViewVolumetricCloudsSettings(clouds, glm::vec3(600.0f, 10.0f, 0.0f), 1030.0);
		Require(Near(start.WeatherOrigin.x, later.WeatherOrigin.x, 1e-6) && Near(start.ShapeOrigin.x, later.ShapeOrigin.x, 1e-6) && Near(start.DetailOrigin.x, later.DetailOrigin.x, 1e-5),
			"A camera carried with the wind doesn't see the same clouds");
		Require(Near(start.WindVelocity.x, 0.02, 1e-7) && Near(start.WindVelocity.y, 0.0, 1e-7), "The wind isn't along +X in kilometres a second");

		for (const double time : { 0.0, 3600.0, 360000.0 })
		{
			const SceneViewVolumetricCloudsSettings a = MakeSceneViewVolumetricCloudsSettings(clouds, glm::vec3(12345.0f, 0.0f, -6789.0f), time);
			const SceneViewVolumetricCloudsSettings b = MakeSceneViewVolumetricCloudsSettings(clouds, glm::vec3(12345.0f, 0.0f, -6789.0f), time + 1.0);
			for (const glm::vec2& origin : { a.WeatherOrigin, a.ShapeOrigin, a.DetailOrigin })
			{
				Require(origin.x >= 0.0f && origin.x < 1.0f && origin.y >= 0.0f && origin.y < 1.0f, "An origin isn't kept between 0 and 1");
			}
			// A second of 20 m/s: 0.02 km over the shapes' repeat, the same whatever the time
			double step = static_cast<double>(a.ShapeOrigin.x) - static_cast<double>(b.ShapeOrigin.x);
			step -= std::round(step);
			Require(Near(std::abs(step), 0.02 / a.ShapeRepeat, 1e-6), "After " + std::to_string(time) + " s, a second of wind isn't as far as at the start");
		}
	}

	// Everything marched again only when the look changes, not as the wind moves the clouds; the
	// sun turning more than VolumetricCloudSunTolerance in a frame
	void TestLookChanges()
	{
		VolumetricCloudsComponent clouds;
		const SceneViewVolumetricCloudsSettings settings = MakeSceneViewVolumetricCloudsSettings(clouds, glm::vec3(0.0f), 0.0);
		const glm::vec3 sun = glm::normalize(glm::vec3(0.0f, 0.5f, -1.0f));
		const VolumetricCloudsLook look{ settings, sun };
		Require(!IsVolumetricCloudsLookChanged(look, look), "The same look is taken as changed");
		VolumetricCloudsLook moved = look;
		moved.Settings = MakeSceneViewVolumetricCloudsSettings(clouds, glm::vec3(500.0f, 0.0f, 0.0f), 20.0);
		Require(!IsVolumetricCloudsLookChanged(look, moved), "The wind carrying the clouds is taken as a new look");
		VolumetricCloudsLook covered = look;
		covered.Settings.Coverage += 0.01f;
		Require(IsVolumetricCloudsLookChanged(look, covered), "A new coverage isn't a new look");
		VolumetricCloudsLook seeded = look;
		seeded.Settings.Seed = 7;
		Require(IsVolumetricCloudsLookChanged(look, seeded), "A new seed isn't a new look");
		const auto turned = [&](float degrees)
		{
			VolumetricCloudsLook result = look;
			const float angle = glm::radians(degrees);
			result.TowardsSun = glm::vec3(sun.x, sun.y * std::cos(angle) - sun.z * std::sin(angle), sun.y * std::sin(angle) + sun.z * std::cos(angle));
			return result;
		};
		Require(!IsVolumetricCloudsLookChanged(look, turned(0.01f)), "A sun turned a hundredth of a degree is a new look");
		Require(IsVolumetricCloudsLookChanged(look, turned(0.1f)), "A sun turned a tenth of a degree isn't a new look");
		Require(IsVolumetricCloudsLookChanged(look, turned(180.0f)), "A sun turned round isn't a new look");
	}

	// A preset chosen sets the values once; changed after, they stay; Custom changes none
	void TestPresets()
	{
		const VolumetricCloudsComponent puffs = MakeVolumetricCloudsPreset(EVolumetricCloudsPreset::SmallPuffs);
		const VolumetricCloudsComponent cumulus = MakeVolumetricCloudsPreset(EVolumetricCloudsPreset::Cumulus);
		const VolumetricCloudsComponent broken = MakeVolumetricCloudsPreset(EVolumetricCloudsPreset::Broken);
		Require(puffs.AppliedPreset == EVolumetricCloudsPreset::SmallPuffs && cumulus.AppliedPreset == EVolumetricCloudsPreset::Cumulus, "A preset isn't marked as applied");
		Require(puffs.CloudSize < cumulus.CloudSize && puffs.Type < cumulus.Type && puffs.TopAltitude < cumulus.TopAltitude, "The small puffs aren't smaller and lower than the cumulus");
		for (const VolumetricCloudsComponent* preset : { &puffs, &cumulus, &broken })
		{
			Require(preset->BottomAltitude >= 300.0f && preset->BottomAltitude <= 600.0f && preset->TopAltitude <= 2000.0f && preset->CloudSize <= 1000.0f,
				"A preset isn't at a game's scale (a base a few hundred metres up, tops under 2 km, clouds under a kilometre)");
		}
		Require(puffs.Cellularity > cumulus.Cellularity && broken.Wispiness > cumulus.Wispiness, "The small puffs aren't parted into cells, or the broken cloud isn't wispier");
		Require(broken.SilverLining > cumulus.SilverLining, "The broken cloud's silver lining isn't stronger");

		VolumetricCloudsComponent clouds = cumulus;
		clouds.Coverage = 0.9f;
		ApplyVolumetricCloudsPreset(clouds);
		Require(clouds.Coverage == 0.9f, "A value changed after its preset was set back");
		clouds.Preset = EVolumetricCloudsPreset::Custom;
		ApplyVolumetricCloudsPreset(clouds);
		Require(clouds.Coverage == 0.9f && clouds.AppliedPreset == EVolumetricCloudsPreset::Custom, "Custom changed the values");
		clouds.Preset = EVolumetricCloudsPreset::Broken;
		clouds.Seed = 77;
		clouds.WindSpeed = 3.0f;
		ApplyVolumetricCloudsPreset(clouds);
		Require(clouds.Coverage == broken.Coverage && clouds.Density == broken.Density && clouds.ShadowTint == broken.ShadowTint, "Choosing a preset doesn't set its values");
		Require(clouds.Seed == 77 && clouds.WindSpeed == 3.0f, "A preset changed the seed or the wind");
	}

	// Saved in a scene and read back; its preset set on loading only if it wasn't already
	void TestSavedWithTheScene()
	{
		Registry world;
		const Entity entity = world.CreateEntity();
		VolumetricCloudsComponent clouds = MakeVolumetricCloudsPreset(EVolumetricCloudsPreset::Cumulus);
		clouds.Coverage = 0.33f;
		clouds.BottomAltitude = 1200.0f;
		clouds.TopAltitude = 7000.0f;
		clouds.ShadowTint = glm::vec3(0.7f, 0.6f, 0.9f);
		clouds.Seed = 42;
		clouds.WindDirectionDegrees = 135.0f;
		clouds.WindSpeed = 25.0f;
		clouds.Steps = 200;
		clouds.Update = EVolumetricCloudsUpdate::Blocks4x4;
		clouds.Resolution = EVolumetricCloudsResolution::Full;
		clouds.Wispiness = 0.7f;
		clouds.Cellularity = 0.6f;
		clouds.SunlitTint = glm::vec3(1.0f, 0.9f, 0.8f);
		clouds.AerialPerspective = 2.5f;
		clouds.DebugView = EVolumetricCloudsDebug::WeatherMap;
		world.Add<VolumetricCloudsComponent>(entity, clouds);
		world.Add<GuidComponent>(entity, GuidComponent{ EntityGuid{ 0xC10D5 } });

		Nyx::Tests::TestDirectory directory("NyxVolumetricCloudsTests");
		const std::filesystem::path path = directory.Path / "VolumetricClouds.nyxscene";
		Require(SceneSerializer::SaveToFile(world, path), "The scene can't be saved");
		Registry loaded;
		ScenePostLoadContext context{};
		Require(SceneSerializer::LoadFromFile(path, loaded, context), "The scene can't be loaded");
		bool bFound = false;
		loaded.Each<VolumetricCloudsComponent>([&](Entity, const VolumetricCloudsComponent& read)
			{
				bFound = true;
				Require(read.bClouds && read.Preset == EVolumetricCloudsPreset::Cumulus && read.AppliedPreset == EVolumetricCloudsPreset::Cumulus && read.Coverage == 0.33f &&
						read.BottomAltitude == 1200.0f && read.TopAltitude == 7000.0f && read.ShadowTint == glm::vec3(0.7f, 0.6f, 0.9f) && read.Seed == 42 &&
						read.WindDirectionDegrees == 135.0f && read.WindSpeed == 25.0f && read.Steps == 200 && read.Update == EVolumetricCloudsUpdate::Blocks4x4 &&
						read.Resolution == EVolumetricCloudsResolution::Full && read.Wispiness == 0.7f && read.Cellularity == 0.6f && read.SunlitTint == glm::vec3(1.0f, 0.9f, 0.8f) &&
						read.AerialPerspective == 2.5f && read.DebugView == EVolumetricCloudsDebug::WeatherMap,
					"The volumetric clouds aren't read back as saved");
			});
		Require(bFound, "The volumetric clouds aren't saved");

		// From before Update: Temporal off loads as every pixel, on as it was saved
		VolumetricCloudsComponent legacy;
		legacy.bTemporal = false;
		legacy.Update = EVolumetricCloudsUpdate::Blocks4x4;
		FoldLegacyVolumetricCloudsUpdate(legacy);
		Require(legacy.Update == EVolumetricCloudsUpdate::EveryPixel && legacy.bTemporal, "Temporal off from an older scene isn't every pixel");
		VolumetricCloudsComponent current;
		current.Update = EVolumetricCloudsUpdate::Blocks4x4;
		FoldLegacyVolumetricCloudsUpdate(current);
		Require(current.Update == EVolumetricCloudsUpdate::Blocks4x4, "A scene's Update is changed as it loads");

		// The update and the size in the view's settings
		Require(MakeSceneViewVolumetricCloudsSettings(current, glm::vec3(0.0f), 0.0).BlockSize == 4, "Blocks4x4 doesn't march squares of 4");
		current.Update = EVolumetricCloudsUpdate::EveryPixel;
		current.Resolution = EVolumetricCloudsResolution::Full;
		const SceneViewVolumetricCloudsSettings every = MakeSceneViewVolumetricCloudsSettings(current, glm::vec3(0.0f), 0.0);
		Require(every.BlockSize == 1 && every.bFullResolution, "Every Pixel at Full isn't every pixel at the view's size");
	}
}

int main()
{
	Nyx::Logger::Get().Init();
	try
	{
		RegisterComponentTypes();
		TestBandSpans();
		TestUpdateOrder();
		TestSettings();
		TestWind();
		TestLookChanges();
		TestPresets();
		TestSavedWithTheScene();
	}
	catch (const std::exception& error)
	{
		std::cerr << "NyxVolumetricCloudsTests: " << error.what() << "\n";
		return 1;
	}
	std::cout << "NyxVolumetricCloudsTests: all passed\n";
	return 0;
}
