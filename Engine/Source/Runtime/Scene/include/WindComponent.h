#pragma once

#include "Entity.h"
#include "ReflectionMacros.h"
#include "Wind.h"

#include <glm/glm.hpp>

#include <cstdint>
#include <optional>

namespace Nyx
{
	// What the wind's debug view draws over the world round each view's camera
	NYX_ENUM()
	enum class EWindDebug : uint8_t
	{
		Off,
		// An arrow at each point of a grid on the ground, along the wind and as long as it is fast,
		// warmer the stronger the gust there
		Arrows,
		// The arrows, and the crest of each gust front passing, a line across the wind
		ArrowsAndGustFronts
	};

	// The world's prevailing wind (Docs/Design/wind.md): the one wind grass, trees, particles, clouds
	// and a game's own air all take, as a WindState (Wind.h) made from these values. They are the
	// wind's targets: set them (in the editor, by a live edit, or from a game's or a weather
	// system's code) and the wind turns to them over Change Time, its gusts and eddies carried on
	// without a jump (WindDrive below). The world's first such component counts; without one, the
	// air is still.
	NYX_REFLECT(Component, meta = (DisplayName = "Wind"))
	struct WindComponent
	{
		NYX_PROPERTY(Edit, Undo, Serialize, meta = (Category = "Wind", DisplayName = "Blowing", Tooltip = "Whether the wind blows: off, the air is still, as if Speed were 0, and the wind dies down or picks up over Change Time"))
		bool bBlowing = true;

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (Category = "Wind", DisplayName = "Direction", DragSpeed = 0.5, Tooltip = "Where the wind blows to, in degrees round the up axis from +X towards -Z: 0 blows along +X, 90 along -Z"))
		float DirectionDegrees = 30.0f;

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (Category = "Wind", DisplayName = "Speed", DragSpeed = 0.05, Tooltip = "How fast the wind blows between gusts at the reference height, in metres a second: 3 a light breeze, 8 a fresh one, 15 a strong wind"))
		float Speed = 4.0f;

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (Category = "Gusts", DisplayName = "Gustiness", DragSpeed = 0.01, Tooltip = "How much faster the wind blows at the peak of the strongest gust front, as a part of its speed: 0 a steady wind, 1 twice as fast"))
		float Gustiness = 0.7f;

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (Category = "Gusts", DisplayName = "Frequency", DragSpeed = 0.005, Tooltip = "How many gust fronts a second pass a place; with the spacing, how fast they sweep over the land"))
		float GustFrequency = 0.12f;

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (Category = "Gusts", DisplayName = "Spacing", DragSpeed = 0.5, Tooltip = "Metres from one gust front to the next, along the wind"))
		float GustSpacing = 40.0f;

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (Category = "Gusts", DisplayName = "Patch Width", DragSpeed = 0.5, Tooltip = "Metres across the wind over which a front's strength changes: smaller, a front is strong in narrow places and weak between them"))
		float GustPatchWidth = 30.0f;

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (Category = "Turbulence", DisplayName = "Turbulence", DragSpeed = 0.01, Tooltip = "How fast the eddies swirl, as a part of the wind's speed, each its own way: 0 none"))
		float Turbulence = 0.2f;

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (Category = "Turbulence", DisplayName = "Eddy Size", DragSpeed = 0.1, Tooltip = "How big the eddies are, in metres; the wind carries them along"))
		float EddySize = 6.0f;

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (Category = "Height", DisplayName = "Ground Height", DragSpeed = 0.1, Tooltip = "The height of the ground the wind blows over, in metres: its heights are from here"))
		float GroundHeight = 0.0f;

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (Category = "Height", DisplayName = "Near Ground", DragSpeed = 0.01, Tooltip = "How fast the wind is at the ground, as a part of its speed at the reference height: 1 as fast everywhere"))
		float GroundFactor = 0.6f;

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (Category = "Height", DisplayName = "Reference Height", DragSpeed = 0.1, Tooltip = "How high above the ground the wind is as fast as its Speed, in metres; below, it slows towards Near Ground"))
		float ReferenceHeight = 10.0f;

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (Category = "Height", DisplayName = "Aloft", DragSpeed = 0.01, Tooltip = "How much faster the wind is high up, as a part of its speed: 1 twice as fast at Aloft Height and above, 0 no faster"))
		float Aloft = 1.0f;

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (Category = "Height", DisplayName = "Aloft Height", DragSpeed = 10.0, Tooltip = "How high above the ground, in metres, the wind is as much faster as Aloft says; from the reference height up to here it grows"))
		float AloftHeight = 1500.0f;

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (Category = "Change", DisplayName = "Change Time", DragSpeed = 0.1, Tooltip = "How many seconds of the world's time the wind takes to turn to new values, set in the editor or by a game: 0 at once"))
		float ChangeSeconds = 5.0f;

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (Category = "Local Air", DisplayName = "Texture Cells", Tooltip = "How many cells across, along x and z, the texture of a game's local air round the camera has, which grass and trees bend in: 8 to 128; more cost more"))
		int32_t LocalAirCells = 48;

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (Category = "Local Air", DisplayName = "Texture Layers", Tooltip = "How many cells high the texture of local air is, 1 to 32"))
		int32_t LocalAirLayers = 12;

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (Category = "Local Air", DisplayName = "Cell Size", DragSpeed = 0.05, Tooltip = "How big a cell of the texture of local air is, in metres: it reaches Cells times this round the camera"))
		float LocalAirCellSize = 1.0f;

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (Category = "Debug", DisplayName = "Debug View", Tooltip = "Draws the wind over the world round each view's camera: arrows along it, as long as it is fast and warmer in a gust, and the crests of the gust fronts"))
		EWindDebug Debug = EWindDebug::Off;
	};

	// The world's first wind, or null
	const WindComponent* FindWorldWind(const Registry& world);

	// The wind as these values make it at the world's `time` (seconds), without any change in
	// progress: its fronts and eddies where a wind blowing so since time 0 has carried them
	WindState MakeSteadyWind(const WindComponent& wind, double time);

	// The numbers of a pattern of gust fronts and eddies (WindPattern) a drive fades between: the way
	// its fronts travel, their spacing, the width of their patches, the eddies' size
	struct WindPatternValues
	{
		double DirectionRadians = 0.0;
		double GustSpacing = 1.0;
		double GustPatchWidth = 1.0;
		double EddySize = 1.0;

		friend bool operator==(const WindPatternValues&, const WindPatternValues&) = default;
	};

	// A wind turning to new values over time, smoothly and from the world's time alone. Between
	// Start and Start + Seconds every value goes from From's to To's along a smoothstep; the fronts'
	// phase and the eddies' offset are the integrals of their rates over that, worked out exactly,
	// so they never jump, however the rates change. Which way the fronts lie and how they and the
	// eddies are spaced isn't turned but faded, from the pattern before to the new one over the
	// same time (WindState::PatternBlend), so no front sweeps over the land far from the origin; a
	// new pattern set while one fades in waits for it (Pending), and the latest such wins. The same
	// targets set at the same times give the same wind at any time, however the steps or frames fall.
	struct WindDrive
	{
		WindComponent From;
		WindComponent To;
		double Start = 0.0;
		double Seconds = 0.0;
		// The phase and the offset at Start, not wrapped
		double GustPhase = 0.0;
		glm::dvec2 EddyOffset{ 0.0 };
		// The patterns faded between, from PatternStart over PatternSeconds, and one waiting
		WindPatternValues PatternFrom;
		WindPatternValues PatternTo;
		double PatternStart = 0.0;
		double PatternSeconds = 0.0;
		std::optional<WindPatternValues> Pending;
		double PendingSeconds = 0.0;
		// Whether it has begun: before its first target, it starts steady at the first one
		bool bStarted = false;
	};

	// Turns the drive to `target` from the wind as it is at `time`, over the target's Change Time;
	// the same target as before changes nothing. Its first target starts it steady, as
	// MakeSteadyWind() has it. Returns whether it changed.
	bool SetWindTarget(WindDrive& drive, const WindComponent& target, double time);

	// The wind at `time` as the drive has it
	WindState EvaluateWind(const WindDrive& drive, double time);

	// How far the wind has carried what it carries at its reference height by `time`, in metres
	// along the ground, not wrapped: the integral of its velocity, exact as its phases are; for
	// what moves with the wind as a whole, such as clouds (scaled by the height profile at their
	// altitude)
	glm::dvec2 GetWindTravel(const WindDrive& drive, double time);
	glm::dvec2 GetSteadyWindTravel(const WindComponent& wind, double time);

	// Whether the drive is still turning at `time`
	bool IsWindChanging(const WindDrive& drive, double time);

	// The drive of the world's wind while a game runs: made and turned by UpdateWorldWind() on the
	// wind's entity, never saved. The renderer draws the wind from it where it has one, so the
	// game's systems and the GPU see the same wind; in the editor it keeps one of its own.
	struct WindDriveComponent
	{
		WindDrive Drive;
	};

	// Turns the world's wind to its first WindComponent's values at the world's `time`, keeping its
	// drive on that component's entity (WindDriveComponent; a second wind's drive goes); returns the
	// wind at `time`, still without a WindComponent. GameWorld::Step() calls it before its systems.
	WindState UpdateWorldWind(Registry& world, double time);

	// The world's wind at `time` as its drive has it, or its first WindComponent steady without one;
	// still without either
	WindState GetWorldWind(const Registry& world, double time);
}

#include "Generated/Runtime/WindComponent.reflect.h"
