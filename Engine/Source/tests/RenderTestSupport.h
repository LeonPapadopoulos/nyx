#pragma once

// What the GPU tests that draw scenes share (NyxSceneGpuTests, NyxMaterialGpuTests): a renderer
// in a hidden window, the tonemap and the Standard material's light computed on the CPU, the
// sky's light as VulkanEnvironment made it, files of a test's own in the assets folder.

#include "ImageFile.h"
#include "MeshRendererComponent.h"
#include "Paths.h"
#include "VulkanRenderer.h"
#include "TestSupport.h"

#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <imgui.h>

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
#include <vector>

namespace Nyx::RenderTests
{
	using Nyx::RgbaImage;

	using Nyx::Tests::Require;

	// The samples a pixel the test's renderer draws with (SceneRenderer). The reference images are
	// made with one; with more, RequireLooksLike() matches them away from edges.
	inline uint32_t TestMsaaSamples = 1;

	// "--msaa <samples>" among a test's arguments, else 1: the GPU tests run again with more
	// samples a pixel that way (ctest's ...Msaa tests)
	inline uint32_t MsaaSamplesFromArguments(int argc, char** argv)
	{
		for (int index = 1; index + 1 < argc; ++index)
		{
			if (std::string(argv[index]) == "--msaa")
			{
				return static_cast<uint32_t>(std::max(1, std::atoi(argv[index + 1])));
			}
		}
		return 1;
	}

	// A hidden window, and a renderer drawing into it, with `samples` samples a pixel: one unless a
	// test asks for more, so that the reference images stay as they were made
	class SceneRenderer
	{
	public:
		explicit SceneRenderer(const char* name, uint32_t samples = 1)
		{
			TestMsaaSamples = samples;
			Renderer.SetMsaaSamples(samples);
			if (!glfwInit())
			{
				throw std::runtime_error("GLFW failed to start");
			}
			glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
			glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
			Window = glfwCreateWindow(640, 360, name, nullptr, nullptr);
			if (!Window)
			{
				throw std::runtime_error("No window for the Vulkan device");
			}
			Renderer.Initialize(name, Window);
			// No imgui.ini wherever the test runs
			ImGui::GetIO().IniFilename = nullptr;
			if (samples > 1)
			{
				std::cout << name << ": " << Renderer.GetMsaaSamples() << " samples a pixel\n";
			}
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

	// The pixel in the middle of the view, after drawing a frame
	inline std::array<uint8_t, 3> MiddlePixel(SceneRenderer& gpu, uint64_t view)
	{
		gpu.DrawFrames(1);
		RgbaImage image;
		Require(gpu.Renderer.ReadSceneViewPixels(view, image), "The scene view can't be read back");
		const size_t offset = (static_cast<size_t>(image.Height / 2) * image.Width + image.Width / 2) * 4;
		return { image.Pixels[offset], image.Pixels[offset + 1], image.Pixels[offset + 2] };
	}

	// What the tonemap (Tonemap.slang) makes of linear light: Khronos PBR Neutral, then sRGB
	inline std::array<uint8_t, 3> Tonemapped(glm::vec3 color)
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

	inline bool Near(const std::array<uint8_t, 3>& actual, const std::array<uint8_t, 3>& expected, int tolerance = 2)
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

	inline std::string Text(const std::array<uint8_t, 3>& rgb)
	{
		return std::to_string(rgb[0]) + " " + std::to_string(rgb[1]) + " " + std::to_string(rgb[2]);
	}

	// The pixels of `image` near an edge: those within `reach` pixels of one whose neighbour, across
	// or down, differs from it by more than `step` in a channel
	inline std::vector<bool> FindEdges(const RgbaImage& image, int step, int reach)
	{
		const int width = static_cast<int>(image.Width);
		const int height = static_cast<int>(image.Height);
		const auto differs = [&](int x0, int y0, int x1, int y1)
		{
			const size_t a = (static_cast<size_t>(y0) * width + x0) * 4;
			const size_t b = (static_cast<size_t>(y1) * width + x1) * 4;
			for (size_t channel = 0; channel < 3; ++channel)
			{
				if (std::abs(int(image.Pixels[a + channel]) - int(image.Pixels[b + channel])) > step)
				{
					return true;
				}
			}
			return false;
		};
		std::vector<bool> edges(static_cast<size_t>(width) * height, false);
		for (int y = 0; y < height; ++y)
		{
			for (int x = 0; x < width; ++x)
			{
				if (!((x + 1 < width && differs(x, y, x + 1, y)) || (y + 1 < height && differs(x, y, x, y + 1))))
				{
					continue;
				}
				for (int dy = -reach; dy <= reach + 1; ++dy)
				{
					for (int dx = -reach; dx <= reach + 1; ++dx)
					{
						if (x + dx >= 0 && x + dx < width && y + dy >= 0 && y + dy < height)
						{
							edges[static_cast<size_t>(y + dy) * width + x + dx] = true;
						}
					}
				}
			}
		}
		return edges;
	}

	// Rendering on the same GPU and driver gives the same picture; another GPU or driver may round
	// a little differently, and may differ in a few pixels along edges: at most one in a thousand
	// may differ by more than 3 in a channel. With more than one sample a pixel (TestMsaaSamples),
	// edges are smoothed where the reference has steps: the pixels near the reference's edges are
	// left out, as far as the occlusion's blur reaches from them, and the rest must match alike.
	inline void RequireLooksLike(const RgbaImage& image, const std::filesystem::path& referencePath)
	{
		RgbaImage reference;
		std::string error;
		Require(LoadRgbaImage(referencePath, reference, error),
			"No reference image " + referencePath.string() + " (" + error + "); run with NYX_SCENE_TEST_OUTPUT set to make one");
		Require(reference.Width == image.Width && reference.Height == image.Height, "The reference image has another size");

		const bool bMultisampled = TestMsaaSamples > 1;
		const std::vector<bool> edges = bMultisampled ? FindEdges(reference, 24, 4) : std::vector<bool>();
		size_t edgePixels = 0;
		constexpr int ChannelTolerance = 3;
		size_t differentPixels = 0;
		size_t changedPixels = 0;
		int largestDifference = 0;
		for (size_t pixel = 0; pixel < static_cast<size_t>(image.Width) * image.Height; ++pixel)
		{
			if (bMultisampled && edges[pixel])
			{
				++edgePixels;
				continue;
			}
			int difference = 0;
			for (size_t channel = 0; channel < 3; ++channel)
			{
				difference = std::max(difference, std::abs(int(image.Pixels[pixel * 4 + channel]) - int(reference.Pixels[pixel * 4 + channel])));
			}
			largestDifference = std::max(largestDifference, difference);
			differentPixels += difference > ChannelTolerance ? 1 : 0;
			changedPixels += difference > 0 ? 1 : 0;
		}
		std::cout << referencePath.filename().string() << ": " << changedPixels << " pixels differ, by up to " << largestDifference;
		if (bMultisampled)
		{
			std::cout << " (" << TestMsaaSamples << " samples a pixel: " << edgePixels << " pixels near edges left out)";
			Require(edgePixels * 4 < static_cast<size_t>(image.Width) * image.Height * 3, "Three quarters of the picture are near edges: too little is left to compare");
		}
		std::cout << "\n";

		const size_t allowed = static_cast<size_t>(image.Width) * image.Height / 1000;
		Require(differentPixels <= allowed,
			"The picture differs from " + referencePath.filename().string() + " in " + std::to_string(differentPixels) + " pixels (at most " + std::to_string(allowed) +
				" may), by up to " + std::to_string(largestDifference) + "; NYX_SCENE_TEST_OUTPUT saves it");
	}

	// A file of the test's own in the assets folder, removed afterwards
	struct LiveMaterialFile
	{
		std::string Path;
		std::filesystem::path File;

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

	// What VulkanEnvironment made, sampled as nyx.environment samples it at a cube's +Z middle
	struct EnvironmentSamples
	{
		std::array<glm::vec3, 9> Irradiance{};
		std::vector<std::vector<glm::vec4>> Specular;
		std::vector<glm::vec2> Lookup;

		// The light of the sky in `skySlot`, e.g. the renderer's (VulkanRenderer::GetSky())
		EnvironmentSamples(Nyx::VulkanEnvironment& environment, uint32_t skySlot)
		{
			Irradiance = environment.GetIrradiance(skySlot);
			for (uint32_t level = 0; level < Nyx::VulkanEnvironment::SpecularLevels; ++level)
			{
				Specular.push_back(environment.ReadSpecularLevel(skySlot, level));
			}
			Lookup = environment.ReadLookup();
		}

		// environmentDiffuse((0, 0, 1))
		glm::vec3 DiffuseFacingZ() const
		{
			return Irradiance[0] * 0.282095f + Irradiance[2] * 0.488603f + Irradiance[6] * 0.315392f * 2.0f;
		}

		// environmentSpecular((0, 0, 1), roughness): between two mips, each sampled at the middle
		// of its +Z face, between four texels
		glm::vec3 SpecularFacingZ(float roughness) const
		{
			const float level = roughness * static_cast<float>(Nyx::VulkanEnvironment::SpecularLevels - 1);
			const uint32_t lower = static_cast<uint32_t>(std::floor(level));
			const uint32_t upper = std::min(lower + 1, Nyx::VulkanEnvironment::SpecularLevels - 1);
			return glm::mix(MiddleOfZ(lower), MiddleOfZ(upper), level - static_cast<float>(lower));
		}

		// environmentBrdf(1, roughness): the table's last column, between two rows
		glm::vec2 BrdfHeadOn(float roughness) const
		{
			constexpr uint32_t Size = Nyx::VulkanEnvironment::LookupSize;
			// Clamped at the edges, as the sampler clamps
			const float row = std::clamp(roughness * Size - 0.5f, 0.0f, static_cast<float>(Size - 1));
			const uint32_t lower = static_cast<uint32_t>(std::floor(row));
			const uint32_t upper = std::min(lower + 1, Size - 1);
			return glm::mix(Lookup[lower * Size + Size - 1], Lookup[upper * Size + Size - 1], row - static_cast<float>(lower));
		}

	private:
		glm::vec3 MiddleOfZ(uint32_t level) const
		{
			const uint32_t size = Nyx::VulkanEnvironment::SpecularSize >> level;
			glm::vec3 sum(0.0f);
			for (const uint32_t y : { size / 2 - 1, size / 2 })
			{
				for (const uint32_t x : { size / 2 - 1, size / 2 })
				{
					sum += glm::vec3(Specular[level][(static_cast<size_t>(4) * size + y) * size + x]) * 0.25f;
				}
			}
			return sum;
		}
	};

	// nyx::lightSurface() for n = v = (0, 0, 1), under the view's default sun (no light in the world)
	inline glm::vec3 LightFacingZ(const EnvironmentSamples& environment, glm::vec3 baseColor, float metallic, float roughness)
	{
		constexpr float Pi = 3.14159265358979f;
		const glm::vec3 n(0.0f, 0.0f, 1.0f);
		const glm::vec3 v = n;
		const glm::vec3 l = glm::normalize(glm::vec3(0.4f, 1.0f, 0.2f));
		const glm::vec3 sunColor(1.0f, 0.98f, 0.95f);

		const float perceptual = std::clamp(roughness, 0.045f, 1.0f);
		const float alpha = perceptual * perceptual;
		const glm::vec3 f0 = glm::mix(glm::vec3(0.04f), baseColor, metallic);
		const glm::vec3 diffuseColor = baseColor * (1.0f - metallic);

		const glm::vec3 h = glm::normalize(v + l);
		const float nDotV = 1.0f;
		const float nDotL = std::clamp(glm::dot(n, l), 0.0f, 1.0f);
		const float nDotH = std::clamp(glm::dot(n, h), 0.0f, 1.0f);
		const float lDotH = std::clamp(glm::dot(l, h), 0.0f, 1.0f);
		const float a2 = alpha * alpha;
		const float d = nDotH * nDotH * (a2 - 1.0f) + 1.0f;
		const float distribution = a2 / (Pi * d * d);
		const float lambdaV = nDotL * std::sqrt(nDotV * nDotV * (1.0f - a2) + a2);
		const float lambdaL = nDotV * std::sqrt(nDotL * nDotL * (1.0f - a2) + a2);
		const float visibility = 0.5f / (lambdaV + lambdaL);
		const glm::vec3 fresnel = f0 + (1.0f - f0) * std::pow(1.0f - lDotH, 5.0f);
		const glm::vec3 sun = (diffuseColor + Pi * distribution * visibility * fresnel) * sunColor * nDotL;

		const glm::vec2 brdf = environment.BrdfHeadOn(perceptual);
		const glm::vec3 specularShare = f0 * brdf.x + brdf.y;
		const glm::vec3 skySpecular = environment.SpecularFacingZ(perceptual) * specularShare;
		const glm::vec3 skyDiffuse = diffuseColor * environment.DiffuseFacingZ() * (1.0f - specularShare);
		return sun + skySpecular + skyDiffuse;
	}
}
