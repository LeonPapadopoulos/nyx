// Instanced drawing and culling on a real GPU: a mesh drawn at every transform of its entity's
// MeshInstancesComponent looks as the same mesh on one entity for each, turned, scaled and lit
// alike; its transforms go up once, and again only when the revision changes; entities sharing
// instances share their upload; a click on an instance picks its entity; cells and entities out of
// view aren't drawn, and the picture is the same for it; a view frozen for its culling draws what
// its camera saw.
//
// NYX_SCENE_TEST_OUTPUT=<folder> saves what was rendered there.
#include "CameraComponent.h"
#include "CameraProjection.h"
#include "Entity.h"
#include "ImageFile.h"
#include "Log.h"
#include "MeshInstancesComponent.h"
#include "MeshRendererComponent.h"
#include "RuntimeMeshComponent.h"
#include "TransformComponent.h"
#include "VulkanRenderer.h"
#include "RenderTestSupport.h"

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtx/matrix_decompose.hpp>

#include <cstdlib>
#include <iostream>
#include <memory>
#include <optional>
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
		const std::filesystem::path path = std::filesystem::path(folder) / ("Instancing-" + name + ".png");
		std::string error;
		Require(SavePng(path, image, error), "Couldn't save " + path.string() + ": " + error);
		std::cout << "Saved " << path.string() << "\n";
	}

	std::array<uint8_t, 3> PixelAt(const RgbaImage& image, uint32_t x, uint32_t y)
	{
		const size_t offset = (static_cast<size_t>(y) * image.Width + x) * 4;
		return { image.Pixels[offset], image.Pixels[offset + 1], image.Pixels[offset + 2] };
	}

	// How many pixels differ by more than `tolerance` in a channel
	size_t CountDifferent(const RgbaImage& a, const RgbaImage& b, int tolerance)
	{
		size_t different = 0;
		for (size_t pixel = 0; pixel < a.Pixels.size(); pixel += 4)
		{
			for (size_t channel = 0; channel < 3; ++channel)
			{
				if (std::abs(int(a.Pixels[pixel + channel]) - int(b.Pixels[pixel + channel])) > tolerance)
				{
					++different;
					break;
				}
			}
		}
		return different;
	}

	TransformComponent Unmoved()
	{
		return TransformComponent{ .Position = glm::vec3(0.0f), .Rotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f), .Scale = glm::vec3(1.0f) };
	}

	// A world seen by a camera 6 m in front of the origin, looking at it, in a view of its own
	struct TestScene
	{
		SceneRenderer& Gpu;
		Registry World;
		Entity Camera;
		uint64_t View = 0;

		TestScene(SceneRenderer& gpu, bool bEditorOverlays)
			: Gpu(gpu)
		{
			Camera = World.CreateEntity();
			World.Add<TransformComponent>(Camera, TransformComponent{ .Position = glm::vec3(0.0f, 0.0f, 6.0f), .Rotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f), .Scale = glm::vec3(1.0f) });
			World.Add<CameraComponent>(Camera, CameraComponent{});

			IEditorRenderer& renderer = gpu.Renderer;
			renderer.SetWorld(&World);
			View = renderer.CreateSceneView();
			renderer.SetSceneViewShowEditorOverlays(View, bEditorOverlays);
			renderer.SetSceneViewCameraMode(View, EViewportCameraMode::ScenePrimaryCamera);
			renderer.SetSceneViewSize(View, ViewWidth, ViewHeight);
		}

		~TestScene()
		{
			IEditorRenderer& renderer = Gpu.Renderer;
			renderer.SetSelectedEntity(std::nullopt);
			renderer.DestroySceneView(View);
			renderer.SetWorld(nullptr);
			Gpu.DrawFrames(1);
		}

		Entity AddMesh(const TransformComponent& transform, Mesh* mesh, Material* material)
		{
			const Entity entity = World.CreateEntity();
			World.Add<TransformComponent>(entity, transform);
			MeshRendererComponent meshRenderer;
			meshRenderer.MeshAsset = mesh;
			meshRenderer.MaterialAsset = material;
			World.Add<MeshRendererComponent>(entity, meshRenderer);
			return entity;
		}

		Entity AddInstanced(const TransformComponent& transform, Mesh* mesh, Material* material, std::shared_ptr<const MeshInstances> instances)
		{
			const Entity entity = AddMesh(transform, mesh, material);
			MeshInstancesComponent meshInstances;
			SetMeshInstances(meshInstances, std::move(instances));
			World.Add<MeshInstancesComponent>(entity, meshInstances);
			return entity;
		}

		RgbaImage Draw(int frames = 2)
		{
			Gpu.DrawFrames(frames);
			RgbaImage image;
			Require(Gpu.Renderer.ReadSceneViewPixels(View, image), "The scene view can't be read back");
			return image;
		}

		const SceneViewCullingStats& Culling() const
		{
			const SceneViewCullingStats* stats = Gpu.Renderer.GetSceneViewCullingStats(View);
			Require(stats != nullptr, "The view has no culling statistics");
			return *stats;
		}
	};

	// Five spheres in a row, each turned and squashed its own way: where they are in the picture
	glm::mat4 RowTransform(int index)
	{
		const float x = -2.4f + 1.2f * static_cast<float>(index);
		glm::mat4 transform = glm::translate(glm::mat4(1.0f), glm::vec3(x, 0.2f * static_cast<float>(index % 2), 0.0f));
		transform = glm::rotate(transform, 0.6f * static_cast<float>(index), glm::normalize(glm::vec3(1.0f, 1.0f, 0.3f)));
		return glm::scale(transform, glm::vec3(0.9f, 0.6f + 0.1f * static_cast<float>(index), 0.8f));
	}

	TransformComponent FromMatrix(const glm::mat4& matrix)
	{
		TransformComponent transform;
		glm::vec3 skew;
		glm::vec4 perspective;
		glm::decompose(matrix, transform.Scale, transform.Rotation, transform.Position, skew, perspective);
		return transform;
	}

	// The row drawn instanced, on an entity moved and turned itself, against the same five drawn as
	// five entities at the same places: alike but for rounding at the edges, normals included, as
	// the light shows them
	void TestDrawsEveryInstance(SceneRenderer& gpu)
	{
		Material* material = gpu.Renderer.GetMaterial("Materials/Untextured.nyxmat");
		const glm::mat4 entityTransform = glm::rotate(glm::translate(glm::mat4(1.0f), glm::vec3(0.1f, -0.3f, 0.0f)), 0.3f, glm::vec3(0.0f, 0.0f, 1.0f));
		auto row = std::make_shared<MeshInstances>();
		for (int index = 0; index < 5; ++index)
		{
			row->Transforms.push_back(RowTransform(index));
		}

		RgbaImage plain;
		{
			TestScene scene(gpu, false);
			for (int index = 0; index < 5; ++index)
			{
				scene.AddMesh(FromMatrix(entityTransform * RowTransform(index)), gpu.Renderer.GetSphereMesh(), material);
			}
			gpu.DrawUntilShadersCompiled();
			plain = scene.Draw();
			SaveIfAsked(plain, "Plain");
			Require(scene.Culling().Draws < 5, "Five spheres alike should be drawn in fewer than five draws, not " + std::to_string(scene.Culling().Draws));
		}

		RgbaImage empty;
		{
			TestScene nothing(gpu, false);
			empty = nothing.Draw();
		}

		TestScene scene(gpu, false);
		scene.AddInstanced(FromMatrix(entityTransform), gpu.Renderer.GetSphereMesh(), material, row);
		const RgbaImage instanced = scene.Draw();
		SaveIfAsked(instanced, "Instanced");
		// Every sphere is there: the picture differs from an empty one at each of their middles
		for (int index = 0; index < 5; ++index)
		{
			const glm::vec3 middle = glm::vec3(entityTransform * RowTransform(index) * glm::vec4(0.0f, 0.0f, 0.0f, 1.0f));
			const std::optional<glm::vec2> pixel = WorldToScreenPoint(scene.World.Get<TransformComponent>(scene.Camera), scene.World.Get<CameraComponent>(scene.Camera),
				glm::vec2(ViewWidth, ViewHeight), middle);
			Require(pixel.has_value(), "Instance " + std::to_string(index) + " should be in front of the camera");
			const uint32_t x = static_cast<uint32_t>(pixel->x);
			const uint32_t y = static_cast<uint32_t>(pixel->y);
			Require(!Near(PixelAt(instanced, x, y), PixelAt(empty, x, y), 8), "Instance " + std::to_string(index) + " isn't drawn");
		}

		const size_t different = CountDifferent(instanced, plain, 6);
		Require(different < 200, "The instanced row differs from the row of entities in " + std::to_string(different) + " pixels");
		const SceneViewCullingStats& culling = scene.Culling();
		Require(culling.Instances == 5 && culling.Draws == 1, "The row should be one draw of five instances, not " + std::to_string(culling.Draws) + " of " + std::to_string(culling.Instances));
	}

	// A rectangle made at run time, drawn at three places: uploaded once while it stays, again for
	// a new revision or new data, shared by two entities, let go of when nobody draws it
	void TestRevisionReuploads(SceneRenderer& gpu)
	{
		TestScene scene(gpu, false);
		// What the middle of the view shows without anything in it
		const std::array<uint8_t, 3> background = PixelAt(scene.Draw(), ViewWidth / 2, ViewHeight / 2);
		auto rectangle = std::make_shared<MeshData>();
		const glm::vec3 normal(0.0f, 0.0f, 1.0f);
		rectangle->Vertices = { Vertex{ .Position = glm::vec3(-0.4f, -0.4f, 0.0f), .Normal = normal }, Vertex{ .Position = glm::vec3(0.4f, -0.4f, 0.0f), .Normal = normal },
			Vertex{ .Position = glm::vec3(0.4f, 0.4f, 0.0f), .Normal = normal }, Vertex{ .Position = glm::vec3(-0.4f, 0.4f, 0.0f), .Normal = normal } };
		rectangle->Indices = { 0, 1, 2, 2, 3, 0 };

		auto places = std::make_shared<MeshInstances>();
		for (const float x : { -2.0f, 0.0f, 2.0f })
		{
			places->Transforms.push_back(glm::translate(glm::mat4(1.0f), glm::vec3(x, 0.0f, 0.0f)));
		}
		Material* material = gpu.Renderer.GetMaterial("Materials/Untextured.nyxmat");
		const Entity entity = scene.AddInstanced(Unmoved(), nullptr, material, places);
		RuntimeMeshComponent runtimeMesh;
		SetRuntimeMesh(runtimeMesh, rectangle);
		scene.World.Add<RuntimeMeshComponent>(entity, runtimeMesh);
		gpu.DrawUntilShadersCompiled();

		const MeshInstanceCache& cache = gpu.Renderer.GetMeshInstances();
		const RgbaImage drawn = scene.Draw();
		SaveIfAsked(drawn, "Three");
		const uint64_t uploads = cache.GetUploadCount();
		Require(!Near(PixelAt(drawn, ViewWidth / 2, ViewHeight / 2), background, 8), "The middle rectangle isn't drawn");
		scene.Draw(5);
		Require(cache.GetUploadCount() == uploads, "Unchanged instances are uploaded again");

		// The middle one moved away without saying so: drawn where it was
		places->Transforms[1] = glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 10.0f, 0.0f));
		Require(scene.Draw().Pixels == drawn.Pixels, "Instances changed without a new revision are drawn anew");
		Require(cache.GetUploadCount() == uploads, "Instances changed without a new revision are uploaded");

		++scene.World.Get<MeshInstancesComponent>(entity).Revision;
		const RgbaImage moved = scene.Draw();
		SaveIfAsked(moved, "MiddleMoved");
		Require(cache.GetUploadCount() == uploads + 1, "Instances with a new revision aren't uploaded once");
		Require(Near(PixelAt(moved, ViewWidth / 2, ViewHeight / 2), background, 8), "The middle rectangle moved away is still drawn");
		scene.Draw(3);
		Require(cache.GetUploadCount() == uploads + 1, "Changed instances are uploaded again while they stay the same");

		// New data: drawn at once
		auto back = std::make_shared<MeshInstances>();
		for (const float x : { -2.0f, 0.0f, 2.0f })
		{
			back->Transforms.push_back(glm::translate(glm::mat4(1.0f), glm::vec3(x, 0.0f, 0.0f)));
		}
		SetMeshInstances(scene.World.Get<MeshInstancesComponent>(entity), back);
		Require(scene.Draw().Pixels == drawn.Pixels, "The instances given anew aren't drawn as they were at first");

		// Another entity on the same instances shares their upload; instances nobody draws are let go of
		const Entity second = scene.AddInstanced(Unmoved(), nullptr, material, back);
		scene.World.Add<RuntimeMeshComponent>(second, runtimeMesh);
		scene.World.Get<TransformComponent>(second).Position.y = 1.0f;
		scene.Draw();
		Require(cache.GetCount() == 1, "Two entities on one set of instances have " + std::to_string(cache.GetCount()) + " on the GPU");
		scene.World.Remove<MeshInstancesComponent>(entity);
		scene.World.Remove<MeshInstancesComponent>(second);
		scene.Draw();
		Require(cache.GetCount() == 0, "Instances nobody draws are kept on the GPU");

		// No transforms: nothing drawn, nothing broken
		scene.World.DestroyEntity(second);
		const RgbaImage single = scene.Draw();
		scene.World.Add<MeshInstancesComponent>(entity, MeshInstancesComponent{ .Instances = std::make_shared<MeshInstances>(), .Revision = 1 });
		const RgbaImage none = scene.Draw();
		Require(!Near(PixelAt(single, ViewWidth / 2, ViewHeight / 2), background, 8) && Near(PixelAt(none, ViewWidth / 2, ViewHeight / 2), background, 8),
			"An entity with no instances should draw nothing, and without the component once");
	}

	// A click on an instance picks its entity
	void TestPicking(SceneRenderer& gpu)
	{
		TestScene scene(gpu, true);
		auto instances = std::make_shared<MeshInstances>();
		instances->Transforms = { glm::translate(glm::mat4(1.0f), glm::vec3(-1.5f, 0.0f, 0.0f)), glm::mat4(1.0f), glm::translate(glm::mat4(1.0f), glm::vec3(1.5f, 0.0f, 0.0f)) };
		const Entity entity = scene.AddInstanced(Unmoved(), gpu.Renderer.GetCubeMesh(), gpu.Renderer.GetMaterial("Materials/Untextured.nyxmat"), instances);
		gpu.DrawUntilShadersCompiled();
		const RgbaImage unselected = scene.Draw();

		IEditorRenderer& renderer = gpu.Renderer;
		renderer.RequestPick(scene.View, ViewWidth / 2, ViewHeight / 2);
		gpu.DrawFrames(3);
		const IEditorRenderer::PickResult pick = renderer.ConsumeLastPickResult(scene.View);
		Require(pick.bHasNewResult && pick.HitEntity == entity, "A click on an instance doesn't pick its entity");

		renderer.SetSelectedEntity(entity);
		const RgbaImage selected = scene.Draw();
		SaveIfAsked(selected, "Selected");
		Require(selected.Pixels != unselected.Pixels, "The selected instances aren't outlined");
	}

	// A field of four cells along -z ahead of the camera and one behind it, and a sphere behind: only
	// what is in view is drawn, the picture as it would be without culling. Frozen, the view culls
	// by where its camera was.
	void TestCulling(SceneRenderer& gpu)
	{
		TestScene scene(gpu, false);
		auto field = std::make_shared<MeshInstances>();
		const float rows[] = { 0.0f, -4.0f, -8.0f, -12.0f, 12.0f };
		for (const float z : rows)
		{
			MeshInstanceCell cell;
			cell.First = static_cast<uint32_t>(field->Transforms.size());
			cell.Count = 3;
			cell.BoundsMin = glm::vec3(-1.5f, -1.0f, z);
			cell.BoundsMax = glm::vec3(1.5f, -1.0f, z);
			for (const float x : { -1.5f, 0.0f, 1.5f })
			{
				field->Transforms.push_back(glm::scale(glm::translate(glm::mat4(1.0f), glm::vec3(x, -1.0f, z)), glm::vec3(0.5f)));
			}
			field->Cells.push_back(cell);
		}
		Material* material = gpu.Renderer.GetMaterial("Materials/Untextured.nyxmat");
		scene.AddInstanced(Unmoved(), gpu.Renderer.GetCubeMesh(), material, field);
		scene.AddMesh(TransformComponent{ .Position = glm::vec3(0.0f, 0.0f, 20.0f), .Rotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f), .Scale = glm::vec3(1.0f) },
			gpu.Renderer.GetSphereMesh(), material);
		scene.AddMesh(Unmoved(), gpu.Renderer.GetSphereMesh(), material);
		gpu.DrawUntilShadersCompiled();

		const RgbaImage culled = scene.Draw();
		SaveIfAsked(culled, "Culled");
		const SceneViewCullingStats& culling = scene.Culling();
		Require(culling.Objects == 3 && culling.ObjectsCulled == 1, "One of three entities is behind the camera, not " + std::to_string(culling.ObjectsCulled));
		Require(culling.Cells == 5 && culling.CellsCulled == 1, "One of five cells is behind the camera, not " + std::to_string(culling.CellsCulled));
		Require(culling.Instances == 12 + 1, "Twelve instances and a sphere should be drawn, not " + std::to_string(culling.Instances));

		// Turned round: the cells ahead and the sphere in the middle are culled, the ones behind drawn
		scene.World.Get<TransformComponent>(scene.Camera).Rotation = glm::angleAxis(glm::radians(180.0f), glm::vec3(0.0f, 1.0f, 0.0f));
		scene.Draw();
		Require(scene.Culling().CellsCulled == 4 && scene.Culling().ObjectsCulled == 1, "Turned round, four cells and the sphere at the origin should be culled");

		// Frozen facing the field again: turned round once more, it still culls as it did then
		scene.World.Get<TransformComponent>(scene.Camera).Rotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
		gpu.Renderer.SetSceneViewCullingDebug(scene.View, ESceneViewCullingDebug::Frozen);
		scene.Draw();
		scene.World.Get<TransformComponent>(scene.Camera).Rotation = glm::angleAxis(glm::radians(180.0f), glm::vec3(0.0f, 1.0f, 0.0f));
		const RgbaImage frozen = scene.Draw();
		SaveIfAsked(frozen, "Frozen");
		Require(scene.Culling().CellsCulled == 1, "A frozen view should cull by where its camera was");

		// The bounds drawn over the picture change it; off again, it is as before
		gpu.Renderer.SetSceneViewCullingDebug(scene.View, ESceneViewCullingDebug::Bounds);
		scene.World.Get<TransformComponent>(scene.Camera).Rotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
		const RgbaImage bounds = scene.Draw();
		SaveIfAsked(bounds, "Bounds");
		gpu.Renderer.SetSceneViewCullingDebug(scene.View, ESceneViewCullingDebug::Off);
		Require(scene.Draw().Pixels == culled.Pixels, "The view with its culling shown and hidden again should be as before");
		if constexpr (DebugDraw::Enabled)
		{
			Require(bounds.Pixels != culled.Pixels, "The culling's bounds aren't drawn");
		}
	}
}

int main()
{
	try
	{
		Nyx::Logger::Get().Init();
		SceneRenderer gpu("NyxInstancingGpuTests");
		TestDrawsEveryInstance(gpu);
		TestRevisionReuploads(gpu);
		TestPicking(gpu);
		TestCulling(gpu);
	}
	catch (const std::exception& error)
	{
		std::cerr << "FAILED: " << error.what() << "\n";
		return 1;
	}

	std::cout << "All instancing GPU tests passed\n";
	return 0;
}
