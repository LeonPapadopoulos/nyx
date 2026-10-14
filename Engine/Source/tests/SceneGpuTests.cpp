// The renderer drawing the editor's default scene on a real GPU: three cubes (textured,
// reflective, untextured) under the sun, the skybox, the grid, debug lines and the outline of the
// selected cube, seen through the scene's camera. The picture is compared with a reference image,
// so changes to how the scene is drawn that should look the same are seen to look the same; and a
// click on the middle cube picks it.
//
// NYX_SCENE_TEST_OUTPUT=<folder> saves what was rendered there, e.g. to make a new reference
// after a change that is meant to change the picture.
#include "CameraComponent.h"
#include "DirectionalLightComponent.h"
#include "Entity.h"
#include "ImageFile.h"
#include "Log.h"
#include "MeshRendererComponent.h"
#include "TransformComponent.h"
#include "VulkanImGuiBackend.h" // VulkanRenderer's destructor needs it
#include "VulkanRenderer.h"

#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>

namespace
{
	using namespace Nyx::Engine;

	constexpr uint32_t ViewWidth = 480;
	constexpr uint32_t ViewHeight = 270;

	void Require(bool condition, const std::string& message)
	{
		if (!condition)
		{
			throw std::runtime_error(message);
		}
	}

	// A hidden window, and a renderer drawing into it
	class SceneRenderer
	{
	public:
		SceneRenderer()
		{
			if (!glfwInit())
			{
				throw std::runtime_error("GLFW failed to start");
			}
			glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
			glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
			Window = glfwCreateWindow(640, 360, "NyxSceneGpuTests", nullptr, nullptr);
			if (!Window)
			{
				throw std::runtime_error("No window for the Vulkan device");
			}
			Renderer.Initialize("NyxSceneGpuTests", Window);
		}

		~SceneRenderer()
		{
			Renderer.Shutdown();
			glfwDestroyWindow(Window);
			glfwTerminate();
		}

		void DrawFrames(int count)
		{
			for (int frame = 0; frame < count; ++frame)
			{
				Renderer.DrawFrame([]
					{
					});
			}
		}

		GLFWwindow* Window = nullptr;
		Nyx::VulkanRenderer Renderer;
	};

	struct DefaultScene
	{
		Registry World;
		Entity TexturedCube;
		Entity ReflectiveCube;
		Entity UntexturedCube;
	};

	Entity AddCube(Registry& world, Nyx::VulkanRenderer& renderer, float x, Nyx::Material* material)
	{
		const Entity entity = world.CreateEntity();
		world.Add<TransformComponent>(entity, TransformComponent{ .Position = glm::vec3(x, 0.0f, 0.0f), .Rotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f), .Scale = glm::vec3(1.0f) });
		MeshRendererComponent meshRenderer;
		meshRenderer.MeshAsset = renderer.GetCubeMesh();
		meshRenderer.MaterialAsset = material;
		world.Add<MeshRendererComponent>(entity, meshRenderer);
		return entity;
	}

	// As the editor's SpawnDefaultScene() makes it
	void MakeDefaultScene(DefaultScene& scene, Nyx::VulkanRenderer& renderer)
	{
		scene.TexturedCube = AddCube(scene.World, renderer, -2.0f, renderer.GetTexturedMaterial());
		scene.ReflectiveCube = AddCube(scene.World, renderer, 0.0f, renderer.GetReflectiveMaterial());
		scene.UntexturedCube = AddCube(scene.World, renderer, 2.0f, renderer.GetUntexturedMaterial());

		const Entity camera = scene.World.CreateEntity();
		scene.World.Add<TransformComponent>(camera, TransformComponent{ .Position = glm::vec3(0.0f, 2.0f, 6.0f), .Rotation = glm::quat(glm::radians(glm::vec3(-15.0f, 0.0f, 0.0f))), .Scale = glm::vec3(1.0f) });
		scene.World.Add<CameraComponent>(camera, CameraComponent{});

		const Entity sun = scene.World.CreateEntity();
		scene.World.Add<TransformComponent>(sun, TransformComponent{ .Position = glm::vec3(0.0f, 5.0f, 0.0f), .Rotation = glm::quat(glm::radians(glm::vec3(-65.0f, 65.0f, 0.0f))), .Scale = glm::vec3(1.0f) });
		scene.World.Add<DirectionalLightComponent>(sun, DirectionalLightComponent{});
	}

	void SaveIfAsked(const RgbaImage& image, const std::string& name)
	{
		const char* folder = std::getenv("NYX_SCENE_TEST_OUTPUT");
		if (folder == nullptr || *folder == '\0')
		{
			return;
		}
		const std::filesystem::path path = std::filesystem::path(folder) / (name + ".png");
		std::string error;
		Require(SavePng(path, image, error), "Couldn't save " + path.string() + ": " + error);
		std::cout << "Saved " << path.string() << "\n";
	}

	// Rendering on the same GPU and driver gives the same picture; another GPU or driver may round
	// a little differently, and may differ in a few pixels along edges.
	void RequireLooksLike(const RgbaImage& image, const std::filesystem::path& referencePath)
	{
		RgbaImage reference;
		std::string error;
		Require(LoadRgbaImage(referencePath, reference, error),
			"No reference image " + referencePath.string() + " (" + error + "); run with NYX_SCENE_TEST_OUTPUT set to make one");
		Require(reference.Width == image.Width && reference.Height == image.Height, "The reference image has another size");

		constexpr int ChannelTolerance = 3;
		size_t differentPixels = 0;
		size_t changedPixels = 0;
		int largestDifference = 0;
		for (size_t pixel = 0; pixel < static_cast<size_t>(image.Width) * image.Height; ++pixel)
		{
			int difference = 0;
			for (size_t channel = 0; channel < 3; ++channel)
			{
				difference = std::max(difference, std::abs(int(image.Pixels[pixel * 4 + channel]) - int(reference.Pixels[pixel * 4 + channel])));
			}
			largestDifference = std::max(largestDifference, difference);
			differentPixels += difference > ChannelTolerance ? 1 : 0;
			changedPixels += difference > 0 ? 1 : 0;
		}
		std::cout << referencePath.filename().string() << ": " << changedPixels << " pixels differ, by up to " << largestDifference << "\n";

		const size_t allowed = static_cast<size_t>(image.Width) * image.Height / 1000;
		Require(differentPixels <= allowed,
			"The picture differs from " + referencePath.filename().string() + " in " + std::to_string(differentPixels) + " pixels (at most " + std::to_string(allowed) +
				" may), by up to " + std::to_string(largestDifference) + "; NYX_SCENE_TEST_OUTPUT saves it");
	}

	void TestDefaultScene(SceneRenderer& gpu)
	{
		// As the editor and game use it; ReadSceneViewPixels() is the renderer's own
		Nyx::IRenderer& renderer = gpu.Renderer;
		DefaultScene scene;
		MakeDefaultScene(scene, gpu.Renderer);
		renderer.SetWorld(&scene.World);

		const uint64_t view = renderer.CreateSceneView();
		renderer.SetSceneViewCameraMode(view, Nyx::EViewportCameraMode::ScenePrimaryCamera);
		renderer.SetSceneViewSize(view, ViewWidth, ViewHeight);
		renderer.SetSelectedEntity(scene.ReflectiveCube);
		gpu.DrawFrames(3);

		RgbaImage image;
		Require(gpu.Renderer.ReadSceneViewPixels(view, image), "The scene view can't be read back");
		Require(image.Width == ViewWidth && image.Height == ViewHeight, "The scene view has another size than asked for");
		SaveIfAsked(image, "DefaultScene");
		RequireLooksLike(image, std::filesystem::path(NYX_TEST_REFERENCES_DIR) / "DefaultScene.png");

		// A click on the middle of the picture hits the reflective cube, one on the sky nothing
		renderer.RequestPick(view, ViewWidth / 2, ViewHeight / 2);
		gpu.DrawFrames(3);
		Nyx::IRenderer::PickResult pick = renderer.ConsumeLastPickResult(view);
		Require(pick.bHasNewResult && pick.HitEntity == scene.ReflectiveCube, "A click on the middle cube doesn't pick it");

		renderer.RequestPick(view, ViewWidth / 2, 2);
		gpu.DrawFrames(3);
		pick = renderer.ConsumeLastPickResult(view);
		Require(pick.bHasNewResult && !pick.HitEntity, "A click on the sky picks something");

		renderer.DestroySceneView(view);
		renderer.SetSelectedEntity(std::nullopt);
		renderer.SetWorld(nullptr);
		gpu.DrawFrames(1);
	}
}

int main()
{
	try
	{
		Nyx::Core::Logger::Get().Init();
		SceneRenderer gpu;
		TestDefaultScene(gpu);
	}
	catch (const std::exception& error)
	{
		std::cerr << "FAILED: " << error.what() << "\n";
		return 1;
	}

	std::cout << "All scene GPU tests passed\n";
	return 0;
}
