#pragma once

#include "Entity.h"
#include "LightningComponent.h"
#include "PointLights.h"
#include "SceneCulling.h"

#include <glm/glm.hpp>

#include <array>
#include <cstdint>
#include <optional>
#include <vector>

namespace Nyx
{
	class Registry;

	// Lightning (LightningComponent) as plain math (Docs/Design/lightning.md): when a strike or a
	// column is on, how bright it is, its paths, its strips as a view draws them and its lights.
	// Nothing is kept from frame to frame: what shows is worked out from the component, its entity's
	// position and the world's time alone. No GPU, so tests check it as the renderer
	// (VulkanLightning) and Lightning.slang use it.

	// How big a lightning entity's icon in the editor's scene views is, in metres round it, which a
	// click picks it by
	inline constexpr float LightningIconRadius = 0.3f;

	// The most points and paths one bolt has (a column's filaments, arcs and stray bolts together):
	// past them no more branches, arcs or filaments are made
	inline constexpr uint32_t MaxLightningPoints = 8192;
	inline constexpr uint32_t MaxLightningPaths = 128;

	// How long a column fades in and out, in seconds
	inline constexpr float LightningColumnFadeIn = 0.15f;
	inline constexpr float LightningColumnFadeOut = 0.3f;

	// The most return strokes a strike has
	inline constexpr int32_t MaxLightningStrokes = 10;

	// A stray bolt beside a column: a short strike of its own
	inline constexpr float LightningStrayLeaderTime = 0.04f;
	inline constexpr float LightningStrayFadeTime = 0.12f;

	// The component's values held in their ranges, as everything here reads them
	LightningComponent GetClampedLightning(const LightningComponent& lightning);

	// Random numbers for one bolt: PCG's output on a linear congruential state, from a seed
	struct LightningRandom
	{
		uint64_t State = 0;

		explicit LightningRandom(uint32_t seed);
		uint32_t Next();
		// 0 to 1, never 1
		float Unit();
		// -1 to 1
		float Signed();
		float Range(float low, float high);
		// A whole number from `low` to `high`, both included
		int32_t Between(int32_t low, int32_t high);
	};

	// A seed for a part of a bolt, from its entity's seed and the numbers that tell the parts apart
	// (the slot, the reform frame, which arc): the same numbers give the same seed
	uint32_t HashLightning(uint32_t a, uint32_t b, uint32_t c = 0, uint32_t d = 0);

	// A bolt on at a time: its slot (from Start Time, one Interval each), when it started, how long
	// it has been on and how long it lasts (the longest a strike of these values can last; a column's
	// Duration, or as long as its interval)
	struct LightningEvent
	{
		bool bActive = false;
		uint32_t Slot = 0;
		float Start = 0.0f;
		float Age = 0.0f;
		float Length = 0.0f;
	};

	// How long a bolt of these values lasts at most, in seconds: a strike's leader, its strokes at
	// their furthest apart, the last one's decay and its afterglow; a column's Duration (with Repeat,
	// never past its interval; 0 for ever)
	float GetLightningLength(const LightningComponent& lightning);

	// When slot `slot`'s bolt starts: Start Time + slot · Interval, moved by up to Interval Jitter of
	// the room the interval leaves after the bolt, at random by the seed and the slot; without Repeat,
	// Start Time
	float GetLightningStart(const LightningComponent& lightning, uint32_t slot);

	// The bolt on at the world's time `time`, if any: a division and a hash, however late
	LightningEvent FindLightningEvent(const LightningComponent& lightning, float time);

	// A strike's flashes: when each comes down (seconds from the strike's start; the first when the
	// leader reaches the ground) and how bright it is at its peak (1 for the first), and when its
	// afterglow is gone
	struct LightningStrokes
	{
		int32_t Count = 0;
		std::array<float, MaxLightningStrokes> Times{};
		std::array<float, MaxLightningStrokes> Peaks{};
		float End = 0.0f;
	};

	// The flashes of a strike from `seed`, with its leader taking `leaderTime` and its afterglow
	// `fadeTime`
	LightningStrokes MakeLightningStrokes(const LightningComponent& lightning, uint32_t seed, float leaderTime, float fadeTime);

	// How bright a strike's main channel is `age` seconds after it started, against a flash's peak:
	// the leader's while it comes down, then the flashes' sum, each dying as exp(-t / Stroke Decay),
	// with the channel's afterglow after them fading to 0 by their End; 0 after
	float GetLightningStrikeBrightness(const LightningComponent& lightning, const LightningStrokes& strokes, float leaderTime, float fadeTime, float age);
	// How bright its branches are: with the first flash only, dying with it (the later strokes come
	// down the main channel alone), and the leader's while it comes down
	float GetLightningBranchBrightness(const LightningComponent& lightning, const LightningStrokes& strokes, float leaderTime, float age);
	// How far down the leader has come, 0 to 1 of the main channel; 1 from the first flash on
	float GetLightningLeaderProgress(float leaderTime, float age);

	// How bright a column is `age` seconds after it started: fading in and out at its ends (none
	// with a Duration of 0 at the end), times its reform frame's flicker
	float GetLightningColumnBrightness(const LightningComponent& lightning, const LightningEvent& event, uint32_t eventSeed);

	// A column's reform frame at `age`: a new shape Reform Rate times a second
	uint32_t GetLightningReformFrame(const LightningComponent& lightning, float age);

	// A point of a bolt's path: where it is, its width against the bolt's Width (tapering along a
	// branch, wider at a knot), and its arrival: how far down the leader has come when it gets there,
	// as a part of the main channel's length
	struct LightningPoint
	{
		glm::vec3 Position{ 0.0f };
		float Width = 1.0f;
		float Arrival = 0.0f;
		// How much of the glow it has, 0 to 1 (a column's, most low down and in patches)
		float Glow = 1.0f;
	};

	// A path of a bolt: PointCount points from FirstPoint on in its shape's points, how bright it is
	// against the bolt's Brightness, how deep a branch it is (0 the main channel or a filament)
	struct LightningPath
	{
		uint32_t FirstPoint = 0;
		uint32_t PointCount = 0;
		float Brightness = 1.0f;
		uint32_t Depth = 0;
	};

	// A round glow of a bolt, drawn as its strips are, from its middle out: a column's knot, the
	// glow at its foot. Its size against the bolt's Width, its brightness against its Brightness.
	struct LightningSprite
	{
		glm::vec3 Position{ 0.0f };
		float Size = 1.0f;
		float Brightness = 1.0f;
		float Glow = 1.0f;
	};

	// A bolt's paths and round glows at a moment
	struct LightningShape
	{
		std::vector<LightningPoint> Points;
		std::vector<LightningPath> Paths;
		std::vector<LightningSprite> Sprites;

		void Clear()
		{
			Points.clear();
			Paths.clear();
			Sprites.clear();
		}
	};

	// How a path is split and branches (MakeLightningPath())
	struct LightningPathSettings
	{
		float Jaggedness = 0.22f;
		uint32_t Detail = 7;
		float BranchChance = 0.3f;
		float BranchLength = 0.35f;
		uint32_t BranchDepth = 2;
		// A branch's brightness against its parent's
		float BranchBrightness = 0.45f;
		// The branches start only at the first splits (the coarse ones), so they are long
		uint32_t BranchSplits = 4;
	};

	// A path from `top` to `bottom` into `shape`, then its branches: the straight line split
	// Detail times, each split moving a stretch's middle across it, in a random direction at right
	// angles to it, by up to Jaggedness times its length, the ends kept; at the middles of the first
	// splits, branches by Branch Chance (halving at each depth) heading down and away from the
	// stretch, Branch Length of what is left below, split as often as is left, up to Branch Depth
	// deep, while the budgets allow. The main path's arrival goes from `arrivalStart` at the top by
	// its length times `arrivalScale` (1 / the main channel's length); its brightness `brightness`,
	// its widths `width` tapering to 0.3 of it along a branch. Returns the main path's index, or
	// none if the budget was spent.
	std::optional<uint32_t> MakeLightningPath(const glm::vec3& top, const glm::vec3& bottom, const LightningPathSettings& settings, LightningRandom& random, float brightness,
		float width, LightningShape& shape, float arrivalStart = 0.0f, float arrivalScale = -1.0f, uint32_t depth = 0);

	// The part of `shape` the leader has reached, `progress` of the main channel down, into `out`
	// (cleared first): each path's points that have arrived, and one where the leader's tip is on the
	// stretch it is on
	void RevealLightningShape(const LightningShape& shape, float progress, LightningShape& out);

	// A column's paths at one reform frame, standing on `base`: its filaments braided round the axis
	// up to Height, knots (round glows at a few of their kinks), the side arcs whipping out in this frame and the one before; `seed` the
	// event's, `brightness` the column's at the moment (each arc's against it)
	void MakeLightningColumn(const LightningComponent& lightning, const glm::vec3& base, uint32_t seed, uint32_t frame, float brightness, LightningShape& shape);

	// A bolt as a frame draws it, worked out from the component at a moment: its shape, where it
	// lands and starts, how bright its main channel is against a flash's peak (for its lights), and
	// its box (the points, without the glow)
	struct LightningFrame
	{
		bool bActive = false;
		LightningShape Shape;
		glm::vec3 Ground{ 0.0f };
		glm::vec3 Top{ 0.0f };
		float Brightness = 0.0f;
		// The main channel's path in Shape, for its lights; none while only the leader is down
		std::optional<uint32_t> Channel;
		BoundingBox Bounds;
	};

	// What `lightning` on an entity at `position` shows at the world's time `time`, into `frame`:
	// nothing while it is off or between bolts; else the strike's or the column's paths, each with its
	// brightness at the moment (a strike's revealed down to its leader's tip; a column's stray bolts
	// with it)
	void EvaluateLightning(const LightningComponent& lightning, const glm::vec3& position, float time, LightningFrame& frame);

	// A frame's lights: Light Count point lights along the main channel's lowest stretch (a column's
	// axis), the lowest at the ground, sharing Light Intensity times its brightness, each of Light
	// Range, into `outLights` (appended), with no icon; none while it is off or dark
	void AddLightningLights(const LightningComponent& lightning, const LightningFrame& frame, Entity entity, std::vector<RenderPointLight>& outLights);

	// How a bolt's strips look, as its draw's push constants give them to Lightning.slang: each
	// colour times its brightness, and its core's and body's half widths against the glow's
	struct LightningLook
	{
		glm::vec3 Core{ 0.0f };
		glm::vec3 Body{ 0.0f };
		glm::vec3 Glow{ 0.0f };
		float CoreFraction = 0.07f;
		float BodyFraction = 0.2f;
		// The glow's half width at a point of width 1, in metres, and the core's least width in pixels
		float HalfWidth = 3.0f;
		float MinPixelWidth = 1.5f;
		// How hard the core's and the body's edges are, 0 (soft peaks) to 1 (a step, as drawn by hand)
		float Sharpness = 0.8f;
		// Over how many metres it fades in front of what is behind it
		float SoftDistance = 0.5f;
	};
	LightningLook MakeLightningLook(const LightningComponent& lightning);

	// One vertex of a bolt's strips, as Lightning.slang reads it (LightningVertex): where it is, and
	// the stretch of path its quad draws: its ends, the glow's half width and the brightness at each
	// (the path's, lowered where it was widened). Each pixel measures its distance to that stretch,
	// so the strip is round at its joins and ends however the quads are skewed; a round sprite is a
	// stretch whose ends are one point.
	struct LightningVertex
	{
		glm::vec3 Position{ 0.0f };
		float Padding0 = 0.0f;
		glm::vec3 Start{ 0.0f };
		float StartHalfWidth = 0.0f;
		glm::vec3 End{ 0.0f };
		float EndHalfWidth = 0.0f;
		float StartIntensity = 0.0f;
		float EndIntensity = 0.0f;
		// How much of the glow it has at each end
		float StartGlow = 1.0f;
		float EndGlow = 1.0f;
	};
	static_assert(sizeof(LightningVertex) == 64, "LightningVertex must match LightningVertex in Lightning.slang");

	// How big a pixel is at a distance from a camera, in metres: `Offset` + `Slope` · distance (a
	// perspective camera's slope is the angle a pixel covers; an orthographic one's offset its size)
	struct LightningPixelScale
	{
		float Offset = 0.0f;
		float Slope = 0.001f;
	};

	// The pixel scale of a view from its projection and its height in pixels
	LightningPixelScale GetLightningPixelScale(const glm::mat4& projection, uint32_t height);

	// A shape's paths as strips facing a camera at `camera`, into `outVertices` (appended), six
	// vertices a stretch (and a quad facing the camera for each round glow): at each point the strip's side at right angles to the path and to the way to
	// the camera, mitred at the joins (the mitre held to twice the width), its edges at plus and minus
	// the glow's half width times the point's width, and its ends pushed out by that half width so
	// they are round; widened where the core would be thinner than the look's least pixels, its
	// brightness lowered by the square root of the widening
	void BuildLightningStrips(const LightningShape& shape, const LightningLook& look, const glm::vec3& camera, const LightningPixelScale& pixels,
		std::vector<LightningVertex>& outVertices);

	// A world's lightning entity as a frame draws it: its icon's place, and its bolt if one is on
	struct RenderLightningBolt
	{
		Nyx::Entity SourceEntity{};
		// Where its entity is, for picking and the editor's overlays, and its reach for them
		glm::vec3 WorldPosition{ 0.0f };
		float StrikeRadius = 0.0f;
		float Height = 0.0f;
		bool bDrawn = false;
		LightningShape Shape;
		LightningLook Look;
		// The box round its strips at their widest, without the widening for far views
		BoundingBox Bounds;
		// Its entity's picking id in its world; 0 until given one
		uint32_t PickingId = 0;
	};

	// The world's lightning at its time `time`, into `outBolts` (cleared first), in the order the world
	// holds them, each placed where its entity is drawn (with `interpolation`, between its last two
	// steps), every entity listed, also one with no bolt on (for the editor's icons); their lights
	// appended to `outLights`
	void ExtractLightning(const Registry& world, float time, std::optional<float> interpolation, std::vector<RenderLightningBolt>& outBolts,
		std::vector<RenderPointLight>& outLights);
}
