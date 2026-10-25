// The time of day without a GPU (TimeOfDay.h, TimeOfDayClock): the sun's direction at sunrise,
// noon, sunset and midnight on known paths, the time wrapping past 24; the light's rotation along
// the path, turning smoothly all day without rolling; the light fading out below the horizon, and
// the air's sky and light from noon through sunset to night; the sun found by guid or as the
// primary light, and turned; a game world's steps moving the time on, the same with any frame
// times, through midnight, from a time set by a live edit and at a new day length without a jump;
// and the component saved in a scene and read back.
#include "Atmosphere.h"
#include "ComponentRegistration.h"
#include "ComponentTypeRegistry.h"
#include "DirectionalLightComponent.h"
#include "Entity.h"
#include "EnvironmentComponent.h"
#include "FixedStepClock.h"
#include "GameWorld.h"
#include "GuidComponent.h"
#include "LiveEdits.h"
#include "Log.h"
#include "ReflectionUtils.h"
#include "SceneSerializer.h"
#include "TimeOfDay.h"
#include "TimeOfDayComponent.h"
#include "TransformComponent.h"
#include "TestSupport.h"

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <cmath>
#include <iostream>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace
{
	using namespace Nyx;
	using Nyx::Tests::Require;

	std::string Text(const glm::vec3& v)
	{
		return std::to_string(v.x) + " " + std::to_string(v.y) + " " + std::to_string(v.z);
	}

	bool Near(const glm::vec3& a, const glm::vec3& b, float tolerance = 1e-5f)
	{
		return glm::length(a - b) <= tolerance;
	}

	bool Near(float a, float b, float tolerance = 1e-4f)
	{
		return std::abs(a - b) <= tolerance;
	}

	TimeOfDayComponent Path(float tiltDegrees, float sunriseDegrees)
	{
		TimeOfDayComponent timeOfDay;
		timeOfDay.PathTiltDegrees = tiltDegrees;
		timeOfDay.SunriseDirectionDegrees = sunriseDegrees;
		return timeOfDay;
	}

	glm::vec3 Forward(const glm::quat& rotation)
	{
		return rotation * glm::vec3(0.0f, 0.0f, -1.0f);
	}

	// The sun's direction where it is known: up the sunrise direction at 6, overhead less the tilt
	// at noon, opposite at 18, below at midnight
	void TestSunDirections()
	{
		const TimeOfDayComponent straight = Path(0.0f, 0.0f);
		Require(Near(GetSunDirection(straight, 6.0f), glm::vec3(1.0f, 0.0f, 0.0f)), "Sunrise isn't at +X: " + Text(GetSunDirection(straight, 6.0f)));
		Require(Near(GetSunDirection(straight, 12.0f), glm::vec3(0.0f, 1.0f, 0.0f)), "Noon isn't overhead: " + Text(GetSunDirection(straight, 12.0f)));
		Require(Near(GetSunDirection(straight, 18.0f), glm::vec3(-1.0f, 0.0f, 0.0f)), "Sunset isn't at -X: " + Text(GetSunDirection(straight, 18.0f)));
		Require(Near(GetSunDirection(straight, 0.0f), glm::vec3(0.0f, -1.0f, 0.0f)), "Midnight isn't below: " + Text(GetSunDirection(straight, 0.0f)));
		Require(Near(GetSunDirection(straight, 9.0f), glm::normalize(glm::vec3(1.0f, 1.0f, 0.0f))), "9 isn't halfway up: " + Text(GetSunDirection(straight, 9.0f)));

		// Rising at -Z, a 30 degree tilt: at noon 60 degrees up, leaning to +X, the right of where
		// it rises; at midnight as far below on the other side
		const TimeOfDayComponent tilted = Path(30.0f, 90.0f);
		const float s = 0.5f;
		const float c = std::sqrt(3.0f) * 0.5f;
		Require(Near(GetSunDirection(tilted, 6.0f), glm::vec3(0.0f, 0.0f, -1.0f)), "The tilted path's sunrise isn't at -Z: " + Text(GetSunDirection(tilted, 6.0f)));
		Require(Near(GetSunDirection(tilted, 12.0f), glm::vec3(s, c, 0.0f)), "The tilted path's noon isn't 60 degrees up at +X: " + Text(GetSunDirection(tilted, 12.0f)));
		Require(Near(GetSunDirection(tilted, 18.0f), glm::vec3(0.0f, 0.0f, 1.0f)), "The tilted path's sunset isn't at +Z: " + Text(GetSunDirection(tilted, 18.0f)));
		Require(Near(GetSunDirection(tilted, 0.0f), glm::vec3(-s, -c, 0.0f)), "The tilted path's midnight isn't opposite noon: " + Text(GetSunDirection(tilted, 0.0f)));

		// Every direction of a path is on one circle about its axis, a unit vector
		for (float hours = 0.0f; hours < 24.0f; hours += 0.5f)
		{
			Require(Near(glm::length(GetSunDirection(tilted, hours)), 1.0f, 1e-5f), "A sun direction isn't normalised");
		}
		// A tilt beyond 90 degrees is held there
		Require(Near(GetSunDirection(Path(120.0f, 0.0f), 12.0f), GetSunDirection(Path(90.0f, 0.0f), 12.0f)), "The tilt isn't held at 90 degrees");
	}

	// Past 24 the time wraps, and the sun with it
	void TestWrapping()
	{
		Require(WrapHours(25.0) == 1.0f && WrapHours(-1.0) == 23.0f && WrapHours(24.0) == 0.0f && WrapHours(48.0) == 0.0f && WrapHours(12.5) == 12.5f,
			"The time doesn't wrap into 0 to 24");
		Require(WrapHours(23.9999999999) < 24.0f, "A time just below 24 wraps to 24");
		const TimeOfDayComponent path = Path(20.0f, 30.0f);
		Require(Near(GetSunDirection(path, 30.0f), GetSunDirection(path, 6.0f)) && Near(GetSunDirection(path, -6.0f), GetSunDirection(path, 18.0f)),
			"The sun doesn't wrap with the time");
	}

	// The light shines away from the sun, all day, turning about the path's axis only
	void TestSunRotation()
	{
		for (const TimeOfDayComponent& path : { Path(0.0f, 0.0f), Path(35.0f, 0.0f), Path(-50.0f, 200.0f), Path(90.0f, 45.0f) })
		{
			const glm::vec3 axis = GetSunRotation(path, 0.0f) * glm::vec3(1.0f, 0.0f, 0.0f);
			glm::quat before = GetSunRotation(path, 0.0f);
			for (int quarter = 1; quarter <= 24 * 4; ++quarter)
			{
				const float hours = quarter * 0.25f;
				const glm::quat rotation = GetSunRotation(path, hours);
				Require(Near(Forward(rotation), -GetSunDirection(path, hours), 1e-5f),
					"The light doesn't shine away from the sun at " + std::to_string(hours) + ": " + Text(Forward(rotation)));
				Require(Near(rotation * glm::vec3(1.0f, 0.0f, 0.0f), axis, 1e-5f), "The light rolls at " + std::to_string(hours));
				// A quarter hour is 3.75 degrees, a turn of cos(1.875 degrees) as a quaternion
				Require(std::abs(glm::dot(before, rotation)) > 0.999f, "The light jumps at " + std::to_string(hours));
				before = rotation;
			}
		}
	}

	// Below the horizon the sun's light fades out over the fade, and is gone past it
	void TestHorizonFade()
	{
		const auto sinOf = [](float degrees) { return std::sin(glm::radians(degrees)); };
		Require(GetSunHorizonFade(sinOf(30.0f), 3.0f) == 1.0f && GetSunHorizonFade(0.0f, 3.0f) == 1.0f, "The sun above the horizon is faded");
		const float halfway = GetSunHorizonFade(sinOf(-1.5f), 3.0f);
		Require(halfway > 0.3f && halfway < 0.7f, "The sun 1.5 degrees down isn't half faded: " + std::to_string(halfway));
		Require(GetSunHorizonFade(sinOf(-3.0f), 3.0f) == 0.0f && GetSunHorizonFade(-1.0f, 3.0f) == 0.0f, "The sun past the fade still gives light");
		Require(GetSunHorizonFade(sinOf(-0.1f), 0.0f) == 0.0f && GetSunHorizonFade(0.0f, 0.0f) == 1.0f, "Without a fade the light doesn't stop at the horizon");
		Require(GetSunHorizonFade(sinOf(-25.0f), 40.0f) == 0.0f, "The fade isn't held at 20 degrees");
	}

	struct SunScene
	{
		Registry World;
		Entity Sun;
		Entity Other;
		Entity Clock;

		explicit SunScene(bool bNameTheSun = true)
		{
			// Another light first, not the primary one, so the sun is found by its guid or as the
			// primary light, not as the first light
			Other = World.CreateEntity();
			World.Add<TransformComponent>(Other, TransformComponent{});
			World.Add<GuidComponent>(Other, GuidComponent{ EntityGuid{ 0x0A } });
			DirectionalLightComponent other;
			other.bPrimary = false;
			World.Add<DirectionalLightComponent>(Other, other);

			Sun = World.CreateEntity();
			World.Add<TransformComponent>(Sun, TransformComponent{ .Position = glm::vec3(1.0f, 2.0f, 3.0f) });
			World.Add<GuidComponent>(Sun, GuidComponent{ EntityGuid{ 0x50 } });
			World.Add<DirectionalLightComponent>(Sun, DirectionalLightComponent{});

			Clock = World.CreateEntity();
			World.Add<GuidComponent>(Clock, GuidComponent{ EntityGuid{ 0xC0 } });
			TimeOfDayComponent timeOfDay = Path(30.0f, 0.0f);
			timeOfDay.Sun = bNameTheSun ? EntityGuid{ 0x50 } : EntityGuid{};
			World.Add<TimeOfDayComponent>(Clock, timeOfDay);
		}

		TimeOfDayComponent& TimeOfDay() { return World.Get<TimeOfDayComponent>(Clock); }
		glm::quat SunRotation() const { return World.Get<TransformComponent>(Sun).Rotation; }
		glm::vec3 TowardsSun() const { return -Forward(SunRotation()); }
	};

	// From noon through sunset to night: the sun's light, through the time of day's fade and the
	// air (MakeSceneViewAtmosphere(), as the renderer reads them), bright and white, then warm and
	// dim, then gone; the sky's tint down to the night's brightness
	void TestNoonToNight()
	{
		SunScene scene;
		EnvironmentComponent environment;
		const glm::vec3 white(1.0f);
		struct Seen
		{
			float Fade;
			SceneViewAtmosphere Air;
		};
		const auto at = [&](float hours)
		{
			scene.TimeOfDay().Hours = hours;
			ApplyTimeOfDay(scene.World);
			const glm::vec3 towardsSun = scene.TowardsSun();
			const float fade = GetTimeOfDayLightFade(scene.World, scene.Sun, towardsSun);
			return Seen{ fade, MakeSceneViewAtmosphere(environment, towardsSun, white * fade) };
		};

		const Seen noon = at(12.0f);
		const Seen evening = at(17.5f);
		const Seen sunset = at(18.0f);
		const Seen night = at(0.0f);
		Require(noon.Fade == 1.0f && evening.Fade == 1.0f && sunset.Fade == 1.0f, "The sun is faded above the horizon");
		Require(night.Fade == 0.0f, "The sun at midnight still gives light");
		Require(noon.Air.SunColor.r > 0.9f && noon.Air.SunColor.b > 0.8f, "The noon sun isn't bright and white: " + Text(noon.Air.SunColor));
		Require(evening.Air.SunColor.r < noon.Air.SunColor.r && evening.Air.SunColor.r > evening.Air.SunColor.b * 1.5f,
			"The sun low in the evening isn't dimmer and warmer: " + Text(evening.Air.SunColor));
		Require(night.Air.SunColor == glm::vec3(0.0f), "The sun at night still lights the ground: " + Text(night.Air.SunColor));
		Require(Near(night.Air.SkyTint, glm::vec3(environment.NightBrightness)), "The night sky isn't at the night's brightness: " + Text(night.Air.SkyTint));
		Require(night.Air.SkyTint.g < sunset.Air.SkyTint.g && sunset.Air.SkyTint.g < noon.Air.SkyTint.g, "The sky doesn't darken from noon through sunset to night");

		// Without the air, the time of day's fade alone keeps a sun that has set from lighting
		// anything from below; another light isn't the sun, and isn't faded
		Require(GetTimeOfDayLightFade(scene.World, scene.Other, glm::vec3(0.0f, -1.0f, 0.0f)) == 1.0f, "A light that isn't the sun is faded");
		scene.World.Remove<TimeOfDayComponent>(scene.Clock);
		Require(GetTimeOfDayLightFade(scene.World, scene.Sun, glm::vec3(0.0f, -1.0f, 0.0f)) == 1.0f, "A sun without a time of day is faded");
	}

	// The time of day turns its Sun, or the primary light when it names none; only its rotation,
	// and nothing for a Sun the world doesn't have. The world's first time of day counts.
	void TestSunFoundAndTurned()
	{
		SunScene named;
		named.TimeOfDay().Hours = 15.0f;
		ApplyTimeOfDay(named.World);
		Require(Near(named.TowardsSun(), GetSunDirection(named.TimeOfDay(), 15.0f)), "The named sun isn't turned to the time");
		Require(named.World.Get<TransformComponent>(named.Sun).Position == glm::vec3(1.0f, 2.0f, 3.0f), "Turning the sun moved it");
		Require(named.World.Get<TransformComponent>(named.Other).Rotation == glm::quat(1.0f, 0.0f, 0.0f, 0.0f), "Another light was turned");

		SunScene primary(false);
		primary.TimeOfDay().Hours = 8.0f;
		ApplyTimeOfDay(primary.World);
		Require(Near(primary.TowardsSun(), GetSunDirection(primary.TimeOfDay(), 8.0f)), "Without a named sun, the primary light isn't turned");

		SunScene missing;
		missing.TimeOfDay().Sun = EntityGuid{ 0x77 };
		ApplyTimeOfDay(missing.World);
		Require(missing.SunRotation() == glm::quat(1.0f, 0.0f, 0.0f, 0.0f), "A light was turned for a sun the world doesn't have");

		// A second time of day doesn't count
		SunScene two;
		two.TimeOfDay().Hours = 12.0f;
		const Entity second = two.World.CreateEntity();
		TimeOfDayComponent other = Path(0.0f, 0.0f);
		other.Hours = 3.0f;
		other.Sun = EntityGuid{ 0x50 };
		two.World.Add<TimeOfDayComponent>(second, other);
		ApplyTimeOfDay(two.World);
		Require(Near(two.TowardsSun(), GetSunDirection(two.TimeOfDay(), 12.0f)), "The second time of day turned the sun");
	}

	// A game world with the sun scene, its time running from `hours`, a day `dayLength` seconds
	struct RunningWorld
	{
		GameWorld Game;
		Entity Sun;
		Entity Clock;

		RunningWorld(float hours, float dayLength)
		{
			Registry& world = Game.GetRegistry();
			Sun = world.CreateEntity();
			world.Add<TransformComponent>(Sun, TransformComponent{});
			world.Add<GuidComponent>(Sun, GuidComponent{ EntityGuid{ 0x50 } });
			world.Add<DirectionalLightComponent>(Sun, DirectionalLightComponent{});
			Clock = world.CreateEntity();
			world.Add<GuidComponent>(Clock, GuidComponent{ EntityGuid{ 0xC0 } });
			TimeOfDayComponent timeOfDay = Path(25.0f, 10.0f);
			timeOfDay.Hours = hours;
			timeOfDay.bTimeRuns = true;
			timeOfDay.DayLengthSeconds = dayLength;
			timeOfDay.Sun = EntityGuid{ 0x50 };
			world.Add<TimeOfDayComponent>(Clock, timeOfDay);
		}

		TimeOfDayComponent& TimeOfDay() { return Game.GetRegistry().Get<TimeOfDayComponent>(Clock); }
		glm::quat SunRotation() { return Game.GetRegistry().Get<TransformComponent>(Sun).Rotation; }

		// Steps from `first` on, at 60 a second, as FixedStepClock numbers them
		void Step(uint64_t first, uint64_t count)
		{
			for (uint64_t index = first; index < first + count; ++index)
			{
				Game.Step(StepTime{ FixedStepClock::DefaultStepSeconds, index, static_cast<double>(index) * FixedStepClock::DefaultStepSeconds });
			}
		}
	};

	// While time runs, the steps move it on, a day a day's length; through midnight it wraps. Not
	// running, it stays, and the sun is still turned to it.
	void TestTimeRunsWithSteps()
	{
		// 24 hours in 240 s: an hour in 10 s, 600 steps
		RunningWorld world(5.5f, 240.0f);
		world.Step(0, 600);
		Require(Near(world.TimeOfDay().Hours, 6.5f), "Ten seconds of a 240 s day aren't an hour: " + std::to_string(world.TimeOfDay().Hours));
		Require(Near(-Forward(world.SunRotation()), GetSunDirection(world.TimeOfDay(), world.TimeOfDay().Hours)), "The steps didn't turn the sun");

		RunningWorld late(23.5f, 240.0f);
		late.Step(0, 600);
		Require(Near(late.TimeOfDay().Hours, 0.5f), "The time doesn't wrap past midnight: " + std::to_string(late.TimeOfDay().Hours));

		RunningWorld stopped(9.0f, 240.0f);
		stopped.TimeOfDay().bTimeRuns = false;
		stopped.Step(0, 600);
		Require(stopped.TimeOfDay().Hours == 9.0f, "The time moved while not running");
		Require(Near(-Forward(stopped.SunRotation()), GetSunDirection(stopped.TimeOfDay(), 9.0f)), "The sun isn't turned to a time that doesn't run");

		// Running again, it goes on from where it stopped
		stopped.TimeOfDay().bTimeRuns = true;
		stopped.Step(600, 300);
		Require(Near(stopped.TimeOfDay().Hours, 9.5f), "The time doesn't go on from where it stopped: " + std::to_string(stopped.TimeOfDay().Hours));

		// Three days and more in 20 000 steps: the time is the start plus the steps' time, not a
		// sum of 20 000 small steps of a float, which would be off by about a hundredth of an hour
		RunningWorld many(0.3f, 100.0f);
		many.Step(0, 20000);
		const float expected = WrapHours(0.3 + 20000.0 / 60.0 * 24.0 / 100.0);
		Require(Near(many.TimeOfDay().Hours, expected, 1e-4f), "20 000 steps drift: " + std::to_string(many.TimeOfDay().Hours) + " against " + std::to_string(expected));
	}

	// The same start and the same number of steps give the same sun to the bit, however long the
	// frames were: the frames only decide how many steps run when
	void TestDeterministic()
	{
		const auto run = [](const std::vector<double>& frames, uint64_t steps)
		{
			RunningWorld world(4.25f, 90.0f);
			FixedStepClock clock;
			uint64_t done = 0;
			for (size_t frame = 0; done < steps; ++frame)
			{
				const uint32_t count = clock.Advance(frames[frame % frames.size()]);
				for (uint32_t step = 0; step < count && done < steps; ++step, ++done)
				{
					world.Game.Step(clock.GetFrameStep(step));
				}
			}
			return std::pair{ world.TimeOfDay().Hours, world.SunRotation() };
		};

		const auto smooth = run({ 1.0 / 144.0 }, 1000);
		const auto slow = run({ 1.0 / 30.0 }, 1000);
		const auto uneven = run({ 0.013, 0.021, 0.0071, 0.04, 0.0167 }, 1000);
		Require(smooth.first == slow.first && smooth.first == uneven.first, "Different frame times give different times of day");
		Require(smooth.second == slow.second && smooth.second == uneven.second, "Different frame times give different sun rotations");
		// 1000 steps are 16.7 s of a 90 s day: 4.44 hours
		Require(Near(smooth.first, 4.25f + 1000.0f / 60.0f * 24.0f / 90.0f), "The time after 1000 steps isn't the start plus their time: " + std::to_string(smooth.first));
	}

	// A time set from outside while it runs, by a live edit as the editor sends it, goes on from
	// there; a paused game's frame turns the sun to it at once. Another day length goes on from the
	// time it is, without a jump.
	void TestSetWhileRunning()
	{
		RunningWorld game(10.0f, 240.0f);
		game.Step(0, 60);

		// The editor's copy of the time of day, with the time dragged to 20, sent as the one property
		Registry editor;
		const Entity clock = editor.CreateEntity();
		editor.Add<GuidComponent>(clock, GuidComponent{ EntityGuid{ 0xC0 } });
		TimeOfDayComponent edited = game.TimeOfDay();
		edited.Hours = 20.0f;
		edited.bTimeRuns = false;
		editor.Add<TimeOfDayComponent>(clock, edited);
		const ComponentTypeOps* ops = ComponentTypeRegistry::Get().FindByTypeMetadata(Reflection::GetTypeMetadata<TimeOfDayComponent>());
		const std::optional<size_t> hours = Reflection::FindPropertyIndexByName(Reflection::GetTypeMetadata<TimeOfDayComponent>(), "Hours");
		Require(ops && hours, "The time of day isn't registered");
		const std::optional<SetPropertiesMessage> scrub = MakeSetPropertiesMessage(editor, clock, *ops, { *hours });
		Require(scrub && ApplyLiveEdit(MakeNetMessage(*scrub), game.Game.GetRegistry(), {}) == ELiveEditResult::Applied, "The time wasn't sent live");
		Require(game.TimeOfDay().Hours == 20.0f && game.TimeOfDay().bTimeRuns, "The live edit didn't set the time alone");

		// Paused, no steps: the frame turns the sun
		ApplyTimeOfDay(game.Game.GetRegistry());
		Require(Near(-Forward(game.SunRotation()), GetSunDirection(game.TimeOfDay(), 20.0f)), "A paused game's sun doesn't follow the time sent");

		// Running on from 20: an hour in 600 steps
		game.Step(60, 600);
		Require(Near(game.TimeOfDay().Hours, 21.0f), "The time doesn't run on from the one sent: " + std::to_string(game.TimeOfDay().Hours));

		// Twice as long a day: from 21, half an hour in the next 600 steps
		game.TimeOfDay().DayLengthSeconds = 480.0f;
		game.Step(660, 600);
		Require(Near(game.TimeOfDay().Hours, 21.5f), "Another day length jumps the time: " + std::to_string(game.TimeOfDay().Hours));

		// A day shorter than a second is held at one
		game.TimeOfDay().DayLengthSeconds = 0.0f;
		game.Step(1260, 6);
		Require(std::isfinite(game.TimeOfDay().Hours) && game.TimeOfDay().Hours >= 0.0f && game.TimeOfDay().Hours < 24.0f, "A day length of 0 breaks the time");
	}

	void TestSavedWithTheScene()
	{
		Registry world;
		const Entity entity = world.CreateEntity();
		TimeOfDayComponent timeOfDay = Path(-12.5f, 135.0f);
		timeOfDay.Hours = 17.25f;
		timeOfDay.bTimeRuns = true;
		timeOfDay.DayLengthSeconds = 600.0f;
		timeOfDay.Sun = EntityGuid{ 0x5A };
		timeOfDay.HorizonFadeDegrees = 6.0f;
		world.Add<TimeOfDayComponent>(entity, timeOfDay);
		world.Add<GuidComponent>(entity, GuidComponent{ EntityGuid{ 0x7D } });

		Nyx::Tests::TestDirectory directory("NyxTimeOfDayTests");
		const std::filesystem::path path = directory.Path / "TimeOfDay.nyxscene";
		Require(SceneSerializer::SaveToFile(world, path), "The scene can't be saved");
		Registry loaded;
		ScenePostLoadContext context{};
		Require(SceneSerializer::LoadFromFile(path, loaded, context), "The scene can't be loaded");
		const TimeOfDayComponent* read = FindTimeOfDay(loaded);
		Require(read && read->Hours == 17.25f && read->bTimeRuns && read->DayLengthSeconds == 600.0f && read->Sun == EntityGuid{ 0x5A } &&
				read->PathTiltDegrees == -12.5f && read->SunriseDirectionDegrees == 135.0f && read->HorizonFadeDegrees == 6.0f,
			"The time of day isn't read back as saved");
	}
}

int main()
{
	try
	{
		Nyx::Logger::Get().Init();
		Nyx::RegisterComponentTypes();
		TestSunDirections();
		TestWrapping();
		TestSunRotation();
		TestHorizonFade();
		TestNoonToNight();
		TestSunFoundAndTurned();
		TestTimeRunsWithSteps();
		TestDeterministic();
		TestSetWhileRunning();
		TestSavedWithTheScene();
	}
	catch (const std::exception& error)
	{
		std::cerr << "FAILED: " << error.what() << "\n";
		return 1;
	}

	std::cout << "All time of day tests passed\n";
	return 0;
}
