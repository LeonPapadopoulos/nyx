#include "NyxPCH.h"
#include "Lightning.h"

#include "PreviousTransformComponent.h"
#include "TransformComponent.h"

#include <glm/gtc/constants.hpp>

#include <algorithm>
#include <bit>
#include <cmath>
#include <limits>

namespace Nyx
{
	namespace
	{
		uint32_t Mix(uint32_t x)
		{
			x ^= x >> 16;
			x *= 0x7feb352du;
			x ^= x >> 15;
			x *= 0x846ca68bu;
			x ^= x >> 16;
			return x;
		}

		// 0 to 1, never 1, from a hash
		float UnitFrom(uint32_t hash)
		{
			return static_cast<float>(hash >> 8) * (1.0f / 16777216.0f);
		}

		float Saturate(float value)
		{
			return std::clamp(value, 0.0f, 1.0f);
		}

		// A random direction at right angles to `direction` (normalised)
		glm::vec3 RandomAcross(const glm::vec3& direction, LightningRandom& random)
		{
			const float z = random.Signed();
			const float angle = random.Unit() * glm::two_pi<float>();
			const float ring = std::sqrt(std::max(1.0f - z * z, 0.0f));
			glm::vec3 across = glm::cross(direction, glm::vec3(ring * std::cos(angle), z, ring * std::sin(angle)));
			if (glm::dot(across, across) < 1e-6f)
			{
				across = glm::cross(direction, std::abs(direction.y) < 0.9f ? glm::vec3(0.0f, 1.0f, 0.0f) : glm::vec3(1.0f, 0.0f, 0.0f));
			}
			return glm::normalize(across);
		}

		// `vector` turned by `angle` radians about `axis` (normalised, at right angles to it)
		glm::vec3 TurnAbout(const glm::vec3& vector, const glm::vec3& axis, float angle)
		{
			return vector * std::cos(angle) + glm::cross(axis, vector) * std::sin(angle);
		}

		// A point anywhere in a disc of `radius` round the up axis, as likely anywhere in it
		glm::vec3 InDisc(LightningRandom& random, float radius)
		{
			const float distance = radius * std::sqrt(random.Unit());
			const float angle = random.Unit() * glm::two_pi<float>();
			return glm::vec3(distance * std::cos(angle), 0.0f, distance * std::sin(angle));
		}

		// Each stretch of `points` split `splits` times, its middle moved across it at random by up to
		// `jaggedness` times its length
		void SplitPolyline(std::vector<glm::vec3>& points, uint32_t splits, float jaggedness, LightningRandom& random)
		{
			std::vector<glm::vec3> next;
			for (uint32_t split = 0; split < splits; ++split)
			{
				next.clear();
				next.reserve(points.size() * 2);
				next.push_back(points.front());
				for (size_t index = 0; index + 1 < points.size(); ++index)
				{
					const glm::vec3 along = points[index + 1] - points[index];
					const float length = glm::length(along);
					glm::vec3 middle = (points[index] + points[index + 1]) * 0.5f;
					if (length > 0.0f)
					{
						middle += RandomAcross(along / length, random) * (random.Signed() * jaggedness * length);
					}
					next.push_back(middle);
					next.push_back(points[index + 1]);
				}
				points.swap(next);
			}
		}

		LightningPathSettings MakePathSettings(const LightningComponent& lightning)
		{
			LightningPathSettings settings;
			settings.Jaggedness = lightning.Jaggedness;
			settings.Detail = lightning.Detail;
			settings.BranchChance = lightning.BranchChance;
			settings.BranchLength = lightning.BranchLength;
			settings.BranchDepth = lightning.BranchDepth;
			settings.BranchBrightness = lightning.BranchBrightness;
			return settings;
		}

		// `source`'s paths appended to `target`, each path's brightness times `scale`; dark ones left out.
		// Returns the index in `target` of `source`'s path `keep`, if it was kept.
		std::optional<uint32_t> AppendShape(const LightningShape& source, LightningShape& target, std::optional<uint32_t> keep = std::nullopt)
		{
			std::optional<uint32_t> kept;
			for (uint32_t index = 0; index < source.Paths.size(); ++index)
			{
				const LightningPath& path = source.Paths[index];
				if (path.Brightness <= 1e-4f || path.PointCount < 2 || target.Paths.size() >= MaxLightningPaths ||
					target.Points.size() + path.PointCount > MaxLightningPoints)
				{
					continue;
				}
				LightningPath copy = path;
				copy.FirstPoint = static_cast<uint32_t>(target.Points.size());
				target.Points.insert(target.Points.end(), source.Points.begin() + path.FirstPoint, source.Points.begin() + path.FirstPoint + path.PointCount);
				if (keep && *keep == index)
				{
					kept = static_cast<uint32_t>(target.Paths.size());
				}
				target.Paths.push_back(copy);
			}
			return kept;
		}

		// A strike from `top` to `ground`, `age` seconds in, into `out`: its path from `seed`, revealed
		// down to the leader's tip, each path as bright as the moment makes it times `scale`. Returns
		// the main channel's brightness, and its path in `out` once the leader is down.
		float AddStrike(const LightningComponent& lightning, const glm::vec3& top, const glm::vec3& ground, uint32_t seed, float age, float leaderTime, float fadeTime,
			float scale, LightningShape& out, std::optional<uint32_t>* channel)
		{
			const LightningStrokes strokes = MakeLightningStrokes(lightning, seed, leaderTime, fadeTime);
			if (age < 0.0f || age >= strokes.End)
			{
				return 0.0f;
			}
			LightningShape full;
			LightningRandom random(HashLightning(seed, 0x5a4eu));
			MakeLightningPath(top, ground, MakePathSettings(lightning), random, 1.0f, 1.0f, full);

			const float main = GetLightningStrikeBrightness(lightning, strokes, leaderTime, fadeTime, age);
			const float branches = GetLightningBranchBrightness(lightning, strokes, leaderTime, age);
			const float progress = GetLightningLeaderProgress(leaderTime, age);
			for (LightningPath& path : full.Paths)
			{
				path.Brightness *= (path.Depth == 0 ? main : branches) * scale;
			}
			if (progress < 1.0f)
			{
				LightningShape revealed;
				RevealLightningShape(full, progress, revealed);
				AppendShape(revealed, out);
				return main;
			}
			const std::optional<uint32_t> kept = AppendShape(full, out, 0u);
			if (channel)
			{
				*channel = kept;
			}
			return main;
		}

		BoundingBox BoundsOf(const LightningShape& shape)
		{
			BoundingBox bounds;
			if (shape.Points.empty())
			{
				return bounds;
			}
			bounds.Min = bounds.Max = shape.Points.front().Position;
			for (const LightningPoint& point : shape.Points)
			{
				bounds.Min = glm::min(bounds.Min, point.Position);
				bounds.Max = glm::max(bounds.Max, point.Position);
			}
			for (const LightningSprite& sprite : shape.Sprites)
			{
				bounds.Min = glm::min(bounds.Min, sprite.Position);
				bounds.Max = glm::max(bounds.Max, sprite.Position);
			}
			return bounds;
		}
	}

	LightningComponent GetClampedLightning(const LightningComponent& lightning)
	{
		LightningComponent clamped = lightning;
		clamped.Interval = std::clamp(lightning.Interval, 0.5f, 3600.0f);
		clamped.IntervalJitter = Saturate(lightning.IntervalJitter);
		clamped.Duration = std::max(lightning.Duration, 0.0f);
		clamped.Height = std::clamp(lightning.Height, 10.0f, 20000.0f);
		clamped.StrikeRadius = std::max(lightning.StrikeRadius, 0.0f);
		clamped.TopSpread = std::max(lightning.TopSpread, 0.0f);
		clamped.Jaggedness = Saturate(lightning.Jaggedness);
		clamped.Detail = std::clamp(lightning.Detail, 1u, 10u);
		clamped.BranchChance = Saturate(lightning.BranchChance);
		clamped.BranchLength = Saturate(lightning.BranchLength);
		clamped.BranchDepth = std::min(lightning.BranchDepth, 3u);
		clamped.LeaderTime = std::clamp(lightning.LeaderTime, 0.0f, 2.0f);
		clamped.MinStrokes = std::clamp(lightning.MinStrokes, 1, MaxLightningStrokes);
		clamped.MaxStrokes = std::clamp(lightning.MaxStrokes, clamped.MinStrokes, MaxLightningStrokes);
		clamped.StrokeInterval = std::clamp(lightning.StrokeInterval, 0.005f, 1.0f);
		clamped.StrokeDecay = std::clamp(lightning.StrokeDecay, 0.002f, 1.0f);
		clamped.FadeTime = std::clamp(lightning.FadeTime, 0.01f, 5.0f);
		clamped.ReformRate = std::clamp(lightning.ReformRate, 1.0f, 60.0f);
		clamped.MinFilaments = std::clamp(lightning.MinFilaments, 1, 12);
		clamped.MaxFilaments = std::clamp(lightning.MaxFilaments, clamped.MinFilaments, 12);
		clamped.ColumnRadius = std::max(lightning.ColumnRadius, 0.0f);
		clamped.Knots = Saturate(lightning.Knots);
		clamped.KnotSize = std::clamp(lightning.KnotSize, 1.0f, 10.0f);
		clamped.ColumnKinks = std::min(lightning.ColumnKinks, 3u);
		clamped.FilamentWidth = std::clamp(lightning.FilamentWidth, 0.1f, 10.0f);
		clamped.ColumnJaggedness = Saturate(lightning.ColumnJaggedness);
		clamped.Joins = Saturate(lightning.Joins);
		clamped.WidthVariation = Saturate(lightning.WidthVariation);
		clamped.GlowHeight = std::max(lightning.GlowHeight, 0.0f);
		clamped.GlowPatchiness = Saturate(lightning.GlowPatchiness);
		clamped.SideArcRate = std::clamp(lightning.SideArcRate, 0.0f, 60.0f);
		clamped.SideArcLength = std::max(lightning.SideArcLength, 0.0f);
		clamped.StrayBoltRate = std::clamp(lightning.StrayBoltRate, 0.0f, 20.0f);
		clamped.StrayBoltDistance = std::max(lightning.StrayBoltDistance, 0.0f);
		clamped.CoreColor = glm::max(lightning.CoreColor, glm::vec3(0.0f));
		clamped.BodyColor = glm::max(lightning.BodyColor, glm::vec3(0.0f));
		clamped.GlowColor = glm::max(lightning.GlowColor, glm::vec3(0.0f));
		clamped.Brightness = std::max(lightning.Brightness, 0.0f);
		clamped.BodyBrightness = Saturate(lightning.BodyBrightness);
		clamped.GlowStrength = Saturate(lightning.GlowStrength);
		clamped.Width = std::max(lightning.Width, 0.001f);
		clamped.CoreWidth = std::clamp(lightning.CoreWidth, 0.05f, 1.0f);
		clamped.GlowWidth = std::clamp(lightning.GlowWidth, 1.0f, 20.0f);
		clamped.MinPixelWidth = std::clamp(lightning.MinPixelWidth, 0.0f, 16.0f);
		clamped.Sharpness = Saturate(lightning.Sharpness);
		clamped.BranchBrightness = Saturate(lightning.BranchBrightness);
		clamped.LeaderBrightness = Saturate(lightning.LeaderBrightness);
		clamped.LightCount = std::clamp(lightning.LightCount, 1, 8);
		clamped.LightColor = glm::max(lightning.LightColor, glm::vec3(0.0f));
		clamped.LightIntensity = std::max(lightning.LightIntensity, 0.0f);
		clamped.LightRange = std::max(lightning.LightRange, 0.0f);
		return clamped;
	}

	LightningRandom::LightningRandom(uint32_t seed)
	{
		Next();
		State += seed;
		Next();
	}

	uint32_t LightningRandom::Next()
	{
		const uint64_t old = State;
		State = old * 6364136223846793005ull + 1442695040888963407ull;
		const uint32_t shifted = static_cast<uint32_t>(((old >> 18u) ^ old) >> 27u);
		const uint32_t rotation = static_cast<uint32_t>(old >> 59u);
		return (shifted >> rotation) | (shifted << ((32u - rotation) & 31u));
	}

	float LightningRandom::Unit()
	{
		return UnitFrom(Next());
	}

	float LightningRandom::Signed()
	{
		return Unit() * 2.0f - 1.0f;
	}

	float LightningRandom::Range(float low, float high)
	{
		return low + (high - low) * Unit();
	}

	int32_t LightningRandom::Between(int32_t low, int32_t high)
	{
		if (high <= low)
		{
			return low;
		}
		return low + static_cast<int32_t>(Next() % static_cast<uint32_t>(high - low + 1));
	}

	uint32_t HashLightning(uint32_t a, uint32_t b, uint32_t c, uint32_t d)
	{
		uint32_t hash = Mix(a ^ 0x2545f491u);
		hash = Mix(hash + b + 0x9e3779b9u);
		hash = Mix(hash + c + 0x7f4a7c15u);
		return Mix(hash + d + 0x94d049bbu);
	}

	float GetLightningLength(const LightningComponent& lightning)
	{
		float length = 0.0f;
		if (lightning.Mode == ELightningMode::Strike)
		{
			length = lightning.LeaderTime + static_cast<float>(lightning.MaxStrokes - 1) * lightning.StrokeInterval * 1.5f + lightning.FadeTime;
		}
		else
		{
			length = lightning.Duration > 0.0f ? lightning.Duration : std::numeric_limits<float>::infinity();
		}
		return lightning.bRepeat ? std::min(length, lightning.Interval) : length;
	}

	float GetLightningStart(const LightningComponent& lightning, uint32_t slot)
	{
		if (!lightning.bRepeat)
		{
			return lightning.StartTime;
		}
		const double room = std::max(static_cast<double>(lightning.Interval) - static_cast<double>(GetLightningLength(lightning)), 0.0);
		const double jitter = UnitFrom(HashLightning(lightning.Seed, slot, 0x51u)) * lightning.IntervalJitter * room;
		return static_cast<float>(static_cast<double>(lightning.StartTime) + static_cast<double>(slot) * lightning.Interval + jitter);
	}

	LightningEvent FindLightningEvent(const LightningComponent& lightning, float time)
	{
		LightningEvent event;
		const double since = static_cast<double>(time) - static_cast<double>(lightning.StartTime);
		if (since < 0.0)
		{
			return event;
		}
		const float length = GetLightningLength(lightning);
		uint32_t slot = 0;
		if (lightning.bRepeat)
		{
			const double slots = std::floor(since / static_cast<double>(lightning.Interval));
			if (slots >= static_cast<double>(std::numeric_limits<uint32_t>::max()))
			{
				return event;
			}
			slot = static_cast<uint32_t>(slots);
		}
		const float start = GetLightningStart(lightning, slot);
		const float age = static_cast<float>(static_cast<double>(time) - static_cast<double>(start));
		if (age < 0.0f || age >= length)
		{
			return event;
		}
		event.bActive = true;
		event.Slot = slot;
		event.Start = start;
		event.Age = age;
		event.Length = length;
		return event;
	}

	LightningStrokes MakeLightningStrokes(const LightningComponent& lightning, uint32_t seed, float leaderTime, float fadeTime)
	{
		LightningStrokes strokes;
		LightningRandom random(HashLightning(seed, 0x57eu));
		strokes.Count = random.Between(lightning.MinStrokes, lightning.MaxStrokes);
		float time = leaderTime;
		for (int32_t stroke = 0; stroke < strokes.Count; ++stroke)
		{
			strokes.Times[stroke] = time;
			strokes.Peaks[stroke] = stroke == 0 ? 1.0f : random.Range(0.45f, 0.85f);
			time += lightning.StrokeInterval * random.Range(0.5f, 1.5f);
		}
		strokes.End = strokes.Times[strokes.Count - 1] + fadeTime;
		return strokes;
	}

	float GetLightningLeaderProgress(float leaderTime, float age)
	{
		if (leaderTime <= 0.0f)
		{
			return age >= 0.0f ? 1.0f : 0.0f;
		}
		return Saturate(age / leaderTime);
	}

	namespace
	{
		// The afterglow's part of a flash's peak while the strokes come down, and the window that
		// brings everything to 0 by the strike's end after the last one
		constexpr float LightningAfterglow = 0.15f;

		float StrikeWindow(const LightningStrokes& strokes, float fadeTime, float age)
		{
			const float last = strokes.Times[strokes.Count - 1];
			if (age <= last)
			{
				return 1.0f;
			}
			const float fade = Saturate(1.0f - (age - last) / std::max(fadeTime, 1e-4f));
			return fade * fade;
		}
	}

	float GetLightningStrikeBrightness(const LightningComponent& lightning, const LightningStrokes& strokes, float leaderTime, float fadeTime, float age)
	{
		if (age < 0.0f || age >= strokes.End || strokes.Count == 0)
		{
			return 0.0f;
		}
		if (age < leaderTime)
		{
			return lightning.LeaderBrightness;
		}
		float brightness = 0.0f;
		float peak = 0.0f;
		for (int32_t stroke = 0; stroke < strokes.Count && strokes.Times[stroke] <= age; ++stroke)
		{
			brightness += strokes.Peaks[stroke] * std::exp(-(age - strokes.Times[stroke]) / lightning.StrokeDecay);
			peak = strokes.Peaks[stroke];
		}
		brightness += LightningAfterglow * peak;
		return std::min(brightness, 1.5f) * StrikeWindow(strokes, fadeTime, age);
	}

	float GetLightningBranchBrightness(const LightningComponent& lightning, const LightningStrokes& strokes, float leaderTime, float age)
	{
		if (age < 0.0f || age >= strokes.End || strokes.Count == 0)
		{
			return 0.0f;
		}
		if (age < leaderTime)
		{
			return lightning.LeaderBrightness;
		}
		return strokes.Peaks[0] * std::exp(-(age - strokes.Times[0]) / lightning.StrokeDecay);
	}

	uint32_t GetLightningReformFrame(const LightningComponent& lightning, float age)
	{
		return static_cast<uint32_t>(std::max(std::floor(age * lightning.ReformRate), 0.0f));
	}

	float GetLightningColumnBrightness(const LightningComponent& lightning, const LightningEvent& event, uint32_t eventSeed)
	{
		if (!event.bActive)
		{
			return 0.0f;
		}
		const float in = Saturate(event.Age / LightningColumnFadeIn);
		const float out = std::isinf(event.Length) ? 1.0f : Saturate((event.Length - event.Age) / LightningColumnFadeOut);
		const uint32_t frame = GetLightningReformFrame(lightning, event.Age);
		const float flicker = 0.75f + 0.25f * UnitFrom(HashLightning(eventSeed, frame, 0xf1u));
		return in * out * flicker;
	}

	std::optional<uint32_t> MakeLightningPath(const glm::vec3& top, const glm::vec3& bottom, const LightningPathSettings& settings, LightningRandom& random, float brightness,
		float width, LightningShape& shape, float arrivalStart, float arrivalScale, uint32_t depth)
	{
		const uint32_t detail = std::min(settings.Detail, 10u);
		const uint32_t count = (1u << detail) + 1u;
		if (shape.Paths.size() >= MaxLightningPaths || shape.Points.size() + count > MaxLightningPoints)
		{
			return std::nullopt;
		}

		// The branches asked for on the way: where they start (their index among the final points),
		// where they go, and how many splits are left for them
		struct Branch
		{
			uint32_t Root = 0;
			glm::vec3 End{ 0.0f };
			uint32_t Splits = 0;
		};
		std::vector<Branch> branches;
		std::vector<glm::vec3> points{ top, bottom };
		std::vector<glm::vec3> next;
		for (uint32_t split = 0; split < detail; ++split)
		{
			next.clear();
			next.reserve(points.size() * 2);
			next.push_back(points.front());
			const uint32_t splitsLeft = detail - split - 1u;
			for (size_t index = 0; index + 1 < points.size(); ++index)
			{
				const glm::vec3 along = points[index + 1] - points[index];
				const float length = glm::length(along);
				const glm::vec3 direction = length > 0.0f ? along / length : glm::vec3(0.0f, -1.0f, 0.0f);
				glm::vec3 middle = (points[index] + points[index + 1]) * 0.5f;
				if (length > 0.0f)
				{
					middle += RandomAcross(direction, random) * (random.Signed() * settings.Jaggedness * length);
				}
				next.push_back(middle);
				const uint32_t middleIndex = static_cast<uint32_t>(next.size() - 1) << splitsLeft;
				next.push_back(points[index + 1]);

				// A branch from the middle: heading down and away from the stretch (down only as much
				// as the stretch heads down), Branch Length of what is left below
				if (depth < settings.BranchDepth && split < settings.BranchSplits)
				{
					const float chance = settings.BranchChance * std::ldexp(1.0f, -static_cast<int>(depth));
					if (random.Unit() < chance)
					{
						const glm::vec3 axis = RandomAcross(direction, random);
						glm::vec3 heading = TurnAbout(direction, axis, glm::radians(random.Range(20.0f, 50.0f)));
						heading = glm::normalize(heading + glm::vec3(0.0f, -0.3f * std::max(-direction.y, 0.0f), 0.0f));
						const float remaining = glm::distance(middle, points.back());
						const float branchLength = settings.BranchLength * remaining * random.Range(0.6f, 1.2f);
						if (branchLength > 0.0f)
						{
							branches.push_back({ middleIndex, middle + heading * branchLength, splitsLeft });
						}
					}
				}
			}
			points.swap(next);
		}

		float total = 0.0f;
		for (size_t index = 1; index < points.size(); ++index)
		{
			total += glm::distance(points[index - 1], points[index]);
		}
		const float scale = arrivalScale >= 0.0f ? arrivalScale : (total > 0.0f ? 1.0f / total : 0.0f);

		const uint32_t pathIndex = static_cast<uint32_t>(shape.Paths.size());
		const uint32_t first = static_cast<uint32_t>(shape.Points.size());
		float run = 0.0f;
		for (size_t index = 0; index < points.size(); ++index)
		{
			if (index > 0)
			{
				run += glm::distance(points[index - 1], points[index]);
			}
			const float along = total > 0.0f ? run / total : 0.0f;
			LightningPoint point;
			point.Position = points[index];
			point.Width = width * (depth > 0 ? 1.0f - 0.7f * along : 1.0f);
			point.Arrival = arrivalStart + run * scale;
			shape.Points.push_back(point);
		}
		LightningPath path;
		path.FirstPoint = first;
		path.PointCount = static_cast<uint32_t>(points.size());
		path.Brightness = brightness;
		path.Depth = depth;
		shape.Paths.push_back(path);

		for (const Branch& branch : branches)
		{
			const LightningPoint root = shape.Points[first + branch.Root];
			LightningPathSettings branchSettings = settings;
			branchSettings.Detail = branch.Splits;
			MakeLightningPath(root.Position, branch.End, branchSettings, random, brightness * settings.BranchBrightness, root.Width * 0.6f, shape, root.Arrival, scale, depth + 1);
		}
		return pathIndex;
	}

	void RevealLightningShape(const LightningShape& shape, float progress, LightningShape& out)
	{
		out.Clear();
		for (const LightningPath& path : shape.Paths)
		{
			LightningPath revealed = path;
			revealed.FirstPoint = static_cast<uint32_t>(out.Points.size());
			for (uint32_t index = 0; index < path.PointCount; ++index)
			{
				const LightningPoint& point = shape.Points[path.FirstPoint + index];
				if (point.Arrival <= progress)
				{
					out.Points.push_back(point);
					continue;
				}
				// The tip: where the leader is on the stretch it is on
				if (index > 0)
				{
					const LightningPoint& before = shape.Points[path.FirstPoint + index - 1];
					const float part = Saturate((progress - before.Arrival) / std::max(point.Arrival - before.Arrival, 1e-6f));
					LightningPoint tip;
					tip.Position = glm::mix(before.Position, point.Position, part);
					tip.Width = glm::mix(before.Width, point.Width, part);
					tip.Arrival = progress;
					out.Points.push_back(tip);
				}
				break;
			}
			revealed.PointCount = static_cast<uint32_t>(out.Points.size()) - revealed.FirstPoint;
			if (revealed.PointCount < 2)
			{
				out.Points.resize(revealed.FirstPoint);
				continue;
			}
			out.Paths.push_back(revealed);
		}
	}

	void MakeLightningColumn(const LightningComponent& lightning, const glm::vec3& base, uint32_t seed, uint32_t frame, float brightness, LightningShape& shape)
	{
		LightningRandom random(HashLightning(seed, frame, 0xc01u));
		const int32_t filaments = random.Between(lightning.MinFilaments, lightning.MaxFilaments);
		const float height = lightning.Height;
		const float radius = lightning.ColumnRadius;
		// The control points up a filament, closer together near the ground (the square of the way up),
		// where the column is seen close: big, angular strokes a few metres to tens of metres long
		constexpr int32_t Levels = 18;
		const float spin = random.Unit() * glm::two_pi<float>();
		std::array<float, 12> starts{};
		std::array<float, 12> thickness{};
		for (int32_t filament = 0; filament < filaments; ++filament)
		{
			starts[filament] = spin + glm::two_pi<float>() * static_cast<float>(filament) / static_cast<float>(filaments) + random.Signed() * 0.5f;
			thickness[filament] = lightning.FilamentWidth * random.Range(0.6f, 1.3f);
		}
		const auto levelHeight = [&](int32_t level)
		{
			const float part = static_cast<float>(level) / static_cast<float>(Levels);
			return height * part * part;
		};
		const auto pinchAt = [&](float up)
		{
			return 0.35f + 0.65f * Saturate(up / std::max(radius, 1e-3f));
		};

		// Joins: levels where some of the filaments meet at one point, splitting apart again after, so
		// they make open loops between the joins, with a fat blob where they meet
		struct Join
		{
			bool bJoin = false;
			glm::vec3 Point{ 0.0f };
			uint32_t Members = 0;
		};
		std::array<Join, Levels + 1> joins{};
		for (int32_t level = 1; level < Levels; ++level)
		{
			if (random.Unit() >= lightning.Joins)
			{
				continue;
			}
			const float up = levelHeight(level);
			const float angle = random.Unit() * glm::two_pi<float>();
			const float distance = radius * random.Range(0.0f, 0.7f) * pinchAt(up);
			Join& join = joins[level];
			join.Point = base + glm::vec3(distance * std::cos(angle), up, distance * std::sin(angle));
			for (int32_t filament = 0; filament < filaments; ++filament)
			{
				join.Members |= random.Unit() < 0.6f ? 1u << filament : 0u;
			}
			join.bJoin = std::popcount(join.Members) >= 2;
			if (join.bJoin)
			{
				shape.Sprites.push_back(LightningSprite{ .Position = join.Point, .Size = lightning.KnotSize * lightning.FilamentWidth * 0.5f, .Brightness = brightness * 2.0f,
					.Glow = lightning.GlowHeight > 0.0f ? std::exp2(-up / lightning.GlowHeight) : 1.0f });
			}
		}

		const uint32_t splits = std::min(lightning.ColumnKinks, 3u);
		std::vector<glm::vec3> points;
		std::vector<float> widths;
		std::vector<float> glows;
		for (int32_t filament = 0; filament < filaments; ++filament)
		{
			points.clear();
			widths.clear();
			glows.clear();
			for (int32_t level = 0; level <= Levels; ++level)
			{
				const float up = levelHeight(level);
				if (joins[level].bJoin && (joins[level].Members & (1u << filament)) != 0)
				{
					points.push_back(joins[level].Point);
				}
				else
				{
					// Pinched together at the ground, wandering out over the whole radius above, its angle
					// thrown far either way, so the filaments cross and loop round each other
					const float distance = radius * random.Range(0.4f, 1.0f) * pinchAt(up);
					const float angle = starts[filament] + glm::two_pi<float>() * lightning.Twist * up / 100.0f + random.Signed() * 0.9f;
					points.push_back(base + glm::vec3(distance * std::cos(angle), up, distance * std::sin(angle)));
				}
				// Thick to thin along it; its glow the most low down and in patches
				widths.push_back(thickness[filament] * glm::mix(1.0f, random.Range(0.2f, 1.6f), lightning.WidthVariation));
				const float fall = lightning.GlowHeight > 0.0f ? std::exp2(-up / lightning.GlowHeight) : 1.0f;
				glows.push_back(fall * glm::mix(1.0f, random.Unit(), lightning.GlowPatchiness));
			}
			SplitPolyline(points, splits, lightning.ColumnJaggedness, random);
			const uint32_t count = static_cast<uint32_t>(points.size());
			if (shape.Paths.size() >= MaxLightningPaths || shape.Points.size() + count > MaxLightningPoints)
			{
				break;
			}
			LightningPath path;
			path.FirstPoint = static_cast<uint32_t>(shape.Points.size());
			path.PointCount = count;
			path.Brightness = brightness * random.Range(0.8f, 1.0f);
			const uint32_t perLevel = 1u << splits;
			for (uint32_t index = 0; index < count; ++index)
			{
				// The control points' widths and glows, straight between them; a knot, a round glow, at a
				// few kinks
				const uint32_t level = std::min(index / perLevel, static_cast<uint32_t>(Levels) - 1u);
				const float part = static_cast<float>(index - level * perLevel) / static_cast<float>(perLevel);
				const float width = glm::mix(widths[level], widths[level + 1], part);
				const float glow = glm::mix(glows[level], glows[level + 1], part);
				if (index > 0 && index + 1 < count && random.Unit() < lightning.Knots)
				{
					shape.Sprites.push_back(LightningSprite{ .Position = points[index], .Size = width * lightning.KnotSize * 0.5f, .Brightness = path.Brightness * 2.0f, .Glow = glow });
				}
				LightningPoint point;
				point.Position = points[index];
				point.Width = width;
				point.Glow = glow;
				shape.Points.push_back(point);
			}
			shape.Paths.push_back(path);
		}

		// Side arcs, each living two reform frames, longer in its second, a new shape in each
		if (lightning.SideArcRate <= 0.0f || lightning.SideArcLength <= 0.0f)
		{
			return;
		}
		LightningPathSettings arcs;
		arcs.Jaggedness = std::max(lightning.ColumnJaggedness, 0.3f);
		arcs.Detail = 3;
		arcs.BranchChance = 0.4f;
		arcs.BranchLength = 0.5f;
		arcs.BranchDepth = 1;
		arcs.BranchBrightness = 0.7f;
		arcs.BranchSplits = 3;
		const float perFrame = lightning.SideArcRate / lightning.ReformRate;
		for (uint32_t age = 0; age < 2 && age <= frame; ++age)
		{
			const uint32_t born = frame - age;
			LightningRandom arcRandom(HashLightning(seed, born, 0xa2cu));
			const int32_t count = static_cast<int32_t>(std::floor(perFrame)) + (arcRandom.Unit() < perFrame - std::floor(perFrame) ? 1 : 0);
			for (int32_t arc = 0; arc < count; ++arc)
			{
				const float up = arcRandom.Range(0.03f, 0.35f) * std::min(height, 400.0f);
				const float angle = arcRandom.Unit() * glm::two_pi<float>();
				const glm::vec3 outward(std::cos(angle), 0.0f, std::sin(angle));
				const glm::vec3 root = base + outward * (radius * 0.7f) + glm::vec3(0.0f, up, 0.0f);
				const glm::vec3 heading = glm::normalize(outward + glm::vec3(0.0f, arcRandom.Range(-0.5f, 0.3f), 0.0f));
				const float length = lightning.SideArcLength * arcRandom.Range(0.7f, 1.3f) * (age == 0 ? 0.55f : 1.0f);
				LightningRandom shapeRandom(HashLightning(seed, born, 0xa2du + static_cast<uint32_t>(arc), age));
				MakeLightningPath(root, root + heading * length, arcs, shapeRandom, brightness * lightning.BranchBrightness, 0.7f, shape);
			}
		}
	}

	void EvaluateLightning(const LightningComponent& component, const glm::vec3& position, float time, LightningFrame& frame)
	{
		frame.bActive = false;
		frame.Shape.Clear();
		frame.Brightness = 0.0f;
		frame.Channel.reset();
		frame.Bounds = {};
		if (!component.bEnabled)
		{
			return;
		}
		const LightningComponent lightning = GetClampedLightning(component);
		const LightningEvent event = FindLightningEvent(lightning, time);
		if (!event.bActive)
		{
			return;
		}
		const uint32_t seed = HashLightning(lightning.Seed, event.Slot);
		LightningRandom place(HashLightning(seed, 0x9au));

		if (lightning.Mode == ELightningMode::Strike)
		{
			frame.Ground = position + InDisc(place, lightning.StrikeRadius);
			frame.Top = frame.Ground + glm::vec3(0.0f, lightning.Height, 0.0f) + InDisc(place, lightning.TopSpread);
			frame.Brightness = AddStrike(lightning, frame.Top, frame.Ground, seed, event.Age, lightning.LeaderTime, lightning.FadeTime, 1.0f, frame.Shape, &frame.Channel);
		}
		else
		{
			frame.Ground = position;
			frame.Top = position + glm::vec3(0.0f, lightning.Height, 0.0f);
			frame.Brightness = GetLightningColumnBrightness(lightning, event, seed);
			MakeLightningColumn(lightning, position, seed, GetLightningReformFrame(lightning, event.Age), frame.Brightness, frame.Shape);

			// Stray bolts: one in each stretch of 1 / Stray Bolt Rate seconds, a short strike of its own
			if (lightning.StrayBoltRate > 0.0f)
			{
				LightningComponent stray = lightning;
				stray.MinStrokes = 1;
				stray.MaxStrokes = 2;
				const float period = 1.0f / lightning.StrayBoltRate;
				const float length = LightningStrayLeaderTime + lightning.StrokeInterval * 1.5f + LightningStrayFadeTime;
				const int64_t current = static_cast<int64_t>(std::floor(event.Age / period));
				for (int64_t slot = std::max<int64_t>(current - 1, 0); slot <= current; ++slot)
				{
					const uint32_t straySeed = HashLightning(seed, static_cast<uint32_t>(slot), 0x57au);
					const float start = static_cast<float>(slot) * period + UnitFrom(straySeed) * std::max(period - length, 0.0f);
					const float age = event.Age - start;
					if (age < 0.0f || age >= length)
					{
						continue;
					}
					LightningRandom where(HashLightning(straySeed, 0x9bu));
					const float angle = where.Unit() * glm::two_pi<float>();
					const glm::vec3 ground = position + glm::vec3(std::cos(angle), 0.0f, std::sin(angle)) * (lightning.StrayBoltDistance * where.Range(0.6f, 1.4f));
					const glm::vec3 top = ground + glm::vec3(0.0f, lightning.Height, 0.0f) + InDisc(where, lightning.TopSpread * 0.3f);
					AddStrike(stray, top, ground, straySeed, age, LightningStrayLeaderTime, LightningStrayFadeTime, lightning.BranchBrightness * 1.5f, frame.Shape, nullptr);
				}
			}
		}
		frame.bActive = !frame.Shape.Paths.empty() || frame.Brightness > 0.0f;
		frame.Bounds = BoundsOf(frame.Shape);
	}

	void AddLightningLights(const LightningComponent& component, const LightningFrame& frame, Entity entity, std::vector<RenderPointLight>& outLights)
	{
		const LightningComponent lightning = GetClampedLightning(component);
		if (!frame.bActive || !lightning.bLights || frame.Brightness <= 0.0f || lightning.LightIntensity <= 0.0f || lightning.LightRange <= 0.0f)
		{
			return;
		}
		const glm::vec3 color = lightning.LightColor * (lightning.LightIntensity * frame.Brightness / static_cast<float>(lightning.LightCount));
		if (std::max(color.r, std::max(color.g, color.b)) <= 0.0f)
		{
			return;
		}
		// Up the lowest stretch: the first 2 m up, the last as high as the lights reach well
		const float span = std::min(lightning.Height * 0.3f, lightning.LightRange * 0.8f);
		const LightningPath* channel = frame.Channel ? &frame.Shape.Paths[*frame.Channel] : nullptr;
		for (int32_t light = 0; light < lightning.LightCount; ++light)
		{
			const float up = 2.0f + span * static_cast<float>(light) / static_cast<float>(std::max(lightning.LightCount - 1, 1));
			glm::vec3 position = frame.Ground + glm::vec3(0.0f, up, 0.0f);
			// Along the channel, from its foot up, where it first gets that high
			if (channel && channel->PointCount >= 2)
			{
				for (uint32_t index = channel->PointCount - 1; index > 0; --index)
				{
					const glm::vec3 lower = frame.Shape.Points[channel->FirstPoint + index].Position;
					const glm::vec3 upper = frame.Shape.Points[channel->FirstPoint + index - 1].Position;
					const float target = frame.Ground.y + up;
					if (upper.y >= target)
					{
						const float part = Saturate((target - lower.y) / std::max(upper.y - lower.y, 1e-4f));
						position = glm::mix(lower, upper, part);
						break;
					}
				}
			}
			RenderPointLight render;
			render.SourceEntity = entity;
			render.Position = position;
			render.WorldPosition = position;
			render.Range = lightning.LightRange;
			render.Color = color;
			render.bIcon = false;
			outLights.push_back(render);
		}
	}

	LightningLook MakeLightningLook(const LightningComponent& component)
	{
		const LightningComponent lightning = GetClampedLightning(component);
		LightningLook look;
		look.Core = lightning.CoreColor * lightning.Brightness;
		look.Body = lightning.BodyColor * (lightning.Brightness * lightning.BodyBrightness);
		look.Glow = lightning.GlowColor * (lightning.Brightness * lightning.GlowStrength);
		look.CoreFraction = lightning.CoreWidth / lightning.GlowWidth;
		look.BodyFraction = 1.0f / lightning.GlowWidth;
		look.HalfWidth = 0.5f * lightning.Width * lightning.GlowWidth;
		look.MinPixelWidth = lightning.MinPixelWidth;
		look.Sharpness = lightning.Sharpness;
		return look;
	}

	LightningPixelScale GetLightningPixelScale(const glm::mat4& projection, uint32_t height)
	{
		LightningPixelScale scale;
		const float focal = std::max(std::abs(projection[1][1]), 1e-6f);
		const float pixel = 2.0f / (focal * static_cast<float>(std::max(height, 1u)));
		const bool bOrthographic = projection[3][3] != 0.0f;
		scale.Offset = bOrthographic ? pixel : 0.0f;
		scale.Slope = bOrthographic ? 0.0f : pixel;
		return scale;
	}

	void BuildLightningStrips(const LightningShape& shape, const LightningLook& look, const glm::vec3& camera, const LightningPixelScale& pixels,
		std::vector<LightningVertex>& outVertices)
	{
		struct Edge
		{
			glm::vec3 Side{ 0.0f };
			float HalfWidth = 0.0f;
			float Intensity = 0.0f;
			float Glow = 1.0f;
		};
		std::vector<Edge> edges;
		for (const LightningPath& path : shape.Paths)
		{
			if (path.PointCount < 2 || path.Brightness <= 0.0f)
			{
				continue;
			}
			const LightningPoint* points = shape.Points.data() + path.FirstPoint;
			edges.resize(path.PointCount);
			for (uint32_t index = 0; index < path.PointCount; ++index)
			{
				const glm::vec3 position = points[index].Position;
				const glm::vec3 toCamera = camera - position;
				const float distance = glm::length(toCamera);
				const glm::vec3 view = distance > 0.0f ? toCamera / distance : glm::vec3(0.0f, 0.0f, 1.0f);

				// The side of each stretch at the point, at right angles to it and to the view; mitred
				const auto sideOf = [&](const glm::vec3& along)
				{
					glm::vec3 side = glm::cross(along, view);
					const float length = glm::length(side);
					if (length < 1e-6f)
					{
						side = glm::cross(view, std::abs(view.y) < 0.9f ? glm::vec3(0.0f, 1.0f, 0.0f) : glm::vec3(1.0f, 0.0f, 0.0f));
						return glm::normalize(side);
					}
					return side / length;
				};
				const glm::vec3 before = index > 0 ? points[index].Position - points[index - 1].Position : points[1].Position - points[0].Position;
				const glm::vec3 after = index + 1 < path.PointCount ? points[index + 1].Position - points[index].Position : before;
				const glm::vec3 sideBefore = sideOf(before);
				const glm::vec3 sideAfter = sideOf(after);
				glm::vec3 mitre = sideBefore + sideAfter;
				float stretch = 1.0f;
				if (glm::dot(mitre, mitre) < 1e-6f)
				{
					mitre = sideBefore;
				}
				else
				{
					mitre = glm::normalize(mitre);
					stretch = 1.0f / std::max(glm::dot(mitre, sideBefore), 0.5f);
				}

				// Widened where its core would be thinner than the least pixels there
				const float width = std::max(points[index].Width, 0.0f);
				const float coreHalf = look.HalfWidth * look.CoreFraction * width;
				const float pixel = pixels.Offset + pixels.Slope * distance;
				const float widen = std::max(1.0f, 0.5f * look.MinPixelWidth * pixel / std::max(coreHalf, 1e-6f));
				edges[index].HalfWidth = look.HalfWidth * width * widen;
				edges[index].Side = mitre * (edges[index].HalfWidth * stretch);
				edges[index].Intensity = path.Brightness / std::sqrt(widen);
				edges[index].Glow = points[index].Glow;
			}
			for (uint32_t index = 0; index + 1 < path.PointCount; ++index)
			{
				const glm::vec3 a = points[index].Position;
				const glm::vec3 b = points[index + 1].Position;
				const Edge& ea = edges[index];
				const Edge& eb = edges[index + 1];
				// The path's ends pushed out along it by their half width, for their round caps
				const glm::vec3 along = b - a;
				const float length = glm::length(along);
				const glm::vec3 direction = length > 0.0f ? along / length : glm::vec3(0.0f);
				const glm::vec3 from = index == 0 ? a - direction * ea.HalfWidth : a;
				const glm::vec3 to = index + 2 == path.PointCount ? b + direction * eb.HalfWidth : b;
				const auto vertex = [&](const glm::vec3& position)
				{
					LightningVertex v;
					v.Position = position;
					v.Start = a;
					v.StartHalfWidth = ea.HalfWidth;
					v.End = b;
					v.EndHalfWidth = eb.HalfWidth;
					v.StartIntensity = ea.Intensity;
					v.EndIntensity = eb.Intensity;
					v.StartGlow = ea.Glow;
					v.EndGlow = eb.Glow;
					outVertices.push_back(v);
				};
				vertex(from - ea.Side);
				vertex(from + ea.Side);
				vertex(to + eb.Side);
				vertex(from - ea.Side);
				vertex(to + eb.Side);
				vertex(to - eb.Side);
			}
		}

		// Each round glow: a square facing the camera round it, a stretch whose ends are its middle
		for (const LightningSprite& sprite : shape.Sprites)
		{
			if (sprite.Brightness <= 0.0f || sprite.Size <= 0.0f)
			{
				continue;
			}
			const glm::vec3 toCamera = camera - sprite.Position;
			const float distance = glm::length(toCamera);
			const glm::vec3 view = distance > 0.0f ? toCamera / distance : glm::vec3(0.0f, 0.0f, 1.0f);
			const glm::vec3 right = glm::normalize(glm::cross(view, std::abs(view.y) < 0.9f ? glm::vec3(0.0f, 1.0f, 0.0f) : glm::vec3(1.0f, 0.0f, 0.0f)));
			const glm::vec3 up = glm::cross(right, view);
			const float coreHalf = look.HalfWidth * look.CoreFraction * sprite.Size;
			const float pixel = pixels.Offset + pixels.Slope * distance;
			const float widen = std::max(1.0f, 0.5f * look.MinPixelWidth * pixel / std::max(coreHalf, 1e-6f));
			const float halfWidth = look.HalfWidth * sprite.Size * widen;
			const float intensity = sprite.Brightness / std::sqrt(widen);
			const auto vertex = [&](float x, float y)
			{
				LightningVertex v;
				v.Position = sprite.Position + (right * x + up * y) * halfWidth;
				v.Start = sprite.Position;
				v.StartHalfWidth = halfWidth;
				v.End = sprite.Position;
				v.EndHalfWidth = halfWidth;
				v.StartIntensity = intensity;
				v.EndIntensity = intensity;
				v.StartGlow = sprite.Glow;
				v.EndGlow = sprite.Glow;
				outVertices.push_back(v);
			};
			vertex(-1.0f, -1.0f);
			vertex(1.0f, -1.0f);
			vertex(1.0f, 1.0f);
			vertex(-1.0f, -1.0f);
			vertex(1.0f, 1.0f);
			vertex(-1.0f, 1.0f);
		}
	}

	void ExtractLightning(const Registry& world, float time, std::optional<float> interpolation, std::vector<RenderLightningBolt>& outBolts,
		std::vector<RenderPointLight>& outLights)
	{
		outBolts.clear();
		LightningFrame frame;
		world.Each<LightningComponent>([&](Entity entity, const LightningComponent& lightning)
			{
				RenderLightningBolt bolt;
				bolt.SourceEntity = entity;
				bolt.WorldPosition = world.Has<TransformComponent>(entity) ? world.Get<TransformComponent>(entity).Position : glm::vec3(0.0f);
				const LightningComponent clamped = GetClampedLightning(lightning);
				bolt.StrikeRadius = clamped.Mode == ELightningMode::Strike ? clamped.StrikeRadius : clamped.ColumnRadius;
				bolt.Height = clamped.Height;
				EvaluateLightning(lightning, GetDrawnTransform(world, entity, interpolation).Position, time, frame);
				AddLightningLights(lightning, frame, entity, outLights);
				if (frame.bActive && !frame.Shape.Paths.empty())
				{
					bolt.bDrawn = true;
					bolt.Look = MakeLightningLook(lightning);
					float widest = 0.0f;
					for (const LightningPoint& point : frame.Shape.Points)
					{
						widest = std::max(widest, point.Width);
					}
					for (const LightningSprite& sprite : frame.Shape.Sprites)
					{
						widest = std::max(widest, sprite.Size);
					}
					bolt.Bounds = ExpandBox(frame.Bounds, bolt.Look.HalfWidth * widest * 2.0f);
					bolt.Shape = std::move(frame.Shape);
					frame.Shape = {};
				}
				outBolts.push_back(std::move(bolt));
			});
	}
}
