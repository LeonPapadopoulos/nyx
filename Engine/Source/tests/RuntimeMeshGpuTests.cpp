// Meshes made at run time (RuntimeMeshComponent) on a real GPU: a sphere made in code draws exactly
// as the built-in sphere loaded by its path; changing a mesh and raising its revision redraws it,
// while drawing on without a change uploads nothing; entities that share a mesh share its upload,
// and a mesh nobody draws any more is let go of; a mesh that isn't valid draws nothing; a click
// picks it and its selection is outlined; two sections draw with the materials of their two slots,
// a slot without one with the first; a surface decal on it is drawn; thousands of meshes in a block
// or two of the pool's memory, given back when let go of.
//
// NYX_SCENE_TEST_OUTPUT=<folder> saves what was rendered there.
#include "CameraComponent.h"
#include "Entity.h"
#include "GuidComponent.h"
#include "ImageFile.h"
#include "Log.h"
#include "MeshRendererComponent.h"
#include "RuntimeMeshComponent.h"
#include "SurfaceDecalComponent.h"
#include "TransformComponent.h"
#include "VulkanRenderer.h"
#include "RenderTestSupport.h"

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <cstdlib>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace
{
	using namespace Nyx;
	using namespace Nyx::RenderTests;

	constexpr uint32_t ViewWidth = 480;
	constexpr uint32_t ViewHeight = 270;

	void SaveIfAsked(const RgbaImage& image, const std::string& name)
	{
		const char* folder = std::getenv("NYX_SCENE_TEST_OUTPUT");
		if (folder == nullptr || *folder == '\0')
		{
			return;
		}
		const std::filesystem::path path = std::filesystem::path(folder) / ("RuntimeMesh-" + name + ".png");
		std::string error;
		Require(SavePng(path, image, error), "Couldn't save " + path.string() + ": " + error);
		std::cout << "Saved " << path.string() << "\n";
	}

	RgbaImage ReadView(SceneRenderer& gpu, uint64_t view)
	{
		RgbaImage image;
		Require(gpu.Renderer.ReadSceneViewPixels(view, image), "The scene view can't be read back");
		return image;
	}

	std::array<uint8_t, 3> PixelAt(const RgbaImage& image, uint32_t x, uint32_t y)
	{
		const size_t offset = (static_cast<size_t>(y) * image.Width + x) * 4;
		return { image.Pixels[offset], image.Pixels[offset + 1], image.Pixels[offset + 2] };
	}

	// A rectangle facing +Z, towards the cameras here, from `min` to `max` in x and y, as two
	// triangles at the end of the mesh
	void AddRectangle(MeshData& mesh, glm::vec2 min, glm::vec2 max)
	{
		const uint32_t first = static_cast<uint32_t>(mesh.Vertices.size());
		const glm::vec3 normal(0.0f, 0.0f, 1.0f);
		mesh.Vertices.push_back(Vertex{ .Position = glm::vec3(min.x, min.y, 0.0f), .UV = glm::vec2(0.0f, 1.0f), .Normal = normal });
		mesh.Vertices.push_back(Vertex{ .Position = glm::vec3(max.x, min.y, 0.0f), .UV = glm::vec2(1.0f, 1.0f), .Normal = normal });
		mesh.Vertices.push_back(Vertex{ .Position = glm::vec3(max.x, max.y, 0.0f), .UV = glm::vec2(1.0f, 0.0f), .Normal = normal });
		mesh.Vertices.push_back(Vertex{ .Position = glm::vec3(min.x, max.y, 0.0f), .UV = glm::vec2(0.0f, 0.0f), .Normal = normal });
		// Counter-clockwise seen from the front, like the cube's faces
		mesh.Indices.insert(mesh.Indices.end(), { first, first + 1, first + 2, first + 2, first + 3, first });
	}

	// A world seen by a camera 3 m in front of the origin, looking at it, in a view of its own
	struct TestScene
	{
		SceneRenderer& Gpu;
		Registry World;
		uint64_t View = 0;

		TestScene(SceneRenderer& gpu, bool bEditorOverlays)
			: Gpu(gpu)
		{
			const Entity camera = World.CreateEntity();
			World.Add<TransformComponent>(camera, TransformComponent{ .Position = glm::vec3(0.0f, 0.0f, 3.0f), .Rotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f), .Scale = glm::vec3(1.0f) });
			World.Add<CameraComponent>(camera, CameraComponent{});

			Nyx::IEditorRenderer& renderer = gpu.Renderer;
			renderer.SetWorld(&World);
			View = renderer.CreateSceneView();
			renderer.SetSceneViewShowEditorOverlays(View, bEditorOverlays);
			renderer.SetSceneViewCameraMode(View, Nyx::EViewportCameraMode::ScenePrimaryCamera);
			renderer.SetSceneViewSize(View, ViewWidth, ViewHeight);
		}

		~TestScene()
		{
			Nyx::IEditorRenderer& renderer = Gpu.Renderer;
			renderer.SetSelectedEntity(std::nullopt);
			renderer.DestroySceneView(View);
			renderer.SetWorld(nullptr);
			Gpu.DrawFrames(1);
		}

		// An entity at the origin drawn with `material`, with the mesh made in code
		Entity AddRuntimeMesh(std::shared_ptr<const MeshData> mesh, Material* material)
		{
			const Entity entity = World.CreateEntity();
			World.Add<TransformComponent>(entity, TransformComponent{ .Position = glm::vec3(0.0f), .Rotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f), .Scale = glm::vec3(1.0f) });
			MeshRendererComponent meshRenderer;
			meshRenderer.MaterialAsset = material;
			World.Add<MeshRendererComponent>(entity, meshRenderer);
			RuntimeMeshComponent runtimeMesh;
			SetRuntimeMesh(runtimeMesh, std::move(mesh));
			World.Add<RuntimeMeshComponent>(entity, runtimeMesh);
			return entity;
		}

		RgbaImage Draw(int frames = 2)
		{
			Gpu.DrawFrames(frames);
			return ReadView(Gpu, View);
		}

		Nyx::IEditorRenderer::PickResult PickMiddle()
		{
			Nyx::IEditorRenderer& renderer = Gpu.Renderer;
			renderer.RequestPick(View, ViewWidth / 2, ViewHeight / 2);
			Gpu.DrawFrames(3);
			return renderer.ConsumeLastPickResult(View);
		}
	};

	// The built-in sphere, by its mesh as a scene's path resolves it, then the same sphere made in
	// code: the same picture, to the bit
	void TestDrawsLikeALoadedMesh(SceneRenderer& gpu)
	{
		TestScene scene(gpu, false);
		Material* material = gpu.Renderer.GetMaterial("Materials/Untextured.nyxmat");
		const Entity loaded = scene.World.CreateEntity();
		scene.World.Add<TransformComponent>(loaded, TransformComponent{ .Position = glm::vec3(0.0f), .Rotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f), .Scale = glm::vec3(2.0f) });
		MeshRendererComponent meshRenderer;
		meshRenderer.MeshAsset = gpu.Renderer.GetSphereMesh();
		meshRenderer.MaterialAsset = material;
		scene.World.Add<MeshRendererComponent>(loaded, meshRenderer);
		gpu.DrawUntilShadersCompiled();
		const RgbaImage fromTheAsset = scene.Draw();
		SaveIfAsked(fromTheAsset, "Loaded");

		// Made in code as the renderer makes its own, on the same entity: it takes the mesh's place
		RuntimeMeshComponent runtimeMesh;
		SetRuntimeMesh(runtimeMesh, std::make_shared<MeshData>(MakeSphere(0.5f, 32, 64)));
		scene.World.Add<RuntimeMeshComponent>(loaded, runtimeMesh);
		const RgbaImage madeInCode = scene.Draw();
		SaveIfAsked(madeInCode, "MadeInCode");
		Require(madeInCode.Pixels == fromTheAsset.Pixels, "The sphere made in code isn't drawn as the built-in sphere is");
		Require(gpu.Renderer.GetRuntimeMeshes().GetMeshCount() == 1, "The sphere made in code isn't one mesh on the GPU");
	}

	// A rectangle in the middle, changed where it is: drawn as it was until its revision says so,
	// then as it is now; nothing is uploaded again while nothing changes
	void TestChangesRedraw(SceneRenderer& gpu)
	{
		TestScene scene(gpu, false);
		const auto mesh = std::make_shared<MeshData>();
		AddRectangle(*mesh, glm::vec2(-1.0f, -0.5f), glm::vec2(1.0f, 0.5f));
		const Entity entity = scene.AddRuntimeMesh(mesh, gpu.Renderer.GetMaterial("Materials/Untextured.nyxmat"));
		gpu.DrawUntilShadersCompiled();

		const RuntimeMeshCache& meshes = gpu.Renderer.GetRuntimeMeshes();
		const RgbaImage drawn = scene.Draw();
		const uint64_t uploads = meshes.GetUploadCount();
		const std::array<uint8_t, 3> rectangle = PixelAt(drawn, ViewWidth / 2, ViewHeight / 2);
		scene.Draw(5);
		Require(meshes.GetUploadCount() == uploads, "Drawing an unchanged mesh uploads it again");

		// Moved out of the picture without saying so: drawn as it was
		for (Vertex& vertex : mesh->Vertices)
		{
			vertex.Position.x += 10.0f;
		}
		Require(scene.Draw().Pixels == drawn.Pixels, "A mesh changed without a new revision is drawn anew");
		Require(meshes.GetUploadCount() == uploads, "A mesh changed without a new revision is uploaded");

		MarkRuntimeMeshChanged(scene.World.Get<RuntimeMeshComponent>(entity));
		const RgbaImage moved = scene.Draw();
		SaveIfAsked(moved, "Moved");
		Require(meshes.GetUploadCount() == uploads + 1, "A mesh with a new revision isn't uploaded once");
		Require(!Near(PixelAt(moved, ViewWidth / 2, ViewHeight / 2), rectangle, 8), "The rectangle moved away is still drawn in the middle");
		scene.Draw(3);
		Require(meshes.GetUploadCount() == uploads + 1, "A changed mesh is uploaded again while it stays the same");

		// New data: drawn at once
		const auto back = std::make_shared<MeshData>();
		AddRectangle(*back, glm::vec2(-1.0f, -0.5f), glm::vec2(1.0f, 0.5f));
		SetRuntimeMesh(scene.World.Get<RuntimeMeshComponent>(entity), back);
		Require(scene.Draw().Pixels == drawn.Pixels, "The rectangle given anew isn't drawn as it was at first");

		// Another entity on the same data shares its upload; a mesh nobody draws is let go of
		const Entity second = scene.AddRuntimeMesh(back, gpu.Renderer.GetMaterial("Materials/Untextured.nyxmat"));
		scene.World.Get<TransformComponent>(second).Position.y = 1.0f;
		scene.Draw();
		Require(meshes.GetMeshCount() == 1, "Two entities on one mesh have " + std::to_string(meshes.GetMeshCount()) + " meshes on the GPU");
		scene.World.Remove<RuntimeMeshComponent>(entity);
		scene.World.Remove<RuntimeMeshComponent>(second);
		scene.Draw();
		Require(meshes.GetMeshCount() == 0, "A mesh nobody draws is kept on the GPU");

		// Indices past the vertices: nothing drawn, nothing broken
		const auto broken = std::make_shared<MeshData>();
		AddRectangle(*broken, glm::vec2(-1.0f, -0.5f), glm::vec2(1.0f, 0.5f));
		broken->Indices[4] = 99;
		const Entity third = scene.AddRuntimeMesh(broken, gpu.Renderer.GetMaterial("Materials/Untextured.nyxmat"));
		const RgbaImage withBroken = scene.Draw();
		scene.World.DestroyEntity(third);
		Require(withBroken.Pixels == scene.Draw().Pixels, "A mesh whose indices run past its vertices is drawn");
	}

	// A click on the rectangle, open at the back as a ground is, picks its entity; selected, it is
	// outlined
	void TestPickingAndOutline(SceneRenderer& gpu)
	{
		TestScene scene(gpu, true);
		const auto mesh = std::make_shared<MeshData>();
		AddRectangle(*mesh, glm::vec2(-1.0f, -0.5f), glm::vec2(1.0f, 0.5f));
		const Entity entity = scene.AddRuntimeMesh(mesh, gpu.Renderer.GetMaterial("Materials/Untextured.nyxmat"));
		gpu.DrawUntilShadersCompiled();
		// Drawn once at its size first: a view made anew drops the clicks before that
		const RgbaImage unselected = scene.Draw();
		SaveIfAsked(unselected, "Unselected");

		const Nyx::IEditorRenderer::PickResult pick = scene.PickMiddle();
		Require(pick.bHasNewResult && pick.HitEntity == entity, "A click on the mesh made in code doesn't pick it");

		gpu.Renderer.SetSelectedEntity(entity);
		const RgbaImage selected = scene.Draw();
		SaveIfAsked(selected, "Selected");
		Require(selected.Pixels != unselected.Pixels, "The selected mesh made in code isn't outlined");
	}

	// Two rectangles side by side, one section each: the left with the first slot's material, the
	// right with the second's
	void TestTwoSections(SceneRenderer& gpu)
	{
		LiveMaterialFile red("Materials/NyxRuntimeMeshGpuTestsRed.nyxmat");
		red.Write("Shader = Shaders/Standard.slang\nBaseColor = 1 0.05 0.05\nRoughness = 0.35\n");

		TestScene scene(gpu, false);
		const auto mesh = std::make_shared<MeshData>();
		AddRectangle(*mesh, glm::vec2(-1.0f, -0.5f), glm::vec2(0.0f, 0.5f));
		AddRectangle(*mesh, glm::vec2(0.0f, -0.5f), glm::vec2(1.0f, 0.5f));
		mesh->Sections = { MeshSection{ .FirstIndex = 0, .IndexCount = 6, .MaterialSlot = 0 }, MeshSection{ .FirstIndex = 6, .IndexCount = 6, .MaterialSlot = 1 } };
		Material* blue = gpu.Renderer.GetMaterial("Materials/Untextured.nyxmat");
		const Entity entity = scene.AddRuntimeMesh(mesh, blue);
		gpu.DrawUntilShadersCompiled();

		const uint32_t left = ViewWidth / 2 - 40;
		const uint32_t right = ViewWidth / 2 + 40;
		// The second slot without a material: drawn with the first's
		const RgbaImage oneMaterial = scene.Draw();
		Require(Near(PixelAt(oneMaterial, left, ViewHeight / 2), PixelAt(oneMaterial, right, ViewHeight / 2)),
			"A section whose slot has no material isn't drawn with the first slot's");

		scene.World.Get<MeshRendererComponent>(entity).ExtraMaterialAssets = { gpu.Renderer.GetMaterial(red.Path) };
		gpu.DrawUntilShadersCompiled();
		const RgbaImage twoMaterials = scene.Draw();
		SaveIfAsked(twoMaterials, "TwoSections");
		Require(PixelAt(twoMaterials, left, ViewHeight / 2) == PixelAt(oneMaterial, left, ViewHeight / 2), "The first section changed with the second slot's material");
		const std::array<uint8_t, 3> rightPixel = PixelAt(twoMaterials, right, ViewHeight / 2);
		Require(rightPixel[0] > 120 && rightPixel[1] < 60 && rightPixel[2] < 60, "The second section isn't drawn red but " + Text(rightPixel));

		// Both slots red: the right half as before, so the material is the slot's, not the place's
		scene.World.Get<MeshRendererComponent>(entity).MaterialAsset = gpu.Renderer.GetMaterial(red.Path);
		const RgbaImage allRed = scene.Draw();
		Require(PixelAt(allRed, right, ViewHeight / 2) == rightPixel && PixelAt(allRed, left, ViewHeight / 2) == rightPixel,
			"The two sections in the same material don't look alike");
	}

	// A decal on the rectangle's front brightens it, as on a cube
	void TestDecalOnIt(SceneRenderer& gpu)
	{
		TestScene scene(gpu, false);
		const auto mesh = std::make_shared<MeshData>();
		AddRectangle(*mesh, glm::vec2(-1.0f, -1.0f), glm::vec2(1.0f, 1.0f));
		const Entity host = scene.AddRuntimeMesh(mesh, gpu.Renderer.GetMaterial("Materials/Untextured.nyxmat"));
		scene.World.Add<GuidComponent>(host, GuidComponent{ EntityGuid{ 0x9A11 } });
		gpu.DrawUntilShadersCompiled();
		const RgbaImage without = scene.Draw();

		const Entity decalEntity = scene.World.CreateEntity();
		SurfaceDecalComponent decal;
		decal.Anchor = SurfaceAnchor{ .Host = EntityGuid{ 0x9A11 }, .LocalPosition = glm::vec3(0.0f), .LocalNormal = glm::vec3(0.0f, 0.0f, 1.0f), .LocalTangent = glm::vec3(1.0f, 0.0f, 0.0f) };
		decal.Size = glm::vec2(0.8f, 0.8f);
		decal.MaterialAsset = gpu.Renderer.GetMaterial("Materials/MaskedDecal.nyxmat");
		decal.Color = glm::vec3(0.2f, 1.0f, 0.6f);
		decal.Intensity = 4.0f;
		scene.World.Add<SurfaceDecalComponent>(decalEntity, decal);
		gpu.DrawUntilShadersCompiled();
		const RgbaImage with = scene.Draw();
		SaveIfAsked(with, "Decal");

		size_t brightened = 0;
		for (size_t pixel = 0; pixel < with.Pixels.size(); pixel += 4)
		{
			int gain = 0;
			for (size_t channel = 0; channel < 3; ++channel)
			{
				gain += int(with.Pixels[pixel + channel]) - int(without.Pixels[pixel + channel]);
			}
			brightened += gain > 30 ? 1 : 0;
		}
		Require(brightened > 500, "The decal on the mesh made in code brightens only " + std::to_string(brightened) + " pixels");
	}
	// Thousands of meshes, each an entity's own: drawn, all in a block or two of the pool's memory
	// instead of an allocation each, their data copied in by the frame; let go of, their memory goes
	// back; made again, they fit in what the pool has
	void TestManyMeshes(SceneRenderer& gpu)
	{
		TestScene scene(gpu, false);
		Material* material = gpu.Renderer.GetMaterial("Materials/Untextured.nyxmat");
		gpu.DrawUntilShadersCompiled();
		const RgbaImage empty = scene.Draw();

		constexpr int Side = 50;
		std::vector<Entity> entities;
		auto addAll = [&]
		{
			for (int row = 0; row < Side; ++row)
			{
				for (int column = 0; column < Side; ++column)
				{
					const auto mesh = std::make_shared<MeshData>();
					const glm::vec2 corner(-2.0f + 4.0f * float(column) / Side, -1.2f + 2.4f * float(row) / Side);
					AddRectangle(*mesh, corner, corner + glm::vec2(0.04f));
					entities.push_back(scene.AddRuntimeMesh(mesh, material));
				}
			}
		};
		addAll();
		const RgbaImage drawn = scene.Draw();
		SaveIfAsked(drawn, "Many");

		const RuntimeMeshCache& meshes = gpu.Renderer.GetRuntimeMeshes();
		const MeshMemoryPool::Stats full = meshes.GetPoolStats();
		Require(meshes.GetMeshCount() == Side * Side, "Every mesh should be on the GPU");
		Require(full.Allocations == 2 * Side * Side, "Each mesh should have its vertices and indices in the pool, not " + std::to_string(full.Allocations));
		Require(full.Blocks <= 2, "Thousands of small meshes should fit in a block or two, not " + std::to_string(full.Blocks));
		Require(drawn.Pixels != empty.Pixels, "The meshes should be drawn");
		size_t changed = 0;
		for (size_t pixel = 0; pixel < drawn.Pixels.size(); pixel += 4)
		{
			changed += drawn.Pixels[pixel] != empty.Pixels[pixel] ? 1 : 0;
		}
		Require(changed > 2000, "The many rectangles should cover many pixels, not " + std::to_string(changed));

		for (const Entity entity : entities)
		{
			scene.World.Remove<RuntimeMeshComponent>(entity);
		}
		scene.Draw(3);
		const MeshMemoryPool::Stats emptied = meshes.GetPoolStats();
		Require(meshes.GetMeshCount() == 0 && emptied.Allocations == 0 && emptied.UsedBytes == 0, "Meshes let go of should give their memory back");
		Require(emptied.Blocks <= 1, "An emptied pool should keep one block at most");

		for (const Entity entity : entities)
		{
			scene.World.DestroyEntity(entity);
		}
		entities.clear();
		addAll();
		const RgbaImage again = scene.Draw();
		Require(meshes.GetPoolStats().Blocks <= full.Blocks, "Meshes made again should fit in the blocks the pool had");
		Require(again.Pixels == drawn.Pixels, "Meshes made again should be drawn as they were");
		std::cout << "Many meshes: " << full.Allocations << " buffers in " << full.Blocks << " blocks, " << full.UsedBytes << " bytes used of " << full.BlockBytes
				  << "\n";
		for (const Entity entity : entities)
		{
			scene.World.DestroyEntity(entity);
		}
	}
}

int main()
{
	try
	{
		Nyx::Logger::Get().Init();
		SceneRenderer gpu("NyxRuntimeMeshGpuTests");
		TestDrawsLikeALoadedMesh(gpu);
		TestChangesRedraw(gpu);
		TestPickingAndOutline(gpu);
		TestTwoSections(gpu);
		TestDecalOnIt(gpu);
		TestManyMeshes(gpu);
	}
	catch (const std::exception& error)
	{
		std::cerr << "FAILED: " << error.what() << "\n";
		return 1;
	}

	std::cout << "All run-time mesh GPU tests passed\n";
	return 0;
}
