#pragma once

#include "Entity.h"

#include <cstdint>

namespace Nyx
{
	inline constexpr uint32_t SceneFileMagic = 0x53434E45; // 'SCNE'

	// Version 1: property values without names, in declaration order. No longer read.
	// Version 2: properties with name hash and kind (see ReflectedArchiveSerializer).
	inline constexpr uint32_t SceneFileVersion = 2;

	// Starts the editor's outline after a scene file's entities (SceneSerializer)
	inline constexpr uint32_t SceneOutlineMarker = 0x4C54554F; // 'OUTL'

	class IAssetResolver;

	// What a component's post-load step (ComponentTypeRegistry::SetPostLoad()) is given besides the
	// component: whatever resolves its assets, and the world and entity it belongs to. With them the
	// step may add or change derived, unsaved components on that entity, such as a mesh made from the
	// component's values (RuntimeMeshComponent), and nothing else: no other entity, no saved component,
	// nothing that undo or a scene file would have to know about.
	struct ScenePostLoadContext
	{
		IAssetResolver* AssetResolver = nullptr;
		// Set by whoever runs the step, for the component's entity: a scene's load, the editor after an
		// edit, undo or redo, a live edit in a game. Null where nobody set them. Only for the step: a
		// scene is loaded into a world of its own, which is then moved into the caller's (the entity
		// keeps its handle), so keep neither past it.
		Registry* World = nullptr;
		Nyx::Entity Entity{};
	};
}