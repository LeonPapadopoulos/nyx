#include "AssetDragDrop.h"

#include "AssetTypeRegistry.h"

#include <imgui.h>

#include <filesystem>

namespace Nyx::Editor
{
	namespace
	{
		// The dragged asset's path, from the payload, when one is being dragged
		std::optional<std::string> DraggedPath()
		{
			const ImGuiPayload* payload = ImGui::GetDragDropPayload();
			if (!payload || !payload->IsDataType(AssetDragPayload) || payload->DataSize <= 0)
			{
				return std::nullopt;
			}
			return std::string(static_cast<const char*>(payload->Data), static_cast<size_t>(payload->DataSize));
		}

		std::string TypeOf(const std::string& path)
		{
			const AssetTypeDescriptor* type = AssetTypeRegistry::Get().FindByExtension(std::filesystem::path(path).extension().string());
			return type ? type->TypeId : std::string();
		}
	}

	void SetAssetDragPayload(const std::string& path)
	{
		ImGui::SetDragDropPayload(AssetDragPayload, path.data(), path.size());
		ImGui::TextUnformatted(std::filesystem::path(path).filename().string().c_str());
	}

	std::optional<std::string> AcceptAssetDrop(std::string_view typeId)
	{
		const std::optional<std::string> path = DraggedPath();
		if (!path || TypeOf(*path) != typeId)
		{
			return std::nullopt;
		}
		const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(AssetDragPayload);
		return payload && payload->IsDelivery() ? path : std::nullopt;
	}

	std::string GetDraggedAssetType()
	{
		const std::optional<std::string> path = DraggedPath();
		return path ? TypeOf(*path) : std::string();
	}
}
