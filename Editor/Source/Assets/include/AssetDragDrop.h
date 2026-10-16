#pragma once

#include <optional>
#include <string>
#include <string_view>

namespace Nyx::Editor
{
	class AssetTypeRegistry;

	// Assets dragged from the asset browser: the payload is the asset's path in the assets folder
	// ("Materials/Stone.nyxmat"), as text
	inline constexpr const char* AssetDragPayload = "NYX_ASSET";

	// In a drag source (ImGui::BeginDragDropSource()): drags the asset, its name beside the mouse
	void SetAssetDragPayload(const std::string& path);

	// In a drop target (ImGui::BeginDragDropTarget()): the path of an asset of type `typeId`
	// ("Material", as `types` names them) dropped this frame. Assets of other types aren't taken,
	// so the target doesn't light up for them.
	std::optional<std::string> AcceptAssetDrop(const AssetTypeRegistry& types, std::string_view typeId);

	// The type of the asset being dragged, while one is; empty otherwise
	std::string GetDraggedAssetType(const AssetTypeRegistry& types);
}
