#pragma once

#include "TerrainSeedTests.h"
#include "TerrainWorldPlanMap.h"

#include <cstdint>
#include <memory>
#include <vector>

namespace Nyx
{
	class IEditorRenderer;
}

namespace Nyx::Editor
{
	class SceneEditor;

	namespace Detail
	{
		struct TerrainSeedsRun;
	}

	// Many seeds of an SDF terrain's world plan side by side (Window > Plan Seeds), to tell whether
	// the plan makes good worlds in general rather than for one seed: the terrain the Plan Map shows,
	// a run of seeds from First Seed made on the job system's workers, each as a small map with its
	// shares of sea, rough land, rifts and tunnels and its rivers and lakes; how each spreads across
	// them (least, mean, most); a click on one gives the terrain that seed, an undo step. A run is of
	// the plan as it was: changed since, it says so until run again. With Checks, each seed's world is
	// also made and checked (TerrainSeedTests.h), one seed after another: its places found and its
	// Route Checks checked, each map framed green where it passed and red where it failed, with what
	// failed on hover, and how many passed and what failed most above them.
	class TerrainSeedsWindow
	{
	public:
		void Draw(Nyx::IEditorRenderer& renderer, SceneEditor& editing);

		bool bOpen = false;
		uint32_t FirstSeed = 1;
		uint32_t Count = 36;
		bool bChecks = false;

	private:
		void TakeImages(Nyx::IEditorRenderer& renderer);

		std::shared_ptr<Detail::TerrainSeedsRun> Run;
		// Each seed's map, once the run is done, and how they spread
		std::vector<uint64_t> Images;
		Nyx::TerrainPlanSpread Spread;
		Nyx::TerrainSeedSummary Summary;
	};
}
