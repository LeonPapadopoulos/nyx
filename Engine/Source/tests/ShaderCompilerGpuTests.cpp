// ShaderCompiler on a real GPU: a material's layout as reflection reports it is the layout the GPU
// loads and stores, the model matrix in the push constants means what glm means, and the
// generated defaults writer gives a material's defaults, vectors included. Needs a Vulkan device;
// its window stays hidden.
#include "Paths.h"
#include "ShaderCompiler.h"
#include "VulkanContext.h"
#include "VulkanUtil.h"
#include "TestSupport.h"

#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace
{
	using namespace Nyx;

	const std::filesystem::path TestShaders = NYX_TEST_SHADERS_DIR;

	// Where a material sits in the material buffer: not the start, as for any material but the first
	constexpr uint32_t MaterialOffset = 64;
	constexpr uint32_t BufferSize = 1024;

	using Nyx::Tests::Require;

	std::string DescribeDiagnostics(const ShaderCompileResult& result)
	{
		std::string text;
		for (const ShaderDiagnostic& diagnostic : result.Diagnostics)
		{
			text += "\n  " + diagnostic.ToString();
		}
		return text;
	}

	ShaderCompiler& Compiler()
	{
		static ShaderCompiler compiler({ Nyx::Paths::GetShaderLibraryDir() });
		return compiler;
	}

	const CompiledEntryPoint& RequireEntryPoint(const ShaderCompileResult& result, const char* name)
	{
		Require(result.bSucceeded, std::string("no ") + name + ":" + DescribeDiagnostics(result));
		const CompiledEntryPoint* entryPoint = result.FindEntryPoint(name);
		Require(entryPoint != nullptr, std::string("no entry point ") + name);
		return *entryPoint;
	}

	// One scalar of a material, flattened: where it lives and what it is
	struct Leaf
	{
		std::string Path;
		uint32_t Offset = 0;
		EShaderScalarType Scalar = EShaderScalarType::None;
	};

	void Flatten(const std::vector<ShaderField>& fields, uint32_t base, const std::string& prefix, std::vector<Leaf>& leaves);

	void FlattenOne(const ShaderField& field, uint32_t base, const std::string& path, std::vector<Leaf>& leaves)
	{
		switch (field.Kind)
		{
		case EShaderFieldKind::Scalar:
			leaves.push_back({ path, base, field.Scalar });
			break;
		case EShaderFieldKind::Vector:
			for (uint32_t index = 0; index < field.Columns; ++index)
			{
				leaves.push_back({ path + "[" + std::to_string(index) + "]", base + index * 4, field.Scalar });
			}
			break;
		case EShaderFieldKind::Texture:
			leaves.push_back({ path + ".Index", base, EShaderScalarType::UInt });
			break;
		case EShaderFieldKind::Struct:
			Flatten(field.Fields, base, path + ".", leaves);
			break;
		default:
			throw std::runtime_error(path + " has a kind the test doesn't know");
		}
	}

	void Flatten(const std::vector<ShaderField>& fields, uint32_t base, const std::string& prefix, std::vector<Leaf>& leaves)
	{
		for (const ShaderField& field : fields)
		{
			if (field.ArrayCount == 0)
			{
				FlattenOne(field, base + field.Offset, prefix + field.Name, leaves);
				continue;
			}
			for (uint32_t element = 0; element < field.ArrayCount; ++element)
			{
				FlattenOne(field, base + field.Offset + element * field.ArrayStride, prefix + field.Name + "[" + std::to_string(element) + "]", leaves);
			}
		}
	}

	// -------------------------------------------------------------------------
	// The GPU: runs one compute entry point once
	// -------------------------------------------------------------------------

	// As nyx/surface.slang declares them
	struct ObjectConstants
	{
		glm::mat4 Model{ 1.0f };
		uint32_t MaterialOffset = 0;
		uint32_t EntityId = 0;
		uint32_t Padding[2]{};
		glm::vec4 CustomData{ 0.0f };
	};
	static_assert(sizeof(ObjectConstants) == 96);

	struct HostBuffer
	{
		vk::raii::Buffer Buffer{ nullptr };
		vk::raii::DeviceMemory Memory{ nullptr };
		vk::DeviceSize Size = 0;

		HostBuffer(Nyx::VulkanContext& context, vk::DeviceSize size)
			: Size(size)
		{
			Nyx::CreateBuffer(context, size, vk::BufferUsageFlagBits::eStorageBuffer,
				vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent, Buffer, Memory);
		}

		void Write(const std::vector<uint8_t>& bytes)
		{
			void* mapped = Memory.mapMemory(0, Size);
			std::memcpy(mapped, bytes.data(), std::min<size_t>(bytes.size(), Size));
			Memory.unmapMemory();
		}

		std::vector<uint32_t> ReadWords(size_t count)
		{
			std::vector<uint32_t> words(count);
			void* mapped = Memory.mapMemory(0, Size);
			std::memcpy(words.data(), mapped, count * sizeof(uint32_t));
			Memory.unmapMemory();
			return words;
		}
	};

	class GpuDevice
	{
	public:
		GpuDevice()
		{
			if (!glfwInit())
			{
				throw std::runtime_error("GLFW failed to start");
			}
			glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
			glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
			Window = glfwCreateWindow(64, 64, "NyxShaderCompilerGpuTests", nullptr, nullptr);
			if (!Window)
			{
				throw std::runtime_error("No window for the Vulkan device");
			}
			Context.Initialize("NyxShaderCompilerGpuTests", Window);
		}

		~GpuDevice()
		{
			Context.GetDevice().waitIdle();
			Context.Shutdown();
			glfwDestroyWindow(Window);
			glfwTerminate();
		}

		GpuDevice(const GpuDevice&) = delete;
		GpuDevice& operator=(const GpuDevice&) = delete;

		// Runs `entryPoint` once with `materials` at set 0, binding 2 (the material buffer) and an
		// output buffer at set 0, binding 3, filled with 0xCD first; returns the output's words
		std::vector<uint32_t> Run(const CompiledEntryPoint& entryPoint, const std::vector<uint8_t>& materials, const ObjectConstants& constants, size_t outputWords)
		{
			vk::raii::Device& device = Context.GetDevice();

			HostBuffer materialBuffer(Context, std::max<size_t>(materials.size(), 16));
			materialBuffer.Write(materials);
			HostBuffer output(Context, outputWords * sizeof(uint32_t));
			output.Write(std::vector<uint8_t>(outputWords * sizeof(uint32_t), 0xCD));

			const std::array<vk::DescriptorSetLayoutBinding, 2> bindings{
				vk::DescriptorSetLayoutBinding(2, vk::DescriptorType::eStorageBuffer, 1, vk::ShaderStageFlagBits::eCompute),
				vk::DescriptorSetLayoutBinding(3, vk::DescriptorType::eStorageBuffer, 1, vk::ShaderStageFlagBits::eCompute),
			};
			vk::raii::DescriptorSetLayout setLayout(device, vk::DescriptorSetLayoutCreateInfo({}, bindings));
			const vk::PushConstantRange pushRange(vk::ShaderStageFlagBits::eCompute, 0, sizeof(ObjectConstants));
			vk::raii::PipelineLayout pipelineLayout(device, vk::PipelineLayoutCreateInfo({}, *setLayout, pushRange));

			vk::raii::ShaderModule module(device, vk::ShaderModuleCreateInfo({}, entryPoint.Spirv.size() * sizeof(uint32_t), entryPoint.Spirv.data()));
			vk::ComputePipelineCreateInfo pipelineInfo{};
			pipelineInfo.stage = vk::PipelineShaderStageCreateInfo({}, vk::ShaderStageFlagBits::eCompute, *module, entryPoint.Name.c_str());
			pipelineInfo.layout = *pipelineLayout;
			vk::raii::Pipeline pipeline(device, nullptr, pipelineInfo);

			const vk::DescriptorPoolSize poolSize(vk::DescriptorType::eStorageBuffer, 2);
			vk::raii::DescriptorPool pool(device, vk::DescriptorPoolCreateInfo(vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet, 1, poolSize));
			vk::raii::DescriptorSet set = std::move(vk::raii::DescriptorSets(device, vk::DescriptorSetAllocateInfo(*pool, *setLayout)).front());
			const vk::DescriptorBufferInfo materialInfo(*materialBuffer.Buffer, 0, VK_WHOLE_SIZE);
			const vk::DescriptorBufferInfo outputInfo(*output.Buffer, 0, VK_WHOLE_SIZE);
			const std::array<vk::WriteDescriptorSet, 2> writes{
				vk::WriteDescriptorSet(*set, 2, 0, vk::DescriptorType::eStorageBuffer, {}, materialInfo),
				vk::WriteDescriptorSet(*set, 3, 0, vk::DescriptorType::eStorageBuffer, {}, outputInfo),
			};
			device.updateDescriptorSets(writes, {});

			vk::CommandBufferAllocateInfo allocInfo(*Context.GetGraphicsCommandPool(), vk::CommandBufferLevel::ePrimary, 1);
			vk::raii::CommandBuffer commands = std::move(vk::raii::CommandBuffers(device, allocInfo).front());
			commands.begin(vk::CommandBufferBeginInfo(vk::CommandBufferUsageFlagBits::eOneTimeSubmit));
			commands.bindPipeline(vk::PipelineBindPoint::eCompute, *pipeline);
			commands.bindDescriptorSets(vk::PipelineBindPoint::eCompute, *pipelineLayout, 0, *set, {});
			commands.pushConstants<ObjectConstants>(*pipelineLayout, vk::ShaderStageFlagBits::eCompute, 0, constants);
			commands.dispatch(1, 1, 1);
			const vk::MemoryBarrier toHost(vk::AccessFlagBits::eShaderWrite, vk::AccessFlagBits::eHostRead);
			commands.pipelineBarrier(vk::PipelineStageFlagBits::eComputeShader, vk::PipelineStageFlagBits::eHost, {}, toHost, {}, {});
			commands.end();

			vk::SubmitInfo submit{};
			submit.commandBufferCount = 1;
			submit.pCommandBuffers = &*commands;
			Context.GetGraphicsQueue().submit(submit);
			device.waitIdle();

			return output.ReadWords(outputWords);
		}

	private:
		GLFWwindow* Window = nullptr;
		Nyx::VulkanContext Context;
	};

	// A value no other scalar of the material has, of the scalar's own type
	uint32_t ValueFor(const Leaf& leaf, size_t index)
	{
		switch (leaf.Scalar)
		{
		case EShaderScalarType::Float:
		{
			const float value = 1000.0f + static_cast<float>(index);
			uint32_t word = 0;
			std::memcpy(&word, &value, 4);
			return word;
		}
		case EShaderScalarType::Int:
			return static_cast<uint32_t>(-static_cast<int32_t>(index) - 1);
		case EShaderScalarType::Bool:
			return 1;
		default:
			return 100u + static_cast<uint32_t>(index);
		}
	}

	float FloatOf(uint32_t word)
	{
		float value = 0.0f;
		std::memcpy(&value, &word, 4);
		return value;
	}

	// -------------------------------------------------------------------------
	// Tests
	// -------------------------------------------------------------------------

	// LayoutProbe puts every vector and the texture where scalar layout puts them and std430
	// wouldn't. probeLayout loads it like surfaceFragment does and writes its scalars out in order.
	void TestLayoutIsWhatTheGpuLoads(GpuDevice& gpu)
	{
		const ShaderCompileResult material = Compiler().CompileMaterial(TestShaders / "LayoutProbe.slang", ShaderDomain::Surface());
		Require(material.bSucceeded && material.MaterialLayout, "LayoutProbe doesn't compile as a material:" + DescribeDiagnostics(material));
		const ShaderCompileResult probe = Compiler().CompileProgram(TestShaders / "LayoutProbeShaders.slang");
		const CompiledEntryPoint& probeLayout = RequireEntryPoint(probe, "probeLayout");

		std::vector<Leaf> leaves;
		Flatten(material.MaterialLayout->Fields, 0, "", leaves);
		Require(leaves.size() == 22, "LayoutProbe has " + std::to_string(leaves.size()) + " scalars, expected 22");

		ObjectConstants constants;
		constants.MaterialOffset = MaterialOffset;
		constants.Model = glm::translate(glm::mat4(1.0f), glm::vec3(10.0f, 20.0f, 30.0f)) * glm::rotate(glm::mat4(1.0f), glm::radians(90.0f), glm::vec3(0.0f, 0.0f, 1.0f));

		// Where the GPU reads each scalar: every word of the buffer holds its own offset from the
		// material's start. Bools only read back as 0 or 1, so they are checked below.
		std::vector<uint8_t> marked(BufferSize, 0);
		for (uint32_t offset = MaterialOffset; offset + 4 <= BufferSize; offset += 4)
		{
			const uint32_t word = 0xA0000000u | (offset - MaterialOffset);
			std::memcpy(&marked[offset], &word, 4);
		}
		const std::vector<uint32_t> found = gpu.Run(probeLayout, marked, constants, leaves.size() + 4);
		for (size_t index = 0; index < leaves.size(); ++index)
		{
			if (leaves[index].Scalar != EShaderScalarType::Bool)
			{
				const uint32_t gpuOffset = found[index] & 0xFFFFF;
				Require(gpuOffset == leaves[index].Offset,
					leaves[index].Path + ": reflection says offset " + std::to_string(leaves[index].Offset) + ", the GPU reads " + std::to_string(gpuOffset));
			}
		}

		// The push constants' model matrix, as surfaceVertex uses it, means what glm means
		const glm::vec4 expectedMoved = constants.Model * glm::vec4(1.0f, 2.0f, 3.0f, 1.0f);
		glm::vec4 moved;
		std::memcpy(&moved, &found[leaves.size()], sizeof(moved));
		Require(glm::all(glm::lessThan(glm::abs(moved - expectedMoved), glm::vec4(1e-4f))),
			"mul(Object.Model, v) gives (" + std::to_string(moved.x) + ", " + std::to_string(moved.y) + ", " + std::to_string(moved.z) + "), glm gives (" +
				std::to_string(expectedMoved.x) + ", " + std::to_string(expectedMoved.y) + ", " + std::to_string(expectedMoved.z) + ")");

		// Real values at the reflected offsets, bools included, come back exactly
		std::vector<uint8_t> bytes(BufferSize, 0);
		std::vector<uint32_t> expected;
		for (size_t index = 0; index < leaves.size(); ++index)
		{
			const uint32_t word = ValueFor(leaves[index], index);
			std::memcpy(&bytes[MaterialOffset + leaves[index].Offset], &word, 4);
			expected.push_back(word);
		}
		const std::vector<uint32_t> loaded = gpu.Run(probeLayout, bytes, constants, leaves.size() + 4);
		for (size_t index = 0; index < leaves.size(); ++index)
		{
			Require(loaded[index] == expected[index], leaves[index].Path + " doesn't read back as written");
		}

		// Load, then Store the whole struct: every value lands at its reflected offset again
		const CompiledEntryPoint& probeStore = RequireEntryPoint(probe, "probeStore");
		const std::vector<uint32_t> stored = gpu.Run(probeStore, bytes, constants, (material.MaterialLayout->Size + 3) / 4);
		for (size_t index = 0; index < leaves.size(); ++index)
		{
			Require(stored[leaves[index].Offset / 4] == expected[index], leaves[index].Path + " isn't stored where reflection says");
		}

		// Informational: Slang's own DescriptorHandle at offset 4, the reason nyx::Texture holds a
		// plain index. Says so when a Slang update fixes it.
		const CompiledEntryPoint& probeRawHandle = RequireEntryPoint(probe, "probeRawHandle");
		std::vector<uint8_t> rawBytes(BufferSize, 0);
		const float a = 1.5f;
		const uint32_t handle[2] = { 77, 0 };
		std::memcpy(&rawBytes[MaterialOffset], &a, 4);
		std::memcpy(&rawBytes[MaterialOffset + 4], handle, 8);
		const std::vector<uint32_t> raw = gpu.Run(probeRawHandle, rawBytes, constants, 3);
		std::cout << "DescriptorHandle at offset 4 reads back " << raw[1] << ", written 77: "
				  << (raw[1] == 77 ? "Slang has fixed this; nyx::Texture could hold a DescriptorHandle again\n" : "still read from the wrong offset, as expected\n");
	}

	// The generated writer stores a material made with its defaults: a new material's bytes
	void TestDefaultsComeFromTheShader(GpuDevice& gpu)
	{
		const ShaderCompileResult material = Compiler().CompileMaterial(TestShaders / "Stone.slang", ShaderDomain::Surface());
		Require(material.bSucceeded && material.MaterialLayout, "Stone doesn't compile:" + DescribeDiagnostics(material));
		const ShaderLayout& layout = *material.MaterialLayout;
		const ShaderCompileResult writer = Compiler().CompileDefaultsWriter(TestShaders / "Stone.slang");
		const std::vector<uint32_t> words = gpu.Run(RequireEntryPoint(writer, "writeDefaults"), {}, ObjectConstants{}, (layout.Size + 3) / 4);

		const auto at = [&](const char* field, uint32_t component)
		{
			return words[(layout.FindField(field)->Offset + component * 4) / 4];
		};
		const auto near = [](float actual, float expected)
		{
			return std::abs(actual - expected) < 1e-6f;
		};
		Require(near(FloatOf(at("BaseColor", 0)), 0.62f) && near(FloatOf(at("BaseColor", 1)), 0.60f) && near(FloatOf(at("BaseColor", 2)), 0.56f),
			"BaseColor's default isn't (0.62, 0.60, 0.56)");
		Require(near(FloatOf(at("Roughness", 0)), 0.78f), "Roughness's default isn't 0.78");
		Require(near(FloatOf(at("Wetness", 0)), 0.3f), "Wetness's default isn't 0.3");
		Require(at("Albedo", 0) == 0, "an unset texture isn't slot 0");
		Require(near(FloatOf(at("Tiling", 0)), 1.0f) && near(FloatOf(at("Tiling", 1)), 1.0f), "Tiling's default isn't (1, 1)");
		Require(static_cast<int32_t>(at("Mode", 0)) == 2, "Mode's default isn't 2");

		// A field without a default starts at 0; the output was filled with 0xCD before
		const ShaderCompileResult noDefault = Compiler().CompileDefaultsWriter(TestShaders / "NoDefault.slang");
		const std::vector<uint32_t> noDefaultWords = gpu.Run(RequireEntryPoint(noDefault, "writeDefaults"), {}, ObjectConstants{}, 2);
		Require(FloatOf(noDefaultWords[0]) == 0.0f, "Metallic, which has no default, isn't 0");
		Require(FloatOf(noDefaultWords[1]) == 0.5f, "Roughness's default isn't 0.5");
	}
}

int main()
{
	try
	{
		Require(Compiler().IsAvailable(), "the compiler isn't available: " + Compiler().GetProblem());
		GpuDevice gpu;
		TestLayoutIsWhatTheGpuLoads(gpu);
		TestDefaultsComeFromTheShader(gpu);
	}
	catch (const std::exception& error)
	{
		std::cerr << "FAILED: " << error.what() << "\n";
		return 1;
	}

	std::cout << "All shader compiler GPU tests passed\n";
	return 0;
}
