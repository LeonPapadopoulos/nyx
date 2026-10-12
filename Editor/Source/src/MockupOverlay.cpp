#include "MockupOverlay.h"

#include "FileDialogs.h"
#include "ImageFile.h"
#include "ImGuiSource.h"
#include "Renderer.h"
#include "UIWidgets.h"
#include "WindowPainter.h"

#include <imgui.h>


namespace Nyx::Editor
{
	void MockupOverlay::Draw(Nyx::IRenderer& renderer)
	{
		Renderer = &renderer;
		if (!bOpen)
		{
			return;
		}

		ImGui::SetNextWindowSize(ImVec2(460.0f, 330.0f), ImGuiCond_FirstUseEver);
		if (Nyx::UI::Begin("Mockup Overlay", &bOpen))
		{
			// The image: a path typed in, or chosen with Windows' Open dialog
			ImGui::SetNextItemWidth(-ImGui::CalcTextSize("Load  Choose...").x - ImGui::GetStyle().ItemSpacing.x * 2.0f - ImGui::GetStyle().FramePadding.x * 4.0f);
			const bool bEntered = NYX_UI(Nyx::UI::InputText("##MockupPath", &PathInput, ImGuiInputTextFlags_EnterReturnsTrue, "Path of a PNG or JPEG"));
			ImGui::SameLine();
			if (NYX_UI(Nyx::UI::Button("Load")) || bEntered)
			{
				Load(std::filesystem::path(std::u8string(reinterpret_cast<const char8_t*>(PathInput.c_str()))));
			}
			ImGui::SameLine();
			if (NYX_UI(Nyx::UI::Button("Choose...")))
			{
				if (const std::optional<std::filesystem::path> chosen = ChooseImageFile())
				{
					// ImGui's text is UTF-8.
					const std::u8string utf8 = chosen->u8string();
					PathInput.assign(reinterpret_cast<const char*>(utf8.c_str()));
					Load(*chosen);
				}
			}
			if (!Message.empty())
			{
				NYX_UI(ImGui::TextWrapped("%s", Message.c_str()));
			}

			if (ImageId != 0)
			{
				NYX_UI(Nyx::UI::SectionHeader("Placement"));
				NYX_UI(Nyx::UI::Toggle("Show", &bShow));
				NYX_UI(Nyx::UI::SliderFloat("Opacity", &Opacity, 0.0f, 1.0f, "%.2f"));
				if (NYX_UI(Nyx::UI::DragVector("Offset", &Offset.x, 2, 1.0f, "%.0f px")))
				{
					Offset = glm::clamp(Offset, glm::vec2(-8000.0f), glm::vec2(8000.0f));
				}
				NYX_UI(Nyx::UI::SliderFloat("Scale", &Scale, 0.1f, 4.0f, "%.2f"));

				// Mockups are often drawn at 200%; these show them at the editor's size or their own.
				if (NYX_UI(Nyx::UI::Button("50%")))
				{
					Scale = 0.5f;
				}
				ImGui::SameLine();
				if (NYX_UI(Nyx::UI::Button("100%")))
				{
					Scale = 1.0f;
				}
				ImGui::SameLine();
				if (NYX_UI(Nyx::UI::Button("Reset offset")))
				{
					Offset = glm::vec2(0.0f);
				}
				NYX_UI(ImGui::TextDisabled("%.0f x %.0f px, shown at %.0f x %.0f", ImageSize.x, ImageSize.y, ImageSize.x * Scale, ImageSize.y * Scale));
			}
		}
		ImGui::End();

		DrawOverlay();
	}

	void MockupOverlay::Release()
	{
		if (Renderer != nullptr && ImageId != 0)
		{
			Renderer->DestroyUIImage(ImageId);
		}
		ImageId = 0;
		ImageSize = glm::vec2(0.0f);
	}

	void MockupOverlay::Load(const std::filesystem::path& path)
	{
		Nyx::Engine::RgbaImage image;
		std::string error;
		if (!Nyx::Engine::LoadRgbaImage(path, image, error))
		{
			Message = error;
			return;
		}

		const uint64_t newImage = Renderer->CreateUIImage(image);
		if (newImage == 0)
		{
			Message = "Couldn't make a texture of " + path.string();
			return;
		}

		// The old image goes once the GPU is done with it.
		Release();
		ImageId = newImage;
		ImageSize = glm::vec2(static_cast<float>(image.Width), static_cast<float>(image.Height));
		Message = "Showing " + path.filename().string();
	}

	void MockupOverlay::DrawOverlay() const
	{
		const ImTextureID texture = Renderer->GetUIImageTextureId(ImageId);
		if (!bShow || texture == ImTextureID_Invalid)
		{
			return;
		}

		// Over everything in the main window, from its top left
		const ImGuiViewport& viewport = *ImGui::GetMainViewport();
		const glm::vec2 min = glm::vec2(viewport.Pos.x, viewport.Pos.y) + Offset;
		const Nyx::Engine::PainterRect rect{ min, min + ImageSize * Scale };
		Nyx::UI::GetForegroundPainter().AddImage(ImTextureRef(texture), rect, Nyx::Engine::PainterStyle::Filled(glm::vec4(1.0f, 1.0f, 1.0f, Opacity)));
	}
}
