// The high cloud layer without a GPU (CloudLayer.h): where rays from the camera meet its shell, from
// the ground, from below and above it, close to it on either side, against a ray marched in small
// steps; none behind the ground; the clouds near the camera faded; the settings in range, off with
// the component; the pattern anchored in the world, carried by the wind with the world's time, the
// same at the same time, its origin kept small; when the sky's cube, which has the clouds in it, is
// made again; and the component saved in a scene and read back.
#include "CloudLayer.h"
#include "CloudLayerComponent.h"
#include "ComponentRegistration.h"
#include "Entity.h"
#include "GuidComponent.h"
#include "Log.h"
#include "SceneSerializer.h"
#include "TestSupport.h"

#include <glm/glm.hpp>

#include <cmath>
#include <iostream>
#include <string>

namespace
{
	using namespace Nyx;
	using Nyx::Tests::Require;

	constexpr float PlanetRadius = 6360.0f;

	bool Near(double a, double b, double tolerance)
	{
		return std::abs(a - b) <= tolerance;
	}

	// The shell's crossings along a ray, by marching it in steps of `step` kilometres and finding
	// where its height above the ground passes the altitude, in doubles; none past the ground
	std::vector<double> MarchedHits(double height, double cosZenith, double altitude, double step, double reach)
	{
		std::vector<double> hits;
		const double sine = std::sqrt(std::max(1.0 - cosZenith * cosZenith, 0.0));
		const auto heightAt = [&](double t)
		{
			const double x = t * sine;
			const double y = PlanetRadius + height + t * cosZenith;
			return std::sqrt(x * x + y * y) - PlanetRadius;
		};
		double before = heightAt(0.0) - altitude;
		for (double t = step; t < reach; t += step)
		{
			const double at = heightAt(t);
			if (at < 0.0)
			{
				break;
			}
			const double now = at - altitude;
			if ((before < 0.0) != (now < 0.0))
			{
				// Where between the two steps, by the line through them
				hits.push_back(t - step * now / (now - before));
			}
			before = now;
		}
		return hits;
	}

	void RequireHitsLikeMarched(float height, float cosZenith, float altitude, const std::string& what)
	{
		const CloudShellHits hits = GetCloudShellHits(PlanetRadius, height, cosZenith, altitude);
		const std::vector<double> marched = MarchedHits(height, cosZenith, altitude, 0.002, 2500.0);
		Require(hits.Count == static_cast<int>(marched.size()),
			what + ": " + std::to_string(hits.Count) + " crossings, marched " + std::to_string(marched.size()));
		for (int hit = 0; hit < hits.Count; ++hit)
		{
			// Within a metre, or a part in 10⁵ far off (the floats' precision at 6360 km)
			const double tolerance = std::max(0.001, marched[hit] * 1e-5);
			Require(Near(hits.Distances[hit], marched[hit], tolerance),
				what + ": crossing " + std::to_string(hit) + " at " + std::to_string(hits.Distances[hit]) + " km, marched " + std::to_string(marched[hit]) + " km");
		}
	}

	void TestShellHits()
	{
		// From the ground: straight up the altitude; level, out to where the shell is that high
		const CloudShellHits up = GetCloudShellHits(PlanetRadius, 0.01f, 1.0f, 4.0f);
		Require(up.Count == 1 && Near(up.Distances[0], 3.99, 1e-4), "Straight up from 10 m, the shell 4 km up isn't 3.99 km away: " + std::to_string(up.Distances[0]));
		const CloudShellHits level = GetCloudShellHits(PlanetRadius, 0.0f, 0.0f, 4.0f);
		Require(level.Count == 1 && Near(level.Distances[0], std::sqrt(4.0 * (2.0 * PlanetRadius + 4.0)), 0.01), "Level from the ground, the shell isn't where it is 4 km up");

		// From the ground, below the shell, at it, above it, every way: as marched
		for (const float height : { 0.01f, 3.0f, 3.99f, 4.01f, 6.0f })
		{
			for (const float cosZenith : { 1.0f, 0.5f, 0.1f, 0.02f, 0.0f, -0.01f, -0.03f, -0.05f, -0.2f, -1.0f })
			{
				RequireHitsLikeMarched(height, cosZenith, 4.0f, "From " + std::to_string(height) + " km, " + std::to_string(cosZenith) + " from the up axis");
			}
		}

		// From above: none looking up, one looking down into the ground, two grazing past it
		Require(GetCloudShellHits(PlanetRadius, 6.0f, 0.2f, 4.0f).Count == 0, "Looking up from above the shell meets it");
		Require(GetCloudShellHits(PlanetRadius, 6.0f, -0.5f, 4.0f).Count == 1, "Looking down from above the shell, into the ground, doesn't meet it once");
		const float horizon = -std::sqrt(6.0f * (2.0f * PlanetRadius + 6.0f)) / (PlanetRadius + 6.0f);
		Require(GetCloudShellHits(PlanetRadius, 6.0f, horizon * 0.95f, 4.0f).Count == 2, "Grazing past the ground from above the shell doesn't meet it twice");

		// Crossing it: the nearest crossing straight up or down shrinks to nothing on either side
		const CloudShellHits justBelow = GetCloudShellHits(PlanetRadius, 3.999f, 0.8f, 4.0f);
		const CloudShellHits justAbove = GetCloudShellHits(PlanetRadius, 4.001f, -0.8f, 4.0f);
		Require(justBelow.Count == 1 && Near(justBelow.Distances[0], 0.00125, 1e-5), "A metre below the shell, it isn't 1.25 m away up the ray");
		Require(justAbove.Count >= 1 && Near(justAbove.Distances[0], 0.00125, 1e-5), "A metre above the shell, it isn't 1.25 m away down the ray");
	}

	void TestNearFade()
	{
		Require(GetCloudNearFade(0.0f, 0.3f) == 0.0f && GetCloudNearFade(0.07f, 0.3f) == 0.0f, "The clouds right by the camera aren't gone");
		Require(GetCloudNearFade(0.6f, 0.3f) == 1.0f && GetCloudNearFade(50.0f, 0.3f) == 1.0f, "The clouds twice their thickness away aren't all there");
		float before = 0.0f;
		for (float distance = 0.0f; distance <= 1.0f; distance += 0.01f)
		{
			const float fade = GetCloudNearFade(distance, 0.3f);
			Require(fade >= before, "The near fade doesn't grow with the distance");
			before = fade;
		}
	}

	void TestSettings()
	{
		CloudLayerComponent clouds;
		const SceneViewCloudLayerSettings layer = MakeSceneViewCloudLayerSettings(clouds, glm::vec3(0.0f), 0.0);
		Require(layer.bClouds && Near(layer.Altitude, 4.0, 1e-6) && Near(layer.Thickness, 0.3, 1e-6) && Near(layer.TileSize, 6.4, 1e-5),
			"The defaults aren't a layer 4 km up, 300 m thick, its pattern 6.4 km across");
		Require(layer.bLightsWorld && layer.LightStrength == 1.0f, "The clouds don't light the world by default, as they look");
		clouds.LightStrength = 0.0f;
		Require(!MakeSceneViewCloudLayerSettings(clouds, glm::vec3(0.0f), 0.0).bLightsWorld, "Clouds of no light strength light the world");
		clouds.LightStrength = 1.0f;
		clouds.bLightsWorld = false;
		Require(!MakeSceneViewCloudLayerSettings(clouds, glm::vec3(0.0f), 0.0).bLightsWorld, "Clouds that don't light the world do");

		clouds.bClouds = false;
		Require(!MakeSceneViewCloudLayerSettings(clouds, glm::vec3(0.0f), 0.0).bClouds, "A layer that is off is drawn");

		CloudLayerComponent wild;
		wild.Altitude = -5.0f;
		wild.Thickness = 1e9f;
		wild.Coverage = 3.0f;
		wild.Type = -1.0f;
		wild.Softness = 0.0f;
		wild.CloudletSize = 0.0f;
		wild.WindSpeed = 1e6f;
		wild.Brightness = -1.0f;
		wild.LightStrength = 9.0f;
		const SceneViewCloudLayerSettings held = MakeSceneViewCloudLayerSettings(wild, glm::vec3(0.0f), 1e7);
		Require(Near(held.Altitude, 0.01, 1e-7) && Near(held.Thickness, 5.0, 1e-6) && held.Coverage == 1.0f && held.Type == 0.0f && held.Softness == 0.01f && Near(held.TileSize, 0.16, 1e-6) &&
				held.Brightness == 0.0f && held.LightStrength == 4.0f,
			"Values out of range aren't held in it");
		Require(held.Origin.x >= 0.0f && held.Origin.x < CloudNoisePeriod && held.Origin.y >= 0.0f && held.Origin.y < CloudNoisePeriod, "The pattern's origin isn't kept small");
	}

	// The sky's cube made again for its clouds: when they come or go, a setting or the noise changes,
	// or the pattern moves under the camera by more than the tolerance, either way round the period;
	// never without clouds
	void TestCapture()
	{
		CloudLayerComponent clouds;
		clouds.WindDirectionDegrees = 0.0f;
		SceneViewCloudCapture built;
		built.bClouds = true;
		built.Layer = MakeSceneViewCloudLayerSettings(clouds, glm::vec3(0.0f), 0.0);
		built.NoiseSlot = 7;
		Require(!IsCloudCaptureChanged(built, built), "The same clouds make the cube again");
		Require(IsCloudCaptureChanged(built, SceneViewCloudCapture{}) && IsCloudCaptureChanged(SceneViewCloudCapture{}, built), "Clouds coming or going don't make the cube again");
		Require(!IsCloudCaptureChanged(SceneViewCloudCapture{}, SceneViewCloudCapture{}), "No clouds make the cube again");

		SceneViewCloudCapture wanted = built;
		wanted.Layer.Coverage = 0.6f;
		Require(IsCloudCaptureChanged(built, wanted), "A new coverage doesn't make the cube again");
		wanted = built;
		wanted.NoiseSlot = 8;
		Require(IsCloudCaptureChanged(built, wanted), "New noise doesn't make the cube again");
		wanted = built;
		wanted.Layer.LightStrength = 0.5f;
		Require(IsCloudCaptureChanged(built, wanted), "A new light strength doesn't make the cube again");

		// The wind, 10 m/s: within a 32nd of a cell (12.5 m, 1.25 s) the same cube, past it again
		wanted.Layer = MakeSceneViewCloudLayerSettings(clouds, glm::vec3(0.0f), 1.2);
		Require(!IsCloudCaptureChanged(built, wanted), "The clouds moved 12 m make the cube again");
		wanted.Layer = MakeSceneViewCloudLayerSettings(clouds, glm::vec3(0.0f), 1.3);
		Require(IsCloudCaptureChanged(built, wanted), "The clouds moved 13 m don't make the cube again");
		// The camera going sideways the same; across the period's end, the short way round
		wanted.Layer = MakeSceneViewCloudLayerSettings(clouds, glm::vec3(0.0f, 0.0f, 13.0f), 0.0);
		Require(IsCloudCaptureChanged(built, wanted), "The camera 13 m across the wind doesn't make the cube again");
		SceneViewCloudCapture atTheEnd = built;
		atTheEnd.Layer.Origin = glm::vec2(15.9999f, 3.0f);
		SceneViewCloudCapture wrapped = built;
		wrapped.Layer.Origin = glm::vec2(0.0001f, 3.0f);
		Require(!IsCloudCaptureChanged(atTheEnd, wrapped), "Across the end of the period the pattern seems to jump");
	}

	void TestWindAndPattern()
	{
		CloudLayerComponent clouds;
		clouds.WindDirectionDegrees = 0.0f;
		clouds.WindSpeed = 10.0f;
		const SceneViewCloudLayerSettings layer = MakeSceneViewCloudLayerSettings(clouds, glm::vec3(0.0f), 0.0);
		Require(Near(layer.WindAxis.x, 1.0, 1e-6) && Near(layer.WindAxis.y, 0.0, 1e-6), "A wind at 0 degrees doesn't blow along +X");

		// The same time, the same pattern; 64 s later the wind has carried it 640 m, a tenth of a
		// square, along the wind
		const glm::dvec2 point(1234.0, -567.0);
		const glm::dvec2 now = GetCloudPatternPoint(layer, point, 100.0, clouds.WindSpeed);
		Require(GetCloudPatternPoint(layer, point, 100.0, clouds.WindSpeed) == now, "The same point at the same time isn't over the same pattern");
		const glm::dvec2 later = GetCloudPatternPoint(layer, point, 164.0, clouds.WindSpeed);
		Require(Near(now.x - later.x, 0.1, 1e-7) && Near(now.y, later.y, 1e-12), "The wind doesn't carry the pattern its speed along its way");
		// A point 640 m downwind, 64 s later, is over the pattern that was over the first
		const glm::dvec2 downwind = GetCloudPatternPoint(layer, point + glm::dvec2(640.0, 0.0), 164.0, clouds.WindSpeed);
		Require(Near(downwind.x, now.x, 1e-7) && Near(downwind.y, now.y, 1e-9), "The pattern doesn't move with the wind");

		// Blowing towards -Z (90 degrees): across it is +X
		clouds.WindDirectionDegrees = 90.0f;
		const SceneViewCloudLayerSettings north = MakeSceneViewCloudLayerSettings(clouds, glm::vec3(0.0f), 0.0);
		Require(Near(north.WindAxis.x, 0.0, 1e-6) && Near(north.WindAxis.y, -1.0, 1e-6), "A wind at 90 degrees doesn't blow towards -Z");

		// The camera's origin is its own point over the pattern, wrapped: moving the camera a whole
		// period of squares changes nothing; half a square, half a square
		clouds.WindDirectionDegrees = 0.0f;
		const float period = static_cast<float>(CloudNoisePeriod) * 6400.0f;
		const SceneViewCloudLayerSettings here = MakeSceneViewCloudLayerSettings(clouds, glm::vec3(300.0f, 10.0f, 200.0f), 5000.0);
		const SceneViewCloudLayerSettings far = MakeSceneViewCloudLayerSettings(clouds, glm::vec3(300.0f + period, 10.0f, 200.0f - period), 5000.0);
		Require(Near(here.Origin.x, far.Origin.x, 1e-4) && Near(here.Origin.y, far.Origin.y, 1e-4), "A camera a whole period away isn't over the same pattern");
		const SceneViewCloudLayerSettings beside = MakeSceneViewCloudLayerSettings(clouds, glm::vec3(300.0f, 10.0f, 200.0f + 3200.0f), 5000.0);
		Require(Near(std::fmod(beside.Origin.y - here.Origin.y + 16.0f, 16.0f), 0.5, 1e-4), "A camera half a square across the wind isn't half a square over");
		// After hours of the world's time, still exact enough
		const SceneViewCloudLayerSettings hours = MakeSceneViewCloudLayerSettings(clouds, glm::vec3(0.0f), 360000.0);
		const SceneViewCloudLayerSettings hoursLater = MakeSceneViewCloudLayerSettings(clouds, glm::vec3(0.0f), 360000.0 + 1.0 / 60.0);
		const double step = std::fmod(static_cast<double>(hours.Origin.x) - hoursLater.Origin.x + 16.0, 16.0);
		Require(Near(step, 10.0 / 60.0 / 6400.0, 1e-6), "After 100 hours a step of the wind isn't its speed's: " + std::to_string(step));
	}

	void TestSavedWithTheScene()
	{
		Registry world;
		const Entity entity = world.CreateEntity();
		CloudLayerComponent clouds;
		clouds.Altitude = 2500.0f;
		clouds.Thickness = 120.0f;
		clouds.Coverage = 0.7f;
		clouds.Type = 0.25f;
		clouds.CloudletSize = 250.0f;
		clouds.Softness = 0.15f;
		clouds.Detail = 0.5f;
		clouds.Warp = 0.6f;
		clouds.Seed = 42;
		clouds.WindDirectionDegrees = 135.0f;
		clouds.WindSpeed = 25.0f;
		clouds.Brightness = 1.2f;
		clouds.Rim = 2.0f;
		clouds.Ambient = 0.8f;
		clouds.bLightsWorld = false;
		clouds.LightStrength = 0.4f;
		world.Add<CloudLayerComponent>(entity, clouds);
		world.Add<GuidComponent>(entity, GuidComponent{ EntityGuid{ 0xC10D } });

		Nyx::Tests::TestDirectory directory("NyxCloudLayerTests");
		const std::filesystem::path path = directory.Path / "CloudLayer.nyxscene";
		Require(SceneSerializer::SaveToFile(world, path), "The scene can't be saved");
		Registry loaded;
		ScenePostLoadContext context{};
		Require(SceneSerializer::LoadFromFile(path, loaded, context), "The scene can't be loaded");
		bool bFound = false;
		loaded.Each<CloudLayerComponent>([&](Entity, const CloudLayerComponent& read)
			{
				bFound = true;
				Require(read.bClouds && read.Altitude == 2500.0f && read.Thickness == 120.0f && read.Coverage == 0.7f && read.Type == 0.25f && read.CloudletSize == 250.0f &&
						read.Softness == 0.15f && read.Detail == 0.5f && read.Warp == 0.6f && read.Seed == 42 && read.WindDirectionDegrees == 135.0f && read.WindSpeed == 25.0f &&
						read.Brightness == 1.2f && read.Rim == 2.0f && read.Ambient == 0.8f && !read.bLightsWorld && read.LightStrength == 0.4f,
					"The cloud layer isn't read back as saved");
			});
		Require(bFound, "The cloud layer isn't saved");
	}
}

int main()
{
	try
	{
		Nyx::Logger::Get().Init();
		Nyx::RegisterComponentTypes();
		TestShellHits();
		TestNearFade();
		TestSettings();
		TestCapture();
		TestWindAndPattern();
		TestSavedWithTheScene();
	}
	catch (const std::exception& error)
	{
		std::cerr << "FAILED: " << error.what() << "\n";
		return 1;
	}

	std::cout << "All cloud layer tests passed\n";
	return 0;
}
