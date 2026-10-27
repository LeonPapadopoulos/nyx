#include "NyxPCH.h"
#include "WindComponent.h"

#include <glm/gtc/constants.hpp>

#include <algorithm>
#include <cmath>

namespace Nyx
{
	namespace
	{
		// The values a WindState is made from, as numbers a drive blends
		struct WindValues
		{
			double DirectionRadians = 0.0;
			double Speed = 0.0;
			double Gustiness = 0.0;
			double GustFrequency = 0.0;
			double GustSpacing = 1.0;
			double GustPatchWidth = 1.0;
			double Turbulence = 0.0;
			double EddySize = 1.0;
			double GroundHeight = 0.0;
			double GroundFactor = 1.0;
			double ReferenceHeight = 1.0;
			double Aloft = 0.0;
			double AloftHeight = 2.0;
		};

		// The component's values as the wind takes them: within what makes sense, the speed 0 while
		// it doesn't blow
		WindValues GetValues(const WindComponent& wind)
		{
			WindValues values;
			values.DirectionRadians = static_cast<double>(wind.DirectionDegrees) * glm::pi<double>() / 180.0;
			values.Speed = wind.bBlowing ? std::clamp(static_cast<double>(wind.Speed), 0.0, 200.0) : 0.0;
			values.Gustiness = std::clamp(static_cast<double>(wind.Gustiness), 0.0, 4.0);
			values.GustFrequency = std::clamp(static_cast<double>(wind.GustFrequency), 0.0, 10.0);
			values.GustSpacing = std::clamp(static_cast<double>(wind.GustSpacing), 0.5, 100000.0);
			values.GustPatchWidth = std::clamp(static_cast<double>(wind.GustPatchWidth), 0.5, 100000.0);
			values.Turbulence = std::clamp(static_cast<double>(wind.Turbulence), 0.0, 4.0);
			values.EddySize = std::clamp(static_cast<double>(wind.EddySize), 0.1, 100000.0);
			values.GroundHeight = static_cast<double>(wind.GroundHeight);
			values.GroundFactor = std::clamp(static_cast<double>(wind.GroundFactor), 0.0, 4.0);
			values.ReferenceHeight = std::clamp(static_cast<double>(wind.ReferenceHeight), 0.01, 100000.0);
			values.Aloft = std::clamp(static_cast<double>(wind.Aloft), 0.0, 16.0);
			values.AloftHeight = std::max(static_cast<double>(wind.AloftHeight), values.ReferenceHeight + 0.01);
			return values;
		}

		glm::dvec2 GetDirection(double radians)
		{
			return glm::dvec2(std::cos(radians), -std::sin(radians));
		}

		// The eddies are carried along at the wind's speed
		glm::dvec2 GetEddyVelocity(const WindValues& values)
		{
			return GetDirection(values.DirectionRadians) * values.Speed;
		}

		// 0 to 1 over a change, along a smoothstep, and the integral of that smoothstep from its
		// start, in parts of the change's length
		double Ease(double s)
		{
			return s * s * (3.0 - 2.0 * s);
		}

		double EaseIntegral(double s)
		{
			const double s3 = s * s * s;
			return s3 - 0.5 * s3 * s;
		}

		// How far along its change a drive is at `time`, 0 to 1
		double GetProgress(const WindDrive& drive, double time)
		{
			if (!(drive.Seconds > 0.0))
			{
				return time >= drive.Start ? 1.0 : 0.0;
			}
			return std::clamp((time - drive.Start) / drive.Seconds, 0.0, 1.0);
		}

		// The integral from the drive's start to `time` of a rate going from `from` to `to` over its
		// change: before the start, `from`'s backwards
		double IntegrateRate(const WindDrive& drive, double from, double to, double time)
		{
			const double since = time - drive.Start;
			if (since <= 0.0)
			{
				return from * since;
			}
			if (!(drive.Seconds > 0.0))
			{
				return to * since;
			}
			if (since <= drive.Seconds)
			{
				return from * since + (to - from) * drive.Seconds * EaseIntegral(since / drive.Seconds);
			}
			return from * drive.Seconds + (to - from) * drive.Seconds * 0.5 + to * (since - drive.Seconds);
		}

		double Lerp(double a, double b, double w)
		{
			return a + (b - a) * w;
		}

		// The shortest way round from one direction to another
		double LerpAngle(double from, double to, double w)
		{
			const double tau = glm::two_pi<double>();
			double delta = std::fmod(to - from, tau);
			if (delta > glm::pi<double>())
			{
				delta -= tau;
			}
			else if (delta < -glm::pi<double>())
			{
				delta += tau;
			}
			return from + delta * w;
		}

		WindValues Blend(const WindValues& a, const WindValues& b, double w)
		{
			WindValues values;
			values.DirectionRadians = LerpAngle(a.DirectionRadians, b.DirectionRadians, w);
			values.Speed = Lerp(a.Speed, b.Speed, w);
			values.Gustiness = Lerp(a.Gustiness, b.Gustiness, w);
			values.GustFrequency = Lerp(a.GustFrequency, b.GustFrequency, w);
			values.GustSpacing = Lerp(a.GustSpacing, b.GustSpacing, w);
			values.GustPatchWidth = Lerp(a.GustPatchWidth, b.GustPatchWidth, w);
			values.Turbulence = Lerp(a.Turbulence, b.Turbulence, w);
			values.EddySize = Lerp(a.EddySize, b.EddySize, w);
			values.GroundHeight = Lerp(a.GroundHeight, b.GroundHeight, w);
			values.GroundFactor = Lerp(a.GroundFactor, b.GroundFactor, w);
			values.ReferenceHeight = Lerp(a.ReferenceHeight, b.ReferenceHeight, w);
			values.Aloft = Lerp(a.Aloft, b.Aloft, w);
			values.AloftHeight = Lerp(a.AloftHeight, b.AloftHeight, w);
			return values;
		}

		// A positive remainder: where a phase or an offset is in its repeat
		double Wrap(double value, double repeat)
		{
			const double wrapped = std::fmod(value, repeat);
			return wrapped < 0.0 ? wrapped + repeat : wrapped;
		}

		WindPatternValues GetPattern(const WindValues& values)
		{
			return WindPatternValues{ values.DirectionRadians, values.GustSpacing, values.GustPatchWidth, values.EddySize };
		}

		// A pattern as the state holds it, its eddies carried `eddyOffset` metres
		WindPattern MakePattern(const WindPatternValues& values, const glm::dvec2& eddyOffset)
		{
			WindPattern pattern;
			pattern.Direction = glm::vec2(GetDirection(values.DirectionRadians));
			pattern.GustsPerMetre = static_cast<float>(1.0 / values.GustSpacing);
			pattern.GustPatchesPerMetre = static_cast<float>(1.0 / values.GustPatchWidth);
			pattern.EddiesPerMetre = static_cast<float>(1.0 / values.EddySize);
			const double eddyRepeat = static_cast<double>(WindEddyCycles) * values.EddySize;
			pattern.EddyOffset = glm::vec2(static_cast<float>(Wrap(eddyOffset.x, eddyRepeat)), static_cast<float>(Wrap(eddyOffset.y, eddyRepeat)));
			return pattern;
		}

		// The state of a wind of these values whose fronts have passed a place `gustPhase` times and
		// whose eddies have been carried `eddyOffset` metres, at `time`, fading from one pattern to
		// another by `blend`
		WindState MakeState(const WindValues& values, double gustPhase, const glm::dvec2& eddyOffset, double time, const WindPatternValues& from,
			const WindPatternValues& to, double blend)
		{
			WindState state;
			const glm::dvec2 direction = GetDirection(values.DirectionRadians);
			state.Direction = glm::vec2(direction);
			state.Speed = static_cast<float>(values.Speed);
			state.Gustiness = static_cast<float>(values.Gustiness);
			state.GustPhase = static_cast<float>(Wrap(gustPhase, static_cast<double>(WindGustCycles)));
			state.GustRate = static_cast<float>(values.GustFrequency);
			state.Turbulence = static_cast<float>(values.Turbulence);
			state.EddyVelocity = glm::vec2(GetEddyVelocity(values));
			state.PatternBlend = static_cast<float>(blend);
			state.From = MakePattern(from, eddyOffset);
			state.To = MakePattern(to, eddyOffset);
			state.GroundHeight = static_cast<float>(values.GroundHeight);
			state.GroundFactor = static_cast<float>(values.GroundFactor);
			state.InverseReferenceHeight = static_cast<float>(1.0 / values.ReferenceHeight);
			state.Aloft = static_cast<float>(values.Aloft);
			const double aloftSpan = values.AloftHeight - values.ReferenceHeight;
			state.InverseAloftSpan = static_cast<float>(1.0 / aloftSpan);
			state.AloftStart = static_cast<float>(values.ReferenceHeight / aloftSpan);
			state.Time = static_cast<float>(time);
			return state;
		}

		// The patterns faded between at `time`: a waiting one fades in once the one before has
		struct PatternFade
		{
			WindPatternValues From;
			WindPatternValues To;
			double Start = 0.0;
			double Seconds = 0.0;
		};

		PatternFade GetPatternFade(const WindDrive& drive, double time)
		{
			const double end = drive.PatternStart + drive.PatternSeconds;
			if (drive.Pending && time >= end)
			{
				return PatternFade{ drive.PatternTo, *drive.Pending, end, drive.PendingSeconds };
			}
			return PatternFade{ drive.PatternFrom, drive.PatternTo, drive.PatternStart, drive.PatternSeconds };
		}

		double GetFadeBlend(const PatternFade& fade, double time)
		{
			if (!(fade.Seconds > 0.0))
			{
				return time >= fade.Start ? 1.0 : 0.0;
			}
			return Ease(std::clamp((time - fade.Start) / fade.Seconds, 0.0, 1.0));
		}

		// Whether two winds blow alike: the values a WindState is made from and how long a change
		// takes, not the debug view or the local air's texture
		bool BlowsAlike(const WindComponent& a, const WindComponent& b)
		{
			return a.bBlowing == b.bBlowing && a.DirectionDegrees == b.DirectionDegrees && a.Speed == b.Speed && a.Gustiness == b.Gustiness &&
				a.GustFrequency == b.GustFrequency && a.GustSpacing == b.GustSpacing && a.GustPatchWidth == b.GustPatchWidth &&
				a.Turbulence == b.Turbulence && a.EddySize == b.EddySize && a.GroundHeight == b.GroundHeight &&
				a.GroundFactor == b.GroundFactor && a.ReferenceHeight == b.ReferenceHeight && a.Aloft == b.Aloft &&
				a.AloftHeight == b.AloftHeight && a.ChangeSeconds == b.ChangeSeconds;
		}

		// The drive's values at `time`, and the phase and the offset it has carried its wind to
		WindValues GetDriveValues(const WindDrive& drive, double time)
		{
			return Blend(GetValues(drive.From), GetValues(drive.To), Ease(GetProgress(drive, time)));
		}

		double GetDrivePhase(const WindDrive& drive, double time)
		{
			const WindValues from = GetValues(drive.From);
			const WindValues to = GetValues(drive.To);
			return drive.GustPhase + IntegrateRate(drive, from.GustFrequency, to.GustFrequency, time);
		}

		glm::dvec2 GetDriveOffset(const WindDrive& drive, double time)
		{
			const glm::dvec2 from = GetEddyVelocity(GetValues(drive.From));
			const glm::dvec2 to = GetEddyVelocity(GetValues(drive.To));
			return drive.EddyOffset + glm::dvec2(IntegrateRate(drive, from.x, to.x, time), IntegrateRate(drive, from.y, to.y, time));
		}
	}

	const WindComponent* FindWorldWind(const Registry& world)
	{
		const WindComponent* found = nullptr;
		world.Each<WindComponent>(
			[&](Entity, const WindComponent& wind)
			{
				if (!found)
				{
					found = &wind;
				}
			});
		return found;
	}

	WindState MakeSteadyWind(const WindComponent& wind, double time)
	{
		const WindValues values = GetValues(wind);
		const WindPatternValues pattern = GetPattern(values);
		return MakeState(values, values.GustFrequency * time, GetEddyVelocity(values) * time, time, pattern, pattern, 1.0);
	}

	bool SetWindTarget(WindDrive& drive, const WindComponent& target, double time)
	{
		if (!drive.bStarted)
		{
			const WindValues values = GetValues(target);
			drive.From = target;
			drive.To = target;
			drive.Start = time;
			drive.Seconds = 0.0;
			drive.GustPhase = values.GustFrequency * time;
			drive.EddyOffset = GetEddyVelocity(values) * time;
			drive.PatternFrom = GetPattern(values);
			drive.PatternTo = drive.PatternFrom;
			drive.PatternStart = time;
			drive.PatternSeconds = 0.0;
			drive.Pending.reset();
			drive.bStarted = true;
			return true;
		}
		if (BlowsAlike(drive.To, target))
		{
			// The debug view and the local air's texture follow at once
			drive.To.Debug = target.Debug;
			drive.To.LocalAirCells = target.LocalAirCells;
			drive.To.LocalAirLayers = target.LocalAirLayers;
			drive.To.LocalAirCellSize = target.LocalAirCellSize;
			return false;
		}

		// From the wind as it is now: its values, as a component would hold them, and where its
		// fronts and eddies have got to
		const WindValues now = GetDriveValues(drive, time);
		const double phase = GetDrivePhase(drive, time);
		const glm::dvec2 offset = GetDriveOffset(drive, time);
		WindComponent from = target;
		from.DirectionDegrees = static_cast<float>(now.DirectionRadians * 180.0 / glm::pi<double>());
		from.bBlowing = true;
		from.Speed = static_cast<float>(now.Speed);
		from.Gustiness = static_cast<float>(now.Gustiness);
		from.GustFrequency = static_cast<float>(now.GustFrequency);
		from.GustSpacing = static_cast<float>(now.GustSpacing);
		from.GustPatchWidth = static_cast<float>(now.GustPatchWidth);
		from.Turbulence = static_cast<float>(now.Turbulence);
		from.EddySize = static_cast<float>(now.EddySize);
		from.GroundHeight = static_cast<float>(now.GroundHeight);
		from.GroundFactor = static_cast<float>(now.GroundFactor);
		from.ReferenceHeight = static_cast<float>(now.ReferenceHeight);
		from.Aloft = static_cast<float>(now.Aloft);
		from.AloftHeight = static_cast<float>(now.AloftHeight);

		drive.From = from;
		drive.To = target;
		drive.Start = time;
		drive.Seconds = std::max(static_cast<double>(target.ChangeSeconds), 0.0);
		drive.GustPhase = phase;
		drive.EddyOffset = offset;

		// The pattern: a waiting one that has begun to fade in is the one fading now; a new one fades
		// in now if none is fading, else waits for it
		const bool bPendingBegun = drive.Pending && time >= drive.PatternStart + drive.PatternSeconds;
		const PatternFade fade = GetPatternFade(drive, time);
		drive.PatternFrom = fade.From;
		drive.PatternTo = fade.To;
		drive.PatternStart = fade.Start;
		drive.PatternSeconds = fade.Seconds;
		if (bPendingBegun)
		{
			drive.Pending.reset();
		}
		const WindPatternValues pattern = GetPattern(GetValues(target));
		const WindPatternValues& newest = drive.Pending ? *drive.Pending : drive.PatternTo;
		if (!(pattern == newest))
		{
			if (time >= drive.PatternStart + drive.PatternSeconds)
			{
				drive.PatternFrom = drive.PatternTo;
				drive.PatternTo = pattern;
				drive.PatternStart = time;
				drive.PatternSeconds = drive.Seconds;
				drive.Pending.reset();
			}
			else
			{
				drive.Pending = pattern;
				drive.PendingSeconds = drive.Seconds;
			}
		}
		return true;
	}

	WindState EvaluateWind(const WindDrive& drive, double time)
	{
		if (!drive.bStarted)
		{
			return WindState{};
		}
		const PatternFade fade = GetPatternFade(drive, time);
		return MakeState(GetDriveValues(drive, time), GetDrivePhase(drive, time), GetDriveOffset(drive, time), time, fade.From, fade.To, GetFadeBlend(fade, time));
	}

	glm::dvec2 GetWindTravel(const WindDrive& drive, double time)
	{
		return drive.bStarted ? GetDriveOffset(drive, time) : glm::dvec2(0.0);
	}

	glm::dvec2 GetSteadyWindTravel(const WindComponent& wind, double time)
	{
		return GetEddyVelocity(GetValues(wind)) * time;
	}

	bool IsWindChanging(const WindDrive& drive, double time)
	{
		return drive.bStarted && drive.Seconds > 0.0 && time < drive.Start + drive.Seconds;
	}

	WindState UpdateWorldWind(Registry& world, double time)
	{
		Entity owner;
		bool bFound = false;
		world.Each<WindComponent>(
			[&](Entity entity, const WindComponent&)
			{
				if (!bFound)
				{
					bFound = true;
					owner = entity;
				}
			});

		// One drive, on the first wind's entity
		std::vector<Entity> stale;
		world.Each<WindDriveComponent>(
			[&](Entity entity, const WindDriveComponent&)
			{
				if (!bFound || entity != owner)
				{
					stale.push_back(entity);
				}
			});
		for (const Entity entity : stale)
		{
			world.Remove<WindDriveComponent>(entity);
		}
		if (!bFound)
		{
			return WindState{};
		}

		if (!world.Has<WindDriveComponent>(owner))
		{
			world.Add<WindDriveComponent>(owner);
		}
		WindDrive& drive = world.Get<WindDriveComponent>(owner).Drive;
		SetWindTarget(drive, world.Get<WindComponent>(owner), time);
		return EvaluateWind(drive, time);
	}

	WindState GetWorldWind(const Registry& world, double time)
	{
		const WindDriveComponent* driven = nullptr;
		world.Each<WindDriveComponent>(
			[&](Entity, const WindDriveComponent& drive)
			{
				if (!driven)
				{
					driven = &drive;
				}
			});
		if (driven && driven->Drive.bStarted)
		{
			return EvaluateWind(driven->Drive, time);
		}
		const WindComponent* wind = FindWorldWind(world);
		return wind ? MakeSteadyWind(*wind, time) : WindState{};
	}
}
