// Multisample anti-aliasing of the scene views on a real GPU (Docs/Design/anti-aliasing.md). A field
// of thin bars, some thinner than a pixel, tilted every way, glowing white under a black sky: with
// one sample a pixel every pixel is the sky or a bar, nothing between; with 4, the pixels a bar's
// edge crosses are partly covered, of as many shades as the samples allow, and 8 give more shades.
// The sample count is clamped to what the device can do, changes while views are open (their
// targets made again), and drawn again is the same picture to the bit. Picking stays exact: the
// same entity under every pixel at every sample count. A view's targets are as big as the samples
// make them, printed at 1920 x 1080.
//
// NYX_SCENE_TEST_OUTPUT=<folder> saves what was rendered there.
#include "CameraComponent.h"
#include "ComponentRegistration.h"
#include "DirectionalLightComponent.h"
#include "Entity.h"
#include "ImageFile.h"
#include "Log.h"
#include "MeshRendererComponent.h"
#include "Multisampling.h"
#include "TransformComponent.h"
#include "VulkanRenderer.h"
#include "RenderTestSupport.h"

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <optional>
#include <set>
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
		const std::filesystem::path path = std::filesystem::path(folder) / ("Multisampling-" + name + ".png");
		std::string error;
		Require(SavePng(path, image, error), "Couldn't save " + path.string() + ": " + error);
		std::cout << "Saved " << path.string() << "\n";
	}

	void TestChoosingTheSamples()
	{
		constexpr uint32_t UpToEight = 1 | 2 | 4 | 8;
		Require(ChooseMsaaSamples(4, UpToEight) == 4 && ChooseMsaaSamples(8, UpToEight) == 8 && ChooseMsaaSamples(1, UpToEight) == 1, "A sample count offered isn't taken");
		Require(ChooseMsaaSamples(3, UpToEight) == 2 && ChooseMsaaSamples(7, UpToEight) == 4 && ChooseMsaaSamples(64, UpToEight) == 8, "A count between isn't rounded down");
		Require(ChooseMsaaSamples(0, UpToEight) == 1 && ChooseMsaaSamples(4, 0) == 1, "None asked or none supported isn't one sample");
		Require(ChooseMsaaSamples(8, 1 | 2 | 4) == 4 && ChooseMsaaSamples(4, 1 | 8) == 1 && ChooseMsaaSamples(8, 1 | 2 | 8) == 8, "More than the device can do isn't the most it can");
	}

	// Bars 2.5 m long, between half a pixel and two and a half wide at 6 m, tilted round the view's
	// axis from nearly upright to nearly flat, glowing white: nothing lights them or the sky
	struct BarField
	{
		Registry World;
		LiveMaterialFile WhiteFile{ "Materials/MultisamplingTestWhite.nyxmat" };
		std::vector<Entity> Bars;

		explicit BarField(VulkanRenderer& renderer)
		{
			WhiteFile.Write("Shader = Shaders/Standard.slang\nBaseColor = 0 0 0\nMetallic = 0\nRoughness = 1\nEmission = 1 1 1\n");
			Material* white = renderer.GetMaterial(WhiteFile.Path);
			const float widths[] = { 0.012f, 0.02f, 0.035f, 0.06f };
			const float angles[] = { 8.0f, 23.0f, 37.0f, 52.0f, 66.0f, 81.0f, 98.0f, 113.0f, 127.0f, 142.0f, 156.0f, 171.0f };
			for (int index = 0; index < 12; ++index)
			{
				const Entity bar = World.CreateEntity();
				const glm::vec3 at(-4.4f + 0.8f * float(index), 0.0f, -6.0f);
				World.Add<TransformComponent>(bar, TransformComponent{ .Position = at,
													   .Rotation = glm::angleAxis(glm::radians(angles[index]), glm::vec3(0.0f, 0.0f, 1.0f)),
													   .Scale = glm::vec3(widths[index % 4], 2.5f, 0.05f) });
				MeshRendererComponent meshRenderer;
				meshRenderer.MeshAsset = renderer.GetCubeMesh();
				meshRenderer.MaterialAsset = white;
				World.Add<MeshRendererComponent>(bar, meshRenderer);
				Bars.push_back(bar);
			}

			const Entity camera = World.CreateEntity();
			World.Add<TransformComponent>(camera, TransformComponent{});
			World.Add<CameraComponent>(camera, CameraComponent{});
			const Entity sun = World.CreateEntity();
			World.Add<TransformComponent>(sun, TransformComponent{});
			DirectionalLightComponent light;
			light.Intensity = 0.0f;
			light.bCastShadows = false;
			World.Add<DirectionalLightComponent>(sun, light);
		}
	};

	RgbaImage Draw(SceneRenderer& gpu, uint64_t view)
	{
		gpu.DrawFrames(2);
		RgbaImage image;
		Require(gpu.Renderer.ReadSceneViewPixels(view, image), "The scene view can't be read back");
		return image;
	}

	// How smooth the bars' edges are: of the pixels a bar covers at all, how many it covers in part
	// (darker than its fully covered pixels and brighter than the sky), and how many shades of grey
	// those are
	struct EdgeSmoothness
	{
		size_t Full = 0;
		size_t Partial = 0;
		std::set<int> Shades;

		double PartialShare() const
		{
			return Full + Partial > 0 ? double(Partial) / double(Full + Partial) : 0.0;
		}
	};

	EdgeSmoothness MeasureEdges(const RgbaImage& image)
	{
		int brightest = 0;
		for (size_t pixel = 0; pixel < static_cast<size_t>(image.Width) * image.Height; ++pixel)
		{
			brightest = std::max(brightest, int(image.Pixels[pixel * 4]));
		}
		Require(brightest > 100, "The bars aren't drawn");
		EdgeSmoothness edges;
		for (size_t pixel = 0; pixel < static_cast<size_t>(image.Width) * image.Height; ++pixel)
		{
			const int value = image.Pixels[pixel * 4];
			if (value <= 2)
			{
				continue;
			}
			if (value >= brightest - 2)
			{
				++edges.Full;
				continue;
			}
			++edges.Partial;
			edges.Shades.insert(value);
		}
		return edges;
	}

	// Every pixel of a few rows across the bars, picked: what is under it
	std::vector<std::optional<Entity>> PickRows(SceneRenderer& gpu, uint64_t view)
	{
		IEditorRenderer& renderer = gpu.Renderer;
		gpu.DrawFrames(2);
		std::vector<std::optional<Entity>> hits;
		for (const uint32_t y : { ViewHeight / 2, ViewHeight / 3 })
		{
			for (uint32_t x = 0; x < ViewWidth; x += 4)
			{
				renderer.RequestPick(view, x, y);
				gpu.DrawFrames(3);
				const IEditorRenderer::PickResult result = renderer.ConsumeLastPickResult(view);
				Require(result.bHasNewResult, "A pick brings no result");
				hits.push_back(result.HitEntity);
			}
		}
		return hits;
	}

	void TestThinBars(SceneRenderer& gpu)
	{
		BarField field(gpu.Renderer);
		IEditorRenderer& renderer = gpu.Renderer;
		renderer.SetWorld(&field.World);
		const uint64_t view = renderer.CreateSceneView();
		renderer.SetSceneViewCameraMode(view, EViewportCameraMode::ScenePrimaryCamera);
		renderer.SetSceneViewSize(view, ViewWidth, ViewHeight);
		renderer.SetSceneViewSky(view, ESceneViewSky::Black, 0.0f);
		gpu.DrawUntilShadersCompiled();

		std::optional<RgbaImage> single;
		std::optional<RgbaImage> four;
		std::optional<std::vector<std::optional<Entity>>> singlePicks;
		size_t fourShades = 0;
		for (const uint32_t samples : { 1u, 2u, 4u, 8u })
		{
			// The frame that changes the count makes the view's targets and the scene's pipelines again
			renderer.SetMsaaSamples(samples);
			const auto changeStart = std::chrono::steady_clock::now();
			gpu.DrawFrames(1);
			const double changeMilliseconds = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - changeStart).count();
			if (renderer.GetMsaaSamples() != samples)
			{
				std::cout << samples << " samples: the device can't, " << renderer.GetMsaaSamples() << " instead; skipped\n";
				continue;
			}
			std::cout << samples << " samples: the frame that changed to them took " << int(changeMilliseconds + 0.5) << " ms\n";
			const RgbaImage image = Draw(gpu, view);
			SaveIfAsked(image, std::to_string(samples) + "x");
			const EdgeSmoothness edges = MeasureEdges(image);
			std::cout << samples << " samples: " << edges.Full << " pixels covered whole, " << edges.Partial << " in part (" << int(edges.PartialShare() * 100.0 + 0.5) << " %), of "
					  << edges.Shades.size() << " shades\n";
			if (samples == 1)
			{
				Require(edges.Partial == 0, "With one sample a pixel, " + std::to_string(edges.Partial) + " pixels are between the sky and a bar");
				single = image;
			}
			else
			{
				// Every edge crosses pixels in part; the thinnest bars are nothing but edge
				Require(edges.PartialShare() > 0.25, std::to_string(samples) + " samples cover only " + std::to_string(edges.Partial) + " pixels in part");
				Require(edges.Shades.size() >= samples - 1, std::to_string(samples) + " samples give only " + std::to_string(edges.Shades.size()) + " shades between the sky and a bar");
			}
			if (samples == 4)
			{
				four = image;
				fourShades = edges.Shades.size();
			}
			if (samples == 8 && fourShades > 0)
			{
				Require(edges.Shades.size() > fourShades, "8 samples give no more shades than 4");
			}

			// Picking is drawn apart, one id a pixel: the same at every sample count
			renderer.SetSceneViewShowEditorOverlays(view, true);
			const std::vector<std::optional<Entity>> picks = PickRows(gpu, view);
			renderer.SetSceneViewShowEditorOverlays(view, false);
			size_t barsHit = 0;
			for (const std::optional<Entity>& hit : picks)
			{
				barsHit += hit ? 1 : 0;
				Require(!hit || std::find(field.Bars.begin(), field.Bars.end(), *hit) != field.Bars.end(), "A pick finds something that isn't a bar");
			}
			Require(barsHit > 4, "The picks find hardly any bar");
			if (!singlePicks)
			{
				singlePicks = picks;
			}
			Require(picks == *singlePicks, std::to_string(samples) + " samples pick otherwise than one");
		}

		// Changed back, the same pictures to the bit
		Require(single.has_value(), "Nothing was drawn with one sample");
		renderer.SetMsaaSamples(1);
		gpu.DrawFrames(1);
		Require(renderer.WasSceneViewRecreatedThisFrame(view) && renderer.GetMsaaSamples() == 1, "The view isn't made again for the new sample count");
		Require(Draw(gpu, view).Pixels == single->Pixels, "Back at one sample, the picture isn't as it was");
		if (four)
		{
			renderer.SetMsaaSamples(4);
			Require(Draw(gpu, view).Pixels == four->Pixels, "Back at 4 samples, the picture isn't as it was");

			// A view made while drawing with 4 draws with 4
			const uint64_t second = renderer.CreateSceneView();
			renderer.SetSceneViewCameraMode(second, EViewportCameraMode::ScenePrimaryCamera);
			renderer.SetSceneViewSize(second, ViewWidth, ViewHeight);
			renderer.SetSceneViewSky(second, ESceneViewSky::Black, 0.0f);
			Require(Draw(gpu, second).Pixels == four->Pixels, "A view made at 4 samples doesn't draw as the first does");
			renderer.DestroySceneView(second);
		}

		renderer.DestroySceneView(view);
		renderer.SetWorld(nullptr);
		gpu.DrawFrames(1);
	}

	// What a view's targets take, at 1920 x 1080, at each sample count
	void TestTargetMemory(SceneRenderer& gpu)
	{
		IEditorRenderer& renderer = gpu.Renderer;
		const uint64_t view = renderer.CreateSceneView();
		renderer.SetSceneViewSize(view, 1920, 1080);
		uint64_t single = 0;
		for (const uint32_t samples : { 1u, 2u, 4u, 8u })
		{
			renderer.SetMsaaSamples(samples);
			gpu.DrawFrames(2);
			if (renderer.GetMsaaSamples() != samples)
			{
				continue;
			}
			const uint64_t bytes = gpu.Renderer.GetSceneViewTargetMemory(view);
			std::cout << "A 1920 x 1080 view's targets at " << samples << " samples: " << bytes / (1024 * 1024) << " MiB\n";
			single = samples == 1 ? bytes : single;
			Require(samples == 1 || bytes > single, "More samples take no more memory");
		}
		renderer.DestroySceneView(view);
		gpu.DrawFrames(1);
	}
}

int main()
{
	Nyx::Logger::Get().Init();
	try
	{
		RegisterComponentTypes();
		TestChoosingTheSamples();
		SceneRenderer gpu("NyxMultisamplingGpuTests");
		TestThinBars(gpu);
		TestTargetMemory(gpu);
	}
	catch (const std::exception& error)
	{
		std::cerr << "NyxMultisamplingGpuTests: " << error.what() << "\n";
		return 1;
	}
	std::cout << "NyxMultisamplingGpuTests: all passed\n";
	return 0;
}
