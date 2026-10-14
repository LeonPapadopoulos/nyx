// The renderer drawing the editor's default scene on a real GPU: three cubes with the materials of
// Assets/Materials under the sun, the skybox, the grid, debug lines and the outline of the
// selected cube, seen through the scene's camera. The picture is compared with a reference image,
// so changes to how the scene is drawn that should look the same are seen to look the same; and a
// click on the middle cube picks it. A material file that doesn't exist draws as the magenta
// checker; one saved while drawing shows its new values within a few frames, and one saved
// broken keeps its last values. A material's shader saved with a new field before the one the
// material sets finds the material's value again, and saved broken keeps its last look. The
// scene is drawn in HDR and tonemapped: light brighter than white survives until then, and the
// view's exposure scales it.
//
// NYX_SCENE_TEST_OUTPUT=<folder> saves what was rendered there, e.g. to make a new reference
// after a change that is meant to change the picture.
#include "CameraComponent.h"
#include "DirectionalLightComponent.h"
#include "Entity.h"
#include "ImageFile.h"
#include "Log.h"
#include "MeshRendererComponent.h"
#include "Paths.h"
#include "TransformComponent.h"
#include "VulkanImGuiBackend.h" // VulkanRenderer's destructor needs it
#include "VulkanRenderer.h"

#include <GLFW/glfw3.h>
#include <imgui.h>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>

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
			// No imgui.ini wherever the test runs
			ImGui::GetIO().IniFilename = nullptr;
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

		// Until the materials' shaders compiled (the first time, or after they changed), for at
		// most a minute
		void DrawUntilShadersCompiled()
		{
			const auto deadline = std::chrono::steady_clock::now() + std::chrono::minutes(1);
			while (Renderer.IsCompilingShaders())
			{
				Require(std::chrono::steady_clock::now() < deadline, "The shaders didn't compile within a minute");
				DrawFrames(1);
				std::this_thread::sleep_for(std::chrono::milliseconds(10));
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
		scene.TexturedCube = AddCube(scene.World, renderer, -2.0f, renderer.GetMaterial("Materials/Textured.nyxmat"));
		scene.ReflectiveCube = AddCube(scene.World, renderer, 0.0f, renderer.GetMaterial("Materials/Reflective.nyxmat"));
		scene.UntexturedCube = AddCube(scene.World, renderer, 2.0f, renderer.GetMaterial("Materials/Untextured.nyxmat"));

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
		gpu.DrawUntilShadersCompiled();
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

	// The pixel in the middle of the view, after drawing a frame
	std::array<uint8_t, 3> MiddlePixel(SceneRenderer& gpu, uint64_t view)
	{
		gpu.DrawFrames(1);
		RgbaImage image;
		Require(gpu.Renderer.ReadSceneViewPixels(view, image), "The scene view can't be read back");
		const size_t offset = (static_cast<size_t>(image.Height / 2) * image.Width + image.Width / 2) * 4;
		return { image.Pixels[offset], image.Pixels[offset + 1], image.Pixels[offset + 2] };
	}

	// What the tonemap (Tonemap.slang) makes of linear light: Khronos PBR Neutral, then sRGB
	std::array<uint8_t, 3> Tonemapped(glm::vec3 color)
	{
		const float startCompression = 0.8f - 0.04f;
		const float desaturation = 0.15f;
		const float x = std::min(color.r, std::min(color.g, color.b));
		const float offset = x < 0.08f ? x - 6.25f * x * x : 0.04f;
		color -= offset;
		const float peak = std::max(color.r, std::max(color.g, color.b));
		if (peak >= startCompression)
		{
			const float d = 1.0f - startCompression;
			const float newPeak = 1.0f - d * d / (peak + d - startCompression);
			color *= newPeak / peak;
			const float g = 1.0f - 1.0f / (desaturation * (peak - newPeak) + 1.0f);
			color = glm::mix(color, glm::vec3(newPeak), g);
		}
		std::array<uint8_t, 3> encoded{};
		for (int channel = 0; channel < 3; ++channel)
		{
			const float c = std::clamp(color[channel], 0.0f, 1.0f);
			const float srgb = c <= 0.0031308f ? c * 12.92f : 1.055f * std::pow(c, 1.0f / 2.4f) - 0.055f;
			encoded[channel] = static_cast<uint8_t>(std::lround(srgb * 255.0f));
		}
		return encoded;
	}

	bool Near(const std::array<uint8_t, 3>& actual, const std::array<uint8_t, 3>& expected, int tolerance = 2)
	{
		for (int channel = 0; channel < 3; ++channel)
		{
			if (std::abs(int(actual[channel]) - int(expected[channel])) > tolerance)
			{
				return false;
			}
		}
		return true;
	}

	std::string Text(const std::array<uint8_t, 3>& rgb)
	{
		return std::to_string(rgb[0]) + " " + std::to_string(rgb[1]) + " " + std::to_string(rgb[2]);
	}

	// A file of the test's own in the assets folder, removed afterwards
	struct LiveMaterialFile
	{
		std::string Path = "Materials/NyxSceneGpuTests-Live.nyxmat";
		std::filesystem::path File = Nyx::Paths::GetAssetsDir() / Path;

		LiveMaterialFile() = default;
		explicit LiveMaterialFile(std::string path)
			: Path(std::move(path))
			, File(Nyx::Paths::GetAssetsDir() / Path)
		{
		}

		void Write(const std::string& text) const
		{
			std::ofstream stream(File, std::ios::binary | std::ios::trunc);
			stream << text;
		}

		~LiveMaterialFile()
		{
			std::error_code error;
			std::filesystem::remove(File, error);
		}
	};

	// A cube filling the view, its material file saved while drawing
	void TestSavedMaterialFilesReload(SceneRenderer& gpu)
	{
		Nyx::IRenderer& renderer = gpu.Renderer;
		LiveMaterialFile file;
		file.Write("Shader = Shaders/Classic.slang\nAlbedo = Textures/checker.jpg\nReflectivity = 0\n");

		DefaultScene scene;
		AddCube(scene.World, gpu.Renderer, 0.0f, renderer.GetMaterial(file.Path));
		const Entity camera = scene.World.CreateEntity();
		scene.World.Add<TransformComponent>(camera, TransformComponent{ .Position = glm::vec3(0.0f, 0.0f, 1.2f), .Rotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f), .Scale = glm::vec3(1.0f) });
		scene.World.Add<CameraComponent>(camera, CameraComponent{});
		renderer.SetWorld(&scene.World);
		const uint64_t view = renderer.CreateSceneView();
		renderer.SetSceneViewCameraMode(view, Nyx::EViewportCameraMode::ScenePrimaryCamera);
		renderer.SetSceneViewSize(view, ViewWidth, ViewHeight);
		gpu.DrawUntilShadersCompiled();
		const std::array<uint8_t, 3> textured = MiddlePixel(gpu, view);

		// A mirror now: the sky instead of the checker, a few frames after the save
		file.Write("Shader = Shaders/Classic.slang\nAlbedo = Textures/checker.jpg\nReflectivity = 1\n");
		std::array<uint8_t, 3> mirrored = textured;
		const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
		while (mirrored == textured && std::chrono::steady_clock::now() < deadline)
		{
			std::this_thread::sleep_for(std::chrono::milliseconds(20));
			mirrored = MiddlePixel(gpu, view);
		}
		Require(mirrored != textured, "The saved material file didn't change the picture within five seconds");

		// Saved broken: the last values stay
		file.Write("Shader = Shaders/Classic.slang\nReflectivity 0\n");
		// Drawn for half a second, so that the save settles and is read
		const auto drawnUntil = std::chrono::steady_clock::now() + std::chrono::milliseconds(500);
		while (std::chrono::steady_clock::now() < drawnUntil)
		{
			std::this_thread::sleep_for(std::chrono::milliseconds(20));
			gpu.DrawFrames(1);
		}
		Require(MiddlePixel(gpu, view) == mirrored, "A material file saved broken changed the picture");

		renderer.DestroySceneView(view);
		renderer.SetWorld(nullptr);
		gpu.DrawFrames(1);
	}

	// Draws until the middle pixel isn't `before` any more and the shaders are compiled, for at most
	// a minute; returns it
	std::array<uint8_t, 3> DrawUntilMiddleChanges(SceneRenderer& gpu, uint64_t view, const std::array<uint8_t, 3>& before)
	{
		const auto deadline = std::chrono::steady_clock::now() + std::chrono::minutes(1);
		std::array<uint8_t, 3> now = before;
		while ((now == before || gpu.Renderer.IsCompilingShaders()) && std::chrono::steady_clock::now() < deadline)
		{
			std::this_thread::sleep_for(std::chrono::milliseconds(20));
			now = MiddlePixel(gpu, view);
		}
		return now;
	}

	// A surface material that only glows; its struct is named like its file
	std::string GlowShader(const std::string& fields, const std::string& emission, const std::string& name = "NyxSceneGpuTestsGlow")
	{
		return "import nyx.surface;\nstruct " + name + " : ISurface\n{\n" + fields +
			"    SurfaceOutput shade(SurfaceInput input)\n    {\n        SurfaceOutput o = SurfaceOutput::defaults(input);\n        o.BaseColor = float3(0.0);\n        o.Emission = " + emission +
			";\n        return o;\n    }\n}\n";
	}

	// A cube filling the view, its material's shader saved while drawing
	void TestSavedMaterialShadersRecompile(SceneRenderer& gpu)
	{
		Nyx::IRenderer& renderer = gpu.Renderer;
		LiveMaterialFile shader("Shaders/NyxSceneGpuTestsGlow.slang");
		shader.Write(GlowShader("    [Color] float3 Glow = float3(1.0, 0.0, 0.0);\n", "Glow"));
		LiveMaterialFile material("Materials/NyxSceneGpuTests-Glow.nyxmat");
		material.Write("Shader = Shaders/NyxSceneGpuTestsGlow.slang\nGlow = 0 0 1\n");

		DefaultScene scene;
		AddCube(scene.World, gpu.Renderer, 0.0f, renderer.GetMaterial(material.Path));
		const Entity camera = scene.World.CreateEntity();
		scene.World.Add<TransformComponent>(camera, TransformComponent{ .Position = glm::vec3(0.0f, 0.0f, 1.2f), .Rotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f), .Scale = glm::vec3(1.0f) });
		scene.World.Add<CameraComponent>(camera, CameraComponent{});
		renderer.SetWorld(&scene.World);
		const uint64_t view = renderer.CreateSceneView();
		renderer.SetSceneViewCameraMode(view, Nyx::EViewportCameraMode::ScenePrimaryCamera);
		renderer.SetSceneViewSize(view, ViewWidth, ViewHeight);

		// The missing material until the shader's first compile, then the file's blue
		const std::array<uint8_t, 3> blue = DrawUntilMiddleChanges(gpu, view, Tonemapped(glm::vec3(1.0f, 0.0f, 1.0f)));
		Require(Near(blue, Tonemapped(glm::vec3(0.0f, 0.0f, 1.0f))), "The material doesn't glow in its file's blue: " + Text(blue));

		// A field before Glow: Glow moves, the file's blue is found there, the new field's default
		// comes from the shader
		shader.Write(GlowShader("    float Strength = 0.5;\n    [Color] float3 Glow = float3(1.0, 0.0, 0.0);\n", "Glow * Strength"));
		const std::array<uint8_t, 3> dimmed = DrawUntilMiddleChanges(gpu, view, blue);
		Require(Near(dimmed, Tonemapped(glm::vec3(0.0f, 0.0f, 0.5f))), "After a field came before it, the file's blue isn't found at half strength: " + Text(dimmed));

		// Broken: the last look stays
		shader.Write(GlowShader("    float Strength = 0.5;\n", "Glow * Strength"));
		const auto drawnUntil = std::chrono::steady_clock::now() + std::chrono::seconds(2);
		while (std::chrono::steady_clock::now() < drawnUntil || renderer.IsCompilingShaders())
		{
			std::this_thread::sleep_for(std::chrono::milliseconds(20));
			gpu.DrawFrames(1);
		}
		Require(MiddlePixel(gpu, view) == dimmed, "A material shader saved broken changed the picture");

		renderer.DestroySceneView(view);
		renderer.SetWorld(nullptr);
		gpu.DrawFrames(1);
	}

	// A cube glowing brighter than white in blue, filling the view, at three exposures: what reaches
	// the tonemap is the HDR value times two to the exposure
	void TestHdrAndExposure(SceneRenderer& gpu)
	{
		Nyx::IRenderer& renderer = gpu.Renderer;
		LiveMaterialFile shader("Shaders/NyxSceneGpuTestsHdr.slang");
		shader.Write(GlowShader("    [Color, HDR] float3 Glow = float3(1.0, 0.0, 0.0);\n", "Glow", "NyxSceneGpuTestsHdr"));
		LiveMaterialFile material("Materials/NyxSceneGpuTests-Hdr.nyxmat");
		const glm::vec3 glow(0.5f, 0.75f, 3.0f);
		material.Write("Shader = Shaders/NyxSceneGpuTestsHdr.slang\nGlow = 0.5 0.75 3\n");

		DefaultScene scene;
		AddCube(scene.World, gpu.Renderer, 0.0f, renderer.GetMaterial(material.Path));
		const Entity camera = scene.World.CreateEntity();
		scene.World.Add<TransformComponent>(camera, TransformComponent{ .Position = glm::vec3(0.0f, 0.0f, 1.2f), .Rotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f), .Scale = glm::vec3(1.0f) });
		scene.World.Add<CameraComponent>(camera, CameraComponent{});
		renderer.SetWorld(&scene.World);
		const uint64_t view = renderer.CreateSceneView();
		renderer.SetSceneViewCameraMode(view, Nyx::EViewportCameraMode::ScenePrimaryCamera);
		renderer.SetSceneViewSize(view, ViewWidth, ViewHeight);
		gpu.DrawUntilShadersCompiled();

		for (const float stops : { 0.0f, -1.0f, 1.0f, -3.0f })
		{
			renderer.SetSceneViewExposure(view, stops);
			const std::array<uint8_t, 3> pixel = MiddlePixel(gpu, view);
			const std::array<uint8_t, 3> expected = Tonemapped(glow * std::exp2(stops));
			Require(Near(pixel, expected), "At " + std::to_string(stops) + " stops, the glow is " + Text(pixel) + ", expected " + Text(expected));
		}

		renderer.DestroySceneView(view);
		renderer.SetWorld(nullptr);
		gpu.DrawFrames(1);
	}

	// A cube whose material file doesn't exist, alone in front of the camera: magenta, unlit
	void TestMissingMaterial(SceneRenderer& gpu)
	{
		Nyx::IRenderer& renderer = gpu.Renderer;
		Nyx::Material* material = renderer.GetMaterial("Materials/DoesNotExist.nyxmat");
		Require(material != nullptr && material->Pipeline != nullptr, "A missing material file gives no material to draw with");

		DefaultScene scene;
		AddCube(scene.World, gpu.Renderer, 0.0f, material);
		const Entity camera = scene.World.CreateEntity();
		scene.World.Add<TransformComponent>(camera, TransformComponent{ .Position = glm::vec3(0.0f, 0.0f, 2.0f), .Rotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f), .Scale = glm::vec3(1.0f) });
		scene.World.Add<CameraComponent>(camera, CameraComponent{});
		renderer.SetWorld(&scene.World);

		const uint64_t view = renderer.CreateSceneView();
		renderer.SetSceneViewCameraMode(view, Nyx::EViewportCameraMode::ScenePrimaryCamera);
		renderer.SetSceneViewSize(view, ViewWidth, ViewHeight);
		gpu.DrawFrames(3);

		RgbaImage image;
		Require(gpu.Renderer.ReadSceneViewPixels(view, image), "The scene view can't be read back");
		SaveIfAsked(image, "MissingMaterial");
		// Unlit magenta, tonemapped
		const std::array<uint8_t, 3> expected = Tonemapped(glm::vec3(1.0f, 0.0f, 1.0f));
		size_t magenta = 0;
		for (size_t pixel = 0; pixel < static_cast<size_t>(image.Width) * image.Height; ++pixel)
		{
			const uint8_t* rgb = &image.Pixels[pixel * 4];
			magenta += Near({ rgb[0], rgb[1], rgb[2] }, expected) ? 1 : 0;
		}
		// The cube's face fills the middle of the picture, half of it magenta
		Require(magenta > static_cast<size_t>(image.Width) * image.Height / 20, "A cube with a missing material isn't magenta: " + std::to_string(magenta) + " magenta pixels");

		renderer.DestroySceneView(view);
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
		TestMissingMaterial(gpu);
		TestSavedMaterialFilesReload(gpu);
		TestSavedMaterialShadersRecompile(gpu);
		TestHdrAndExposure(gpu);
	}
	catch (const std::exception& error)
	{
		std::cerr << "FAILED: " << error.what() << "\n";
		return 1;
	}

	std::cout << "All scene GPU tests passed\n";
	return 0;
}
