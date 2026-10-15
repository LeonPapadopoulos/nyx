// The renderer drawing the editor's default scene on a real GPU: three cubes with the materials of
// Assets/Materials under the sun, the skybox, the grid, debug lines and the outline of the
// selected cube, seen through the scene's camera. The picture is compared with a reference image,
// so changes to how the scene is drawn that should look the same are seen to look the same; and a
// click on the middle cube picks it. A material file that doesn't exist draws as the magenta
// checker; one saved while drawing shows its new values within a few frames, and one saved
// broken keeps its last values. A material's shader saved with a new field before the one the
// material sets finds the material's value again, and saved broken keeps its last look. The
// scene is drawn in HDR and tonemapped: light brighter than white survives until then, and the
// view's exposure scales it. Materials find the sky's light (nyx.environment) as it was made.
// The Standard material lights a dielectric and a metal as the same formulas do on the CPU; a
// normal map tilts the normal up the image; a masked material below its cutoff isn't drawn.
// A view can show a world of its own beside the scene: it draws and picks that world's objects,
// shows no outline of the scene's selection, and keeps its pending pick when the scene is set anew.
// Material values set in memory, as the material editor sets them, show in the next frame and
// leave the file as it is, until the file is saved and read again. A view has a sky of its own
// (black, a studio, the scene's turned) and debug views of its materials' outputs and of light
// that is NaN or out of range. Following the editor, as a game it started does, a saved shader
// compiles nothing; the editor's compile, sent over the link, is drawn, and so are material
// values it sends.
//
// NYX_SCENE_TEST_OUTPUT=<folder> saves what was rendered there, e.g. to make a new reference
// after a change that is meant to change the picture.
#include "CameraComponent.h"
#include "DirectionalLightComponent.h"
#include "EditorLinkMessages.h"
#include "Entity.h"
#include "ImageFile.h"
#include "LiveEdits.h"
#include "Log.h"
#include "MeshRendererComponent.h"
#include "Paths.h"
#include "ShaderCompiler.h"
#include "TransformComponent.h"
#include "VulkanImGuiBackend.h" // VulkanRenderer's destructor needs it
#include "VulkanRenderer.h"
#include "RenderTestSupport.h"

#include <GLFW/glfw3.h>
#include <imgui.h>
#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>
#include <glm/gtc/quaternion.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <thread>

namespace
{
	using namespace Nyx::Engine;
	using namespace Nyx::RenderTests;

	constexpr uint32_t ViewWidth = 480;
	constexpr uint32_t ViewHeight = 270;

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

	// A cube filling the view, its material file saved while drawing
	void TestSavedMaterialFilesReload(SceneRenderer& gpu)
	{
		Nyx::IRenderer& renderer = gpu.Renderer;
		LiveMaterialFile file("Materials/NyxSceneGpuTests-Live.nyxmat");
		file.Write("Shader = Shaders/Standard.slang\nAlbedo = Textures/checker.jpg\nRoughness = 1\n");

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
		file.Write("Shader = Shaders/Standard.slang\nAlbedo = Textures/checker.jpg\nMetallic = 1\nRoughness = 0\n");
		std::array<uint8_t, 3> mirrored = textured;
		const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
		while (mirrored == textured && std::chrono::steady_clock::now() < deadline)
		{
			std::this_thread::sleep_for(std::chrono::milliseconds(20));
			mirrored = MiddlePixel(gpu, view);
		}
		Require(mirrored != textured, "The saved material file didn't change the picture within five seconds");

		// Saved broken: the last values stay
		file.Write("Shader = Shaders/Standard.slang\nMetallic 0\n");
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

	// A surface material that only glows, unlit; its struct is named like its file
	std::string GlowShader(const std::string& fields, const std::string& emission, const std::string& name = "NyxSceneGpuTestsGlow")
	{
		return "import nyx.surface;\nstruct " + name + " : ISurface\n{\n" + fields +
			"    SurfaceOutput shade(SurfaceInput input)\n    {\n        SurfaceOutput o = SurfaceOutput::defaults(input);\n        o.Unlit = true;\n        o.BaseColor = float3(0.0);\n        o.Emission = " + emission +
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
		const Nyx::Engine::ShaderLogEntry* compiled = renderer.GetShaderLog().FindByFile(shader.File);
		Require(compiled && compiled->bSucceeded && compiled->Name == "Shaders/NyxSceneGpuTestsGlow.slang" && compiled->EntryPoints > 0 && compiled->SpirvBytes > 0,
			"The Shader Log doesn't have the shader's compile, by its path in the assets folder");

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
		// The Shader Log says where it broke: Glow, line 10
		const Nyx::Engine::ShaderLogEntry* broken = renderer.GetShaderLog().FindByFile(shader.File);
		Require(broken && !broken->bSucceeded && broken->FindFirstError(), "The Shader Log doesn't have the broken compile");
		Require(broken->FindFirstError()->Line == 10 && broken->FindFirstError()->Message.find("Glow") != std::string::npos,
			"The Shader Log's error isn't at the line with Glow: " + broken->FindFirstError()->ToString());

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

	// A cube that glows with the sky's light as nyx.environment gives it, for each of its three
	// parts in turn, against what VulkanEnvironment made
	void TestMaterialsFindTheEnvironment(SceneRenderer& gpu)
	{
		Nyx::IRenderer& renderer = gpu.Renderer;
		LiveMaterialFile shader("Shaders/NyxSceneGpuTestsEnvironment.slang");
		shader.Write("import nyx.surface;\nimport nyx.environment;\n"
					 "struct NyxSceneGpuTestsEnvironment : ISurface\n{\n    int Part = 0;\n"
					 "    SurfaceOutput shade(SurfaceInput input)\n    {\n        SurfaceOutput o = SurfaceOutput::defaults(input);\n        o.Unlit = true;\n        o.BaseColor = float3(0.0);\n"
					 "        if (Part == 0)\n            o.Emission = nyx::environmentDiffuse(float3(0.0, 0.0, 1.0));\n"
					 "        else if (Part == 1)\n            o.Emission = nyx::environmentSpecular(float3(0.0, 0.0, 1.0), 1.0);\n"
					 "        else\n            o.Emission = float3(nyx::environmentBrdf(64.5 / 128.0, 64.5 / 128.0), 0.0);\n"
					 "        return o;\n    }\n}\n");
		std::array<LiveMaterialFile, 3> materials = { LiveMaterialFile("Materials/NyxSceneGpuTests-Diffuse.nyxmat"), LiveMaterialFile("Materials/NyxSceneGpuTests-Specular.nyxmat"),
			LiveMaterialFile("Materials/NyxSceneGpuTests-Brdf.nyxmat") };
		for (size_t part = 0; part < materials.size(); ++part)
		{
			materials[part].Write("Shader = Shaders/NyxSceneGpuTestsEnvironment.slang\nPart = " + std::to_string(part) + "\n");
		}

		DefaultScene scene;
		const Entity cube = AddCube(scene.World, gpu.Renderer, 0.0f, renderer.GetMaterial(materials[0].Path));
		const Entity camera = scene.World.CreateEntity();
		scene.World.Add<TransformComponent>(camera, TransformComponent{ .Position = glm::vec3(0.0f, 0.0f, 1.2f), .Rotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f), .Scale = glm::vec3(1.0f) });
		scene.World.Add<CameraComponent>(camera, CameraComponent{});
		renderer.SetWorld(&scene.World);
		const uint64_t view = renderer.CreateSceneView();
		renderer.SetSceneViewCameraMode(view, Nyx::EViewportCameraMode::ScenePrimaryCamera);
		renderer.SetSceneViewSize(view, ViewWidth, ViewHeight);

		Nyx::Engine::VulkanEnvironment& environment = gpu.Renderer.GetEnvironment();
		const std::array<glm::vec3, 9>& c = environment.GetIrradiance(gpu.Renderer.GetSky());
		const glm::vec3 diffuse = c[0] * 0.282095f + c[2] * 0.488603f + c[6] * 0.315392f * 2.0f;

		// The roughest mip's +Z face, sampled at its middle: the mean of the four middle texels
		const uint32_t level = Nyx::Engine::VulkanEnvironment::SpecularLevels - 1;
		const uint32_t size = Nyx::Engine::VulkanEnvironment::SpecularSize >> level;
		const std::vector<glm::vec4> rough = environment.ReadSpecularLevel(gpu.Renderer.GetSky(), level);
		glm::vec3 specular(0.0f);
		for (const uint32_t y : { size / 2 - 1, size / 2 })
		{
			for (const uint32_t x : { size / 2 - 1, size / 2 })
			{
				specular += glm::vec3(rough[(static_cast<size_t>(4) * size + y) * size + x]) * 0.25f;
			}
		}

		const std::vector<glm::vec2> lookup = environment.ReadLookup();
		const glm::vec2 brdf = lookup[64 * Nyx::Engine::VulkanEnvironment::LookupSize + 64];

		const std::array<glm::vec3, 3> expected = { diffuse, specular, glm::vec3(brdf, 0.0f) };
		const std::array<const char*, 3> names = { "diffuse light", "specular light", "BRDF table" };
		for (size_t part = 0; part < materials.size(); ++part)
		{
			scene.World.Get<MeshRendererComponent>(cube).MaterialAsset = renderer.GetMaterial(materials[part].Path);
			gpu.DrawUntilShadersCompiled();
			const std::array<uint8_t, 3> pixel = MiddlePixel(gpu, view);
			Require(Near(pixel, Tonemapped(expected[part])), std::string("The material's ") + names[part] + " is " + Text(pixel) + ", expected " + Text(Tonemapped(expected[part])));
		}

		renderer.DestroySceneView(view);
		renderer.SetWorld(nullptr);
		gpu.DrawFrames(1);
	}

	// A cube facing the camera with a Standard material, a dielectric and then a metal: its middle
	// against the same light computed on the CPU
	void TestStandardMatchesTheCpu(SceneRenderer& gpu)
	{
		Nyx::IRenderer& renderer = gpu.Renderer;
		LiveMaterialFile dielectric("Materials/NyxSceneGpuTests-Dielectric.nyxmat");
		dielectric.Write("Shader = Shaders/Standard.slang\nBaseColor = 0.5 0.5 0.5\nRoughness = 0.5\n");
		LiveMaterialFile metal("Materials/NyxSceneGpuTests-Metal.nyxmat");
		metal.Write("Shader = Shaders/Standard.slang\nBaseColor = 0.9 0.6 0.3\nMetallic = 1\nRoughness = 0.3\n");

		DefaultScene scene;
		const Entity cube = AddCube(scene.World, gpu.Renderer, 0.0f, renderer.GetMaterial(dielectric.Path));
		const Entity camera = scene.World.CreateEntity();
		scene.World.Add<TransformComponent>(camera, TransformComponent{ .Position = glm::vec3(0.0f, 0.0f, 1.2f), .Rotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f), .Scale = glm::vec3(1.0f) });
		scene.World.Add<CameraComponent>(camera, CameraComponent{});
		renderer.SetWorld(&scene.World);
		const uint64_t view = renderer.CreateSceneView();
		renderer.SetSceneViewCameraMode(view, Nyx::EViewportCameraMode::ScenePrimaryCamera);
		renderer.SetSceneViewSize(view, ViewWidth, ViewHeight);

		const EnvironmentSamples environment(gpu.Renderer.GetEnvironment(), gpu.Renderer.GetSky());
		const std::array<std::pair<const LiveMaterialFile*, glm::vec3>, 2> cases = {
			std::pair{ &dielectric, LightFacingZ(environment, glm::vec3(0.5f), 0.0f, 0.5f) },
			std::pair{ &metal, LightFacingZ(environment, glm::vec3(0.9f, 0.6f, 0.3f), 1.0f, 0.3f) },
		};
		for (const auto& [material, light] : cases)
		{
			scene.World.Get<MeshRendererComponent>(cube).MaterialAsset = renderer.GetMaterial(material->Path);
			gpu.DrawUntilShadersCompiled();
			const std::array<uint8_t, 3> pixel = MiddlePixel(gpu, view);
			Require(Near(pixel, Tonemapped(light), 3), material->Path + " lights as " + Text(pixel) + ", the CPU as " + Text(Tonemapped(light)));
		}

		renderer.DestroySceneView(view);
		renderer.SetWorld(nullptr);
		gpu.DrawFrames(1);
	}

	// A cube facing the camera: its normal as a normal map tilts it, shown as colour; then masked
	// below its cutoff, where the sky behind it shows instead
	void TestNormalMapsAndMasks(SceneRenderer& gpu)
	{
		Nyx::IRenderer& renderer = gpu.Renderer;

		// One texel: in the map's own space (0, 0.6, 0.8), up the image
		const std::filesystem::path normalFile = Nyx::Paths::GetAssetsDir() / "Textures/NyxSceneGpuTests-Normal.png";
		struct RemoveAfterwards
		{
			std::filesystem::path File;
			~RemoveAfterwards()
			{
				std::error_code error;
				std::filesystem::remove(File, error);
			}
		} removeNormal{ normalFile };
		std::string error;
		Require(SavePng(normalFile, RgbaImage{ 1, 1, { 128, 204, 230, 255 } }, error), "The normal map can't be saved: " + error);

		LiveMaterialFile shader("Shaders/NyxSceneGpuTestsNormals.slang");
		shader.Write("import nyx.surface;\nstruct NyxSceneGpuTestsNormals : ISurface\n{\n    [Linear] nyx::Texture Normals;\n"
					 "    SurfaceOutput shade(SurfaceInput input)\n    {\n        SurfaceOutput o = SurfaceOutput::defaults(input);\n        o.Unlit = true;\n        o.BaseColor = float3(0.0);\n"
					 "        o.Emission = nyx::normalFromMap(Normals.Sample(nyx::PointClamp, input.UV), input) * 0.5 + 0.5;\n        return o;\n    }\n}\n");
		LiveMaterialFile normals("Materials/NyxSceneGpuTests-Normals.nyxmat");
		normals.Write("Shader = Shaders/NyxSceneGpuTestsNormals.slang\nNormals = Textures/NyxSceneGpuTests-Normal.png\n");
		LiveMaterialFile masked("Materials/NyxSceneGpuTests-Masked.nyxmat");
		masked.Write("Shader = Shaders/Standard.slang\nOpacity = 0.3\nMasked = true\n");

		DefaultScene scene;
		const Entity cube = AddCube(scene.World, gpu.Renderer, 0.0f, renderer.GetMaterial(normals.Path));
		const Entity camera = scene.World.CreateEntity();
		scene.World.Add<TransformComponent>(camera, TransformComponent{ .Position = glm::vec3(0.0f, 0.0f, 1.2f), .Rotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f), .Scale = glm::vec3(1.0f) });
		scene.World.Add<CameraComponent>(camera, CameraComponent{});
		renderer.SetWorld(&scene.World);
		const uint64_t view = renderer.CreateSceneView();
		renderer.SetSceneViewCameraMode(view, Nyx::EViewportCameraMode::ScenePrimaryCamera);
		renderer.SetSceneViewSize(view, ViewWidth, ViewHeight);
		gpu.DrawUntilShadersCompiled();

		// The cube's front face has v rising upwards, so up its image is down: (0, -0.6, 0.8)
		const glm::vec3 local = glm::vec3(128.0f, 204.0f, 230.0f) / 255.0f * 2.0f - 1.0f;
		const glm::vec3 expected = glm::normalize(glm::vec3(local.x, -local.y, local.z)) * 0.5f + 0.5f;
		const std::array<uint8_t, 3> tilted = MiddlePixel(gpu, view);
		Require(Near(tilted, Tonemapped(expected), 3), "The normal map's normal is " + Text(tilted) + ", expected " + Text(Tonemapped(expected)));

		// Masked below its cutoff: the same as no cube at all
		scene.World.Get<MeshRendererComponent>(cube).MaterialAsset = renderer.GetMaterial(masked.Path);
		gpu.DrawUntilShadersCompiled();
		const std::array<uint8_t, 3> cutOut = MiddlePixel(gpu, view);
		scene.World.Get<MeshRendererComponent>(cube).bVisible = false;
		const std::array<uint8_t, 3> nothing = MiddlePixel(gpu, view);
		Require(Near(cutOut, nothing, 1), "A masked cube below its cutoff is drawn: " + Text(cutOut) + " where the sky is " + Text(nothing));

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
	// What the material preview shows: a view's own sky (black, the studio, the scene's turned
	// about the up axis) and its debug views: one output of the material as it is, and where the
	// light before tonemapping is NaN or out of range
	void TestSkiesAndDebugViews(SceneRenderer& gpu)
	{
		Nyx::IRenderer& renderer = gpu.Renderer;
		LiveMaterialFile red("Materials/NyxSceneGpuTests-Preview.nyxmat");
		red.Write("Shader = Shaders/Standard.slang\nBaseColor = 0.8 0.2 0.1\nRoughness = 0.4\n");
		LiveMaterialFile glowing("Materials/NyxSceneGpuTests-Bright.nyxmat");
		glowing.Write("Shader = Shaders/Standard.slang\nEmission = 4 4 4\n");
		LiveMaterialFile nanShader("Shaders/NyxSceneGpuTestsNan.slang");
		nanShader.Write(GlowShader("    float Amount = -1.0;\n", "float3(sqrt(Amount))", "NyxSceneGpuTestsNan"));
		LiveMaterialFile nan("Materials/NyxSceneGpuTests-Nan.nyxmat");
		nan.Write("Shader = Shaders/NyxSceneGpuTestsNan.slang\n");

		// A sphere filling the middle of the view, the sky around it
		Registry world;
		const Entity sphere = world.CreateEntity();
		world.Add<TransformComponent>(sphere, TransformComponent{});
		MeshRendererComponent meshRenderer;
		meshRenderer.MeshAsset = renderer.GetSphereMesh();
		meshRenderer.MaterialAsset = renderer.GetMaterial(red.Path);
		world.Add<MeshRendererComponent>(sphere, meshRenderer);
		const Entity camera = world.CreateEntity();
		world.Add<TransformComponent>(camera, TransformComponent{ .Position = glm::vec3(0.0f, 0.0f, 2.0f), .Rotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f), .Scale = glm::vec3(1.0f) });
		world.Add<CameraComponent>(camera, CameraComponent{});
		const Entity sun = world.CreateEntity();
		world.Add<TransformComponent>(sun, TransformComponent{ .Position = glm::vec3(0.0f), .Rotation = glm::quat(glm::radians(glm::vec3(-45.0f, 30.0f, 0.0f))), .Scale = glm::vec3(1.0f) });
		world.Add<DirectionalLightComponent>(sun, DirectionalLightComponent{});
		renderer.GetMaterial(glowing.Path);
		renderer.GetMaterial(nan.Path);

		const uint64_t view = renderer.CreateSceneView();
		renderer.SetSceneViewWorld(view, &world);
		renderer.SetSceneViewCameraMode(view, Nyx::EViewportCameraMode::ScenePrimaryCamera);
		renderer.SetSceneViewShowEditorOverlays(view, false);
		renderer.SetSceneViewSize(view, ViewWidth, ViewHeight);
		gpu.DrawUntilShadersCompiled();

		const auto show = [&](Nyx::ESceneViewSky sky, float rotation, Nyx::ESceneViewDebug debugView)
		{
			renderer.SetSceneViewSky(view, sky, rotation);
			renderer.SetSceneViewDebug(view, debugView);
			gpu.DrawFrames(2);
			RgbaImage image;
			Require(gpu.Renderer.ReadSceneViewPixels(view, image), "The scene view can't be read back");
			return image;
		};
		const auto at = [](const RgbaImage& image, uint32_t x, uint32_t y)
		{
			const uint8_t* rgb = &image.Pixels[(static_cast<size_t>(y) * image.Width + x) * 4];
			return std::array<uint8_t, 3>{ rgb[0], rgb[1], rgb[2] };
		};
		const auto corner = [&](const RgbaImage& image)
		{
			return at(image, 4, 4);
		};
		const auto middle = [&](const RgbaImage& image)
		{
			return at(image, ViewWidth / 2, ViewHeight / 2);
		};
		const auto isGrey = [](const std::array<uint8_t, 3>& rgb)
		{
			return std::abs(rgb[0] - rgb[1]) <= 2 && std::abs(rgb[1] - rgb[2]) <= 2;
		};
		using Sky = Nyx::ESceneViewSky;
		using Debug = Nyx::ESceneViewDebug;

		// The skies: black, a grey studio lighting the sphere more, the scene's sky turned
		const RgbaImage black = show(Sky::Black, 0.0f, Debug::Lit);
		Require(corner(black) == std::array<uint8_t, 3>{ 0, 0, 0 }, "The black sky isn't black: " + Text(corner(black)));
		const RgbaImage studio = show(Sky::Studio, 0.0f, Debug::Lit);
		SaveIfAsked(studio, "Studio");
		Require(isGrey(corner(studio)) && corner(studio)[0] > 5, "The studio's sky isn't grey: " + Text(corner(studio)));
		Require(middle(studio)[0] > middle(black)[0] + 5, "The studio's light doesn't reach the sphere: " + Text(middle(studio)) + " against " + Text(middle(black)) + " under the black sky");
		const std::array<uint8_t, 3> unturned = corner(show(Sky::Scene, 0.0f, Debug::Lit));
		const std::array<uint8_t, 3> halfTurned = corner(show(Sky::Scene, glm::pi<float>(), Debug::Lit));
		const std::array<uint8_t, 3> turnedRound = corner(show(Sky::Scene, glm::two_pi<float>(), Debug::Lit));
		Require(unturned != halfTurned, "Turning the scene's sky half round doesn't change it: " + Text(unturned));
		Require(Near(turnedRound, unturned, 1), "The scene's sky turned once round isn't as it was: " + Text(turnedRound) + " against " + Text(unturned));

		// The material's outputs, as they are
		const auto encoded = [](float linear)
		{
			const float srgb = linear <= 0.0031308f ? linear * 12.92f : 1.055f * std::pow(linear, 1.0f / 2.4f) - 0.055f;
			return static_cast<uint8_t>(std::lround(srgb * 255.0f));
		};
		const RgbaImage baseColor = show(Sky::Studio, 0.0f, Debug::BaseColor);
		Require(Near(middle(baseColor), { encoded(0.8f), encoded(0.2f), encoded(0.1f) }), "The base colour view shows " + Text(middle(baseColor)));
		Require(Near(corner(baseColor), { 10, 10, 10 }), "Behind a debug view, the sky isn't dark grey: " + Text(corner(baseColor)));
		const std::array<uint8_t, 3> normal = middle(show(Sky::Studio, 0.0f, Debug::Normal));
		Require(Near(normal, { 128, 128, 255 }, 3), "The normal facing the camera shows as " + Text(normal));
		const std::array<uint8_t, 3> roughness = middle(show(Sky::Studio, 0.0f, Debug::Roughness));
		Require(Near(roughness, { 102, 102, 102 }), "Roughness 0.4 shows as " + Text(roughness));
		Require(middle(show(Sky::Studio, 0.0f, Debug::Metallic)) == std::array<uint8_t, 3>{ 0, 0, 0 }, "A dielectric shows as metal");
		Require(middle(show(Sky::Studio, 0.0f, Debug::Occlusion)) == std::array<uint8_t, 3>{ 255, 255, 255 }, "Occlusion without a texture isn't 1");
		Require(middle(show(Sky::Studio, 0.0f, Debug::Opacity)) == std::array<uint8_t, 3>{ 255, 255, 255 }, "Opacity isn't 1");
		Require(middle(show(Sky::Studio, 0.0f, Debug::Emission)) == std::array<uint8_t, 3>{ 0, 0, 0 }, "Emission shows where there is none");
		Require(!isGrey(middle(show(Sky::Studio, 0.0f, Debug::UvChecker))), "The UV checker isn't coloured by the UV");

		// Light that is wrong: brighter than white before tonemapping, and not a number
		world.Get<MeshRendererComponent>(sphere).MaterialAsset = renderer.GetMaterial(glowing.Path);
		const RgbaImage bright = show(Sky::Studio, 0.0f, Debug::OutOfRange);
		Require(Near(middle(bright), { 255, 38, 26 }, 2), "Light above 1 isn't marked red: " + Text(middle(bright)));
		Require(isGrey(corner(bright)), "The studio's sky, within range, isn't grey: " + Text(corner(bright)));
		world.Get<MeshRendererComponent>(sphere).MaterialAsset = renderer.GetMaterial(nan.Path);
		const RgbaImage notANumber = show(Sky::Studio, 0.0f, Debug::NanInf);
		SaveIfAsked(notANumber, "NanInf");
		Require(middle(notANumber) == std::array<uint8_t, 3>{ 255, 0, 255 }, "Light that isn't a number isn't marked magenta: " + Text(middle(notANumber)));
		Require(isGrey(corner(notANumber)), "Light that is fine isn't grey: " + Text(corner(notANumber)));

		renderer.DestroySceneView(view);
		gpu.DrawFrames(1);
	}

	// What the material editor does: it reads a material's values, its shader's parameters and
	// their defaults; values it sets show in the next frame while the file stays as it is; the
	// file saved and read again brings its own values back
	void TestMaterialValuesSetInMemory(SceneRenderer& gpu)
	{
		Nyx::IRenderer& renderer = gpu.Renderer;
		LiveMaterialFile file("Materials/NyxSceneGpuTests-Edited.nyxmat");
		const std::string text = "Shader = Shaders/Standard.slang\nBaseColor = 1 0 0\nRoughness = 1\n";
		file.Write(text);

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
		const std::array<uint8_t, 3> red = MiddlePixel(gpu, view);
		Require(red[0] > red[1] && red[0] > red[2], "The red material isn't red: " + Text(red));

		const Nyx::MaterialView material = renderer.ViewMaterial(file.Path);
		Require(material.Asset && material.Asset->ToText() == text, "The material's values aren't its file's");
		Require(material.Layout && material.Layout->FindField("Roughness") && material.FileRevision > 0, "The material's shader parameters aren't there once it compiled");
		const ShaderField* metallic = material.Layout->FindField("Metallic");
		Require(metallic && ReadMaterialValue(*metallic, material.Defaults, metallic->Offset) == "0", "Standard's Metallic doesn't read as its default, 0");
		const uint64_t fileRevision = material.FileRevision;

		// Green in the next frame; the file stays red
		MaterialAsset edited = *material.Asset;
		edited.SetValue("BaseColor", "0 1 0");
		renderer.SetMaterialValues(file.Path, edited);
		const std::array<uint8_t, 3> green = MiddlePixel(gpu, view);
		Require(green[1] > green[0] && green[1] > green[2], "The values set didn't show in the next frame: " + Text(green));
		Require(*renderer.ViewMaterial(file.Path).Asset->FindValue("BaseColor") == "0 1 0", "The material doesn't hold the values set");
		std::ifstream stream(file.File, std::ios::binary);
		const std::string onDisk((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());
		Require(onDisk == text, "Setting values changed the file");

		// Saved blue: read again, the file's values are drawn
		file.Write("Shader = Shaders/Standard.slang\nBaseColor = 0 0 1\nRoughness = 1\n");
		std::array<uint8_t, 3> blue = green;
		const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
		while (!(blue[2] > blue[0] && blue[2] > blue[1]) && std::chrono::steady_clock::now() < deadline)
		{
			std::this_thread::sleep_for(std::chrono::milliseconds(20));
			blue = MiddlePixel(gpu, view);
		}
		Require(blue[2] > blue[0] && blue[2] > blue[1], "The file saved and read again didn't bring its values back: " + Text(blue));
		Require(renderer.ViewMaterial(file.Path).FileRevision > fileRevision, "Reading the file again didn't count");

		renderer.DestroySceneView(view);
		renderer.SetWorld(nullptr);
		gpu.DrawFrames(1);
	}

	// As the editor's windows do: a view made while the UI is built, after the frame's views were
	// recorded, and one let go of there (the Preview window opened and closed). The frame goes on;
	// the view goes once the GPU is done with it.
	void TestViewsMadeAndLetGoWhileTheUIIsBuilt(SceneRenderer& gpu)
	{
		Nyx::IRenderer& renderer = gpu.Renderer;
		DefaultScene scene;
		MakeDefaultScene(scene, gpu.Renderer);
		renderer.SetWorld(&scene.World);
		const uint64_t kept = renderer.CreateSceneView();
		renderer.SetSceneViewSize(kept, ViewWidth, ViewHeight);
		gpu.DrawFrames(2);

		uint64_t made = 0;
		gpu.Renderer.DrawFrame([&]()
			{
				made = renderer.CreateSceneView();
				renderer.SetSceneViewSize(made, ViewWidth, ViewHeight);
				Require(renderer.WasSceneViewRecreatedThisFrame(made), "A view made while the UI is built shows before it was drawn");
			});
		gpu.DrawFrames(2);
		Require(!renderer.WasSceneViewRecreatedThisFrame(made), "The view made while the UI was built doesn't show after it was drawn");

		gpu.Renderer.DrawFrame([&]()
			{
				renderer.DestroySceneView(made);
			});
		gpu.DrawFrames(2);
		const std::vector<uint64_t> views = renderer.GetSceneViewIds();
		Require(std::find(views.begin(), views.end(), made) == views.end(), "The view let go of while the UI was built is still there");
		Require(std::find(views.begin(), views.end(), kept) != views.end(), "Letting a view go took another one");
		RgbaImage image;
		Require(gpu.Renderer.ReadSceneViewPixels(kept, image), "The view kept can't be read back");

		renderer.DestroySceneView(kept);
		renderer.SetWorld(nullptr);
		gpu.DrawFrames(1);
	}

	// As a game the editor started: the renderer follows the editor's shaders, so a shader saved
	// compiles nothing here, while the editor's compiles of it and the material values it edits
	// arrive as link messages (ShaderProgram, MaterialValues). Last: the renderer follows the
	// editor from here on.
	void TestFollowingTheEditor(SceneRenderer& gpu)
	{
		Nyx::IRenderer& renderer = gpu.Renderer;
		LiveMaterialFile shader("Shaders/NyxSceneGpuTestsLinked.slang");
		shader.Write(GlowShader("    [Color] float3 Glow = float3(1.0, 0.0, 0.0);\n", "Glow", "NyxSceneGpuTestsLinked"));
		LiveMaterialFile material("Materials/NyxSceneGpuTests-Linked.nyxmat");
		material.Write("Shader = Shaders/NyxSceneGpuTestsLinked.slang\n");
		renderer.FollowEditorShaders();

		DefaultScene scene;
		AddCube(scene.World, gpu.Renderer, 0.0f, renderer.GetMaterial(material.Path));
		const Entity camera = scene.World.CreateEntity();
		scene.World.Add<TransformComponent>(camera, TransformComponent{ .Position = glm::vec3(0.0f, 0.0f, 1.2f), .Rotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f), .Scale = glm::vec3(1.0f) });
		scene.World.Add<CameraComponent>(camera, CameraComponent{});
		renderer.SetWorld(&scene.World);
		const uint64_t view = renderer.CreateSceneView();
		renderer.SetSceneViewCameraMode(view, Nyx::EViewportCameraMode::ScenePrimaryCamera);
		renderer.SetSceneViewSize(view, ViewWidth, ViewHeight);

		// Nothing compiled the shader yet: it compiles here all the same
		const std::array<uint8_t, 3> red = DrawUntilMiddleChanges(gpu, view, Tonemapped(glm::vec3(1.0f, 0.0f, 1.0f)));
		Require(Near(red, Tonemapped(glm::vec3(1.0f, 0.0f, 0.0f))), "The shader nothing compiled yet doesn't draw: " + Text(red));

		// Saved at half the glow: nothing compiles here
		shader.Write(GlowShader("    [Color] float3 Glow = float3(1.0, 0.0, 0.0);\n", "Glow * 0.5", "NyxSceneGpuTestsLinked"));
		const auto drawnUntil = std::chrono::steady_clock::now() + std::chrono::seconds(1);
		while (std::chrono::steady_clock::now() < drawnUntil)
		{
			std::this_thread::sleep_for(std::chrono::milliseconds(20));
			gpu.DrawFrames(1);
		}
		Require(MiddlePixel(gpu, view) == red && !renderer.IsCompilingShaders(), "A saved shader compiled while following the editor");

		// The editor's compiles of it, the material's and its defaults writer's, over the link
		Nyx::Engine::ShaderCompiler compiler({ Nyx::Paths::GetShaderLibraryDir() });
		const std::filesystem::path file = std::filesystem::path(shader.File).make_preferred();
		const Nyx::Engine::ShaderCompileRequest materialRequest{ .Kind = Nyx::Engine::EShaderCompileKind::Material, .File = file, .Domain = Nyx::Engine::ShaderDomain::Surface() };
		const Nyx::Engine::ShaderCompileRequest defaultsRequest{ .Kind = Nyx::Engine::EShaderCompileKind::DefaultsWriter, .File = file };
		const Nyx::Engine::ShaderProgramMessage materialCompile{ materialRequest, compiler.CompileMaterial(file, Nyx::Engine::ShaderDomain::Surface()) };
		const Nyx::Engine::ShaderProgramMessage defaultsCompile{ defaultsRequest, compiler.CompileDefaultsWriter(file) };
		Require(materialCompile.Result.bSucceeded && defaultsCompile.Result.bSucceeded, "The editor's compiles failed");
		Require(Nyx::Engine::ApplyRendererEdit(Nyx::Engine::MakeNetMessage(materialCompile), renderer) == Nyx::Engine::ELiveEditResult::Applied &&
				Nyx::Engine::ApplyRendererEdit(Nyx::Engine::MakeNetMessage(defaultsCompile), renderer) == Nyx::Engine::ELiveEditResult::Applied,
			"The editor's compiles weren't taken");
		const std::array<uint8_t, 3> dimmed = DrawUntilMiddleChanges(gpu, view, red);
		Require(Near(dimmed, Tonemapped(glm::vec3(0.5f, 0.0f, 0.0f))), "The editor's compile isn't drawn: " + Text(dimmed));

		// The editor's values, not saved
		const Nyx::Engine::MaterialValuesMessage values{ material.Path, "Shader = Shaders/NyxSceneGpuTestsLinked.slang\nGlow = 0 0 1\n" };
		Require(Nyx::Engine::ApplyRendererEdit(Nyx::Engine::MakeNetMessage(values), renderer) == Nyx::Engine::ELiveEditResult::Applied, "The editor's values weren't taken");
		const std::array<uint8_t, 3> blue = MiddlePixel(gpu, view);
		Require(Near(blue, Tonemapped(glm::vec3(0.0f, 0.0f, 0.5f))), "The editor's values aren't drawn in the next frame: " + Text(blue));

		renderer.DestroySceneView(view);
		renderer.SetWorld(nullptr);
		gpu.DrawFrames(1);
	}

	// Clicks the middle of the view: whether a pick came back, and what it hit
	Nyx::IRenderer::PickResult PickMiddle(SceneRenderer& gpu, uint64_t view)
	{
		Nyx::IRenderer& renderer = gpu.Renderer;
		renderer.RequestPick(view, ViewWidth / 2, ViewHeight / 2);
		gpu.DrawFrames(3);
		return renderer.ConsumeLastPickResult(view);
	}

	bool Picks(SceneRenderer& gpu, uint64_t view, Entity entity)
	{
		const Nyx::IRenderer::PickResult pick = PickMiddle(gpu, view);
		return pick.bHasNewResult && pick.HitEntity == entity;
	}

	RgbaImage ReadView(SceneRenderer& gpu, uint64_t view)
	{
		RgbaImage image;
		Require(gpu.Renderer.ReadSceneViewPixels(view, image), "The scene view can't be read back");
		return image;
	}

	// The default scene in one view, a world of a sphere of its own in another, as a material's
	// preview will show one
	void TestViewsWithWorldsOfTheirOwn(SceneRenderer& gpu)
	{
		Nyx::IRenderer& renderer = gpu.Renderer;
		DefaultScene scene;
		MakeDefaultScene(scene, gpu.Renderer);
		renderer.SetWorld(&scene.World);

		// The sphere has the reflective cube's handle, in another registry
		Registry preview;
		const Entity camera = preview.CreateEntity();
		preview.Add<TransformComponent>(camera, TransformComponent{ .Position = glm::vec3(0.0f, 0.0f, 2.0f), .Rotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f), .Scale = glm::vec3(1.0f) });
		preview.Add<CameraComponent>(camera, CameraComponent{});
		const Entity sphere = preview.CreateEntity();
		preview.Add<TransformComponent>(sphere, TransformComponent{ .Position = glm::vec3(0.0f), .Rotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f), .Scale = glm::vec3(1.0f) });
		MeshRendererComponent meshRenderer;
		meshRenderer.MeshAsset = renderer.GetSphereMesh();
		meshRenderer.MaterialAsset = renderer.GetMaterial("Materials/Untextured.nyxmat");
		preview.Add<MeshRendererComponent>(sphere, meshRenderer);
		Require(sphere == scene.ReflectiveCube, "The test needs the sphere to have the reflective cube's handle");

		const uint64_t sceneView = renderer.CreateSceneView();
		const uint64_t previewView = renderer.CreateSceneView();
		for (const uint64_t view : { sceneView, previewView })
		{
			renderer.SetSceneViewCameraMode(view, Nyx::EViewportCameraMode::ScenePrimaryCamera);
			renderer.SetSceneViewSize(view, ViewWidth, ViewHeight);
		}
		renderer.SetSceneViewWorld(previewView, &preview);
		renderer.SetSelectedEntity(scene.ReflectiveCube);
		gpu.DrawUntilShadersCompiled();
		gpu.DrawFrames(3);

		// The scene's view as if it were the only one; each view picks from its own world
		const RgbaImage sceneImage = ReadView(gpu, sceneView);
		SaveIfAsked(sceneImage, "TwoWorlds-Scene");
		RequireLooksLike(sceneImage, std::filesystem::path(NYX_TEST_REFERENCES_DIR) / "DefaultScene.png");
		SaveIfAsked(ReadView(gpu, previewView), "TwoWorlds-Preview");
		Require(Picks(gpu, sceneView, scene.ReflectiveCube), "A click on the scene's view doesn't pick the middle cube");
		Require(Picks(gpu, previewView, sphere), "A click on the preview doesn't pick its sphere");

		// The outline is the scene's: the preview looks the same without the selection
		const RgbaImage previewSelected = ReadView(gpu, previewView);
		renderer.SetSelectedEntity(std::nullopt);
		gpu.DrawFrames(2);
		Require(ReadView(gpu, previewView).Pixels == previewSelected.Pixels, "The preview outlines the sphere, which has the selected cube's handle");
		Require(ReadView(gpu, sceneView).Pixels != sceneImage.Pixels, "The scene's view looks the same without the selection: the test can't see outlines");

		// Setting a world anew drops the pending picks of its views only
		renderer.RequestPick(sceneView, ViewWidth / 2, ViewHeight / 2);
		renderer.RequestPick(previewView, ViewWidth / 2, ViewHeight / 2);
		renderer.SetWorld(&scene.World);
		gpu.DrawFrames(3);
		Require(!renderer.ConsumeLastPickResult(sceneView).bHasNewResult, "The scene's view kept its pick of the world set anew");
		Nyx::IRenderer::PickResult pick = renderer.ConsumeLastPickResult(previewView);
		Require(pick.bHasNewResult && pick.HitEntity == sphere, "Setting the scene dropped the preview's pick");

		renderer.RequestPick(sceneView, ViewWidth / 2, ViewHeight / 2);
		renderer.RequestPick(previewView, ViewWidth / 2, ViewHeight / 2);
		renderer.SetSceneViewWorld(previewView, &preview);
		gpu.DrawFrames(3);
		Require(!renderer.ConsumeLastPickResult(previewView).bHasNewResult, "The preview kept its pick of the world set anew");
		pick = renderer.ConsumeLastPickResult(sceneView);
		Require(pick.bHasNewResult && pick.HitEntity == scene.ReflectiveCube, "Setting the preview's world dropped the scene's pick");

		// Without a world of its own, the view shows the scene again
		renderer.SetSceneViewWorld(previewView, nullptr);
		gpu.DrawFrames(2);
		Require(ReadView(gpu, previewView).Pixels == ReadView(gpu, sceneView).Pixels, "A view back on the renderer's world doesn't show the scene");
		Require(Picks(gpu, previewView, scene.ReflectiveCube), "A view back on the renderer's world doesn't pick from it");

		// Without the scene, the preview goes on
		renderer.SetSceneViewWorld(previewView, &preview);
		renderer.SetWorld(nullptr);
		gpu.DrawFrames(2);
		Require(!PickMiddle(gpu, sceneView).bHasNewResult, "A view without a world took a click");
		Require(Picks(gpu, previewView, sphere), "The preview stopped picking without the scene");

		renderer.DestroySceneView(sceneView);
		renderer.DestroySceneView(previewView);
		gpu.DrawFrames(1);
	}
}

int main()
{
	try
	{
		Nyx::Core::Logger::Get().Init();
		SceneRenderer gpu("NyxSceneGpuTests");
		TestDefaultScene(gpu);
		TestMissingMaterial(gpu);
		TestSavedMaterialFilesReload(gpu);
		TestSavedMaterialShadersRecompile(gpu);
		TestHdrAndExposure(gpu);
		TestMaterialsFindTheEnvironment(gpu);
		TestStandardMatchesTheCpu(gpu);
		TestNormalMapsAndMasks(gpu);
		TestViewsWithWorldsOfTheirOwn(gpu);
		TestMaterialValuesSetInMemory(gpu);
		TestSkiesAndDebugViews(gpu);
		TestViewsMadeAndLetGoWhileTheUIIsBuilt(gpu);
		// Last: the renderer follows the editor's shaders from there on
		TestFollowingTheEditor(gpu);
	}
	catch (const std::exception& error)
	{
		std::cerr << "FAILED: " << error.what() << "\n";
		return 1;
	}

	std::cout << "All scene GPU tests passed\n";
	return 0;
}
