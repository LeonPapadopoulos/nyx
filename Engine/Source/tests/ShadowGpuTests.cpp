// The primary directional light's shadows on a real GPU: a box above a plane and a field of small
// cubes drawn instanced, under a sun from the side, look as the reference image does; where the
// box's shadow falls the plane is a quarter darker than in the sun or more (the sky still lights
// it), and the field's instances darken it too. Switched off on the light, or on the box's Mesh
// Renderer, the shadow is gone and the plane there is as bright as with no box at all. A one-sided
// quad whose back faces the sun casts nothing; a two-sided one casts. Each cascade is culled by
// its own frustum: the nearest leaves out what is far away, the farthest draws it. The editor's
// camera draws the scene's shadows too. The debug view tints the picture by cascade and shows the
// cascades' maps along the bottom. On a large generated ground that casts and receives, a place in
// a cube's shadow stays in it and a sunlit place on a far hillside stays lit, whichever way the
// camera looks from wherever it is.
//
// NYX_SCENE_TEST_OUTPUT=<folder> saves what was rendered there, e.g. to make a new reference
// after a change that is meant to change the picture.
#include "CameraComponent.h"
#include "CameraProjection.h"
#include "DirectionalLightComponent.h"
#include "Entity.h"
#include "GroundComponent.h"
#include "GroundMesh.h"
#include "ImageFile.h"
#include "Log.h"
#include "MeshInstancesComponent.h"
#include "MeshRendererComponent.h"
#include "RuntimeMeshComponent.h"
#include "TransformComponent.h"
#include "VulkanRenderer.h"
#include "RenderTestSupport.h"

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

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
		const std::filesystem::path path = std::filesystem::path(folder) / ("Shadows-" + name + ".png");
		std::string error;
		Require(SavePng(path, image, error), "Couldn't save " + path.string() + ": " + error);
		std::cout << "Saved " << path.string() << "\n";
	}

	std::array<uint8_t, 3> PixelAt(const RgbaImage& image, const glm::vec2& pixel)
	{
		const uint32_t x = std::min(static_cast<uint32_t>(pixel.x), image.Width - 1);
		const uint32_t y = std::min(static_cast<uint32_t>(pixel.y), image.Height - 1);
		const size_t offset = (static_cast<size_t>(y) * image.Width + x) * 4;
		return { image.Pixels[offset], image.Pixels[offset + 1], image.Pixels[offset + 2] };
	}

	int Brightness(const std::array<uint8_t, 3>& rgb)
	{
		return int(rgb[0]) + int(rgb[1]) + int(rgb[2]);
	}

	TransformComponent Placed(const glm::vec3& position, const glm::vec3& scale = glm::vec3(1.0f), const glm::quat& rotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f))
	{
		return TransformComponent{ .Position = position, .Rotation = rotation, .Scale = scale };
	}

	Entity AddMesh(Registry& world, const TransformComponent& transform, Mesh* mesh, Material* material)
	{
		const Entity entity = world.CreateEntity();
		world.Add<TransformComponent>(entity, transform);
		MeshRendererComponent meshRenderer;
		meshRenderer.MeshAsset = mesh;
		meshRenderer.MaterialAsset = material;
		world.Add<MeshRendererComponent>(entity, meshRenderer);
		return entity;
	}

	// A plane 24 m across, a box floating a metre above it, a field of small cubes drawn instanced
	// beside it in two cells, a far box 60 m away, a camera looking down at them from the front
	// and a sun from the left behind, 50 degrees up
	struct ShadowScene
	{
		Registry World;
		Entity Plane;
		Entity Box;
		Entity Field;
		Entity FarBox;
		Entity Camera;
		Entity Sun;
		CameraComponent Lens;

		explicit ShadowScene(VulkanRenderer& renderer)
		{
			Material* material = renderer.GetMaterial("Materials/Untextured.nyxmat");
			Plane = AddMesh(World, Placed(glm::vec3(0.0f), glm::vec3(24.0f, 1.0f, 24.0f)), renderer.GetPlaneMesh(), material);
			Box = AddMesh(World, Placed(glm::vec3(-1.5f, 1.5f, 0.0f)), renderer.GetCubeMesh(), material);
			FarBox = AddMesh(World, Placed(glm::vec3(0.0f, 1.0f, -60.0f), glm::vec3(2.0f)), renderer.GetCubeMesh(), material);

			auto instances = std::make_shared<MeshInstances>();
			for (int row = 0; row < 6; ++row)
			{
				for (int column = 0; column < 6; ++column)
				{
					instances->Transforms.push_back(Placed(glm::vec3(2.0f + column * 0.6f, 0.2f, -1.5f + row * 0.6f), glm::vec3(0.3f, 0.4f, 0.3f)).ToMatrix());
				}
			}
			instances->Cells = { MeshInstanceCell{ .BoundsMin = glm::vec3(2.0f, 0.2f, -1.5f), .BoundsMax = glm::vec3(5.0f, 0.2f, 0.3f), .First = 0, .Count = 18 },
				MeshInstanceCell{ .BoundsMin = glm::vec3(2.0f, 0.2f, 0.3f), .BoundsMax = glm::vec3(5.0f, 0.2f, 1.5f), .First = 18, .Count = 18 } };
			Field = AddMesh(World, Placed(glm::vec3(0.0f)), renderer.GetCubeMesh(), material);
			MeshInstancesComponent meshInstances;
			SetMeshInstances(meshInstances, std::move(instances));
			World.Add<MeshInstancesComponent>(Field, meshInstances);

			Camera = World.CreateEntity();
			World.Add<TransformComponent>(Camera, Placed(glm::vec3(0.5f, 6.0f, 9.0f), glm::vec3(1.0f), glm::quat(glm::radians(glm::vec3(-32.0f, 0.0f, 0.0f)))));
			World.Add<CameraComponent>(Camera, Lens);

			Sun = World.CreateEntity();
			World.Add<TransformComponent>(Sun, Placed(glm::vec3(0.0f, 10.0f, 0.0f), glm::vec3(1.0f), glm::quat(glm::radians(glm::vec3(-50.0f, -60.0f, 0.0f)))));
			World.Add<DirectionalLightComponent>(Sun, DirectionalLightComponent{});
		}

		// Towards the sun
		glm::vec3 TowardsLight() const
		{
			return -glm::normalize(World.Get<TransformComponent>(Sun).Rotation * glm::vec3(0.0f, 0.0f, -1.0f));
		}

		// Where a point of the world is in the picture
		glm::vec2 Pixel(const glm::vec3& point) const
		{
			const std::optional<glm::vec2> pixel = WorldToScreenPoint(World.Get<TransformComponent>(Camera), Lens, glm::vec2(ViewWidth, ViewHeight), point);
			Require(pixel && pixel->x >= 0.0f && pixel->y >= 0.0f && pixel->x < ViewWidth && pixel->y < ViewHeight, "A point the test looks at is out of the picture");
			return *pixel;
		}

		// Where the shadow of a point falls on the plane
		glm::vec3 ShadowOnPlane(const glm::vec3& point) const
		{
			const glm::vec3 l = TowardsLight();
			return point - l * (point.y / l.y);
		}
	};

	RgbaImage Draw(SceneRenderer& gpu, uint64_t view, int frames = 2)
	{
		gpu.DrawFrames(frames);
		RgbaImage image;
		Require(gpu.Renderer.ReadSceneViewPixels(view, image), "The scene view can't be read back");
		return image;
	}

	uint64_t MakeView(SceneRenderer& gpu, ShadowScene& scene)
	{
		IEditorRenderer& renderer = gpu.Renderer;
		renderer.SetWorld(&scene.World);
		const uint64_t view = renderer.CreateSceneView();
		renderer.SetSceneViewCameraMode(view, EViewportCameraMode::ScenePrimaryCamera);
		renderer.SetSceneViewSize(view, ViewWidth, ViewHeight);
		gpu.DrawUntilShadersCompiled();
		return view;
	}

	void EndView(SceneRenderer& gpu, uint64_t view)
	{
		IEditorRenderer& renderer = gpu.Renderer;
		renderer.DestroySceneView(view);
		renderer.SetWorld(nullptr);
		gpu.DrawFrames(1);
	}

	void TestShadowedScene(SceneRenderer& gpu)
	{
		ShadowScene scene(gpu.Renderer);
		const uint64_t view = MakeView(gpu, scene);
		const RgbaImage image = Draw(gpu, view, 3);
		SaveIfAsked(image, "Scene");
		RequireLooksLike(image, std::filesystem::path(NYX_TEST_REFERENCES_DIR) / "Shadows.png");

		const SceneViewShadows* shadows = gpu.Renderer.GetSceneViewShadows(view);
		Require(shadows && shadows->CascadeCount == 4, "The view doesn't draw the light's four cascades");

		// Under the box's middle, on the plane, against a place in the sun
		const glm::vec2 boxShadow = scene.Pixel(scene.ShadowOnPlane(glm::vec3(-1.5f, 1.5f, 0.0f)));
		const glm::vec2 inTheSun = scene.Pixel(glm::vec3(-3.5f, 0.0f, 3.0f));
		const int shaded = Brightness(PixelAt(image, boxShadow));
		const int lit = Brightness(PixelAt(image, inTheSun));
		Require(shaded * 4 < lit * 3, "The box's shadow isn't darker than the sun: " + std::to_string(shaded) + " against " + std::to_string(lit));

		// The box's shadow, but not the box itself, goes when it doesn't cast; and it is as bright
		// there as with no box at all
		scene.World.Get<MeshRendererComponent>(scene.Box).bCastShadows = false;
		const RgbaImage notCasting = Draw(gpu, view);
		SaveIfAsked(notCasting, "BoxNotCasting");
		scene.World.Get<MeshRendererComponent>(scene.Box).bVisible = false;
		const RgbaImage noBox = Draw(gpu, view);
		Require(Near(PixelAt(notCasting, boxShadow), PixelAt(noBox, boxShadow), 2), "A box that casts no shadow still darkens the plane: " + Text(PixelAt(notCasting, boxShadow)) + " without it " + Text(PixelAt(noBox, boxShadow)));
		const glm::vec2 boxItself = scene.Pixel(glm::vec3(-1.5f, 1.5f, 0.5f));
		Require(!Near(PixelAt(notCasting, boxItself), PixelAt(noBox, boxItself), 10), "A box that casts no shadow isn't drawn");
		scene.World.Get<MeshRendererComponent>(scene.Box).bVisible = true;
		scene.World.Get<MeshRendererComponent>(scene.Box).bCastShadows = true;

		// The instanced field darkens the plane where its shadows fall: off, the plane around it is
		// brighter in many pixels
		scene.World.Get<MeshRendererComponent>(scene.Field).bCastShadows = false;
		const RgbaImage fieldNotCasting = Draw(gpu, view);
		scene.World.Get<MeshRendererComponent>(scene.Field).bCastShadows = true;
		size_t darkened = 0;
		for (size_t pixel = 0; pixel < image.Pixels.size(); pixel += 4)
		{
			const int with = int(image.Pixels[pixel]) + int(image.Pixels[pixel + 1]) + int(image.Pixels[pixel + 2]);
			const int without = int(fieldNotCasting.Pixels[pixel]) + int(fieldNotCasting.Pixels[pixel + 1]) + int(fieldNotCasting.Pixels[pixel + 2]);
			darkened += with + 30 < without ? 1 : 0;
		}
		Require(darkened > 150, "The instanced field casts hardly any shadow: " + std::to_string(darkened) + " pixels darker");
		gpu.DrawFrames(1);
		uint32_t fieldInstances = 0;
		for (uint32_t cascade = 0; cascade < shadows->CascadeCount; ++cascade)
		{
			fieldInstances = std::max(fieldInstances, shadows->Culling[cascade].Instances);
		}
		Require(fieldInstances >= 36, "No cascade draws the field's instances");

		// Off on the light: no cascades, and the box's shadow is gone
		scene.World.Get<DirectionalLightComponent>(scene.Sun).bCastShadows = false;
		const RgbaImage lightNotCasting = Draw(gpu, view);
		SaveIfAsked(lightNotCasting, "LightNotCasting");
		Require(gpu.Renderer.GetSceneViewShadows(view)->CascadeCount == 0, "A light that casts no shadows has cascades");
		Require(Near(PixelAt(lightNotCasting, boxShadow), PixelAt(noBox, boxShadow), 2), "A light that casts no shadows still shadows the plane");
		scene.World.Get<DirectionalLightComponent>(scene.Sun).bCastShadows = true;

		EndView(gpu, view);
	}

	// Each cascade culls by its own frustum: the nearest, a few metres round the camera, leaves out
	// the box 60 m away, which the farthest draws; fewer cascades are as many as asked for
	void TestCascadesCullByTheirOwnFrustum(SceneRenderer& gpu)
	{
		ShadowScene scene(gpu.Renderer);
		const uint64_t view = MakeView(gpu, scene);
		gpu.DrawFrames(2);
		const SceneViewShadows& shadows = *gpu.Renderer.GetSceneViewShadows(view);
		Require(shadows.CascadeCount == 4, "The view doesn't draw four cascades");
		Require(shadows.Culling[0].ObjectsCulled >= 1, "The nearest cascade draws the box far away");
		Require(shadows.Culling[3].ObjectsCulled < shadows.Culling[0].ObjectsCulled, "The farthest cascade culls as much as the nearest");
		for (uint32_t cascade = 1; cascade < 4; ++cascade)
		{
			Require(shadows.Cascades[cascade].SplitNear == shadows.Cascades[cascade - 1].SplitFar, "The cascades don't follow on");
			Require(shadows.Cascades[cascade].TexelSize > shadows.Cascades[cascade - 1].TexelSize, "A farther cascade's texels aren't bigger");
		}
		Require(std::abs(shadows.Cascades[3].SplitFar - DirectionalLightComponent{}.ShadowDistance) < 1e-3f, "The last cascade doesn't end at the shadow distance");

		scene.World.Get<DirectionalLightComponent>(scene.Sun).CascadeCount = 2;
		scene.World.Get<DirectionalLightComponent>(scene.Sun).ShadowMapResolution = 1000;
		gpu.DrawFrames(1);
		Require(shadows.CascadeCount == 2 && shadows.Settings.Resolution == 1024, "Two cascades of 1000 texels aren't two of 1024");
		const RgbaImage two = Draw(gpu, view);
		SaveIfAsked(two, "TwoCascades");
		const glm::vec2 boxShadow = scene.Pixel(scene.ShadowOnPlane(glm::vec3(-1.5f, 1.5f, 0.0f)));
		const glm::vec2 inTheSun = scene.Pixel(glm::vec3(-3.5f, 0.0f, 3.0f));
		Require(Brightness(PixelAt(two, boxShadow)) * 4 < Brightness(PixelAt(two, inTheSun)) * 3, "With two cascades, the box's shadow is gone");

		EndView(gpu, view);
	}

	// A horizontal quad 2 m up whose front faces down: its back faces the sun. One-sided, it casts
	// nothing; two-sided, it casts from its back.
	void TestTwoSidedCasting(SceneRenderer& gpu)
	{
		LiveMaterialFile twoSidedFile("Materials/NyxShadowGpuTestsTwoSided.nyxmat");
		twoSidedFile.Write("Parent = Materials/Untextured.nyxmat\nTwoSided = true\n");
		ShadowScene scene(gpu.Renderer);
		scene.World.Get<MeshRendererComponent>(scene.Box).bVisible = false;
		const glm::vec3 quadCenter(-2.0f, 2.0f, 1.0f);
		const Entity quad = AddMesh(scene.World, Placed(quadCenter, glm::vec3(1.5f), glm::quat(glm::radians(glm::vec3(180.0f, 0.0f, 0.0f)))), gpu.Renderer.GetPlaneMesh(),
			gpu.Renderer.GetMaterial("Materials/Untextured.nyxmat"));
		const uint64_t view = MakeView(gpu, scene);

		const glm::vec2 quadShadow = scene.Pixel(scene.ShadowOnPlane(quadCenter));
		Require(glm::length(quadShadow - scene.Pixel(quadCenter)) > 30.0f, "The quad hides its own shadow from the camera");
		scene.World.Get<MeshRendererComponent>(quad).bVisible = false;
		const int open = Brightness(PixelAt(Draw(gpu, view), quadShadow));
		scene.World.Get<MeshRendererComponent>(quad).bVisible = true;

		const RgbaImage oneSided = Draw(gpu, view);
		SaveIfAsked(oneSided, "OneSidedQuad");
		Require(std::abs(Brightness(PixelAt(oneSided, quadShadow)) - open) <= 6, "A one-sided quad whose back faces the sun casts a shadow");

		scene.World.Get<MeshRendererComponent>(quad).MaterialAsset = gpu.Renderer.GetMaterial(twoSidedFile.Path);
		gpu.DrawUntilShadersCompiled();
		const RgbaImage twoSided = Draw(gpu, view);
		SaveIfAsked(twoSided, "TwoSidedQuad");
		Require(Brightness(PixelAt(twoSided, quadShadow)) * 4 < open * 3, "A two-sided quad doesn't cast from its back: " + std::to_string(Brightness(PixelAt(twoSided, quadShadow))) + " against " + std::to_string(open));

		EndView(gpu, view);
	}

	// The editor's free camera, put where the scene's camera is, draws the scene's shadows; the
	// debug view tints the picture and shows the maps
	void TestEditorCameraAndDebugView(SceneRenderer& gpu)
	{
		ShadowScene scene(gpu.Renderer);
		const uint64_t view = MakeView(gpu, scene);
		IEditorRenderer& renderer = gpu.Renderer;
		renderer.SetSceneViewCameraMode(view, EViewportCameraMode::EditorFreeCamera);
		const TransformComponent& camera = scene.World.Get<TransformComponent>(scene.Camera);
		renderer.SetSceneViewEditorCameraTransform(view, camera.Position, glm::eulerAngles(camera.Rotation));
		const RgbaImage editor = Draw(gpu, view);
		SaveIfAsked(editor, "EditorCamera");
		Require(gpu.Renderer.GetSceneViewShadows(view)->CascadeCount == 4, "The editor's camera draws no shadows");
		const glm::vec2 boxShadow = scene.Pixel(scene.ShadowOnPlane(glm::vec3(-1.5f, 1.5f, 0.0f)));
		const glm::vec2 inTheSun = scene.Pixel(glm::vec3(-3.5f, 0.0f, 3.0f));
		Require(Brightness(PixelAt(editor, boxShadow)) * 4 < Brightness(PixelAt(editor, inTheSun)) * 3, "Through the editor's camera, the box casts no shadow");

		renderer.SetSceneViewShadowDebug(view, ESceneViewShadowDebug::Cascades);
		const RgbaImage debug = Draw(gpu, view);
		SaveIfAsked(debug, "Debug");
		// The plane in front of the camera, tinted by its cascade
		const glm::vec2 front = scene.Pixel(glm::vec3(0.5f, 0.0f, 4.0f));
		Require(!Near(PixelAt(debug, front), PixelAt(editor, front), 20), "The debug view doesn't tint the plane: " + Text(PixelAt(debug, front)) + ", lit " + Text(PixelAt(editor, front)));
		// The first map's square, at the bottom left: grey inside its frame
		const std::array<uint8_t, 3> map = PixelAt(debug, glm::vec2(ViewWidth * 0.04f, ViewHeight * 0.85f));
		Require(map[0] == map[1] && map[1] == map[2], "The first cascade's map isn't shown at the bottom left: " + Text(map));
		renderer.SetSceneViewShadowDebug(view, ESceneViewShadowDebug::Off);

		EndView(gpu, view);
	}

	// A camera looking from `position` at `target`, upright
	TransformComponent LookingAt(const glm::vec3& position, const glm::vec3& target)
	{
		return Placed(position, glm::vec3(1.0f), glm::quatLookAt(glm::normalize(target - position), glm::vec3(0.0f, 1.0f, 0.0f)));
	}

	// The default generated ground, 128 m across with its hills, casting and receiving, a lone 1 m cube
	// on it and a few dozen drawn instanced, under the ground test scene's sun. The ground in the
	// cube's shadow stays in it, and a sunlit place on a hillside 55 m from the middle stays lit,
	// seen from cameras near and far, high and low, turned every way: what the light's cascades
	// cover follows the camera, but what is in shadow doesn't.
	void TestGroundShadowsStayAsTheCameraMoves(SceneRenderer& gpu)
	{
		IEditorRenderer& renderer = gpu.Renderer;
		Registry world;
		const GroundComponent groundSettings{};
		const auto heightAt = [&groundSettings](float x, float z)
		{
			return SampleGroundHeight(groundSettings, x, z);
		};
		const Entity ground = world.CreateEntity();
		world.Add<TransformComponent>(ground, Placed(glm::vec3(0.0f)));
		MeshRendererComponent groundRenderer;
		groundRenderer.MaterialAsset = renderer.GetMaterial("Materials/VertexColor.nyxmat");
		world.Add<MeshRendererComponent>(ground, groundRenderer);
		RuntimeMeshComponent groundMesh;
		SetRuntimeMesh(groundMesh, std::make_shared<MeshData>(MakeGroundMesh(groundSettings)));
		world.Add<RuntimeMeshComponent>(ground, groundMesh);

		Material* material = renderer.GetMaterial("Materials/Untextured.nyxmat");
		auto instances = std::make_shared<MeshInstances>();
		for (int row = 0; row < 6; ++row)
		{
			for (int column = 0; column < 6; ++column)
			{
				const glm::vec2 at(-16.0f + column * 2.5f, -8.0f + row * 2.5f);
				instances->Transforms.push_back(Placed(glm::vec3(at.x, heightAt(at.x, at.y) + 0.5f, at.y)).ToMatrix());
			}
		}
		const Entity field = AddMesh(world, Placed(glm::vec3(0.0f)), renderer.GetCubeMesh(), material);
		MeshInstancesComponent meshInstances;
		SetMeshInstances(meshInstances, std::move(instances));
		world.Add<MeshInstancesComponent>(field, meshInstances);
		const glm::vec3 cubeFoot(6.0f, heightAt(6.0f, -2.0f), -2.0f);
		const Entity cube = AddMesh(world, Placed(cubeFoot + glm::vec3(0.0f, 0.5f, 0.0f)), renderer.GetCubeMesh(), material);

		const Entity sun = world.CreateEntity();
		world.Add<TransformComponent>(sun, Placed(glm::vec3(0.0f, 5.0f, 0.0f), glm::vec3(1.0f), glm::quat(glm::radians(glm::vec3(-50.0f, 30.0f, 0.0f)))));
		world.Add<DirectionalLightComponent>(sun, DirectionalLightComponent{});
		const glm::vec3 towardsLight = -glm::normalize(world.Get<TransformComponent>(sun).Rotation * glm::vec3(0.0f, 0.0f, -1.0f));

		// Under the middle of the shadow of the cube's top; and a place on the hillside to the west,
		// turned to the sun, which nothing shades: no ground rises between it and the sun
		const glm::vec3 shadedFlat = cubeFoot + glm::vec3(0.0f, 1.0f, 0.0f) - towardsLight * (1.0f / towardsLight.y);
		const glm::vec3 shaded(shadedFlat.x, heightAt(shadedFlat.x, shadedFlat.z), shadedFlat.z);
		const glm::vec3 open(-40.0f, heightAt(-40.0f, -40.0f), -40.0f);
		for (float along = 0.5f; along < 150.0f; along += 0.25f)
		{
			const glm::vec3 point = open + towardsLight * along;
			Require(point.y > heightAt(point.x, point.z), "The test's open place is in the ground's own shadow");
		}

		const CameraComponent lens{};
		const Entity camera = world.CreateEntity();
		world.Add<TransformComponent>(camera, Placed(glm::vec3(0.0f, 2.0f, 6.0f)));
		world.Add<CameraComponent>(camera, lens);
		renderer.SetWorld(&world);
		const uint64_t view = renderer.CreateSceneView();
		renderer.SetSceneViewCameraMode(view, EViewportCameraMode::ScenePrimaryCamera);
		renderer.SetSceneViewSize(view, ViewWidth, ViewHeight);
		gpu.DrawUntilShadersCompiled();

		struct Shot
		{
			glm::vec3 Position;
			// Where the camera looks, off the place so it isn't always in the middle
			glm::vec3 Aside;
		};
		const auto check = [&](const glm::vec3& place, bool bShaded, const std::vector<Shot>& shots, const std::string& what)
		{
			for (size_t index = 0; index < shots.size(); ++index)
			{
				const std::string shot = what + " from shot " + std::to_string(index);
				TransformComponent& transform = world.Get<TransformComponent>(camera);
				transform = LookingAt(shots[index].Position, place + shots[index].Aside);
				const std::optional<glm::vec2> pixel = WorldToScreenPoint(transform, lens, glm::vec2(ViewWidth, ViewHeight), place);
				Require(pixel && pixel->x >= 0.0f && pixel->y >= 0.0f && pixel->x < ViewWidth && pixel->y < ViewHeight, shot + ": the place is out of the picture");
				// Nothing of the ground hides the place from the camera
				for (float part = 0.02f; part < 0.98f; part += 0.01f)
				{
					const glm::vec3 point = glm::mix(shots[index].Position, place, part);
					Require(point.y > heightAt(point.x, point.z), shot + ": the ground hides the place");
				}

				world.Get<DirectionalLightComponent>(sun).bCastShadows = true;
				const RgbaImage with = Draw(gpu, view);
				SaveIfAsked(with, "Ground-" + std::string(bShaded ? "Shaded-" : "Open-") + std::to_string(index));
				world.Get<DirectionalLightComponent>(sun).bCastShadows = false;
				const RgbaImage without = Draw(gpu, view);
				// The cube doesn't hide the place either: without it, the place looks the same
				world.Get<MeshRendererComponent>(cube).bVisible = false;
				const RgbaImage withoutCube = Draw(gpu, view);
				world.Get<MeshRendererComponent>(cube).bVisible = true;
				Require(Near(PixelAt(without, *pixel), PixelAt(withoutCube, *pixel), 2), shot + ": the cube hides the place");

				const int lit = Brightness(PixelAt(without, *pixel));
				const int drawn = Brightness(PixelAt(with, *pixel));
				if (bShaded)
				{
					Require(drawn * 4 < lit * 3, shot + ": the place isn't in the cube's shadow, " + std::to_string(drawn) + " against " + std::to_string(lit) + " in the sun");
				}
				else
				{
					Require(std::abs(drawn - lit) <= 6, shot + ": the place in the sun is shadowed, " + std::to_string(drawn) + " against " + std::to_string(lit) + " without shadows");
				}
			}
		};
		// The cube's shadow falls away from the sun, towards -X and -Z: seen from that side and from
		// above, near and far
		check(shaded, true,
			{ { glm::vec3(-6.0f, 5.0f, -14.0f), glm::vec3(0.0f) }, { glm::vec3(-12.0f, 3.0f, 0.0f), glm::vec3(0.0f, 0.0f, 2.0f) },
				{ glm::vec3(0.0f, 20.0f, -10.0f), glm::vec3(1.5f, 0.0f, 0.0f) }, { glm::vec3(20.0f, 6.0f, -20.0f), glm::vec3(0.0f) },
				{ glm::vec3(-40.0f, 25.0f, -50.0f), glm::vec3(-4.0f, 0.0f, 3.0f) }, { glm::vec3(5.0f, 1.5f, -12.0f), glm::vec3(-1.0f, 0.5f, 0.0f) } },
			"The cube's shadow");
		// The hillside faces the sun, east: seen from the valley, from above, from far
		check(open, false,
			{ { glm::vec3(0.0f, 2.0f, 6.0f), glm::vec3(10.0f, 4.0f, 0.0f) }, { glm::vec3(10.0f, 15.0f, -60.0f), glm::vec3(0.0f) },
				{ glm::vec3(-20.0f, 40.0f, 10.0f), glm::vec3(5.0f, 0.0f, 5.0f) }, { glm::vec3(-15.0f, 6.0f, -10.0f), glm::vec3(0.0f) },
				{ glm::vec3(30.0f, 8.0f, 30.0f), glm::vec3(-6.0f, 3.0f, 0.0f) }, { glm::vec3(20.0f, 30.0f, -45.0f), glm::vec3(0.0f, 0.0f, -8.0f) } },
			"The open hillside");

		EndView(gpu, view);
	}
}

int main()
{
	try
	{
		Nyx::Logger::Get().Init();
		SceneRenderer gpu("NyxShadowGpuTests");
		TestShadowedScene(gpu);
		TestCascadesCullByTheirOwnFrustum(gpu);
		TestTwoSidedCasting(gpu);
		TestEditorCameraAndDebugView(gpu);
		TestGroundShadowsStayAsTheCameraMoves(gpu);
	}
	catch (const std::exception& error)
	{
		std::cerr << "FAILED: " << error.what() << "\n";
		return 1;
	}

	std::cout << "All shadow GPU tests passed\n";
	return 0;
}
