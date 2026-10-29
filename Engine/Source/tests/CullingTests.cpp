// Culling without a GPU (SceneCulling.h): a camera's frustum from its view and projection, boxes
// inside, outside and across its planes, boxes taken into another space, the cells of mesh
// instances with the mesh added at each instance, and the runs of instances drawn of cells inside,
// outside and across the frustum.
#include "CameraProjection.h"
#include "MeshInstancesComponent.h"
#include "SceneCulling.h"
#include "TestSupport.h"

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <cmath>
#include <iostream>
#include <string>
#include <vector>

namespace
{
	using namespace Nyx;
	using Nyx::Tests::Require;

	bool Near(const glm::vec3& a, const glm::vec3& b, float tolerance = 1e-4f)
	{
		return glm::all(glm::lessThanEqual(glm::abs(a - b), glm::vec3(tolerance)));
	}

	BoundingBox BoxAround(const glm::vec3& center, float halfSize)
	{
		return BoundingBox{ center - glm::vec3(halfSize), center + glm::vec3(halfSize) };
	}

	const char* Name(EFrustumOverlap overlap)
	{
		switch (overlap)
		{
		case EFrustumOverlap::Outside:
			return "outside";
		case EFrustumOverlap::Straddling:
			return "straddling";
		default:
			return "inside";
		}
	}

	void RequireOverlap(const Frustum& frustum, const BoundingBox& box, EFrustumOverlap expected, const std::string& what)
	{
		const EFrustumOverlap overlap = ClassifyBox(frustum, box);
		Require(overlap == expected, what + " should be " + Name(expected) + ", not " + Name(overlap));
	}

	// At the origin looking along -Z, 90 degrees up and across, from 0.1 to 100 m
	Frustum MakeTestFrustum()
	{
		return MakeFrustum(MakePerspectiveProjection(glm::radians(90.0f), 1.0f, 0.1f, 100.0f));
	}

	void TestFrustumPlanes()
	{
		const Frustum frustum = MakeTestFrustum();
		for (const glm::vec4& plane : frustum.Planes)
		{
			Require(std::abs(glm::length(glm::vec3(plane)) - 1.0f) < 1e-4f, "Every plane should have a unit normal");
		}
		// A point in the middle is on the inner side of every plane, one behind the camera isn't
		for (const glm::vec4& plane : frustum.Planes)
		{
			Require(glm::dot(glm::vec3(plane), glm::vec3(0.0f, 0.0f, -10.0f)) + plane.w > 0.0f, "A point ahead should be inside every plane");
		}
		bool bOutsideOne = false;
		for (const glm::vec4& plane : frustum.Planes)
		{
			bOutsideOne = bOutsideOne || glm::dot(glm::vec3(plane), glm::vec3(0.0f, 0.0f, 10.0f)) + plane.w < 0.0f;
		}
		Require(bOutsideOne, "A point behind the camera should be outside a plane");
	}

	void TestBoxesAgainstTheFrustum()
	{
		const Frustum frustum = MakeTestFrustum();
		RequireOverlap(frustum, BoxAround(glm::vec3(0.0f, 0.0f, -10.0f), 1.0f), EFrustumOverlap::Inside, "A box ahead");
		RequireOverlap(frustum, BoxAround(glm::vec3(3.0f, -2.0f, -50.0f), 5.0f), EFrustumOverlap::Inside, "A big box far ahead");
		RequireOverlap(frustum, BoxAround(glm::vec3(0.0f, 0.0f, 10.0f), 1.0f), EFrustumOverlap::Outside, "A box behind");
		RequireOverlap(frustum, BoxAround(glm::vec3(0.0f, 0.0f, -200.0f), 1.0f), EFrustumOverlap::Outside, "A box past the far plane");
		RequireOverlap(frustum, BoxAround(glm::vec3(30.0f, 0.0f, -10.0f), 1.0f), EFrustumOverlap::Outside, "A box off to the right");
		RequireOverlap(frustum, BoxAround(glm::vec3(0.0f, 30.0f, -10.0f), 1.0f), EFrustumOverlap::Outside, "A box high above");
		// The left plane is x = z here: at 10 m ahead, x = -10
		RequireOverlap(frustum, BoxAround(glm::vec3(-10.0f, 0.0f, -10.0f), 1.0f), EFrustumOverlap::Straddling, "A box across the left plane");
		RequireOverlap(frustum, BoxAround(glm::vec3(0.0f, 0.0f, -100.0f), 1.0f), EFrustumOverlap::Straddling, "A box across the far plane");
		RequireOverlap(frustum, BoxAround(glm::vec3(0.0f), 0.5f), EFrustumOverlap::Straddling, "A box around the camera");
		RequireOverlap(frustum, BoundingBox{ glm::vec3(-1000.0f), glm::vec3(1000.0f) }, EFrustumOverlap::Straddling, "A box around everything");
		// Just past the right plane, and just inside it
		RequireOverlap(frustum, BoundingBox{ glm::vec3(10.01f, -1.0f, -10.0f), glm::vec3(12.0f, 1.0f, -9.99f) }, EFrustumOverlap::Outside, "A box just right of the frustum");
		RequireOverlap(frustum, BoundingBox{ glm::vec3(8.0f, -1.0f, -10.0f), glm::vec3(9.8f, 1.0f, -9.9f) }, EFrustumOverlap::Inside, "A box just inside the right plane");

		// Moved and turned: at (5, 1, 0) looking along +X
		const glm::mat4 view = glm::lookAt(glm::vec3(5.0f, 1.0f, 0.0f), glm::vec3(6.0f, 1.0f, 0.0f), glm::vec3(0.0f, 1.0f, 0.0f));
		const Frustum turned = MakeFrustum(MakePerspectiveProjection(glm::radians(60.0f), 16.0f / 9.0f, 0.1f, 1000.0f) * view);
		RequireOverlap(turned, BoxAround(glm::vec3(25.0f, 1.0f, 0.0f), 1.0f), EFrustumOverlap::Inside, "A box ahead of the turned camera");
		RequireOverlap(turned, BoxAround(glm::vec3(-5.0f, 1.0f, 0.0f), 1.0f), EFrustumOverlap::Outside, "A box behind the turned camera");
		RequireOverlap(turned, BoxAround(glm::vec3(5.0f, 1.0f, -10.0f), 1.0f), EFrustumOverlap::Outside, "A box beside the turned camera");
	}

	void TestTransformedBoxes()
	{
		const BoundingBox unit{ glm::vec3(-0.5f), glm::vec3(0.5f) };
		const BoundingBox moved = TransformBox(unit, glm::scale(glm::translate(glm::mat4(1.0f), glm::vec3(1.0f, 2.0f, 3.0f)), glm::vec3(2.0f, 1.0f, 4.0f)));
		Require(Near(moved.Min, glm::vec3(0.0f, 1.5f, 1.0f)) && Near(moved.Max, glm::vec3(2.0f, 2.5f, 5.0f)), "A box should move and scale with its transform");

		const BoundingBox turned = TransformBox(unit, glm::rotate(glm::mat4(1.0f), glm::radians(45.0f), glm::vec3(0.0f, 1.0f, 0.0f)));
		const float reach = std::sqrt(2.0f) * 0.5f;
		Require(Near(turned.Min, glm::vec3(-reach, -0.5f, -reach)) && Near(turned.Max, glm::vec3(reach, 0.5f, reach)), "A box turned 45 degrees should be the box around its corners");

		const BoundingBox both = UniteBoxes(unit, BoxAround(glm::vec3(3.0f, 0.0f, 0.0f), 0.5f));
		Require(Near(both.Min, glm::vec3(-0.5f)) && Near(both.Max, glm::vec3(3.5f, 0.5f, 0.5f)), "Two boxes should unite in the box around both");
	}

	glm::mat4 Placed(const glm::vec3& position, float scale = 1.0f)
	{
		return glm::scale(glm::translate(glm::mat4(1.0f), position), glm::vec3(scale));
	}

	void TestInstanceCells()
	{
		const BoundingBox mesh{ glm::vec3(-0.5f, 0.0f, -0.5f), glm::vec3(0.5f, 2.0f, 0.5f) };

		// Without cells: one over all, around the mesh at each instance
		MeshInstances loose;
		loose.Transforms = { Placed(glm::vec3(0.0f)), Placed(glm::vec3(10.0f, 0.0f, 0.0f), 2.0f) };
		std::string problem;
		std::vector<InstanceCullCell> cells = MakeInstanceCullCells(loose, mesh, &problem);
		Require(problem.empty(), "Instances without cells are fine, not " + problem);
		Require(cells.size() == 1 && cells[0].First == 0 && cells[0].Count == 2, "Instances without cells should be one cell over all of them");
		Require(Near(cells[0].Bounds.Min, glm::vec3(-0.5f, 0.0f, -1.0f)) && Near(cells[0].Bounds.Max, glm::vec3(11.0f, 4.0f, 1.0f)),
			"The one cell should hold the mesh at both instances, the second twice as big");

		// Cells: their bounds of the origins, with the mesh added; bounds given too small grow
		MeshInstances grid;
		grid.Transforms = { Placed(glm::vec3(0.0f)), Placed(glm::vec3(1.0f, 0.0f, 0.0f)), Placed(glm::vec3(20.0f, 0.0f, 0.0f)) };
		grid.Cells = {
			MeshInstanceCell{ .BoundsMin = glm::vec3(0.0f), .BoundsMax = glm::vec3(1.0f, 0.0f, 0.0f), .First = 0, .Count = 2 },
			// Says nothing of where its instance is
			MeshInstanceCell{ .BoundsMin = glm::vec3(0.0f), .BoundsMax = glm::vec3(0.0f), .First = 2, .Count = 1 },
		};
		cells = MakeInstanceCullCells(grid, mesh, &problem);
		Require(problem.empty() && cells.size() == 2, "Two cells should stay two");
		Require(Near(cells[0].Bounds.Min, glm::vec3(-0.5f, 0.0f, -0.5f)) && Near(cells[0].Bounds.Max, glm::vec3(1.5f, 2.0f, 0.5f)),
			"A cell's bounds should hold the mesh at each of its instances");
		Require(cells[1].Bounds.Min.x <= 19.5f && cells[1].Bounds.Max.x >= 20.5f, "A cell given bounds that miss its instance should grow to hold it");

		// Turned: a long mesh along x, turned a quarter about y, reaches along z
		MeshInstances turned;
		turned.Transforms = { glm::rotate(glm::mat4(1.0f), glm::radians(90.0f), glm::vec3(0.0f, 1.0f, 0.0f)) };
		cells = MakeInstanceCullCells(turned, BoundingBox{ glm::vec3(-3.0f, -0.1f, -0.1f), glm::vec3(3.0f, 0.1f, 0.1f) });
		Require(Near(cells[0].Bounds.Min, glm::vec3(-0.1f, -0.1f, -3.0f)) && Near(cells[0].Bounds.Max, glm::vec3(0.1f, 0.1f, 3.0f)), "A turned instance's mesh should reach along its turned axis");

		// A cell past the end is cut short and said to be; one without instances is left out
		MeshInstances broken;
		broken.Transforms = { Placed(glm::vec3(0.0f)), Placed(glm::vec3(1.0f, 0.0f, 0.0f)) };
		broken.Cells = { MeshInstanceCell{ .First = 1, .Count = 5 }, MeshInstanceCell{ .First = 0, .Count = 0 }, MeshInstanceCell{ .First = 7, .Count = 2 } };
		cells = MakeInstanceCullCells(broken, mesh, &problem);
		Require(!problem.empty(), "A cell past the transforms should be said to be wrong");
		Require(cells.size() == 1 && cells[0].First == 1 && cells[0].Count == 1, "A cell past the end should be cut short, and empty ones left out");

		Require(MakeInstanceCullCells(MeshInstances{}, mesh).empty(), "No transforms should make no cells");
	}

	void TestCellsInsideOutsideAndStraddling()
	{
		const Frustum frustum = MakeTestFrustum();
		// A row of cells along -Z, 10 m apart, each 2 m across, then one behind the camera, then one
		// across the left plane
		const std::vector<InstanceCullCell> cells = {
			{ BoxAround(glm::vec3(0.0f, 0.0f, -10.0f), 1.0f), 0, 4 },
			{ BoxAround(glm::vec3(0.0f, 0.0f, -20.0f), 1.0f), 4, 4 },
			{ BoxAround(glm::vec3(0.0f, 0.0f, 10.0f), 1.0f), 8, 4 },
			{ BoxAround(glm::vec3(-10.0f, 0.0f, -10.0f), 1.0f), 12, 4 },
			{ BoxAround(glm::vec3(30.0f, 0.0f, -10.0f), 1.0f), 16, 4 },
		};
		RequireOverlap(frustum, cells[0].Bounds, EFrustumOverlap::Inside, "The first cell");
		RequireOverlap(frustum, cells[2].Bounds, EFrustumOverlap::Outside, "The cell behind");
		RequireOverlap(frustum, cells[3].Bounds, EFrustumOverlap::Straddling, "The cell across the left plane");

		std::vector<InstanceRun> runs;
		const uint32_t drawn = AppendInstanceRuns(cells, &frustum, runs);
		Require(drawn == 3, "Three cells should be drawn, not " + std::to_string(drawn));
		Require(runs.size() == 2 && runs[0].First == 0 && runs[0].Count == 8 && runs[1].First == 12 && runs[1].Count == 4,
			"The two cells inside should be one run, the one across the plane another");

		// Without a frustum, every cell, all following on: one run
		std::vector<InstanceRun> all{ InstanceRun{ .First = 99, .Count = 1 } };
		Require(AppendInstanceRuns(cells, nullptr, all) == 5, "Without a frustum every cell should be drawn");
		Require(all.size() == 2 && all[0].First == 99 && all[1].First == 0 && all[1].Count == 20, "Runs already there should be kept, and not joined");

		// The cells of an entity moved 100 m to the right: all out of view
		const glm::mat4 entity = glm::translate(glm::mat4(1.0f), glm::vec3(100.0f, 0.0f, 0.0f));
		std::vector<InstanceCullCell> moved = cells;
		for (InstanceCullCell& cell : moved)
		{
			cell.Bounds = TransformBox(cell.Bounds, entity);
		}
		std::vector<InstanceRun> none;
		Require(AppendInstanceRuns(moved, &frustum, none) == 0 && none.empty(), "Cells moved out of view should draw nothing");
	}
	// How far a box is from a point, and the cells within a draw distance of the camera
	void TestDrawDistance()
	{
		const BoundingBox box = BoxAround(glm::vec3(0.0f, 0.0f, -10.0f), 1.0f);
		Require(GetBoxDistance(box, glm::vec3(0.0f, 0.0f, -10.5f)) == 0.0f, "A point inside a box should be 0 from it");
		Require(std::abs(GetBoxDistance(box, glm::vec3(0.0f)) - 9.0f) < 1e-5f, "A box should be as far as its nearest side");
		Require(std::abs(GetBoxDistance(box, glm::vec3(4.0f, 0.0f, -6.0f)) - std::sqrt(18.0f)) < 1e-5f, "A box should be as far as its nearest edge");

		const Frustum frustum = MakeTestFrustum();
		const std::vector<InstanceCullCell> cells = {
			{ BoxAround(glm::vec3(0.0f, 0.0f, -10.0f), 1.0f), 0, 4 },
			{ BoxAround(glm::vec3(0.0f, 0.0f, -20.0f), 1.0f), 4, 4 },
			{ BoxAround(glm::vec3(-10.0f, 0.0f, -10.0f), 1.0f), 8, 4 },
		};
		const DrawDistanceLimit limit{ .From = glm::vec3(0.0f), .Distance = 15.0f };
		std::vector<InstanceRun> runs;
		Require(AppendInstanceRuns(cells, &frustum, runs, &limit) == 2, "The cells within 15 m should be drawn, the one 19 m off not");
		Require(runs.size() == 2 && runs[0].First == 0 && runs[1].First == 8, "The cell beyond the distance should break the run");
		std::vector<InstanceRun> unlimited;
		Require(AppendInstanceRuns(cells, &frustum, unlimited) == 3, "Without a limit, every cell in view should be drawn");
	}
}

int main()
{
	try
	{
		TestFrustumPlanes();
		TestBoxesAgainstTheFrustum();
		TestTransformedBoxes();
		TestInstanceCells();
		TestCellsInsideOutsideAndStraddling();
		TestDrawDistance();
	}
	catch (const std::exception& error)
	{
		std::cerr << "FAILED: " << error.what() << "\n";
		return 1;
	}

	std::cout << "All culling tests passed\n";
	return 0;
}
