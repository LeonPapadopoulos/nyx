#include "SkyTablesWindow.h"

#include "EditorRenderer.h"
#include "ImGuiSource.h"
#include "UIDiagnostics.h"
#include "UIWidgets.h"

#include <imgui.h>

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

namespace Nyx::Editor
{
	void SkyTablesWindow::Draw(Nyx::IEditorRenderer& renderer)
	{
		if (!bOpen)
		{
			return;
		}
		ImGui::SetNextWindowSize(ImVec2(560.0f, 720.0f), ImGuiCond_FirstUseEver);
		if (!Nyx::UI::Begin("Sky Tables", &bOpen))
		{
			ImGui::End();
			return;
		}

		const std::vector<uint64_t> views = renderer.GetSceneViewIds();
		if (views.empty())
		{
			NYX_UI(ImGui::TextDisabled("No scene view"));
			ImGui::End();
			return;
		}
		// Each view by its number and size; until one is chosen, the largest, which is the one on show
		const auto viewName = [&](size_t index)
		{
			const Nyx::Extent2D size = renderer.GetSceneViewExtent(views[index]);
			return "Scene view " + std::to_string(index + 1) + ", " + std::to_string(size.Width) + " x " + std::to_string(size.Height);
		};
		if (!bViewChosen)
		{
			const auto area = [&](size_t index)
			{
				const Nyx::Extent2D size = renderer.GetSceneViewExtent(views[index]);
				return static_cast<uint64_t>(size.Width) * size.Height;
			};
			ViewIndex = 0;
			for (size_t index = 1; index < views.size(); ++index)
			{
				ViewIndex = area(index) > area(ViewIndex) ? index : ViewIndex;
			}
		}
		ViewIndex = std::min(ViewIndex, views.size() - 1);

		ImGui::SetNextItemWidth(ImGui::GetFontSize() * 16.0f);
		if (NYX_UI(Nyx::UI::BeginCombo("##SkyView", viewName(ViewIndex).c_str())))
		{
			for (size_t index = 0; index < views.size(); ++index)
			{
				if (NYX_UI(Nyx::UI::Selectable(viewName(index).c_str(), index == ViewIndex)))
				{
					ViewIndex = index;
					bViewChosen = true;
				}
			}
			Nyx::UI::EndCombo();
		}
		ImGui::SameLine();
		ImGui::SetNextItemWidth(ImGui::GetFontSize() * 8.0f);
		NYX_UI(ImGui::SliderFloat("Exposure", &ExposureStops, -8.0f, 8.0f, "%.1f stops"));

		const Nyx::IEditorRenderer::SkyTablePictures pictures = renderer.GetSceneViewSkyTablePictures(views[ViewIndex], std::exp2(ExposureStops));
		if (pictures.Textures[0] == ImTextureID_Invalid || pictures.Textures[0] == ImTextureID{})
		{
			NYX_UI(ImGui::TextDisabled("The view draws no Atmosphere sky (an Environment with Follow Sun on, in the Atmosphere mode), or not yet"));
			ImGui::End();
			return;
		}

		// Each picture as wide as the window, its texels square
		static constexpr const char* Captions[] = { "Transmittance: from a height (up) at an angle (across) to the top of the air, as it is",
			"Multiple scattering: by the height (up) and the sun's angle (across), x 50", "Sky view: round the camera from the sun (left) to away from it, zenith at the top",
			"The sky's cube, unfolded: the world's light, without the sun's disk" };
		const float width = std::max(ImGui::GetContentRegionAvail().x, 64.0f);
		for (size_t picture = 0; picture < pictures.Textures.size(); ++picture)
		{
			const glm::uvec2 size = pictures.Sizes[picture];
			if (size.x == 0 || pictures.Textures[picture] == ImTextureID_Invalid)
			{
				continue;
			}
			NYX_UI(ImGui::TextDisabled("%s (%u x %u)", Captions[picture], size.x, size.y));
			NYX_UI(ImGui::Image(pictures.Textures[picture], ImVec2(width, std::floor(width * static_cast<float>(size.y) / static_cast<float>(size.x)))));
		}
		ImGui::End();
	}
}
