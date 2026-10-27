// The world's wind on a real GPU (Docs/Design/wind.md): nyx.wind's sampleWind(), sampleWindGust()
// and windHeightFactor() give Wind.cpp's numbers to the bit, for hand-made states and the
// component's, after hours of wind and far from the origin. A view's materials read its world's
// wind: none without one, the editor's own, steady at first, a game's as its drive has it, mid-turn;
// a game's local air on top, near it and not away from it, its texture copied again only when it
// changes. Grass (VertexColorWind) bends in the wind, and not at a Response of 0.
//
// NYX_SCENE_TEST_OUTPUT=<folder> saves what was rendered there.
#include "CameraComponent.h"
#include "CameraProjection.h"
#include "DrawnWorldTime.h"
#include "FixedStepClock.h"
#include "ImageFile.h"
#include "Log.h"
#include "MeshRendererComponent.h"
#include "ShaderCompiler.h"
#include "TransformComponent.h"
#include "VulkanContext.h"
#include "VulkanRenderer.h"
#include "VulkanUtil.h"
#include "Wind.h"
#include "WindComponent.h"
#include "WindField.h"
#include "RenderTestSupport.h"

#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <iostream>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace
{
	using namespace Nyx;
	using namespace Nyx::RenderTests;

	const std::filesystem::path TestShaders = NYX_TEST_SHADERS_DIR;
	constexpr uint32_t ViewWidth = 320;
	constexpr uint32_t ViewHeight = 180;

	// -------------------------------------------------------------------------
	// The contract on the GPU: one compute dispatch of WindProbe.slang
	// -------------------------------------------------------------------------

	class ComputeDevice
	{
	public:
		ComputeDevice()
		{
			if (!glfwInit())
			{
				throw std::runtime_error("GLFW failed to start");
			}
			glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
			glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
			Window = glfwCreateWindow(64, 64, "NyxWindGpuTests", nullptr, nullptr);
			if (!Window)
			{
				throw std::runtime_error("No window for the Vulkan device");
			}
			Context.Initialize("NyxWindGpuTests", Window);
		}

		~ComputeDevice()
		{
			Context.GetDevice().waitIdle();
			Context.Shutdown();
			glfwDestroyWindow(Window);
			glfwTerminate();
		}

		ComputeDevice(const ComputeDevice&) = delete;
		ComputeDevice& operator=(const ComputeDevice&) = delete;

		// Runs `entryPoint` over `groups` groups with `input` at set 0, binding 2, `probes` at
		// binding 4 and an output of `outputBytes` at binding 3; returns the output
		std::vector<uint8_t> Run(const CompiledEntryPoint& entryPoint, const std::vector<uint8_t>& input, const std::vector<uint8_t>& probes, size_t outputBytes,
			uint32_t groups)
		{
			vk::raii::Device& device = Context.GetDevice();
			vk::raii::Buffer inputBuffer{ nullptr };
			vk::raii::DeviceMemory inputMemory{ nullptr };
			vk::raii::Buffer probeBuffer{ nullptr };
			vk::raii::DeviceMemory probeMemory{ nullptr };
			vk::raii::Buffer outputBuffer{ nullptr };
			vk::raii::DeviceMemory outputMemory{ nullptr };
			const vk::MemoryPropertyFlags host = vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent;
			CreateBuffer(Context, input.size(), vk::BufferUsageFlagBits::eStorageBuffer, host, inputBuffer, inputMemory);
			CreateBuffer(Context, probes.size(), vk::BufferUsageFlagBits::eStorageBuffer, host, probeBuffer, probeMemory);
			CreateBuffer(Context, outputBytes, vk::BufferUsageFlagBits::eStorageBuffer, host, outputBuffer, outputMemory);
			std::memcpy(inputMemory.mapMemory(0, input.size()), input.data(), input.size());
			inputMemory.unmapMemory();
			std::memcpy(probeMemory.mapMemory(0, probes.size()), probes.data(), probes.size());
			probeMemory.unmapMemory();

			const std::array<vk::DescriptorSetLayoutBinding, 3> bindings{
				vk::DescriptorSetLayoutBinding(2, vk::DescriptorType::eStorageBuffer, 1, vk::ShaderStageFlagBits::eCompute),
				vk::DescriptorSetLayoutBinding(3, vk::DescriptorType::eStorageBuffer, 1, vk::ShaderStageFlagBits::eCompute),
				vk::DescriptorSetLayoutBinding(4, vk::DescriptorType::eStorageBuffer, 1, vk::ShaderStageFlagBits::eCompute),
			};
			vk::raii::DescriptorSetLayout setLayout(device, vk::DescriptorSetLayoutCreateInfo({}, bindings));
			vk::raii::PipelineLayout pipelineLayout(device, vk::PipelineLayoutCreateInfo({}, *setLayout));
			vk::raii::ShaderModule module(device, vk::ShaderModuleCreateInfo({}, entryPoint.Spirv.size() * sizeof(uint32_t), entryPoint.Spirv.data()));
			vk::ComputePipelineCreateInfo pipelineInfo{};
			pipelineInfo.stage = vk::PipelineShaderStageCreateInfo({}, vk::ShaderStageFlagBits::eCompute, *module, entryPoint.Name.c_str());
			pipelineInfo.layout = *pipelineLayout;
			vk::raii::Pipeline pipeline(device, nullptr, pipelineInfo);

			const vk::DescriptorPoolSize poolSize(vk::DescriptorType::eStorageBuffer, 3);
			vk::raii::DescriptorPool pool(device, vk::DescriptorPoolCreateInfo(vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet, 1, poolSize));
			vk::raii::DescriptorSet set = std::move(vk::raii::DescriptorSets(device, vk::DescriptorSetAllocateInfo(*pool, *setLayout)).front());
			const vk::DescriptorBufferInfo inputInfo(*inputBuffer, 0, VK_WHOLE_SIZE);
			const vk::DescriptorBufferInfo outputInfo(*outputBuffer, 0, VK_WHOLE_SIZE);
			const vk::DescriptorBufferInfo probeInfo(*probeBuffer, 0, VK_WHOLE_SIZE);
			const std::array<vk::WriteDescriptorSet, 3> writes{
				vk::WriteDescriptorSet(*set, 2, 0, vk::DescriptorType::eStorageBuffer, {}, inputInfo),
				vk::WriteDescriptorSet(*set, 3, 0, vk::DescriptorType::eStorageBuffer, {}, outputInfo),
				vk::WriteDescriptorSet(*set, 4, 0, vk::DescriptorType::eStorageBuffer, {}, probeInfo),
			};
			device.updateDescriptorSets(writes, {});

			vk::CommandBufferAllocateInfo allocInfo(*Context.GetGraphicsCommandPool(), vk::CommandBufferLevel::ePrimary, 1);
			vk::raii::CommandBuffer commands = std::move(vk::raii::CommandBuffers(device, allocInfo).front());
			commands.begin(vk::CommandBufferBeginInfo(vk::CommandBufferUsageFlagBits::eOneTimeSubmit));
			commands.bindPipeline(vk::PipelineBindPoint::eCompute, *pipeline);
			commands.bindDescriptorSets(vk::PipelineBindPoint::eCompute, *pipelineLayout, 0, *set, {});
			commands.dispatch(groups, 1, 1);
			const vk::MemoryBarrier toHost(vk::AccessFlagBits::eShaderWrite, vk::AccessFlagBits::eHostRead);
			commands.pipelineBarrier(vk::PipelineStageFlagBits::eComputeShader, vk::PipelineStageFlagBits::eHost, {}, toHost, {}, {});
			commands.end();
			vk::SubmitInfo submit{};
			submit.commandBufferCount = 1;
			submit.pCommandBuffers = &*commands;
			Context.GetGraphicsQueue().submit(submit);
			device.waitIdle();

			std::vector<uint8_t> output(outputBytes);
			std::memcpy(output.data(), outputMemory.mapMemory(0, outputBytes), outputBytes);
			outputMemory.unmapMemory();
			return output;
		}

	private:
		GLFWwindow* Window = nullptr;
		Nyx::VulkanContext Context;
	};

	std::string DescribeDiagnostics(const ShaderCompileResult& result)
	{
		std::string text;
		for (const ShaderDiagnostic& diagnostic : result.Diagnostics)
		{
			text += "\n  " + diagnostic.ToString();
		}
		return text;
	}

	struct Mismatch
	{
		size_t Count = 0;
		std::string First;
	};

	// The GPU's numbers for `wind` at each probe against Wind.cpp's, word for word
	Mismatch CompareWithTheCpu(ComputeDevice& gpu, const CompiledEntryPoint& entryPoint, const WindState& wind, const std::vector<glm::vec4>& probes)
	{
		std::vector<uint8_t> input(sizeof(WindState));
		std::memcpy(input.data(), &wind, sizeof(WindState));
		std::vector<uint8_t> probeBytes(probes.size() * sizeof(glm::vec4));
		std::memcpy(probeBytes.data(), probes.data(), probeBytes.size());
		const std::vector<uint8_t> output = gpu.Run(entryPoint, input, probeBytes, probes.size() * 32, static_cast<uint32_t>(probes.size() / 64));

		Mismatch mismatch;
		for (size_t index = 0; index < probes.size(); ++index)
		{
			const glm::vec3 point(probes[index]);
			const float time = probes[index].w;
			const glm::vec3 velocity = SampleWind(wind, point, time);
			const float cpu[5] = { velocity.x, velocity.y, velocity.z, SampleWindGust(wind, point, time), GetWindHeightFactor(wind, point.y) };
			float gpuValues[5];
			std::memcpy(gpuValues, output.data() + index * 32, 16);
			std::memcpy(gpuValues + 4, output.data() + index * 32 + 16, 4);
			if (std::memcmp(cpu, gpuValues, sizeof(cpu)) != 0)
			{
				if (mismatch.Count++ == 0)
				{
					for (int value = 0; value < 5; ++value)
					{
						uint32_t a = 0;
						uint32_t b = 0;
						std::memcpy(&a, &cpu[value], 4);
						std::memcpy(&b, &gpuValues[value], 4);
						if (a != b)
						{
							mismatch.First += "value " + std::to_string(value) + " CPU " + std::to_string(a) + " GPU " + std::to_string(b) + "; ";
						}
					}
					mismatch.First += "at (" + std::to_string(point.x) + ", " + std::to_string(point.y) + ", " + std::to_string(point.z) + ") t " + std::to_string(time) +
						": CPU " + std::to_string(cpu[0]) + " " + std::to_string(cpu[2]) + " gust " + std::to_string(cpu[3]) + " factor " + std::to_string(cpu[4]) +
						", GPU " + std::to_string(gpuValues[0]) + " " + std::to_string(gpuValues[2]) + " gust " + std::to_string(gpuValues[3]) + " factor " + std::to_string(gpuValues[4]);
				}
			}
		}
		return mismatch;
	}

	void TestTheGpuAgreesToTheBit()
	{
		ShaderCompiler compiler({ Nyx::Paths::GetShaderLibraryDir() });
		const ShaderCompileResult result = compiler.CompileProgram(TestShaders / "WindProbe.slang");
		Require(result.bSucceeded, "WindProbe.slang doesn't compile:" + DescribeDiagnostics(result));
		const CompiledEntryPoint* entryPoint = result.FindEntryPoint("probeWind");
		Require(entryPoint != nullptr, "WindProbe.slang has no probeWind");

		// Points near and far, low and high, at times round each state's own
		std::vector<glm::vec4> probes;
		uint32_t random = 12345u;
		const auto next = [&]()
		{
			random = random * 1664525u + 1013904223u;
			return static_cast<float>(random >> 8) / 16777216.0f;
		};
		for (int index = 0; index < 64 * 64; ++index)
		{
			const float reach = index < 2048 ? 60.0f : 5000.0f;
			probes.emplace_back((next() * 2.0f - 1.0f) * reach, next() * (index % 3 == 0 ? 3000.0f : 12.0f) - 1.0f, (next() * 2.0f - 1.0f) * reach, next() * 2.0f);
		}

		ComputeDevice gpu;
		const auto check = [&](const WindState& state, const std::string& what)
		{
			std::vector<glm::vec4> timed = probes;
			for (glm::vec4& probe : timed)
			{
				probe.w = state.Time + probe.w - 1.0f;
			}
			const Mismatch mismatch = CompareWithTheCpu(gpu, *entryPoint, state, timed);
			Require(mismatch.Count == 0, what + ": the GPU's wind differs from the CPU's at " + std::to_string(mismatch.Count) + " of " + std::to_string(timed.size()) +
				" points, first " + mismatch.First);
		};

		WindState handMade;
		handMade.Direction = glm::normalize(glm::vec2(0.3f, -0.9f));
		handMade.Speed = 7.0f;
		handMade.Gustiness = 0.8f;
		handMade.GustPhase = 17.25f;
		handMade.GustRate = 0.2f;
		handMade.Turbulence = 0.3f;
		handMade.EddyVelocity = handMade.Direction * 7.0f;
		handMade.PatternBlend = 0.35f;
		handMade.From = WindPattern{ .Direction = glm::normalize(glm::vec2(1.0f, 0.2f)), .GustsPerMetre = 1.0f / 50.0f, .GustPatchesPerMetre = 1.0f / 30.0f,
			.EddyOffset = glm::vec2(-310.0f, 44.5f), .EddiesPerMetre = 1.0f / 8.0f };
		handMade.To = WindPattern{ .Direction = handMade.Direction, .GustsPerMetre = 1.0f / 33.0f, .GustPatchesPerMetre = 1.0f / 21.0f,
			.EddyOffset = glm::vec2(31.0f, -12.0f), .EddiesPerMetre = 1.0f / 5.0f };
		handMade.GroundFactor = 0.4f;
		handMade.InverseReferenceHeight = 0.1f;
		handMade.Aloft = 1.5f;
		handMade.InverseAloftSpan = 1.0f / 990.0f;
		handMade.AloftStart = 10.0f / 990.0f;
		handMade.Time = 41.0f;
		check(handMade, "A hand-made wind");
		check(MakeSteadyWind(WindComponent{}, 3.5), "The component's default wind");
		check(MakeSteadyWind(WindComponent{}, 360000.0), "The default wind after 100 hours");
		WindComponent storm;
		storm.Speed = 22.0f;
		storm.Gustiness = 1.4f;
		storm.Turbulence = 0.6f;
		storm.DirectionDegrees = 200.0f;
		WindDrive drive;
		SetWindTarget(drive, WindComponent{}, 0.0);
		SetWindTarget(drive, storm, 5.0);
		check(EvaluateWind(drive, 8.3), "A wind turning to a storm");
		std::cout << "The GPU's wind agrees with the CPU's to the bit at " << probes.size() << " points of four winds\n";
	}

	// -------------------------------------------------------------------------
	// The view's wind as its materials read it
	// -------------------------------------------------------------------------

	// Unlit, seen in the base colour debug view: 0.5 + the air at At (from nyx::windAt()) / 20
	constexpr const char* ProbeShader = R"(import nyx.surface;
import nyx.worldwind;

struct NyxWindProbe : ISurface
{
    float3 At = float3(0.0, 0.0, 0.0);

    SurfaceOutput shade(SurfaceInput input)
    {
        SurfaceOutput o = SurfaceOutput::defaults(input);
        o.Unlit = true;
        o.BaseColor = saturate(0.5 + nyx::windAt(At, View.Time) / 20.0);
        return o;
    }
}
)";

	// A game's air: 6 m/s along +z within a box, nothing outside
	struct Gale final : IWindField
	{
		glm::vec3 Min{ -3.0f, -2.0f, -3.0f };
		glm::vec3 Max{ 3.0f, 2.0f, 3.0f };
		mutable int Asked = 0;

		void SampleWind(std::span<const glm::vec3> positions, float, std::span<glm::vec3> outVelocities) const override
		{
			for (size_t index = 0; index < positions.size(); ++index)
			{
				const bool bInside = glm::all(glm::greaterThanEqual(positions[index], Min)) && glm::all(glm::lessThanEqual(positions[index], Max));
				outVelocities[index] = bInside ? glm::vec3(0.0f, 0.0f, 6.0f) : glm::vec3(0.0f);
			}
		}

		void SampleWindGrid(const WindFieldGrid& grid, float time, std::span<glm::vec3> outVelocities) const override
		{
			++Asked;
			SampleWindGridAtCentres(*this, grid, time, outVelocities);
		}
	};

	TransformComponent At(const glm::vec3& position)
	{
		return TransformComponent{ .Position = position, .Rotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f), .Scale = glm::vec3(1.0f) };
	}

	glm::vec3 Decoded(const std::array<uint8_t, 3>& pixel)
	{
		glm::vec3 linear(0.0f);
		for (int channel = 0; channel < 3; ++channel)
		{
			const float srgb = static_cast<float>(pixel[channel]) / 255.0f;
			linear[channel] = srgb <= 0.04045f ? srgb / 12.92f : std::pow((srgb + 0.055f) / 1.055f, 2.4f);
		}
		return linear;
	}

	// A world seen by a camera 6 m in front of the origin, in a view of its own, drawn by a paused
	// clock at a known time, with a probe's cube at the origin
	struct WindScene
	{
		SceneRenderer& Gpu;
		Registry World;
		Entity Camera;
		Entity Cube;
		uint64_t View = 0;
		FixedStepClock Clock;

		WindScene(SceneRenderer& gpu, Material* material, ESceneViewDebug debug = ESceneViewDebug::BaseColor)
			: Gpu(gpu)
		{
			Camera = World.CreateEntity();
			World.Add<TransformComponent>(Camera, At(glm::vec3(0.0f, 0.5f, 4.0f)));
			World.Add<CameraComponent>(Camera, CameraComponent{});
			Cube = World.CreateEntity();
			World.Add<TransformComponent>(Cube, At(glm::vec3(0.0f)));
			MeshRendererComponent meshRenderer;
			meshRenderer.MeshAsset = gpu.Renderer.GetCubeMesh();
			meshRenderer.MaterialAsset = material;
			World.Add<MeshRendererComponent>(Cube, meshRenderer);

			Clock.SetStepSeconds(0.1);
			for (int frame = 0; frame < 25; ++frame)
			{
				Clock.Advance(0.1);
			}
			Clock.SetPaused(true);

			IEditorRenderer& renderer = gpu.Renderer;
			renderer.SetWorld(&World);
			renderer.SetWorldInterpolation(&World, &Clock);
			View = renderer.CreateSceneView();
			renderer.SetSceneViewCameraMode(View, EViewportCameraMode::ScenePrimaryCamera);
			renderer.SetSceneViewSize(View, ViewWidth, ViewHeight);
			renderer.SetSceneViewDebug(View, debug);
		}

		~WindScene()
		{
			IEditorRenderer& renderer = Gpu.Renderer;
			renderer.DestroySceneView(View);
			renderer.SetWorldInterpolation(&World, nullptr);
			renderer.SetWorldWindField(&World, nullptr);
			renderer.SetWorld(nullptr);
			Gpu.DrawFrames(1);
		}

		float Time() const
		{
			return GetDrawnWorldTime(Clock);
		}

		RgbaImage Draw(int frames = 2)
		{
			Gpu.DrawFrames(frames);
			RgbaImage image;
			Require(Gpu.Renderer.ReadSceneViewPixels(View, image), "The scene view can't be read back");
			return image;
		}

		// What the probe's cube shows: the wind at its point, as the GPU had it
		glm::vec3 ShownWind()
		{
			const RgbaImage image = Draw();
			const std::optional<glm::vec2> pixel =
				WorldToScreenPoint(World.Get<TransformComponent>(Camera), World.Get<CameraComponent>(Camera), glm::vec2(ViewWidth, ViewHeight), glm::vec3(0.0f, 0.0f, 0.5f));
			Require(pixel.has_value(), "The cube should be in front of the camera");
			const size_t offset = (static_cast<size_t>(pixel->y) * image.Width + static_cast<size_t>(pixel->x)) * 4;
			return (Decoded({ image.Pixels[offset], image.Pixels[offset + 1], image.Pixels[offset + 2] }) - 0.5f) * 20.0f;
		}
	};

	bool NearWind(const glm::vec3& a, const glm::vec3& b)
	{
		return glm::all(glm::lessThanEqual(glm::abs(a - b), glm::vec3(0.15f)));
	}

	std::string Describe(const glm::vec3& v)
	{
		return "(" + std::to_string(v.x) + ", " + std::to_string(v.y) + ", " + std::to_string(v.z) + ")";
	}

	struct Probes
	{
		LiveMaterialFile Shader{ "Shaders/NyxWindProbe.slang" };
		LiveMaterialFile Near{ "Materials/NyxWindProbeNear.nyxmat" };
		LiveMaterialFile Away{ "Materials/NyxWindProbeAway.nyxmat" };

		Probes()
		{
			Shader.Write(ProbeShader);
			Near.Write("Shader = Shaders/NyxWindProbe.slang\nAt = 1 0.5 -1\n");
			Away.Write("Shader = Shaders/NyxWindProbe.slang\nAt = 12 0.5 -9\n");
		}
	};

	void TestTheViewsWind(SceneRenderer& gpu)
	{
		const Probes probes;
		const glm::vec3 near(1.0f, 0.5f, -1.0f);
		const glm::vec3 away(12.0f, 0.5f, -9.0f);
		Material* nearProbe = gpu.Renderer.GetMaterial("Materials/NyxWindProbeNear.nyxmat");
		Material* awayProbe = gpu.Renderer.GetMaterial("Materials/NyxWindProbeAway.nyxmat");
		Require(nearProbe && awayProbe, "The wind probes don't load");

		// No wind: still
		{
			WindScene scene(gpu, nearProbe);
			gpu.DrawUntilShadersCompiled();
			Require(NearWind(scene.ShownWind(), glm::vec3(0.0f)), "A world without a wind blows on the GPU: " + Describe(scene.ShownWind()));
		}

		// The editor's own: the wind steady at its values from the first frame
		WindComponent breeze;
		breeze.Speed = 5.0f;
		breeze.DirectionDegrees = -70.0f;
		{
			WindScene scene(gpu, nearProbe);
			scene.World.Add<WindComponent>(scene.World.CreateEntity(), breeze);
			const glm::vec3 shown = scene.ShownWind();
			const glm::vec3 expected = SampleWind(MakeSteadyWind(breeze, scene.Time()), near, scene.Time());
			Require(NearWind(shown, expected) && glm::length(expected) > 1.0f, "The view's wind isn't its world's: " + Describe(shown) + " for " + Describe(expected));
		}

		// A game's: as its drive has it, turning to a storm
		{
			WindScene scene(gpu, nearProbe);
			const Entity owner = scene.World.CreateEntity();
			WindComponent storm = breeze;
			storm.Speed = 12.0f;
			storm.DirectionDegrees = 10.0f;
			scene.World.Add<WindComponent>(owner, storm);
			WindDriveComponent& driven = scene.World.Add<WindDriveComponent>(owner);
			SetWindTarget(driven.Drive, breeze, 0.0);
			SetWindTarget(driven.Drive, storm, 1.0);
			const glm::vec3 shown = scene.ShownWind();
			const WindState expectedState = EvaluateWind(driven.Drive, scene.Time());
			const glm::vec3 expected = SampleWind(expectedState, near, scene.Time());
			Require(IsWindChanging(driven.Drive, scene.Time()) && NearWind(shown, expected), "The view's wind isn't the game's as its drive has it: " + Describe(shown) + " for " + Describe(expected));
		}

		// A game's local air on top, near it, and not away from it; copied once while it stays
		{
			WindScene scene(gpu, nearProbe);
			scene.World.Add<WindComponent>(scene.World.CreateEntity(), breeze);
			Gale gale;
			gpu.Renderer.SetWorldWindField(&scene.World, &gale);
			const glm::vec3 shown = scene.ShownWind();
			const glm::vec3 prevailing = SampleWind(MakeSteadyWind(breeze, scene.Time()), near, scene.Time());
			Require(NearWind(shown, prevailing + glm::vec3(0.0f, 0.0f, 6.0f)), "A game's local air isn't added to the wind near it: " + Describe(shown) + " for " + Describe(prevailing));
			const SceneViewLocalWind* local = gpu.Renderer.GetSceneViewLocalWind(scene.View);
			Require(local && local->Slot != 0 && gale.Asked > 0, "The view has no texture of local air");
			const uint64_t uploads = local->UploadCount;
			scene.Draw(4);
			Require(local->UploadCount == uploads, "The texture of local air is copied again while it stays the same");
			gale.Min.x = -2.0f;
			scene.Draw(2);
			Require(local->UploadCount > uploads, "The texture of local air isn't copied again when it changes");
		}
		{
			WindScene scene(gpu, awayProbe);
			scene.World.Add<WindComponent>(scene.World.CreateEntity(), breeze);
			Gale gale;
			gpu.Renderer.SetWorldWindField(&scene.World, &gale);
			const glm::vec3 shown = scene.ShownWind();
			const glm::vec3 prevailing = SampleWind(MakeSteadyWind(breeze, scene.Time()), away, scene.Time());
			Require(NearWind(shown, prevailing), "A game's local air blows away from it: " + Describe(shown) + " for " + Describe(prevailing));
		}
	}

	// -------------------------------------------------------------------------
	// Grass bends in it
	// -------------------------------------------------------------------------

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

	void SaveIfAsked(const RgbaImage& image, const std::string& name)
	{
		const char* folder = std::getenv("NYX_SCENE_TEST_OUTPUT");
		if (folder == nullptr || *folder == '\0')
		{
			return;
		}
		const std::filesystem::path path = std::filesystem::path(folder) / ("Wind-" + name + ".png");
		std::string error;
		Require(SavePng(path, image, error), "Couldn't save " + path.string() + ": " + error);
		std::cout << "Saved " << path.string() << "\n";
	}

	void TestGrassBends(SceneRenderer& gpu)
	{
		LiveMaterialFile stiff("Materials/NyxWindStillGrass.nyxmat");
		stiff.Write("Parent = Materials/VertexColorWind.nyxmat\nResponse = 0\n");
		Material* grass = gpu.Renderer.GetMaterial("Materials/VertexColorWind.nyxmat");
		Material* still = gpu.Renderer.GetMaterial("Materials/NyxWindStillGrass.nyxmat");
		Require(grass && still, "The grass materials don't load");

		const auto draw = [&](Material* material, std::optional<WindComponent> wind)
		{
			WindScene scene(gpu, material);
			MeshRendererComponent& meshRenderer = scene.World.Get<MeshRendererComponent>(scene.Cube);
			meshRenderer.MeshAsset = gpu.Renderer.GetCrossedQuadsMesh();
			scene.World.Get<TransformComponent>(scene.Cube) = TransformComponent{ .Position = glm::vec3(0.0f, -0.5f, 0.0f),
				.Rotation = glm::quat(glm::radians(glm::vec3(0.0f, 30.0f, 0.0f))),
				.Scale = glm::vec3(2.0f) };
			if (wind)
			{
				scene.World.Add<WindComponent>(scene.World.CreateEntity(), *wind);
			}
			gpu.DrawUntilShadersCompiled();
			return scene.Draw();
		};

		WindComponent gale;
		gale.Speed = 14.0f;
		gale.DirectionDegrees = 0.0f;
		const RgbaImage calm = draw(grass, std::nullopt);
		const RgbaImage blown = draw(grass, gale);
		const RgbaImage held = draw(still, gale);
		SaveIfAsked(calm, "GrassCalm");
		SaveIfAsked(blown, "GrassBlown");
		const size_t bent = CountDifferent(calm, blown, 8);
		Require(bent > 200, "Grass doesn't bend in a gale: " + std::to_string(bent) + " pixels changed");
		Require(CountDifferent(calm, held, 0) == 0, "Grass of Response 0 bends in a gale");
	}
}

int main(int argc, char** argv)
{
	try
	{
		Nyx::Logger::Get().Init();
		TestTheGpuAgreesToTheBit();
		SceneRenderer gpu("NyxWindGpuTests", MsaaSamplesFromArguments(argc, argv));
		TestTheViewsWind(gpu);
		TestGrassBends(gpu);
	}
	catch (const std::exception& error)
	{
		std::cerr << "FAILED: " << error.what() << "\n";
		return 1;
	}

	std::cout << "All wind GPU tests passed\n";
	return 0;
}
