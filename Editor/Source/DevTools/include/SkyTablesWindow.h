#pragma once

#include <cstddef>

namespace Nyx
{
	class IEditorRenderer;
}

namespace Nyx::Editor
{
	// A dev tool for the sky made by the air's scattering (Docs/Design/atmosphere.md): a scene
	// view's tables as pictures, as the GPU made them for its camera and sun: the transmittance, the
	// multiple scattering, the sky view, and the sky's cube that lights the world, unfolded
	class SkyTablesWindow
	{
	public:
		void Draw(Nyx::IEditorRenderer& renderer);

		bool bOpen = false;

	private:
		// The view shown, an index into the renderer's scene views; the largest until one is chosen
		size_t ViewIndex = 0;
		bool bViewChosen = false;
		// What the pictures' light is multiplied by, in stops
		float ExposureStops = 0.0f;
	};
}
