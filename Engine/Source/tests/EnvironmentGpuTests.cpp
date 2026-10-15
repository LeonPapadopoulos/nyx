// VulkanEnvironment on a real GPU, from skies whose light is known: a uniform sky lights every
// direction with its own colour, diffuse and specular, at every roughness; a sky lit from above
// gives the spherical harmonics a projection on the CPU gives, light from above, and smoother
// specular mips as they get rougher; the BRDF table matches the same integral on the CPU.
// Needs a Vulkan device; its window stays hidden.
#include "Log.h"
#include "Paths.h"
#include "ShaderPrograms.h"
#include "VulkanBindless.h"
#include "VulkanContext.h"
#include "VulkanEnvironment.h"
#include "TestSupport.h"

#include <GLFW/glfw3.h>
#include <glm/glm.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace
{
	using namespace Nyx;

	constexpr float Pi = 3.14159265358979f;

	using Nyx::Tests::Require;

	std::string Text(const glm::vec3& value)
	{
		return std::to_string(value.x) + " " + std::to_string(value.y) + " " + std::to_string(value.z);
	}

	bool Near(const glm::vec3& actual, const glm::vec3& expected, float tolerance)
	{
		return glm::all(glm::lessThanEqual(glm::abs(actual - expected), glm::vec3(tolerance)));
	}

	// A hidden window, a device, the bindless set and the environment, for the whole program
	class Gpu
	{
	public:
		Gpu()
		{
			if (!glfwInit())
			{
				throw std::runtime_error("GLFW failed to start");
			}
			glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
			glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
			Window = glfwCreateWindow(64, 64, "NyxEnvironmentGpuTests", nullptr, nullptr);
			if (!Window)
			{
				throw std::runtime_error("No window for the Vulkan device");
			}
			Context.Initialize("NyxEnvironmentGpuTests", Window);
			Bindless.Initialize(Context);
			Environment.Initialize(Context, Bindless, Programs);
		}

		~Gpu()
		{
			Context.WaitIdle();
			Environment.Shutdown();
			Bindless.Shutdown();
			Context.Shutdown();
			glfwDestroyWindow(Window);
			glfwTerminate();
		}

		// A sky whose faces (+X, -X, +Y, -Y, +Z, -Z) are each of one colour (8-bit, linear)
		uint32_t MakeSky(const std::array<glm::vec3, 6>& faces, uint32_t size = 64)
		{
			std::vector<uint8_t> pixels;
			for (const glm::vec3& face : faces)
			{
				for (uint32_t texel = 0; texel < size * size; ++texel)
				{
					for (int channel = 0; channel < 3; ++channel)
					{
						pixels.push_back(static_cast<uint8_t>(std::lround(std::clamp(face[channel], 0.0f, 1.0f) * 255.0f)));
					}
					pixels.push_back(255);
				}
			}
			return Bindless.CreateCubeTexture(size, pixels, vk::Format::eR8G8B8A8Unorm);
		}

		GLFWwindow* Window = nullptr;
		Nyx::VulkanContext Context;
		VulkanBindless Bindless;
		ShaderPrograms Programs{ Nyx::Paths::GetCompiledShadersDir(), Nyx::Paths::GetShadersDir() };
		VulkanEnvironment Environment;
	};

	// nyx/environment.slang's environmentDiffuse(), on the CPU
	glm::vec3 Diffuse(const std::array<glm::vec3, 9>& c, const glm::vec3& n)
	{
		return c[0] * 0.282095f + c[1] * 0.488603f * n.y + c[2] * 0.488603f * n.z + c[3] * 0.488603f * n.x + c[4] * 1.092548f * n.x * n.y + c[5] * 1.092548f * n.y * n.z +
			c[6] * 0.315392f * (3.0f * n.z * n.z - 1.0f) + c[7] * 1.092548f * n.x * n.z + c[8] * 0.546274f * (n.x * n.x - n.y * n.y);
	}

	// Environment.slang's directionOf()
	glm::vec3 DirectionOf(uint32_t x, uint32_t y, uint32_t face, uint32_t size)
	{
		const float s = (static_cast<float>(x) + 0.5f) / static_cast<float>(size) * 2.0f - 1.0f;
		const float t = (static_cast<float>(y) + 0.5f) / static_cast<float>(size) * 2.0f - 1.0f;
		const std::array<glm::vec3, 6> directions = {
			glm::vec3(1.0f, -t, -s), glm::vec3(-1.0f, -t, s), glm::vec3(s, 1.0f, t), glm::vec3(s, -1.0f, -t), glm::vec3(s, -t, 1.0f), glm::vec3(-s, -t, -1.0f)
		};
		return glm::normalize(directions[face]);
	}

	// The coefficients of a sky of one colour per face, as VulkanEnvironment makes them: projected
	// at 32 texels per side, convolved with the cosine and divided by pi
	std::array<glm::vec3, 9> Project(const std::array<glm::vec3, 6>& faces)
	{
		constexpr uint32_t Size = VulkanEnvironment::SourceSize >> VulkanEnvironment::IrradianceLevel;
		constexpr std::array<float, 9> Convolution = { 1.0f, 2.0f / 3.0f, 2.0f / 3.0f, 2.0f / 3.0f, 0.25f, 0.25f, 0.25f, 0.25f, 0.25f };
		std::array<glm::vec3, 9> c{};
		for (uint32_t face = 0; face < 6; ++face)
		{
			for (uint32_t y = 0; y < Size; ++y)
			{
				for (uint32_t x = 0; x < Size; ++x)
				{
					const glm::vec3 d = DirectionOf(x, y, face, Size);
					const float s = (static_cast<float>(x) + 0.5f) / Size * 2.0f - 1.0f;
					const float t = (static_cast<float>(y) + 0.5f) / Size * 2.0f - 1.0f;
					const glm::vec3 radiance = faces[face] * (4.0f / (Size * Size * std::pow(1.0f + s * s + t * t, 1.5f)));
					const std::array<float, 9> basis = { 0.282095f, 0.488603f * d.y, 0.488603f * d.z, 0.488603f * d.x, 1.092548f * d.x * d.y, 1.092548f * d.y * d.z,
						0.315392f * (3.0f * d.z * d.z - 1.0f), 1.092548f * d.x * d.z, 0.546274f * (d.x * d.x - d.y * d.y) };
					for (uint32_t term = 0; term < 9; ++term)
					{
						c[term] += radiance * basis[term];
					}
				}
			}
		}
		for (uint32_t term = 0; term < 9; ++term)
		{
			c[term] *= Convolution[term];
		}
		return c;
	}

	// The middle texel of a face of one specular mip
	glm::vec3 FaceMiddle(const std::vector<glm::vec4>& level, uint32_t face, uint32_t size)
	{
		return glm::vec3(level[(static_cast<size_t>(face) * size + size / 2) * size + size / 2]);
	}

	void TestUniformSky(Gpu& gpu)
	{
		const glm::vec3 color = glm::vec3(64.0f, 128.0f, 255.0f) / 255.0f;
		const uint32_t sky = gpu.MakeSky({ color, color, color, color, color, color });
		gpu.Environment.Build(sky);

		// Diffuse: the sky's colour from every side, and nothing that depends on the side
		const std::array<glm::vec3, 9>& c = gpu.Environment.GetIrradiance(sky);
		for (const glm::vec3& n : { glm::vec3(1, 0, 0), glm::vec3(-1, 0, 0), glm::vec3(0, 1, 0), glm::vec3(0, -1, 0), glm::vec3(0, 0, 1), glm::normalize(glm::vec3(1, -2, 3)) })
		{
			const glm::vec3 diffuse = Diffuse(c, n);
			Require(Near(diffuse, color, 0.01f), "A uniform sky's diffuse light towards " + Text(n) + " is " + Text(diffuse) + ", not " + Text(color));
		}
		for (uint32_t term = 1; term < 9; ++term)
		{
			Require(Near(c[term], glm::vec3(0.0f), 0.005f), "A uniform sky has a direction: coefficient " + std::to_string(term) + " is " + Text(c[term]));
		}

		// Specular: the sky's colour at every roughness
		for (uint32_t level = 0; level < VulkanEnvironment::SpecularLevels; ++level)
		{
			const std::vector<glm::vec4> texels = gpu.Environment.ReadSpecularLevel(sky, level);
			for (size_t texel = 0; texel < texels.size(); texel += 7)
			{
				Require(Near(glm::vec3(texels[texel]), color, 0.01f), "A uniform sky's specular mip " + std::to_string(level) + " is " + Text(glm::vec3(texels[texel])) + " at texel " + std::to_string(texel));
			}
		}
	}

	void TestSkyLitFromAbove(Gpu& gpu)
	{
		const std::array<glm::vec3, 6> faces = { glm::vec3(0.0f), glm::vec3(0.0f), glm::vec3(1.0f), glm::vec3(0.0f), glm::vec3(0.0f), glm::vec3(0.0f) };
		const uint32_t sky = gpu.MakeSky(faces);
		gpu.Environment.Build(sky);

		const std::array<glm::vec3, 9>& c = gpu.Environment.GetIrradiance(sky);
		const std::array<glm::vec3, 9> expected = Project(faces);
		for (uint32_t term = 0; term < 9; ++term)
		{
			Require(Near(c[term], expected[term], 0.01f), "Coefficient " + std::to_string(term) + " is " + Text(c[term]) + ", the CPU's projection " + Text(expected[term]));
		}
		const glm::vec3 up = Diffuse(c, glm::vec3(0, 1, 0));
		const glm::vec3 side = Diffuse(c, glm::vec3(1, 0, 0));
		const glm::vec3 down = Diffuse(c, glm::vec3(0, -1, 0));
		Require(up.x > side.x && side.x > down.x && up.x > 0.3f, "The light from above doesn't come from above: up " + Text(up) + ", side " + Text(side) + ", down " + Text(down));

		// Mirror-like, the top is white and the bottom black; rough, the light spreads
		const std::vector<glm::vec4> sharp = gpu.Environment.ReadSpecularLevel(sky, 0);
		const uint32_t sharpSize = VulkanEnvironment::SpecularSize;
		Require(Near(FaceMiddle(sharp, 2, sharpSize), glm::vec3(1.0f), 0.01f) && Near(FaceMiddle(sharp, 3, sharpSize), glm::vec3(0.0f), 0.01f), "Mip 0 isn't the sky");
		Require(FaceMiddle(sharp, 0, sharpSize).x < 0.01f, "Mip 0 shows light at the side's middle");

		const uint32_t roughLevel = VulkanEnvironment::SpecularLevels - 1;
		const std::vector<glm::vec4> rough = gpu.Environment.ReadSpecularLevel(sky, roughLevel);
		const uint32_t roughSize = VulkanEnvironment::SpecularSize >> roughLevel;
		const float top = FaceMiddle(rough, 2, roughSize).x;
		const float sideMiddle = FaceMiddle(rough, 0, roughSize).x;
		const float bottom = FaceMiddle(rough, 3, roughSize).x;
		Require(top < 0.99f && top > sideMiddle && sideMiddle > 0.01f && sideMiddle > bottom, "The roughest mip doesn't spread the light: top " + std::to_string(top) + ", side " + std::to_string(sideMiddle) + ", bottom " + std::to_string(bottom));
	}

	// Environment.slang's brdfLookup(), on the CPU
	glm::vec2 IntegrateBrdf(float nDotV, float roughness, uint32_t sampleCount)
	{
		const float alpha = roughness * roughness;
		const glm::vec3 v(std::sqrt(1.0f - nDotV * nDotV), 0.0f, nDotV);
		glm::vec2 sum(0.0f);
		for (uint32_t index = 0; index < sampleCount; ++index)
		{
			uint32_t bits = index;
			bits = (bits << 16u) | (bits >> 16u);
			bits = ((bits & 0x55555555u) << 1u) | ((bits & 0xAAAAAAAAu) >> 1u);
			bits = ((bits & 0x33333333u) << 2u) | ((bits & 0xCCCCCCCCu) >> 2u);
			bits = ((bits & 0x0F0F0F0Fu) << 4u) | ((bits & 0xF0F0F0F0u) >> 4u);
			bits = ((bits & 0x00FF00FFu) << 8u) | ((bits & 0xFF00FF00u) >> 8u);
			const glm::vec2 xi(static_cast<float>(index) / sampleCount, static_cast<float>(bits) * 2.3283064365386963e-10f);

			const float phi = 2.0f * Pi * xi.x;
			const float cosTheta = std::min(std::sqrt((1.0f - xi.y) / (1.0f + (alpha * alpha - 1.0f) * xi.y)), 1.0f);
			const float sinTheta = std::sqrt(std::max(1.0f - cosTheta * cosTheta, 0.0f));
			// In the shader's tangent frame around n = (0, 0, 1): the same samples, not just the same
			// distribution
			const glm::vec3 tangent = glm::normalize(glm::cross(glm::vec3(1.0f, 0.0f, 0.0f), glm::vec3(0.0f, 0.0f, 1.0f)));
			const glm::vec3 bitangent = glm::cross(glm::vec3(0.0f, 0.0f, 1.0f), tangent);
			const glm::vec3 h = glm::normalize(tangent * (sinTheta * std::cos(phi)) + bitangent * (sinTheta * std::sin(phi)) + glm::vec3(0.0f, 0.0f, cosTheta));
			const glm::vec3 l = glm::normalize(2.0f * glm::dot(v, h) * h - v);
			const float nDotL = std::clamp(l.z, 0.0f, 1.0f);
			const float nDotH = std::clamp(h.z, 0.0f, 1.0f);
			const float vDotH = std::clamp(glm::dot(v, h), 0.0f, 1.0f);
			if (nDotL > 0.0f)
			{
				const float a2 = alpha * alpha;
				const float lambdaV = nDotL * std::sqrt(nDotV * nDotV * (1.0f - a2) + a2);
				const float lambdaL = nDotV * std::sqrt(nDotL * nDotL * (1.0f - a2) + a2);
				const float visibility = 0.5f / (lambdaV + lambdaL) * 4.0f * nDotL * vDotH / nDotH;
				const float fresnel = std::pow(1.0f - vDotH, 5.0f);
				sum += glm::vec2((1.0f - fresnel) * visibility, fresnel * visibility);
			}
		}
		return sum / static_cast<float>(sampleCount);
	}

	void TestBrdfLookup(Gpu& gpu)
	{
		const std::vector<glm::vec2> lookup = gpu.Environment.ReadLookup();
		constexpr uint32_t Size = VulkanEnvironment::LookupSize;
		Require(lookup.size() == Size * Size, "The BRDF table has " + std::to_string(lookup.size()) + " texels");
		// Away from grazing angles, where the integral divides by almost nothing and rounding alone
		// moves it
		for (const uint32_t row : { 0u, 20u, 64u, 127u })
		{
			for (const uint32_t column : { 12u, 31u, 100u, 127u })
			{
				const float nDotV = (column + 0.5f) / Size;
				const float roughness = (row + 0.5f) / Size;
				const glm::vec2 expected = IntegrateBrdf(nDotV, roughness, VulkanEnvironment::LookupSamples);
				const glm::vec2 actual = lookup[row * Size + column];
				Require(std::abs(actual.x - expected.x) < 0.01f && std::abs(actual.y - expected.y) < 0.01f,
					"The BRDF table at n.v " + std::to_string(nDotV) + ", roughness " + std::to_string(roughness) + " holds " + std::to_string(actual.x) + " " + std::to_string(actual.y) + ", the CPU " +
						std::to_string(expected.x) + " " + std::to_string(expected.y));
			}
		}
		// Smooth and seen head-on, all of F0 is reflected
		const glm::vec2 mirror = lookup[Size - 1];
		Require(mirror.x > 0.9f && mirror.y < 0.05f, "A smooth surface seen head-on doesn't reflect F0: " + std::to_string(mirror.x) + " " + std::to_string(mirror.y));
	}
}

int main()
{
	try
	{
		Nyx::Logger::Get().Init();
		Gpu gpu;
		TestUniformSky(gpu);
		TestSkyLitFromAbove(gpu);
		TestBrdfLookup(gpu);
	}
	catch (const std::exception& error)
	{
		std::cerr << "FAILED: " << error.what() << "\n";
		return 1;
	}

	std::cout << "All environment GPU tests passed\n";
	return 0;
}
