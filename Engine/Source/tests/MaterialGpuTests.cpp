// The material test scene on a real GPU:
//   - Assets/Scenes/MaterialTest.nyxscene, a grid of Standard spheres, metallic rising by row and
//     roughness by column, loaded as the editor loads scenes and compared with an approved
//     reference image (the mean difference and how many pixels differ much);
//   - spheres facing the camera light as the same formulas do on the CPU;
//   - the furnace test: under a uniform sky with the sun off, white dielectric spheres send back
//     exactly the sky's light and vanish into it, at any roughness.
//
// NyxMaterialGpuTests --write-scene writes the scene and its 25 materials into the assets folder:
// this file is their source. NYX_MATERIAL_TEST_OUTPUT=<folder> saves what was rendered, e.g. to
// approve a new reference after a change that is meant to change the picture.
#include "AssetResolver.h"
#include "CameraComponent.h"
#include "ComponentRegistration.h"
#include "DirectionalLightComponent.h"
#include "GuidComponent.h"
#include "Log.h"
#include "NameComponent.h"
#include "RenderTestSupport.h"
#include "SceneSerializer.h"
#include "TransformComponent.h"

#include <glm/gtc/quaternion.hpp>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>

namespace
{
	using namespace Nyx::Engine;
	using namespace Nyx::RenderTests;

	constexpr uint32_t Width = 640;
	constexpr uint32_t Height = 360;
	constexpr int GridSize = 5;
	const std::filesystem::path ScenePath = "Scenes/MaterialTest.nyxscene";
	const glm::vec3 BaseColor(1.0f, 0.77f, 0.34f);

	// "Materials/MaterialTest/M050-R025.nyxmat": metallic 0.5, roughness 0.25
	std::string MaterialPath(int metallic, int roughness)
	{
		char name[64];
		std::snprintf(name, sizeof(name), "Materials/MaterialTest/M%03d-R%03d.nyxmat", metallic * 100 / (GridSize - 1), roughness * 100 / (GridSize - 1));
		return name;
	}

	Entity AddEntity(Registry& world, const std::string& name, const TransformComponent& transform)
	{
		const Entity entity = world.CreateEntity();
		world.Add<NameComponent>(entity, NameComponent{ name });
		world.Add<GuidComponent>(entity, GuidComponent{ EntityGuid::Generate() });
		world.Add<TransformComponent>(entity, transform);
		return entity;
	}

	TransformComponent At(glm::vec3 position, glm::vec3 degrees = glm::vec3(0.0f))
	{
		return TransformComponent{ .Position = position, .Rotation = glm::quat(glm::radians(degrees)), .Scale = glm::vec3(1.0f) };
	}

	// The scene and its materials, as this test expects them
	void WriteScene()
	{
		const std::filesystem::path assets = Nyx::Paths::GetAssetsDir();
		std::filesystem::create_directories(assets / "Materials/MaterialTest");
		Registry world;
		for (int metallic = 0; metallic < GridSize; ++metallic)
		{
			for (int roughness = 0; roughness < GridSize; ++roughness)
			{
				const float m = static_cast<float>(metallic) / (GridSize - 1);
				const float r = static_cast<float>(roughness) / (GridSize - 1);
				const std::string path = MaterialPath(metallic, roughness);
				std::ofstream material(assets / path, std::ios::binary | std::ios::trunc);
				char text[256];
				std::snprintf(text, sizeof(text), "# The material test scene's sphere of metallic %.2f, roughness %.2f (NyxMaterialGpuTests --write-scene)\nShader = Shaders/Standard.slang\nBaseColor = %.2f %.2f %.2f\nMetallic = %.2f\nRoughness = %.2f\n", m, r, BaseColor.r, BaseColor.g, BaseColor.b, m, r);
				material << text;

				const glm::vec3 position((roughness - (GridSize - 1) / 2.0f) * 1.2f, (metallic - (GridSize - 1) / 2.0f) * 1.2f, 0.0f);
				const Entity sphere = AddEntity(world, "Sphere M" + std::to_string(metallic * 25) + " R" + std::to_string(roughness * 25), At(position));
				world.Add<MeshRendererComponent>(sphere, MeshRendererComponent{ .Mesh = AssetReference{ .Type = "Mesh", .Path = "Meshes/Sphere.nyxmesh" }, .Material = AssetReference{ .Type = "Material", .Path = path }, .bVisible = true });
			}
		}
		const Entity camera = AddEntity(world, "Camera", At(glm::vec3(0.0f, 0.0f, 6.5f)));
		world.Add<CameraComponent>(camera, CameraComponent{});
		const Entity sun = AddEntity(world, "Sun", At(glm::vec3(0.0f, 5.0f, 0.0f), glm::vec3(-50.0f, 30.0f, 0.0f)));
		world.Add<DirectionalLightComponent>(sun, DirectionalLightComponent{});

		Require(SceneSerializer::SaveToFile(world, assets / ScenePath), "The scene can't be saved");
		std::cout << "Wrote " << (assets / ScenePath).string() << " and its materials\n";
	}

	uint64_t MakeView(Nyx::IRenderer& renderer, uint32_t width, uint32_t height)
	{
		const uint64_t view = renderer.CreateSceneView();
		renderer.SetSceneViewCameraMode(view, Nyx::EViewportCameraMode::ScenePrimaryCamera);
		renderer.SetSceneViewShowEditorOverlays(view, false);
		renderer.SetSceneViewSize(view, width, height);
		return view;
	}

	void SaveIfAsked(const RgbaImage& image, const std::string& name)
	{
		const char* folder = std::getenv("NYX_MATERIAL_TEST_OUTPUT");
		if (folder == nullptr || *folder == '\0')
		{
			return;
		}
		const std::filesystem::path path = std::filesystem::path(folder) / (name + ".png");
		std::string error;
		Require(SavePng(path, image, error), "Couldn't save " + path.string() + ": " + error);
		std::cout << "Saved " << path.string() << "\n";
	}

	// The scene file, loaded as the editor loads it, against its approved reference. Another GPU
	// or driver rounds a little differently: the mean difference stays small, and few pixels
	// differ much.
	void TestTheSceneLooksAsApproved(SceneRenderer& gpu)
	{
		Nyx::IRenderer& renderer = gpu.Renderer;
		AssetResolver resolver(renderer);
		Registry world;
		Require(SceneSerializer::LoadFromFile(Nyx::Paths::GetAssetsDir() / ScenePath, world, ScenePostLoadContext{ &resolver }),
			"The material test scene can't be loaded; NyxMaterialGpuTests --write-scene writes it");
		renderer.SetWorld(&world);
		const uint64_t view = MakeView(renderer, Width, Height);
		gpu.DrawUntilShadersCompiled();
		gpu.DrawFrames(3);

		RgbaImage image;
		Require(gpu.Renderer.ReadSceneViewPixels(view, image), "The scene view can't be read back");
		SaveIfAsked(image, "MaterialTest");

		RgbaImage reference;
		std::string error;
		const std::filesystem::path referencePath = std::filesystem::path(NYX_TEST_REFERENCES_DIR) / "MaterialTest.png";
		Require(LoadRgbaImage(referencePath, reference, error), "No reference image " + referencePath.string() + "; run with NYX_MATERIAL_TEST_OUTPUT set to make one");
		Require(reference.Width == image.Width && reference.Height == image.Height, "The reference image has another size");
		double sum = 0.0;
		int largest = 0;
		size_t differentPixels = 0;
		for (size_t pixel = 0; pixel < static_cast<size_t>(image.Width) * image.Height; ++pixel)
		{
			int difference = 0;
			for (size_t channel = 0; channel < 3; ++channel)
			{
				const int channelDifference = std::abs(int(image.Pixels[pixel * 4 + channel]) - int(reference.Pixels[pixel * 4 + channel]));
				sum += channelDifference;
				difference = std::max(difference, channelDifference);
			}
			largest = std::max(largest, difference);
			differentPixels += difference > 16 ? 1 : 0;
		}
		const double mean = sum / (static_cast<double>(image.Width) * image.Height * 3);
		std::cout << "MaterialTest.png: mean difference " << mean << ", largest " << largest << ", " << differentPixels << " pixels by more than 16\n";
		Require(mean <= 1.0 && differentPixels <= static_cast<size_t>(image.Width) * image.Height / 500,
			"The material test scene differs from its reference: mean " + std::to_string(mean) + ", " + std::to_string(differentPixels) + " pixels by more than 16; NYX_MATERIAL_TEST_OUTPUT saves it");

		renderer.DestroySceneView(view);
		renderer.SetWorld(nullptr);
		gpu.DrawFrames(1);
	}

	// One sphere at a time in front of the camera, under the default sun: its middle (normal and
	// view both towards the camera) against the light computed on the CPU
	void TestSpheresMatchTheCpu(SceneRenderer& gpu)
	{
		Nyx::IRenderer& renderer = gpu.Renderer;
		Registry world;
		const Entity sphere = world.CreateEntity();
		world.Add<TransformComponent>(sphere, At(glm::vec3(0.0f)));
		MeshRendererComponent meshRenderer;
		meshRenderer.MeshAsset = renderer.GetSphereMesh();
		world.Add<MeshRendererComponent>(sphere, meshRenderer);
		const Entity camera = world.CreateEntity();
		world.Add<TransformComponent>(camera, At(glm::vec3(0.0f, 0.0f, 1.6f)));
		world.Add<CameraComponent>(camera, CameraComponent{});
		renderer.SetWorld(&world);
		const uint64_t view = MakeView(renderer, 480, 270);

		const EnvironmentSamples environment(gpu.Renderer.GetEnvironment(), gpu.Renderer.GetSky());
		for (const auto& [metallic, roughness] : { std::pair{ 0, 2 }, std::pair{ 4, 1 }, std::pair{ 2, 3 }, std::pair{ 0, 0 }, std::pair{ 4, 4 } })
		{
			world.Get<MeshRendererComponent>(sphere).MaterialAsset = renderer.GetMaterial(MaterialPath(metallic, roughness));
			gpu.DrawUntilShadersCompiled();
			const std::array<uint8_t, 3> pixel = MiddlePixel(gpu, view);
			const glm::vec3 light = LightFacingZ(environment, BaseColor, static_cast<float>(metallic) / (GridSize - 1), static_cast<float>(roughness) / (GridSize - 1));
			Require(Near(pixel, Tonemapped(light), 3), MaterialPath(metallic, roughness) + " lights as " + Text(pixel) + ", the CPU as " + Text(Tonemapped(light)));
		}

		renderer.DestroySceneView(view);
		renderer.SetWorld(nullptr);
		gpu.DrawFrames(1);
	}

	// A uniform grey sky, the sun off, and three white spheres: dielectrics vanish into the sky at
	// any roughness, metals come out no brighter than it
	void TestFurnace(SceneRenderer& gpu)
	{
		Nyx::IRenderer& renderer = gpu.Renderer;
		const uint32_t loadedSky = gpu.Renderer.GetSky();
		const std::vector<uint8_t> grey(static_cast<size_t>(4) * 4 * 6 * 4, 128);
		gpu.Renderer.SetSky(gpu.Renderer.GetBindless().CreateCubeTexture(4, grey, vk::Format::eR8G8B8A8Unorm));
		const float sky = 128.0f / 255.0f;

		std::array<LiveMaterialFile, 3> dielectrics = { LiveMaterialFile("Materials/NyxMaterialGpuTests-White0.nyxmat"), LiveMaterialFile("Materials/NyxMaterialGpuTests-White50.nyxmat"),
			LiveMaterialFile("Materials/NyxMaterialGpuTests-White100.nyxmat") };
		std::array<LiveMaterialFile, 3> metals = { LiveMaterialFile("Materials/NyxMaterialGpuTests-Metal0.nyxmat"), LiveMaterialFile("Materials/NyxMaterialGpuTests-Metal50.nyxmat"),
			LiveMaterialFile("Materials/NyxMaterialGpuTests-Metal100.nyxmat") };
		const std::array<const char*, 3> roughness = { "0", "0.5", "1" };
		for (size_t index = 0; index < 3; ++index)
		{
			dielectrics[index].Write(std::string("Shader = Shaders/Standard.slang\nRoughness = ") + roughness[index] + "\n");
			metals[index].Write(std::string("Shader = Shaders/Standard.slang\nMetallic = 1\nRoughness = ") + roughness[index] + "\n");
		}

		Registry world;
		std::array<Entity, 3> spheres{};
		for (size_t index = 0; index < 3; ++index)
		{
			spheres[index] = world.CreateEntity();
			world.Add<TransformComponent>(spheres[index], At(glm::vec3((static_cast<float>(index) - 1.0f) * 1.2f, 0.0f, 0.0f)));
			MeshRendererComponent meshRenderer;
			meshRenderer.MeshAsset = renderer.GetSphereMesh();
			meshRenderer.MaterialAsset = renderer.GetMaterial(dielectrics[index].Path);
			world.Add<MeshRendererComponent>(spheres[index], meshRenderer);
		}
		const Entity camera = world.CreateEntity();
		world.Add<TransformComponent>(camera, At(glm::vec3(0.0f, 0.0f, 3.0f)));
		world.Add<CameraComponent>(camera, CameraComponent{});
		const Entity sun = world.CreateEntity();
		world.Add<TransformComponent>(sun, At(glm::vec3(0.0f), glm::vec3(-45.0f, 0.0f, 0.0f)));
		world.Add<DirectionalLightComponent>(sun, DirectionalLightComponent{ .Intensity = 0.0f });
		renderer.SetWorld(&world);
		const uint64_t view = MakeView(renderer, 480, 270);
		gpu.DrawUntilShadersCompiled();
		gpu.DrawFrames(3);

		// Dielectrics: every pixel the sky's, spheres and all
		const std::array<uint8_t, 3> expected = Tonemapped(glm::vec3(sky));
		RgbaImage image;
		Require(gpu.Renderer.ReadSceneViewPixels(view, image), "The scene view can't be read back");
		SaveIfAsked(image, "FurnaceDielectrics");
		size_t visible = 0;
		for (size_t pixel = 0; pixel < static_cast<size_t>(image.Width) * image.Height; ++pixel)
		{
			visible += Near({ image.Pixels[pixel * 4], image.Pixels[pixel * 4 + 1], image.Pixels[pixel * 4 + 2] }, expected, 1) ? 0 : 1;
		}
		Require(visible == 0, std::to_string(visible) + " pixels of white dielectric spheres stand out of a uniform sky (the furnace test)");

		// Metals: their single-scattering GGX loses light as they get rougher; never more than the sky
		for (size_t index = 0; index < 3; ++index)
		{
			world.Get<MeshRendererComponent>(spheres[index]).MaterialAsset = renderer.GetMaterial(metals[index].Path);
		}
		gpu.DrawUntilShadersCompiled();
		gpu.DrawFrames(3);
		Require(gpu.Renderer.ReadSceneViewPixels(view, image), "The scene view can't be read back");
		SaveIfAsked(image, "FurnaceMetals");
		std::cout << "Furnace, white metal sphere middles at roughness 0, 0.5, 1 (the sky is " << int(expected[0]) << "):";
		for (size_t index = 0; index < 3; ++index)
		{
			const uint32_t x = image.Width / 2 + static_cast<uint32_t>((static_cast<float>(index) - 1.0f) * image.Width * 0.205f);
			const size_t offset = (static_cast<size_t>(image.Height / 2) * image.Width + x) * 4;
			const int value = image.Pixels[offset];
			std::cout << " " << value;
			Require(value <= expected[0] + 1, "A white metal sphere is brighter than the sky in the furnace");
		}
		std::cout << "\n";

		renderer.DestroySceneView(view);
		renderer.SetWorld(nullptr);
		gpu.Renderer.SetSky(loadedSky);
		gpu.DrawFrames(1);
	}
}

int main(int argc, char** argv)
{
	try
	{
		Nyx::Core::Logger::Get().Init();
		RegisterComponentTypes();
		if (argc > 1 && std::strcmp(argv[1], "--write-scene") == 0)
		{
			WriteScene();
			return 0;
		}
		SceneRenderer gpu("NyxMaterialGpuTests");
		TestTheSceneLooksAsApproved(gpu);
		TestSpheresMatchTheCpu(gpu);
		TestFurnace(gpu);
	}
	catch (const std::exception& error)
	{
		std::cerr << "FAILED: " << error.what() << "\n";
		return 1;
	}

	std::cout << "All material GPU tests passed\n";
	return 0;
}
