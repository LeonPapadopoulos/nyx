#include "SceneViewSet.h"

#include "EditorRenderer.h"

#include <glm/glm.hpp>

#include <algorithm>
#include <charconv>
#include <cmath>
#include <optional>
#include <set>
#include <string_view>

namespace Nyx::Editor
{
	namespace
	{
		// A view as its line in the layout files has it
		struct SavedView
		{
			int Number = 1;
			bool bGameView = false;
			glm::vec3 CameraPosition{ 0.0f };
			glm::vec3 CameraRotation{ 0.0f };
		};

		// The shortest text that reads back as the same float
		std::string FormatFloat(float number)
		{
			char buffer[32];
			const auto [end, error] = std::to_chars(buffer, buffer + sizeof(buffer), number);
			return error == std::errc() ? std::string(buffer, end) : std::string("0");
		}

		std::optional<SavedView> ParseView(std::string_view line)
		{
			constexpr std::string_view Key = "View=";
			if (!line.starts_with(Key))
			{
				return std::nullopt;
			}
			line.remove_prefix(Key.size());

			std::vector<std::string_view> fields;
			for (size_t start = 0; start <= line.size();)
			{
				const size_t comma = std::min(line.find(',', start), line.size());
				fields.push_back(line.substr(start, comma - start));
				start = comma + 1;
			}
			if (fields.size() != 8 || (fields[1] != "0" && fields[1] != "1"))
			{
				return std::nullopt;
			}

			SavedView view;
			const auto [numberEnd, numberError] = std::from_chars(fields[0].data(), fields[0].data() + fields[0].size(), view.Number);
			if (numberError != std::errc() || numberEnd != fields[0].data() + fields[0].size() || view.Number < 1)
			{
				return std::nullopt;
			}
			view.bGameView = fields[1] == "1";
			float values[6] = {};
			for (int index = 0; index < 6; ++index)
			{
				const std::string_view field = fields[2 + index];
				const auto [end, error] = std::from_chars(field.data(), field.data() + field.size(), values[index]);
				if (error != std::errc() || end != field.data() + field.size() || !std::isfinite(values[index]))
				{
					return std::nullopt;
				}
			}
			view.CameraPosition = glm::vec3(values[0], values[1], values[2]);
			view.CameraRotation = glm::vec3(values[3], values[4], values[5]);
			return view;
		}
	}

	void SceneViewSet::Attach(Nyx::IEditorRenderer& renderer, const AssetTypeRegistry* assetTypes)
	{
		Renderer = &renderer;
		AssetTypes = assetTypes;
		Add(1);
	}

	void SceneViewSet::UseViews(Nyx::IEditorRenderer& renderer, std::span<const uint64_t> sceneViewIds)
	{
		Renderer = &renderer;
		for (const uint64_t sceneViewId : sceneViewIds)
		{
			AddPanel(static_cast<int>(Views.size()) + 1, sceneViewId);
		}
	}

	void SceneViewSet::Detach()
	{
		for (const std::unique_ptr<SceneViewPanel>& view : Views)
		{
			view->DestroyView();
		}
		Views.clear();
	}

	SceneViewPanel& SceneViewSet::Open()
	{
		int number = 1;
		while (std::any_of(Views.begin(), Views.end(), [number](const std::unique_ptr<SceneViewPanel>& view) { return view->GetNumber() == number; }))
		{
			++number;
		}

		const SceneViewPanel* first = Views.empty() ? nullptr : Views.front().get();
		SceneViewPanel& view = Add(number);
		if (first)
		{
			glm::vec3 position;
			glm::vec3 rotation;
			if (Renderer->GetSceneViewEditorCameraTransform(first->GetViewId(), position, rotation))
			{
				Renderer->SetSceneViewEditorCameraTransform(view.GetViewId(), position, rotation);
			}
			view.DockNextDraw(first->GetDockId());
		}
		return view;
	}

	SceneViewPanel& SceneViewSet::Add(int number)
	{
		AddPanel(number, Renderer->CreateSceneView());
		const uint64_t sceneViewId = Views.back()->GetViewId();
		Renderer->SetSceneViewCullingDebug(sceneViewId, CullingDebug);
		Renderer->SetSceneViewShadowDebug(sceneViewId, ShadowDebug);
		Renderer->SetSceneViewDebug(sceneViewId, ViewDebug);
		return *Views.back();
	}

	void SceneViewSet::AddPanel(int number, uint64_t sceneViewId)
	{
		auto view = std::make_unique<SceneViewPanel>(number);
		view->SetAssetTypes(AssetTypes);
		view->UseView(*Renderer, sceneViewId);
		Views.push_back(std::move(view));
	}

	void SceneViewSet::ApplyPickResults(SceneEditor& editing)
	{
		for (const std::unique_ptr<SceneViewPanel>& view : Views)
		{
			view->ApplyPickResult(editing);
		}
	}

	void SceneViewSet::Draw(SceneEditor& editing, const EditorPreferences& preferences, EditorCameraController& cameraController)
	{
		if (!bShown)
		{
			return;
		}

		const bool bCanClose = Views.size() > 1;
		for (const std::unique_ptr<SceneViewPanel>& view : Views)
		{
			view->Draw(editing, preferences, cameraController, bCanClose);
		}

		// The renderer lets go of a view's scene view once the frame being recorded is done with it
		for (auto view = Views.begin(); view != Views.end() && Views.size() > 1;)
		{
			if ((*view)->bOpen)
			{
				++view;
				continue;
			}
			(*view)->DestroyView();
			view = Views.erase(view);
		}
		// The last stays open, even when every window was closed at once
		if (!Views.empty())
		{
			Views.front()->bOpen = true;
		}
	}

	void SceneViewSet::SetCullingDebug(Nyx::ESceneViewCullingDebug mode)
	{
		CullingDebug = mode;
		if (Renderer)
		{
			for (const uint64_t sceneViewId : Renderer->GetSceneViewIds())
			{
				Renderer->SetSceneViewCullingDebug(sceneViewId, mode);
			}
		}
	}

	void SceneViewSet::SetShadowDebug(Nyx::ESceneViewShadowDebug mode)
	{
		ShadowDebug = mode;
		if (Renderer)
		{
			for (const uint64_t sceneViewId : Renderer->GetSceneViewIds())
			{
				Renderer->SetSceneViewShadowDebug(sceneViewId, mode);
			}
		}
	}

	void SceneViewSet::SetViewDebug(Nyx::ESceneViewDebug view)
	{
		ViewDebug = view;
		if (Renderer)
		{
			for (const std::unique_ptr<SceneViewPanel>& panel : Views)
			{
				Renderer->SetSceneViewDebug(panel->GetViewId(), view);
			}
		}
	}

	std::vector<std::string> SceneViewSet::GetTitles() const
	{
		std::vector<std::string> titles;
		for (const std::unique_ptr<SceneViewPanel>& view : Views)
		{
			titles.push_back(view->GetTitle());
		}
		return titles;
	}

	std::vector<std::string> SceneViewSet::Write() const
	{
		std::vector<std::string> lines;
		for (const std::unique_ptr<SceneViewPanel>& view : Views)
		{
			glm::vec3 position(0.0f);
			glm::vec3 rotation(0.0f);
			if (Renderer)
			{
				Renderer->GetSceneViewEditorCameraTransform(view->GetViewId(), position, rotation);
			}
			std::string line = "View=" + std::to_string(view->GetNumber()) + (view->IsShowingGameView() ? ",1" : ",0");
			for (const float value : { position.x, position.y, position.z, rotation.x, rotation.y, rotation.z })
			{
				line += "," + FormatFloat(value);
			}
			lines.push_back(std::move(line));
		}
		return lines;
	}

	void SceneViewSet::Read(const std::vector<std::string>& lines)
	{
		std::vector<SavedView> saved;
		std::set<int> numbers;
		for (const std::string& line : lines)
		{
			// A damaged line loses only its view; a number twice, the second
			if (const std::optional<SavedView> view = ParseView(line); view && numbers.insert(view->Number).second)
			{
				saved.push_back(*view);
			}
		}
		if (saved.empty() || !Renderer)
		{
			return;
		}

		Detach();
		for (const SavedView& view : saved)
		{
			SceneViewPanel& panel = Add(view.Number);
			Renderer->SetSceneViewEditorCameraTransform(panel.GetViewId(), view.CameraPosition, view.CameraRotation);
			panel.SetShowsGameView(view.bGameView);
		}
	}
}
