#pragma once

#include "ReflectionMacros.h"

#include <glm/glm.hpp>

#include <cstdint>

namespace Nyx
{
	// What a lightning entity's bolts are. Saved as its number: new modes go at the end.
	NYX_ENUM()
	enum class ELightningMode : uint8_t
	{
		// A short strike: a faint leader down to the ground, a flash down the channel it made, a few
		// more flashes down the same channel, then its afterglow fading
		Strike,
		// A column standing for a while: a braid of filaments twisting round one vertical axis from
		// the sky to the ground, re-formed many times a second, with side arcs and stray bolts
		Sustained,
	};

	// Bolts of light between the sky and the ground (Docs/Design/lightning.md): strikes over the
	// entity, landing anywhere within Strike Radius of it, or a sustained column standing on it.
	// Drawn as glowing strips past the bloom's threshold, lighting the world through a few point
	// lights along them. Only for the eye: the renderer works out what shows at the world's time
	// from these values alone (EvaluateLightning()), so the same seed and time give the same bolt,
	// it stands still while a game is paused, and nothing in the world feels it. The entity's
	// position is where the bolts reach the ground; its rotation and scale change nothing.
	NYX_REFLECT(Component, meta = (DisplayName = "Lightning"))
	struct LightningComponent
	{
		NYX_PROPERTY(Edit, Undo, Serialize, meta = (Category = "Timing", DisplayName = "Lightning", Tooltip = "Whether its bolts are drawn"))
		bool bEnabled = true;

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (Category = "Timing", Tooltip = "Strike: a short, flickering strike. Sustained: a braided column standing for Duration"))
		ELightningMode Mode = ELightningMode::Strike;

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (Category = "Timing", DisplayName = "Start Time", DragSpeed = 0.1, Tooltip = "The world's time of the first bolt, in seconds. A game strikes now by setting it to its world's time"))
		float StartTime = 0.0f;

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (Category = "Timing", DisplayName = "Repeat", Tooltip = "Whether it strikes again every Interval; else once, at Start Time"))
		bool bRepeat = true;

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (Category = "Timing", DragSpeed = 0.1, Tooltip = "Seconds from one bolt's start to the next's, on average, 0.5 to 3600"))
		float Interval = 5.0f;

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (Category = "Timing", DisplayName = "Interval Jitter", DragSpeed = 0.005, Tooltip = "How much each start is moved at random, 0 to 1: 0 like a clock, 1 anywhere in its interval"))
		float IntervalJitter = 0.7f;

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (Category = "Timing", DragSpeed = 0.05, Tooltip = "Sustained: how long the column stands, in seconds; 0 for as long as it is on"))
		float Duration = 3.0f;

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (Category = "Timing", Tooltip = "Where its randomness starts: the same seed at the same world time gives the same bolt"))
		uint32_t Seed = 1;

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (Category = "Shape", DragSpeed = 5.0, Tooltip = "How high above the entity the bolts start, in metres, 10 to 20 000: in the clouds"))
		float Height = 1200.0f;

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (Category = "Shape", DisplayName = "Strike Radius", DragSpeed = 1.0, Tooltip = "Strike: each lands anywhere within this many metres of the entity; 0 always on it"))
		float StrikeRadius = 0.0f;

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (Category = "Shape", DisplayName = "Top Spread", DragSpeed = 1.0, Tooltip = "Strike: how far from above where it lands it starts, in metres, at random"))
		float TopSpread = 150.0f;

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (Category = "Shape", DragSpeed = 0.005, Tooltip = "How far each split moves a stretch's middle across it, against its length, 0 to 1: 0 straight, 0.3 jagged, 1 wild"))
		float Jaggedness = 0.22f;

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (Category = "Shape", Tooltip = "How many times each stretch is split in two, 1 to 10: more, finer kinks"))
		uint32_t Detail = 7;

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (Category = "Shape", DisplayName = "Branch Chance", DragSpeed = 0.005, Tooltip = "Strike: the chance a branch starts at a split, 0 to 1, halving at each depth"))
		float BranchChance = 0.3f;

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (Category = "Shape", DisplayName = "Branch Length", DragSpeed = 0.005, Tooltip = "Strike: how long a branch is, against the rest of the bolt below where it starts, 0 to 1"))
		float BranchLength = 0.35f;

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (Category = "Shape", DisplayName = "Branch Depth", Tooltip = "Strike: how many times branches branch again, 0 to 3; 0 none"))
		uint32_t BranchDepth = 2;

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (Category = "Strike", DisplayName = "Leader Time", DragSpeed = 0.005, Tooltip = "How long the faint leader takes to come down to the ground, in seconds"))
		float LeaderTime = 0.12f;

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (Category = "Strike", DisplayName = "Min Strokes", Tooltip = "The fewest flashes down the channel, 1 to 10: the first and those after it"))
		int32_t MinStrokes = 2;

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (Category = "Strike", DisplayName = "Max Strokes", Tooltip = "The most flashes down the channel, 1 to 10"))
		int32_t MaxStrokes = 4;

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (Category = "Strike", DisplayName = "Stroke Interval", DragSpeed = 0.005, Tooltip = "Seconds between flashes, on average: from half to one and a half of it"))
		float StrokeInterval = 0.08f;

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (Category = "Strike", DisplayName = "Stroke Decay", DragSpeed = 0.002, Tooltip = "How fast a flash dies away, in seconds: in this long it is down to about a third"))
		float StrokeDecay = 0.045f;

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (Category = "Strike", DisplayName = "Fade Time", DragSpeed = 0.01, Tooltip = "How long the channel glows on after the last flash, in seconds"))
		float FadeTime = 0.3f;

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (Category = "Column", DisplayName = "Reform Rate", DragSpeed = 0.1, Tooltip = "How many times a second the column takes a new shape, 1 to 60: 12 is a drawn animation's on twos, which boils rather than flickers"))
		float ReformRate = 12.0f;

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (Category = "Column", DisplayName = "Min Filaments", Tooltip = "The fewest filaments braided round its axis, 1 to 12"))
		int32_t MinFilaments = 3;

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (Category = "Column", DisplayName = "Max Filaments", Tooltip = "The most filaments braided round its axis, 1 to 12"))
		int32_t MaxFilaments = 6;

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (Category = "Column", DisplayName = "Column Radius", DragSpeed = 0.1, Tooltip = "How far from the axis the filaments wander, in metres: the braid is twice as wide"))
		float ColumnRadius = 16.0f;

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (Category = "Column", DragSpeed = 0.01, Tooltip = "How many times the filaments wind round the axis every 100 m up"))
		float Twist = 0.3f;

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (Category = "Column", DisplayName = "Column Kinks", Tooltip = "How many times each stroke of a filament is split for its kinks, 0 to 3: few for big, angular strokes"))
		uint32_t ColumnKinks = 1;

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (Category = "Column", DisplayName = "Column Jaggedness", DragSpeed = 0.005, Tooltip = "How far each split moves a stroke's middle across it, against its length, 0 to 1"))
		float ColumnJaggedness = 0.3f;

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (Category = "Column", DragSpeed = 0.005, Tooltip = "The chance, at each of the column's levels, that some of its filaments meet at one point and split apart again after, 0 to 1: open loops between the joins, a blob at each"))
		float Joins = 0.35f;

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (Category = "Column", DisplayName = "Filament Width", DragSpeed = 0.05, Tooltip = "How thick a filament is, against the Width (a strike's channel), 0.1 to 10: thick, chunky strokes"))
		float FilamentWidth = 1.2f;

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (Category = "Column", DisplayName = "Width Variation", DragSpeed = 0.005, Tooltip = "How much a filament goes from thick to thin along its length, 0 to 1"))
		float WidthVariation = 0.75f;

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (Category = "Column", DragSpeed = 0.005, Tooltip = "The chance a filament's kink swells into a blob, 0 to 1"))
		float Knots = 0.06f;

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (Category = "Column", DisplayName = "Knot Size", DragSpeed = 0.05, Tooltip = "How much wider a knot or a join is than the filament, 1 to 10"))
		float KnotSize = 2.5f;

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (Category = "Column", DisplayName = "Glow Height", DragSpeed = 1.0, Tooltip = "How high up the column the filaments' glow halves, in metres, so it is warm and strong low down; 0 the same all the way up"))
		float GlowHeight = 60.0f;

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (Category = "Column", DisplayName = "Glow Patchiness", DragSpeed = 0.005, Tooltip = "How much the glow comes and goes along a filament, in patches, 0 to 1"))
		float GlowPatchiness = 0.6f;

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (Category = "Column", DisplayName = "Side Arc Rate", DragSpeed = 0.05, Tooltip = "How many side arcs whip outward from the column a second, on average; 0 none"))
		float SideArcRate = 1.5f;

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (Category = "Column", DisplayName = "Side Arc Length", DragSpeed = 0.5, Tooltip = "How far a side arc reaches from the column, in metres"))
		float SideArcLength = 20.0f;

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (Category = "Column", DisplayName = "Stray Bolt Rate", DragSpeed = 0.05, Tooltip = "How many lone bolts strike beside the column a second, on average; 0 none"))
		float StrayBoltRate = 0.5f;

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (Category = "Column", DisplayName = "Stray Bolt Distance", DragSpeed = 0.5, Tooltip = "How far from the column a lone bolt lands, in metres, on average"))
		float StrayBoltDistance = 35.0f;

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (Category = "Look", DisplayName = "Core Color", UI = Color, Tooltip = "The colour of the bolt's narrow, hottest middle"))
		glm::vec3 CoreColor{ 1.0f, 0.95f, 0.8f };

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (Category = "Look", DisplayName = "Body Color", UI = Color, Tooltip = "The colour of the bolt round its core"))
		glm::vec3 BodyColor{ 1.0f, 0.5f, 0.08f };

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (Category = "Look", DisplayName = "Glow Color", UI = Color, Tooltip = "The colour of the glow round the bolt, falling off to its edges"))
		glm::vec3 GlowColor{ 0.9f, 0.15f, 0.03f };

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (Category = "Look", DragSpeed = 0.1, Tooltip = "How bright the core is at a flash's peak, as the sun's light is measured: past 1 it shows white and blooms"))
		float Brightness = 4.0f;

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (Category = "Look", DisplayName = "Body Brightness", DragSpeed = 0.005, Tooltip = "How bright the body is, against the core, 0 to 1: kept near 1 after the exposure, its colour shows rather than going white"))
		float BodyBrightness = 0.2f;

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (Category = "Look", DisplayName = "Glow Strength", DragSpeed = 0.005, Tooltip = "How bright the glow is, against the core, 0 to 1"))
		float GlowStrength = 0.3f;

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (Category = "Look", DragSpeed = 0.01, Tooltip = "How wide the bolt's body is, in metres"))
		float Width = 2.5f;

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (Category = "Look", DisplayName = "Core Width", DragSpeed = 0.005, Tooltip = "How wide the core is, against the body, 0.05 to 1"))
		float CoreWidth = 0.18f;

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (Category = "Look", DisplayName = "Glow Width", DragSpeed = 0.05, Tooltip = "How wide the glow is, against the body, 1 to 20"))
		float GlowWidth = 4.0f;

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (Category = "Look", DragSpeed = 0.005, Tooltip = "How hard the edges of the core and the body are, 0 to 1: 0 soft, 1 a clean step as drawn by hand, the glow soft outside it"))
		float Sharpness = 0.85f;

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (Category = "Look", DisplayName = "Min Pixel Width", DragSpeed = 0.05, Tooltip = "How many pixels across the core is at the least, however far: a far bolt widened to it, and dimmed a little, so it doesn't flicker in and out"))
		float MinPixelWidth = 2.0f;

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (Category = "Look", DisplayName = "Branch Brightness", DragSpeed = 0.005, Tooltip = "How bright the branches, side arcs and stray bolts are, against the main channel, 0 to 1"))
		float BranchBrightness = 0.45f;

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (Category = "Look", DisplayName = "Leader Brightness", DragSpeed = 0.005, Tooltip = "Strike: how bright the leader is on its way down, against a flash, 0 to 1"))
		float LeaderBrightness = 0.12f;

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (Category = "Look", DisplayName = "Cloud Hiding", DragSpeed = 0.05, Tooltip = "How soon the clouds before the bolt hide its line, 1 to 16: 1 as much as they hide what is behind them, higher sooner, so the line goes into the cloud and its glow shows instead"))
		float CloudHiding = 4.0f;

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (Category = "Light", DisplayName = "Lights", Tooltip = "Whether the bolts light the world round them, through point lights along their lowest stretch"))
		bool bLights = true;

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (Category = "Light", DisplayName = "Light Count", Tooltip = "How many point lights a bolt lights the world through, 1 to 8: more follow its shape better and cost more"))
		int32_t LightCount = 3;

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (Category = "Light", DisplayName = "Light Color", UI = Color))
		glm::vec3 LightColor{ 1.0f, 0.65f, 0.3f };

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (Category = "Light", DisplayName = "Light Intensity", DragSpeed = 10.0, Tooltip = "How bright its lights are together at a flash's peak, as point lights are measured: what a white matte surface 1 m away shows"))
		float LightIntensity = 4000.0f;

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (Category = "Light", DisplayName = "Light Range", DragSpeed = 1.0, Tooltip = "How far each light reaches, in metres: shorter is cheaper"))
		float LightRange = 250.0f;

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (Category = "Cloud Light", DisplayName = "Cloud Light", Tooltip = "Whether the bolt lights the volumetric clouds round it from within, as a line of light along it"))
		bool bCloudLight = true;

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (Category = "Cloud Light", DisplayName = "Cloud Light Color", UI = Color))
		glm::vec3 CloudLightColor{ 1.0f, 0.6f, 0.25f };

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (Category = "Cloud Light", DisplayName = "Cloud Light Intensity", DragSpeed = 0.5, Tooltip = "How brightly the clouds glow right by the bolt at a flash's peak, as the sun's light is measured; less further away"))
		float CloudLightIntensity = 15.0f;

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (Category = "Cloud Light", DisplayName = "Cloud Light Reach", DragSpeed = 5.0, Tooltip = "How far from the bolt the clouds glow, in metres: none beyond. Shorter is cheaper"))
		float CloudLightReach = 800.0f;
	};
}

#include "Generated/Runtime/LightningComponent.reflect.h"
