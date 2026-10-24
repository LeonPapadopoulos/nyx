#pragma once

#include "ComponentInspection.h"
#include "EditorLinkMessages.h"
#include "Entity.h"
#include "EntityGuid.h"
#include "InspectorDrawContext.h"

#include <optional>
#include <string>
#include <vector>

namespace Nyx::Editor
{
	class SceneEditor;

	// What a running game says of the entity the Runtime section shows (PlaySession::GetEntityInspection())
	struct GameInspection
	{
		// E.g. "Game 1"
		std::string GameName;

		// The game's last answer about the entity; null until one came
		const Nyx::InspectedEntityMessage* Answer = nullptr;
	};

	// What the Details panel's Runtime section shows: where the components come from, and them
	struct RuntimeSection
	{
		// E.g. "This world", "Game 1, step 1234", "Waiting for Game 1"
		std::string Source;
		std::vector<Nyx::InspectedComponent> Components;
	};

	// The Runtime section of an entity: a running game's answer while there is a game, else the
	// world's own run-time components; none when there is no game and the entity has none
	std::optional<RuntimeSection> GetRuntimeSection(const Nyx::Registry& world, Nyx::Entity entity, const std::optional<GameInspection>& game);

	// The "Details" window: the selected entity's components with their reflected properties, Add
	// Component, and Remove Component on a component header's right-click menu. Run-time components
	// (ComponentTypeOps::IsRuntime()), which the world makes itself and never saves, are shown greyed
	// in a Runtime section after them: the editor's own world's, or while Play runs, a running game's.
	class DetailsPanel
	{
	public:
		void Draw(SceneEditor& editing);

		// While Play runs, before each Draw(): the game's answer about the entity the Runtime section
		// shows; none shows the editor's own world's run-time components
		void SetGameInspection(std::optional<GameInspection> inspection)
		{
			Game = std::move(inspection);
		}

		// The entity whose Runtime section was open when last drawn, for a running game to be asked
		// about; none while the panel or the section is closed, or nothing is selected
		std::optional<Nyx::EntityGuid> GetInspectedEntity() const
		{
			return InspectedEntity;
		}

		bool bOpen = true;

	private:
		void DrawWindow(SceneEditor& editing);
		void DrawRuntimeSection(const Nyx::Registry& world, Nyx::Entity entity);
		void DrawInspectedComponent(const Nyx::InspectedComponent& component);

	private:
		std::optional<GameInspection> Game;
		std::optional<Nyx::EntityGuid> InspectedEntity;

		// Run-time components with reflected properties are read into one of their type here, and
		// drawn from it with nothing to undo
		Nyx::Registry Scratch;
		std::optional<Nyx::Entity> ScratchEntity;
		InspectorDrawContext ScratchContext;
	};
}
