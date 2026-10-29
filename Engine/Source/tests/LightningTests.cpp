// Lightning without a GPU (Lightning.h): what a lightning entity shows at a world time, worked out as
// the renderer works it out. Its schedule: one bolt a slot, moved by the jitter, never overlapping
// the next, none before the start, one without Repeat, as exact a day later as at first. A strike's
// brightness: the leader's, the flashes and their decay, the afterglow, nothing after; the branches
// with the first flash only. A column's reform frames at its rate, the same shape within one and
// another in the next, fading in and out, its filaments braided round its axis and drawn in at its
// foot, meeting at joins, its glow strong low down. Paths: their ends kept, jagged within a band, branches heading down, arrivals along the
// channel, budgets held, the same seed the same path to the bit; the leader revealing them by
// arrival. Strips facing the camera, mitred and held at sharp kinks, widened far off and dimmed by
// the square root. The lights along the channel, none while off. The lines of light in the clouds:
// along the channel or the axis, as bright as asked, the brightest kept, the rectangle of the picture
// they show on. A world's lightning listed with its lights; the component saved and loaded.
#include "ComponentRegistration.h"
#include "Entity.h"
#include "Lightning.h"
#include "LightningComponent.h"
#include "Log.h"
#include "SceneSerializer.h"
#include "TestSupport.h"
#include "TransformComponent.h"

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <cmath>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

namespace
{
	using namespace Nyx;
	using Nyx::Tests::Require;

	bool Near(float a, float b, float tolerance = 1e-4f)
	{
		return std::abs(a - b) <= tolerance;
	}

	bool SameShape(const LightningShape& a, const LightningShape& b)
	{
		if (a.Points.size() != b.Points.size() || a.Paths.size() != b.Paths.size())
		{
			return false;
		}
		for (size_t index = 0; index < a.Points.size(); ++index)
		{
			if (std::memcmp(&a.Points[index], &b.Points[index], sizeof(LightningPoint)) != 0)
			{
				return false;
			}
		}
		for (size_t index = 0; index < a.Paths.size(); ++index)
		{
			if (std::memcmp(&a.Paths[index], &b.Paths[index], sizeof(LightningPath)) != 0)
			{
				return false;
			}
		}
		if (a.Sprites.size() != b.Sprites.size())
		{
			return false;
		}
		for (size_t index = 0; index < a.Sprites.size(); ++index)
		{
			if (std::memcmp(&a.Sprites[index], &b.Sprites[index], sizeof(LightningSprite)) != 0)
			{
				return false;
			}
		}
		return true;
	}

	// The distance from a point to the line through a and b
	float DistanceToLine(const glm::vec3& point, const glm::vec3& a, const glm::vec3& b)
	{
		const glm::vec3 along = glm::normalize(b - a);
		const glm::vec3 offset = point - a;
		return glm::length(offset - along * glm::dot(offset, along));
	}

	void TestSchedule()
	{
		LightningComponent lightning;
		lightning.StartTime = 10.0f;
		lightning.Interval = 5.0f;
		lightning.IntervalJitter = 0.7f;
		const LightningComponent clamped = GetClampedLightning(lightning);
		const float length = GetLightningLength(clamped);
		Require(length > clamped.LeaderTime && length < clamped.Interval, "A strike lasts its leader, its strokes and its afterglow, less than its interval");

		// Each slot's bolt within its slot, over before the next starts
		for (uint32_t slot = 0; slot < 2000; ++slot)
		{
			const float start = GetLightningStart(clamped, slot);
			Require(start >= 10.0f + 5.0f * static_cast<float>(slot) - 1e-3f, "A bolt starts within its slot");
			Require(start + length <= GetLightningStart(clamped, slot + 1) + 1e-3f, "A bolt is over before the next one starts");
		}

		// Found at a moment just after it starts, and not between bolts
		const float start = GetLightningStart(clamped, 3);
		const LightningEvent event = FindLightningEvent(clamped, start + 0.01f);
		Require(event.bActive && event.Slot == 3 && Near(event.Age, 0.01f, 1e-3f), "The bolt on just after slot 3's start is slot 3's");
		Require(!FindLightningEvent(clamped, start + length + 0.01f).bActive, "Nothing is on just after a bolt is over");
		Require(!FindLightningEvent(clamped, 9.0f).bActive, "Nothing is on before the start time");

		// Without jitter, like a clock
		LightningComponent clock = clamped;
		clock.IntervalJitter = 0.0f;
		Require(GetLightningStart(clock, 7) == 10.0f + 35.0f, "Without jitter a bolt starts at its slot's start");

		// Once
		LightningComponent once = clamped;
		once.bRepeat = false;
		Require(FindLightningEvent(once, 10.05f).bActive && !FindLightningEvent(once, 15.05f).bActive, "Without Repeat it strikes once");

		// A day later, as exact: the slot found from the time alone
		const uint32_t lateSlot = 17280;
		const float late = GetLightningStart(clamped, lateSlot);
		const LightningEvent lateEvent = FindLightningEvent(clamped, late + 0.05f);
		Require(lateEvent.bActive && lateEvent.Slot == lateSlot && Near(lateEvent.Age, 0.05f, 0.02f), "A day later the bolt is found as exactly");

		// A column lasts its duration; with a duration of 0 and Repeat, its whole interval
		LightningComponent column = clamped;
		column.Mode = ELightningMode::Sustained;
		column.Duration = 3.0f;
		Require(GetLightningLength(column) == 3.0f, "A column lasts its duration");
		column.Duration = 0.0f;
		Require(GetLightningLength(column) == column.Interval, "A column without a duration lasts its interval when it repeats");
		column.bRepeat = false;
		Require(std::isinf(GetLightningLength(column)), "A column without a duration that doesn't repeat stands for ever");
	}

	void TestStrikeBrightness()
	{
		const LightningComponent lightning = GetClampedLightning(LightningComponent{});
		for (uint32_t seed = 0; seed < 200; ++seed)
		{
			const LightningStrokes strokes = MakeLightningStrokes(lightning, seed, lightning.LeaderTime, lightning.FadeTime);
			Require(strokes.Count >= lightning.MinStrokes && strokes.Count <= lightning.MaxStrokes, "A strike has as many strokes as asked");
			Require(strokes.Times[0] == lightning.LeaderTime && strokes.Peaks[0] == 1.0f, "The first stroke comes when the leader is down, at full brightness");
			for (int32_t stroke = 1; stroke < strokes.Count; ++stroke)
			{
				Require(strokes.Times[stroke] > strokes.Times[stroke - 1] && strokes.Peaks[stroke] < 1.0f, "Each later stroke comes after the one before, dimmer");
			}
			Require(strokes.End <= GetLightningLength(lightning) + 1e-4f, "A strike is over within the length it may last");
		}

		const LightningStrokes strokes = MakeLightningStrokes(lightning, 5, lightning.LeaderTime, lightning.FadeTime);
		const auto main = [&](float age)
		{
			return GetLightningStrikeBrightness(lightning, strokes, lightning.LeaderTime, lightning.FadeTime, age);
		};
		const auto branches = [&](float age)
		{
			return GetLightningBranchBrightness(lightning, strokes, lightning.LeaderTime, age);
		};
		Require(main(0.05f) == lightning.LeaderBrightness && branches(0.05f) == lightning.LeaderBrightness, "While the leader comes down, the leader's brightness");
		Require(main(strokes.Times[0]) >= 1.0f, "The first flash at its peak");
		Require(main(strokes.Times[0] + 0.03f) < main(strokes.Times[0]), "The flash dies away");
		Require(main(strokes.End - 0.01f) > 0.0f && main(strokes.End) == 0.0f && main(strokes.End + 1.0f) == 0.0f, "The afterglow fades to nothing by the strike's end");
		Require(main(-0.1f) == 0.0f, "Nothing before the strike");
		if (strokes.Count > 1)
		{
			const float second = strokes.Times[1];
			Require(main(second) > main(second - 0.001f), "A later stroke flashes again");
			Require(branches(second) < branches(strokes.Times[0]) * 0.5f, "The branches go with the first flash, not the later ones");
		}
		Require(GetLightningLeaderProgress(lightning.LeaderTime, lightning.LeaderTime * 0.5f) == 0.5f && GetLightningLeaderProgress(lightning.LeaderTime, 1.0f) == 1.0f,
			"The leader comes down evenly and is down after its time");
	}

	void TestPaths()
	{
		LightningPathSettings settings;
		const glm::vec3 top(10.0f, 1000.0f, -5.0f);
		const glm::vec3 bottom(0.0f, 0.0f, 0.0f);
		LightningShape shape;
		LightningRandom random(42);
		const std::optional<uint32_t> main = MakeLightningPath(top, bottom, settings, random, 1.0f, 1.0f, shape);
		Require(main.has_value() && *main == 0, "The main path is the first");
		const LightningPath& channel = shape.Paths[0];
		Require(channel.PointCount == (1u << settings.Detail) + 1u, "The main path is split Detail times");
		Require(shape.Points[channel.FirstPoint].Position == top && shape.Points[channel.FirstPoint + channel.PointCount - 1].Position == bottom, "Its ends are kept");
		Require(shape.Points[channel.FirstPoint].Arrival == 0.0f && Near(shape.Points[channel.FirstPoint + channel.PointCount - 1].Arrival, 1.0f, 1e-4f),
			"Its arrival goes from 0 at the top to 1 at the ground");
		const float length = glm::distance(top, bottom);
		for (uint32_t index = 0; index < channel.PointCount; ++index)
		{
			const LightningPoint& point = shape.Points[channel.FirstPoint + index];
			Require(DistanceToLine(point.Position, top, bottom) <= settings.Jaggedness * length * 1.5f, "The path stays within the band its jaggedness allows");
			if (index > 0)
			{
				Require(point.Arrival > shape.Points[channel.FirstPoint + index - 1].Arrival, "The arrival grows along the path");
			}
		}
		Require(shape.Paths.size() > 1, "The default settings make branches");
		for (size_t index = 1; index < shape.Paths.size(); ++index)
		{
			const LightningPath& branch = shape.Paths[index];
			const LightningPoint& first = shape.Points[branch.FirstPoint];
			const LightningPoint& last = shape.Points[branch.FirstPoint + branch.PointCount - 1];
			Require(branch.Depth >= 1 && branch.Depth <= settings.BranchDepth, "Branches are no deeper than asked");
			Require(last.Position.y < first.Position.y, "Branches head down");
			Require(first.Arrival > 0.0f && last.Arrival > first.Arrival, "A branch's arrival starts where it leaves its parent");
			Require(last.Width < first.Width && branch.Brightness < 1.0f, "Branches taper and are dimmer");
		}

		// The same seed, the same path to the bit; another seed, another
		LightningShape again;
		LightningRandom sameRandom(42);
		MakeLightningPath(top, bottom, settings, sameRandom, 1.0f, 1.0f, again);
		Require(SameShape(shape, again), "The same seed gives the same path to the bit");
		LightningShape other;
		LightningRandom otherRandom(43);
		MakeLightningPath(top, bottom, settings, otherRandom, 1.0f, 1.0f, other);
		Require(!SameShape(shape, other), "Another seed gives another path");

		// Budgets held however wild the values
		LightningPathSettings wild;
		wild.Detail = 10;
		wild.BranchChance = 1.0f;
		wild.BranchDepth = 3;
		wild.BranchSplits = 10;
		LightningShape big;
		LightningRandom bigRandom(7);
		MakeLightningPath(top, bottom, wild, bigRandom, 1.0f, 1.0f, big);
		Require(big.Points.size() <= MaxLightningPoints && big.Paths.size() <= MaxLightningPaths, "The budgets of points and paths are held");

		// The leader reveals by arrival, its tip on the stretch it is on
		LightningShape revealed;
		RevealLightningShape(shape, 0.5f, revealed);
		Require(!revealed.Paths.empty(), "Half way down, something is revealed");
		const LightningPath& half = revealed.Paths[0];
		Require(Near(revealed.Points[half.FirstPoint + half.PointCount - 1].Arrival, 0.5f, 1e-5f), "The main path is revealed to the leader's tip");
		for (const LightningPoint& point : revealed.Points)
		{
			Require(point.Arrival <= 0.5f + 1e-5f, "Nothing past the leader's tip is revealed");
		}
		RevealLightningShape(shape, 1.0f, revealed);
		Require(SameShape(shape, revealed), "Once down, the whole shape is revealed");
	}

	void TestColumn()
	{
		LightningComponent lightning;
		lightning.Mode = ELightningMode::Sustained;
		lightning.SideArcRate = 0.0f;
		lightning.StrayBoltRate = 0.0f;
		lightning.bRepeat = false;
		lightning.StartTime = 0.0f;
		lightning.Duration = 3.0f;
		const LightningComponent clamped = GetClampedLightning(lightning);
		const glm::vec3 base(5.0f, 2.0f, -3.0f);

		Require(GetLightningReformFrame(clamped, 0.0f) == 0 && GetLightningReformFrame(clamped, 1.0f / 12.0f + 1e-4f) == 1 && GetLightningReformFrame(clamped, 1.0f) == 12,
			"A new shape twelve times a second");

		LightningFrame a;
		LightningFrame b;
		LightningFrame c;
		EvaluateLightning(lightning, base, 1.0f + 0.01f, a);
		EvaluateLightning(lightning, base, 1.0f + 0.05f, b);
		EvaluateLightning(lightning, base, 1.0f + 0.1f, c);
		Require(a.bActive && SameShape(a.Shape, b.Shape), "Within a reform frame the shape holds");
		Require(!SameShape(a.Shape, c.Shape), "The next reform frame has another shape");
		Require(a.Shape.Paths.size() >= static_cast<size_t>(clamped.MinFilaments) && a.Shape.Paths.size() <= static_cast<size_t>(clamped.MaxFilaments),
			"As many filaments as asked");
		for (const LightningPath& path : a.Shape.Paths)
		{
			const glm::vec3 foot = a.Shape.Points[path.FirstPoint].Position;
			const glm::vec3 head = a.Shape.Points[path.FirstPoint + path.PointCount - 1].Position;
			Require(glm::distance(glm::vec2(foot.x, foot.z), glm::vec2(base.x, base.z)) <= clamped.ColumnRadius * 0.4f && Near(foot.y, base.y, 1e-3f),
				"Each filament starts drawn in at the column's foot");
			Require(Near(head.y, base.y + clamped.Height, 1e-2f), "Each filament reaches the column's height");
		}

		// Joins: with every level a join, filaments meet at the same points, a blob at each
		LightningComponent joined = lightning;
		joined.Joins = 1.0f;
		joined.MinFilaments = 4;
		joined.MaxFilaments = 4;
		joined.Knots = 0.0f;
		LightningFrame meeting;
		EvaluateLightning(joined, base, 1.01f, meeting);
		size_t shared = 0;
		for (size_t a = 0; a < meeting.Shape.Paths.size(); ++a)
		{
			for (size_t b = a + 1; b < meeting.Shape.Paths.size(); ++b)
			{
				const LightningPath& first = meeting.Shape.Paths[a];
				const LightningPath& second = meeting.Shape.Paths[b];
				for (uint32_t i = 1; i + 1 < first.PointCount; ++i)
				{
					shared += meeting.Shape.Points[first.FirstPoint + i].Position == meeting.Shape.Points[second.FirstPoint + i].Position ? 1 : 0;
				}
			}
		}
		Require(shared > 0 && !meeting.Shape.Sprites.empty(), "At joins the filaments meet, a blob where they do");

		// The glow the most low down: halving every Glow Height up
		LightningComponent even = lightning;
		even.GlowPatchiness = 0.0f;
		LightningFrame rising;
		EvaluateLightning(even, base, 1.01f, rising);
		const LightningPath& strand = rising.Shape.Paths[0];
		const LightningPoint& low = rising.Shape.Points[strand.FirstPoint];
		const LightningPoint& high = rising.Shape.Points[strand.FirstPoint + strand.PointCount - 1];
		Require(Near(low.Glow, 1.0f, 1e-4f) && high.Glow < 0.01f, "The column's glow is strong at its foot and gone high up");
		even.GlowHeight = 0.0f;
		EvaluateLightning(even, base, 1.01f, rising);
		Require(rising.Shape.Points[rising.Shape.Paths[0].FirstPoint + rising.Shape.Paths[0].PointCount - 1].Glow == 1.0f, "With a Glow Height of 0, the same all the way up");

		// Knots: round glows at kinks, wider than the filaments
		LightningComponent knotty = lightning;
		knotty.Knots = 0.5f;
		LightningFrame knots;
		EvaluateLightning(knotty, base, 1.01f, knots);
		Require(!knots.Shape.Sprites.empty() && a.Shape.Sprites.size() < knots.Shape.Sprites.size(), "Knots make round glows");
		for (const LightningSprite& sprite : knots.Shape.Sprites)
		{
			Require(sprite.Size > 0.0f && sprite.Brightness > 0.0f, "A knot has a size and a brightness");
		}

		// Fading in and out at its ends; nothing after it
		const LightningEvent start = FindLightningEvent(clamped, 0.0f);
		Require(GetLightningColumnBrightness(clamped, start, HashLightning(clamped.Seed, 0)) == 0.0f, "The column fades in from nothing");
		Require(a.Brightness >= 0.75f, "Standing, the column is bright");
		LightningFrame ending;
		EvaluateLightning(lightning, base, 2.95f, ending);
		Require(ending.Brightness < a.Brightness * 0.5f, "The column fades out at its end");
		LightningFrame after;
		EvaluateLightning(lightning, base, 3.5f, after);
		Require(!after.bActive && after.Shape.Paths.empty(), "Nothing after the column");

		// Side arcs and stray bolts add paths, budgets held
		lightning.SideArcRate = 24.0f;
		lightning.StrayBoltRate = 4.0f;
		LightningFrame busy;
		bool bStray = false;
		for (float time = 0.2f; time < 2.8f; time += 0.01f)
		{
			EvaluateLightning(lightning, base, time, busy);
			Require(busy.Shape.Points.size() <= MaxLightningPoints && busy.Shape.Paths.size() <= MaxLightningPaths, "A busy column keeps to the budgets");
			for (const LightningPath& path : busy.Shape.Paths)
			{
				const glm::vec3 foot = busy.Shape.Points[path.FirstPoint + path.PointCount - 1].Position;
				if (path.Depth == 0 && glm::distance(glm::vec2(foot.x, foot.z), glm::vec2(base.x, base.z)) > clamped.StrayBoltDistance * 0.5f && Near(foot.y, base.y, 1e-3f))
				{
					bStray = true;
				}
			}
		}
		Require(bStray, "Stray bolts land beside the column");
	}

	void TestEvaluate()
	{
		LightningComponent lightning;
		lightning.StartTime = 0.0f;
		lightning.StrikeRadius = 50.0f;
		const LightningComponent clamped = GetClampedLightning(lightning);
		const glm::vec3 position(100.0f, 0.0f, 100.0f);
		const float start = GetLightningStart(clamped, 2);

		LightningFrame leader;
		EvaluateLightning(lightning, position, start + clamped.LeaderTime * 0.5f, leader);
		Require(leader.bActive && !leader.Channel.has_value() && Near(leader.Brightness, clamped.LeaderBrightness), "While the leader comes down, no channel yet, the leader's brightness");
		Require(glm::distance(glm::vec2(leader.Ground.x, leader.Ground.z), glm::vec2(position.x, position.z)) <= 50.0f + 1e-3f, "A strike lands within its radius");

		LightningFrame flash;
		EvaluateLightning(lightning, position, start + clamped.LeaderTime + 0.001f, flash);
		Require(flash.bActive && flash.Channel.has_value() && flash.Brightness > 0.9f, "At the flash, the channel at full brightness");
		const LightningPath& channel = flash.Shape.Paths[*flash.Channel];
		Require(flash.Shape.Points[channel.FirstPoint + channel.PointCount - 1].Position == flash.Ground, "The channel reaches where it lands");
		Require(flash.Bounds.Min.y <= flash.Ground.y && flash.Bounds.Max.y >= flash.Top.y - clamped.TopSpread, "The box holds the bolt");

		LightningFrame again;
		EvaluateLightning(lightning, position, start + clamped.LeaderTime + 0.001f, again);
		Require(SameShape(flash.Shape, again.Shape), "The same time, the same bolt to the bit");

		LightningFrame next;
		EvaluateLightning(lightning, position, GetLightningStart(clamped, 3) + clamped.LeaderTime + 0.001f, next);
		Require(next.bActive && next.Ground != flash.Ground, "The next strike lands elsewhere");

		LightningComponent off = lightning;
		off.bEnabled = false;
		LightningFrame none;
		EvaluateLightning(off, position, start + clamped.LeaderTime + 0.001f, none);
		Require(!none.bActive && none.Shape.Paths.empty(), "Off, nothing");
		EvaluateLightning(lightning, position, start - 0.01f, none);
		Require(!none.bActive && none.Shape.Paths.empty(), "Between bolts, nothing");
	}

	void TestStrips()
	{
		LightningShape shape;
		for (int index = 0; index < 3; ++index)
		{
			LightningPoint point;
			point.Position = glm::vec3(0.0f, 10.0f * static_cast<float>(index), 0.0f);
			shape.Points.push_back(point);
		}
		shape.Paths.push_back(LightningPath{ .FirstPoint = 0, .PointCount = 3, .Brightness = 1.0f, .Depth = 0 });
		LightningLook look = MakeLightningLook(LightningComponent{});
		look.MinPixelWidth = 0.0f;

		// Near: across the path and the view, the glow's half width either way
		std::vector<LightningVertex> vertices;
		const LightningPixelScale pixels{ .Offset = 0.0f, .Slope = 0.001f };
		BuildLightningStrips(shape, look, glm::vec3(0.0f, 10.0f, 20.0f), pixels, vertices);
		Require(vertices.size() == 12, "Six vertices a stretch");
		// The first point's edge pushed back along the path by the half width, for its round cap
		const glm::vec3 side = vertices[1].Position - shape.Points[0].Position;
		Require(Near(std::abs(side.x), look.HalfWidth, 1e-4f) && Near(side.y, -look.HalfWidth, 1e-4f) && Near(side.z, 0.0f),
			"The strip's side is at right angles to the path and the view, its start pushed back for its cap");
		const glm::vec3 inner = vertices[2].Position - shape.Points[1].Position;
		Require(Near(std::abs(inner.x), look.HalfWidth, 1e-4f) && Near(inner.y, 0.0f) && Near(inner.z, 0.0f), "At a join the strip's side is its half width");
		Require(vertices[0].Start == shape.Points[0].Position && vertices[0].End == shape.Points[1].Position && vertices[0].StartHalfWidth == look.HalfWidth &&
				vertices[0].StartIntensity == 1.0f && vertices[5].End == shape.Points[1].Position,
			"Each quad's vertices carry its stretch, its half widths and its brightness");
		Require(vertices[0].StartGlow == 1.0f && vertices[0].EndGlow == 1.0f && look.Sharpness == GetClampedLightning(LightningComponent{}).Sharpness,
			"And their glow, and the look its edges' sharpness");

		// Far: widened to the core's least pixels, its brightness lowered by the square root
		look.MinPixelWidth = 2.0f;
		vertices.clear();
		const glm::vec3 far(0.0f, 10.0f, 20000.0f);
		BuildLightningStrips(shape, look, far, pixels, vertices);
		const float pixel = 0.001f * glm::distance(far, shape.Points[0].Position);
		const float widen = 0.5f * look.MinPixelWidth * pixel / (look.HalfWidth * look.CoreFraction);
		Require(widen > 1.0f, "The test's far camera makes the core thinner than its least pixels");
		Require(Near(vertices[0].StartHalfWidth, look.HalfWidth * widen, 1e-2f), "Far off, the strip is widened");
		Require(Near(vertices[0].StartIntensity, 1.0f / std::sqrt(widen), 1e-4f), "Widened, it is dimmed by the square root");

		// A round glow: a square facing the camera, its stretch's ends its middle
		LightningShape glow;
		glow.Sprites.push_back(LightningSprite{ .Position = glm::vec3(1.0f, 2.0f, 3.0f), .Size = 2.0f, .Brightness = 0.5f });
		vertices.clear();
		BuildLightningStrips(glow, look, glm::vec3(1.0f, 2.0f, 50.0f), pixels, vertices);
		Require(vertices.size() == 6 && vertices[0].Start == vertices[0].End && vertices[0].Start == glm::vec3(1.0f, 2.0f, 3.0f), "A round glow is one square round its middle");
		Require(Near(vertices[0].StartHalfWidth, look.HalfWidth * 2.0f, 1e-3f) && Near(std::abs(vertices[0].Position.z - 3.0f), 0.0f, 1e-4f),
			"It faces the camera, as wide as its size");

		// A sharp kink: the mitre held to twice the width
		LightningShape kink;
		kink.Points = { LightningPoint{ .Position = glm::vec3(-10.0f, 0.0f, 0.0f) }, LightningPoint{ .Position = glm::vec3(0.0f, 0.0f, 0.0f) },
			LightningPoint{ .Position = glm::vec3(-10.0f, 0.5f, 0.0f) } };
		kink.Paths.push_back(LightningPath{ .FirstPoint = 0, .PointCount = 3, .Brightness = 1.0f, .Depth = 0 });
		look.MinPixelWidth = 0.0f;
		vertices.clear();
		BuildLightningStrips(kink, look, glm::vec3(0.0f, 0.0f, 50.0f), pixels, vertices);
		const float mitre = glm::length(vertices[2].Position - kink.Points[1].Position);
		Require(mitre <= look.HalfWidth * 2.0f + 1e-3f && mitre >= look.HalfWidth - 1e-3f, "At a sharp kink the mitre is held to twice the width");
	}

	void TestLights()
	{
		LightningComponent lightning;
		lightning.StartTime = 0.0f;
		lightning.LightCount = 4;
		const LightningComponent clamped = GetClampedLightning(lightning);
		const glm::vec3 position(0.0f, 5.0f, 0.0f);
		LightningFrame flash;
		EvaluateLightning(lightning, position, GetLightningStart(clamped, 0) + clamped.LeaderTime + 0.001f, flash);

		std::vector<RenderPointLight> lights;
		const Entity entity{};
		AddLightningLights(lightning, flash, entity, lights);
		Require(lights.size() == 4, "As many lights as asked");
		float total = 0.0f;
		for (const RenderPointLight& light : lights)
		{
			Require(!light.bIcon && light.Range == clamped.LightRange, "Its lights have no icon, and its range");
			total += light.Color.g / clamped.LightColor.g;
		}
		Require(Near(total, clamped.LightIntensity * flash.Brightness, 0.5f), "The lights share its intensity times its brightness");
		Require(Near(lights[0].Position.y, flash.Ground.y + 2.0f, 0.1f), "The lowest light is just above the ground");
		Require(lights[3].Position.y > lights[0].Position.y + 50.0f, "The others go up the channel");

		LightningComponent dark = lightning;
		dark.bLights = false;
		lights.clear();
		AddLightningLights(dark, flash, entity, lights);
		Require(lights.empty(), "Lights off, none");
		LightningFrame none;
		AddLightningLights(lightning, none, entity, lights);
		Require(lights.empty(), "No bolt, no lights");
	}

	void TestCloudLights()
	{
		LightningComponent lightning;
		lightning.StartTime = 0.0f;
		const LightningComponent clamped = GetClampedLightning(lightning);
		LightningFrame flash;
		EvaluateLightning(lightning, glm::vec3(0.0f), GetLightningStart(clamped, 0) + clamped.LeaderTime + 0.001f, flash);
		const std::optional<LightningCloudLight> line = MakeLightningCloudLight(lightning, flash);
		Require(line.has_value(), "A flash lights the clouds");
		const LightningPath& channel = flash.Shape.Paths[*flash.Channel];
		Require(glm::distance(line->Points.front(), flash.Shape.Points[channel.FirstPoint].Position) < 1e-3f && glm::distance(line->Points.back(), flash.Ground) < 1e-3f,
			"The line of light runs down the channel from its top to the ground");
		Require(glm::distance(line->Color, clamped.CloudLightColor * (clamped.CloudLightIntensity * flash.Brightness)) < 1e-3f && line->Reach == clamped.CloudLightReach,
			"The line of light is as bright as asked, as far as asked");
		LightningComponent off = lightning;
		off.bCloudLight = false;
		Require(!MakeLightningCloudLight(off, flash).has_value() && !MakeLightningCloudLight(lightning, LightningFrame{}).has_value(), "Off, or nothing on, no line of light");

		// A column's axis
		LightningComponent column = lightning;
		column.Mode = ELightningMode::Sustained;
		column.Duration = 0.0f;
		column.bRepeat = false;
		LightningFrame standing;
		EvaluateLightning(column, glm::vec3(5.0f, 0.0f, 5.0f), 1.0f, standing);
		const std::optional<LightningCloudLight> axis = MakeLightningCloudLight(column, standing);
		Require(axis.has_value() && axis->Points.front() == standing.Top && axis->Points.back() == standing.Ground, "A column's line of light is its axis");

		// The brightest kept
		std::vector<RenderLightningBolt> bolts(6);
		for (size_t index = 0; index < bolts.size(); ++index)
		{
			LightningCloudLight light;
			light.Color = glm::vec3(static_cast<float>((index * 7) % 6) + 1.0f);
			light.Reach = 100.0f;
			bolts[index].CloudLight = light;
		}
		std::vector<LightningCloudLight> picked;
		PickLightningCloudLights(bolts, picked);
		Require(picked.size() == MaxLightningCloudLights && picked[0].Color.r == 6.0f && picked[3].Color.r == 3.0f, "The brightest lines of light are kept, the brightest first");

		// The rectangle they show on
		const glm::mat4 viewProjection = glm::perspective(glm::radians(60.0f), 16.0f / 9.0f, 1.0f, 100000.0f) * glm::lookAt(glm::vec3(0.0f), glm::vec3(0.0f, 0.0f, -1.0f), glm::vec3(0.0f, 1.0f, 0.0f));
		LightningCloudLight ahead;
		for (uint32_t index = 0; index < LightningCloudLightPoints; ++index)
		{
			ahead.Points[index] = glm::vec3(0.0f, 1000.0f - 300.0f * static_cast<float>(index), -5000.0f);
		}
		ahead.Reach = 300.0f;
		const LightningRect rect = GetLightningCloudRect(std::span<const LightningCloudLight>(&ahead, 1), viewProjection, glm::uvec2(960, 540));
		Require(!rect.IsEmpty() && rect.X0 > 300 && rect.X1 < 660 && rect.X0 >= 0 && rect.Y1 <= 540, "A line of light ahead shows on a part of the picture round it");
		LightningCloudLight behind = ahead;
		behind.Points[3] = glm::vec3(0.0f, 0.0f, 100.0f);
		const LightningRect whole = GetLightningCloudRect(std::span<const LightningCloudLight>(&behind, 1), viewProjection, glm::uvec2(960, 540));
		Require(whole.X0 == 0 && whole.Y0 == 0 && whole.X1 == 960 && whole.Y1 == 540, "A line of light reaching behind the camera may show anywhere");
		Require(GetLightningCloudRect({}, viewProjection, glm::uvec2(960, 540)).IsEmpty(), "No lines of light, nothing");
	}

	void TestWorld()
	{
		Registry world;
		const Entity striking = world.CreateEntity();
		world.Add<TransformComponent>(striking, TransformComponent{ .Position = glm::vec3(1.0f, 2.0f, 3.0f) });
		LightningComponent lightning;
		lightning.StartTime = 0.0f;
		world.Add<LightningComponent>(striking, lightning);
		const Entity quiet = world.CreateEntity();
		world.Add<TransformComponent>(quiet, TransformComponent{});
		LightningComponent off;
		off.bEnabled = false;
		world.Add<LightningComponent>(quiet, off);

		std::vector<RenderLightningBolt> bolts;
		std::vector<RenderPointLight> lights(1);
		const float time = GetLightningStart(GetClampedLightning(lightning), 0) + 0.2f;
		ExtractLightning(world, time, std::nullopt, bolts, lights);
		Require(bolts.size() == 2, "Every lightning entity is listed, for its icon");
		Require(bolts[0].SourceEntity == striking && bolts[0].bDrawn && !bolts[0].Shape.Paths.empty() && bolts[0].WorldPosition == glm::vec3(1.0f, 2.0f, 3.0f),
			"The striking one has its bolt");
		Require(!bolts[1].bDrawn && bolts[1].Shape.Paths.empty(), "The one that is off has none");
		Require(lights.size() == 1 + static_cast<size_t>(lightning.LightCount), "Its lights are appended after the others");
		Require(bolts[0].Bounds.Max.x - bolts[0].Bounds.Min.x > bolts[0].Look.HalfWidth, "Its box takes in the glow");
	}

	void TestSavedAndLoaded()
	{
		Nyx::Tests::TestDirectory directory("NyxLightningTests");
		const std::filesystem::path scenePath = directory.Path / "Lightning.nyxscene";
		Registry world;
		LightningComponent lightning;
		lightning.Mode = ELightningMode::Sustained;
		lightning.StartTime = 12.5f;
		lightning.bRepeat = false;
		lightning.Seed = 99;
		lightning.MaxFilaments = 5;
		lightning.CoreColor = glm::vec3(1.0f, 0.9f, 0.7f);
		lightning.LightIntensity = 1234.0f;
		world.Add<LightningComponent>(world.CreateEntity(), lightning);
		Require(SceneSerializer::SaveToFile(world, scenePath), "Saving a scene with lightning failed");

		Registry loaded;
		Require(SceneSerializer::LoadFromFile(scenePath, loaded, ScenePostLoadContext{}), "Loading a scene with lightning failed");
		std::optional<LightningComponent> read;
		loaded.Each<LightningComponent>([&](Entity, const LightningComponent& component)
			{
				read = component;
			});
		Require(read.has_value(), "The loaded scene has no lightning");
		Require(read->Mode == ELightningMode::Sustained && read->StartTime == 12.5f && !read->bRepeat && read->Seed == 99 && read->MaxFilaments == 5 &&
				read->CoreColor == lightning.CoreColor && read->LightIntensity == 1234.0f,
			"The lightning's values didn't load as saved");
	}
}

int main()
{
	try
	{
		Nyx::Logger::Get().Init();
		RegisterComponentTypes();
		TestSchedule();
		TestStrikeBrightness();
		TestPaths();
		TestColumn();
		TestEvaluate();
		TestStrips();
		TestLights();
		TestCloudLights();
		TestWorld();
		TestSavedAndLoaded();
	}
	catch (const std::exception& error)
	{
		std::cerr << "FAILED: " << error.what() << "\n";
		return 1;
	}

	std::cout << "All lightning tests passed\n";
	return 0;
}
