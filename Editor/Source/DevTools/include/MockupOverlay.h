#pragma once

#include <glm/glm.hpp>

#include <cstdint>
#include <filesystem>
#include <string>

namespace Nyx
{
	class IRenderer;
}

namespace Nyx::Editor
{
	// The "Mockup Overlay" window (Debug > Mockup Overlay): lays a mockup image (a PNG or JPEG from
	// disk) over the editor's main window, half transparent, to match sizes and positions against a
	// design. It is painted over everything and takes no input, so the editor underneath still
	// works. Opacity, offset and scale are set in the window; closing the window hides the image.
	class MockupOverlay
	{
	public:
		// The window and, while it is open, the image over the editor
		void Draw(Nyx::IRenderer& renderer);

		// Gives the image back to the renderer; call it before the renderer shuts down.
		void Release();

		bool bOpen = false;

	private:
		void Load(const std::filesystem::path& path);
		void DrawOverlay() const;

		Nyx::IRenderer* Renderer = nullptr;

		// The image shown (a UI image of the renderer), and the path typed or chosen
		uint64_t ImageId = 0;
		glm::vec2 ImageSize{ 0.0f };
		std::string PathInput;
		// What the last load did, e.g. why it failed
		std::string Message;

		bool bShow = true;
		float Opacity = 0.5f;
		// The image's top left, from the main window's top left, in pixels
		glm::vec2 Offset{ 0.0f };
		// 0.5 shows a mockup drawn at 200% at the editor's size
		float Scale = 1.0f;
	};
}
