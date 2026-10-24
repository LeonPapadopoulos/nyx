#pragma once

#include "EntityGuid.h"
#include "ReflectionMacros.h"

#include <glm/glm.hpp>

#include <cstdint>
#include <memory>

namespace Nyx
{
	struct MeshData;

	// Many copies of its entity's mesh, scattered over a surface: another entity's run-time mesh, such
	// as a ground's, sampled by the area of its triangles, so any mesh will do. Which points are kept
	// is a function of these values and the surface alone, the same on every machine
	// (ScatterOnMesh(), ScatterInstances.h); the copies go into the entity's MeshInstancesComponent,
	// in cells for culling, made by the post-load step in the editor and the game, and never saved.
	// They follow the surface: when its mesh or where it lies changes, they are made again
	// (UpdateScatters(), each frame). Everything is in the entity's space: the area around it, heights
	// along its Y, slopes against it.
	NYX_REFLECT(Component, meta = (DisplayName = "Scatter"))
	struct ScatterComponent
	{
		NYX_PROPERTY(Edit, Undo, Serialize, meta = (Category = "Scatter", Tooltip = "The entity to scatter on: its run-time mesh, e.g. a ground's. None: the flat plane through this entity"))
		EntityGuid Surface;

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (Category = "Scatter", DragSpeed = 0.5, Tooltip = "The area's width along X and depth along Z, in metres, around the entity"))
		glm::vec2 Size{ 32.0f, 32.0f };

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (Category = "Scatter", DragSpeed = 0.01, Tooltip = "Copies a square metre of surface, before the limits and the spacing take some away"))
		float Density = 0.5f;

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (Category = "Scatter", Tooltip = "Another seed, other places for the same values"))
		uint32_t Seed = 1;

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (Category = "Scatter", DragSpeed = 0.01, Tooltip = "The least distance between two copies, in metres; 0: none. It wins over the density"))
		float Spacing = 0.5f;

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (Category = "Limits", DragSpeed = 0.1, Tooltip = "Only where the surface slopes between these, in degrees: 0 is flat, 90 a wall"))
		glm::vec2 SlopeRange{ 0.0f, 40.0f };

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (Category = "Limits", DragSpeed = 0.1, Tooltip = "Only where the surface is between these heights, in metres along the entity's Y"))
		glm::vec2 HeightRange{ -1000.0f, 1000.0f };

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (Category = "Placement", DragSpeed = 0.01, Tooltip = "Each copy's size, between these"))
		glm::vec2 ScaleRange{ 0.8f, 1.2f };

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (Category = "Placement", DragSpeed = 1.0, Tooltip = "How far each copy is turned about its up, at random, in degrees all told: 360 any way, 0 all along X"))
		float YawRange = 360.0f;

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (DisplayName = "Align To Normal", Category = "Placement", DragSpeed = 0.01, Tooltip = "0: copies stand upright, 1: along the surface's normal"))
		float AlignToNormal = 0.0f;

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (DisplayName = "Sink Depth", Category = "Placement", DragSpeed = 0.01, Tooltip = "How deep each copy is sunk into the surface along its up, in metres, so none floats on a slope"))
		float SinkDepth = 0.0f;

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (DisplayName = "Cell Size", Category = "Culling", DragSpeed = 0.5, Tooltip = "The copies are grouped in squares this many metres a side, which the renderer culls whole"))
		float CellSize = 16.0f;

		// What the instances were made from (HashScatterValues(), the surface's revision and where it
		// lies), so a step with nothing changed makes nothing again. Not reflected: a copy or a restored
		// component has 0 and is made again.
		uint64_t MadeFrom = 0;
		// The surface mesh they were made on: not kept alive, only told apart from another one that
		// takes its address
		std::weak_ptr<const MeshData> MadeOn;
	};
}

#include "Generated/Runtime/ScatterComponent.reflect.h"
