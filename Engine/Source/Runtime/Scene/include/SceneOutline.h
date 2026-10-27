#pragma once

#include "EntityGuid.h"
#include "ReflectionMacros.h"

#include <cstdint>
#include <string>
#include <vector>

namespace Nyx
{
	// A folder of the editor's outliner. It only sorts the entities for whoever edits the scene: it
	// moves nothing, parents no transform, and no game reads it.
	NYX_REFLECT()
	struct SceneOutlineFolder
	{
		// Names the folder in the outline; never 0, which stands for the top of the outline
		NYX_PROPERTY(Serialize)
		uint64_t Id = 0;

		NYX_PROPERTY(Serialize)
		std::string Name;

		// The folder it is in; 0 at the top
		NYX_PROPERTY(Serialize)
		uint64_t Parent = 0;
	};

	// An entity's place in the outline: the folder it is in, its order by its place in
	// SceneOutline::Entities
	NYX_REFLECT()
	struct SceneOutlineEntity
	{
		NYX_PROPERTY(Serialize)
		EntityGuid Entity;

		// 0 at the top
		NYX_PROPERTY(Serialize)
		uint64_t Folder = 0;
	};

	// How the editor's outliner shows a scene's entities: their order, and the folders they are
	// sorted into. Saved with the scene (SceneSerializer), for the editor alone: a game loading the
	// scene skips it. An entity it doesn't list, e.g. of a scene saved before there was an outline,
	// shows at the top after the listed ones, in the order the scene has its entities.
	NYX_REFLECT()
	struct SceneOutline
	{
		// In a folder, its subfolders come first, in the order they have here
		NYX_PROPERTY(Serialize)
		std::vector<SceneOutlineFolder> Folders;

		// Then its entities, in the order they have here
		NYX_PROPERTY(Serialize)
		std::vector<SceneOutlineEntity> Entities;
	};
}

#include "Generated/Runtime/SceneOutline.reflect.h"
