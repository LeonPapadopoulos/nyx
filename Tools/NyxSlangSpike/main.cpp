// NyxSlangSpike: stage 1, change 1 of Design/shader-materials.md. Answers, with Slang 2026.1 from
// the Vulkan SDK, whether the authoring model holds up before anything is built on it:
//
//   1. A generic entry point of the engine, specialised with a material's struct, compiles to
//      valid Vulkan SPIR-V.
//   2. The material struct is loaded from a byte buffer at an offset, with texture fields as
//      bindless handles, and its layout as reflection reports it is the layout the GPU reads.
//   3. Reflection returns the fields' attributes, and the material's default values can be had.
//   4. Diagnostics carry file, line and column.
//   5. A module lists the files it depends on.
//   6. How long compiling takes: the global session, a first compile, and compiling again.
//
// The report's sections are numbered in the order they run, not in this list's order.
//
// Prints a report and exits with 1 if a check fails. Needs a Vulkan device for check 2.
#include "VulkanContext.h"

#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <slang/slang-com-ptr.h>
#include <slang/slang-tag-version.h>
#include <slang/slang.h>
#include <spirv-tools/libspirv.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <regex>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace
{
	using Slang::ComPtr;
	using Clock = std::chrono::steady_clock;

	// -------------------------------------------------------------------------
	// Report
	// -------------------------------------------------------------------------

	int FailedChecks = 0;

	void Check(bool condition, const std::string& what, const std::string& detail = {})
	{
		std::cout << (condition ? "  [ok]   " : "  [FAIL] ") << what;
		if (!detail.empty())
		{
			std::cout << ": " << detail;
		}
		std::cout << "\n";
		if (!condition)
		{
			++FailedChecks;
		}
	}

	void Section(const std::string& title)
	{
		std::cout << "\n"
				  << title << "\n";
	}

	double MillisecondsSince(Clock::time_point start)
	{
		return std::chrono::duration<double, std::milli>(Clock::now() - start).count();
	}

	std::string FormatMilliseconds(double milliseconds)
	{
		std::ostringstream text;
		text.setf(std::ios::fixed);
		text.precision(1);
		text << milliseconds << " ms";
		return text.str();
	}

	std::string BlobText(slang::IBlob* blob)
	{
		if (!blob)
		{
			return {};
		}
		return std::string(static_cast<const char*>(blob->getBufferPointer()), blob->getBufferSize());
	}

	// -------------------------------------------------------------------------
	// Diagnostics: Slang reports text only. "file(line): severity code: message", then the source
	// line and a line with a caret under the column.
	// -------------------------------------------------------------------------

	struct Diagnostic
	{
		std::string File;
		int Line = 0;
		int Column = 0; // 0 when no caret line followed
		std::string Severity;
		int Code = 0;
		std::string Message;
	};

	std::vector<Diagnostic> ParseDiagnostics(const std::string& text)
	{
		static const std::regex header(R"(^(.*)\((\d+)\): (error|warning|note|fatal error|internal error) (\d+): (.*)$)");

		std::vector<std::string> lines;
		std::istringstream stream(text);
		for (std::string line; std::getline(stream, line);)
		{
			if (!line.empty() && line.back() == '\r')
			{
				line.pop_back();
			}
			lines.push_back(line);
		}

		std::vector<Diagnostic> diagnostics;
		for (size_t index = 0; index < lines.size(); ++index)
		{
			std::smatch match;
			if (!std::regex_match(lines[index], match, header))
			{
				continue;
			}
			Diagnostic diagnostic;
			diagnostic.File = match[1].str();
			diagnostic.Line = std::stoi(match[2].str());
			diagnostic.Severity = match[3].str();
			diagnostic.Code = std::stoi(match[4].str());
			diagnostic.Message = match[5].str();

			// The caret line follows the source line
			if (index + 2 < lines.size())
			{
				const std::string& caretLine = lines[index + 2];
				const size_t caret = caretLine.find('^');
				const bool bOnlyMarks = caretLine.find_first_not_of(" \t^~") == std::string::npos;
				if (caret != std::string::npos && bOnlyMarks)
				{
					diagnostic.Column = static_cast<int>(caret) + 1;
				}
			}
			diagnostics.push_back(diagnostic);
		}
		return diagnostics;
	}

	// -------------------------------------------------------------------------
	// SPIR-V
	// -------------------------------------------------------------------------

	std::vector<uint32_t> BlobWords(slang::IBlob* blob)
	{
		std::vector<uint32_t> words(blob->getBufferSize() / sizeof(uint32_t));
		std::memcpy(words.data(), blob->getBufferPointer(), words.size() * sizeof(uint32_t));
		return words;
	}

	// Empty when valid
	std::string ValidateSpirv(const std::vector<uint32_t>& words)
	{
		spv_context context = spvContextCreate(SPV_ENV_VULKAN_1_3);
		spv_validator_options options = spvValidatorOptionsCreate();
		// Vulkan 1.2's scalarBlockLayout feature, which the renderer turns on with bindless
		spvValidatorOptionsSetScalarBlockLayout(options, true);
		spv_const_binary_t binary{ words.data(), words.size() };
		spv_diagnostic diagnostic = nullptr;
		const spv_result_t result = spvValidateWithOptions(context, options, &binary, &diagnostic);
		std::string message;
		if (result != SPV_SUCCESS)
		{
			message = diagnostic && diagnostic->error ? diagnostic->error : "invalid, no message";
		}
		spvDiagnosticDestroy(diagnostic);
		spvValidatorOptionsDestroy(options);
		spvContextDestroy(context);
		return message;
	}

	// The names of a module's OpEntryPoint instructions
	std::vector<std::string> EntryPointNames(const std::vector<uint32_t>& words)
	{
		constexpr uint32_t OpEntryPoint = 15;
		std::vector<std::string> names;
		for (size_t index = 5; index < words.size();)
		{
			const uint32_t wordCount = words[index] >> 16;
			const uint32_t opcode = words[index] & 0xFFFF;
			if (wordCount == 0)
			{
				break;
			}
			if (opcode == OpEntryPoint && wordCount > 3)
			{
				names.emplace_back(reinterpret_cast<const char*>(&words[index + 3]));
			}
			index += wordCount;
		}
		return names;
	}

	// The descriptor set and binding of every decorated variable, as "set.binding"
	std::vector<std::string> DescriptorBindings(const std::vector<uint32_t>& words)
	{
		constexpr uint32_t OpDecorate = 71;
		constexpr uint32_t DecorationBinding = 33;
		constexpr uint32_t DecorationDescriptorSet = 34;
		std::vector<std::pair<uint32_t, int>> sets;     // id, set
		std::vector<std::pair<uint32_t, int>> bindings; // id, binding
		for (size_t index = 5; index < words.size();)
		{
			const uint32_t wordCount = words[index] >> 16;
			const uint32_t opcode = words[index] & 0xFFFF;
			if (wordCount == 0)
			{
				break;
			}
			if (opcode == OpDecorate && wordCount == 4)
			{
				if (words[index + 2] == DecorationDescriptorSet)
				{
					sets.emplace_back(words[index + 1], static_cast<int>(words[index + 3]));
				}
				else if (words[index + 2] == DecorationBinding)
				{
					bindings.emplace_back(words[index + 1], static_cast<int>(words[index + 3]));
				}
			}
			index += wordCount;
		}
		std::vector<std::string> result;
		for (const auto& [id, binding] : bindings)
		{
			const auto set = std::find_if(sets.begin(), sets.end(), [id](const auto& entry)
				{
					return entry.first == id;
				});
			const std::string text = std::to_string(set != sets.end() ? set->second : -1) + "." + std::to_string(binding);
			if (std::find(result.begin(), result.end(), text) == result.end())
			{
				result.push_back(text);
			}
		}
		std::sort(result.begin(), result.end());
		return result;
	}

	std::string Join(const std::vector<std::string>& parts, const char* separator = ", ")
	{
		std::string text;
		for (const std::string& part : parts)
		{
			if (!text.empty())
			{
				text += separator;
			}
			text += part;
		}
		return text;
	}

	// -------------------------------------------------------------------------
	// Compiling
	// -------------------------------------------------------------------------

	const std::filesystem::path ShaderDir = NYX_SLANG_SPIKE_SHADERS;

	// `bScalarLayout`: structured and constant buffers use scalar layout, which reflection then
	// reports. ByteAddressBuffer.Load<T>() reads scalar layout either way (check 5), so materials
	// need it for their offsets; the engine compiles everything this way.
	ComPtr<slang::ISession> MakeSession(slang::IGlobalSession* globalSession, bool bScalarLayout = true)
	{
		slang::TargetDesc target{};
		target.format = SLANG_SPIRV;
		target.profile = globalSession->findProfile("spirv_1_6");
		target.forceGLSLScalarBufferLayout = bScalarLayout;

		slang::CompilerOptionEntry options[2]{};
		// Entry points keep their names in the SPIR-V, so one module can hold several
		options[0].name = slang::CompilerOptionName::VulkanUseEntryPointName;
		options[0].value.kind = slang::CompilerOptionValueKind::Int;
		options[0].value.intValue0 = 1;
		// A material field without a default starts at zero, not at whatever was there (check 6)
		options[1].name = slang::CompilerOptionName::ZeroInitialize;
		options[1].value.kind = slang::CompilerOptionValueKind::Int;
		options[1].value.intValue0 = 1;

		const std::string shaderDir = ShaderDir.string();
		const char* searchPaths[] = { shaderDir.c_str() };

		slang::SessionDesc desc{};
		desc.targets = &target;
		desc.targetCount = 1;
		// Matches glm: mul(Model, position) with the matrix as glm stores it. The API's default
		// is row-major, unlike slangc's.
		desc.defaultMatrixLayoutMode = SLANG_MATRIX_LAYOUT_COLUMN_MAJOR;
		desc.searchPaths = searchPaths;
		desc.searchPathCount = 1;
		desc.compilerOptionEntries = options;
		desc.compilerOptionEntryCount = 2;

		ComPtr<slang::ISession> session;
		if (SLANG_FAILED(globalSession->createSession(desc, session.writeRef())))
		{
			throw std::runtime_error("Slang session could not be made");
		}
		return session;
	}

	struct SurfaceProgram
	{
		std::vector<uint32_t> Vertex;
		std::vector<uint32_t> Fragment;
		std::string Diagnostics; // everything Slang said along the way
		bool bOk = false;
	};

	// A module the session has loaded already, e.g. through an import. Modules are named by their
	// path: `import nyx.surface;` loads "nyx/surface", and loadModule("nyx.surface") would look for
	// a file named "nyx.surface.slang".
	slang::IModule* FindLoadedModule(slang::ISession* session, const std::string& name)
	{
		for (SlangInt index = 0; index < session->getLoadedModuleCount(); ++index)
		{
			slang::IModule* module = session->getLoadedModule(index);
			if (module->getName() == name)
			{
				return module;
			}
		}
		return nullptr;
	}

	std::string LoadedModuleNames(slang::ISession* session)
	{
		std::vector<std::string> names;
		for (SlangInt index = 0; index < session->getLoadedModuleCount(); ++index)
		{
			names.push_back(session->getLoadedModule(index)->getName());
		}
		return Join(names);
	}

	// The surface domain's vertex entry point, and its fragment entry point specialised with
	// `materialType` from the module `materialModule`
	SurfaceProgram CompileSurface(slang::ISession* session, const char* materialModule, const char* materialType)
	{
		SurfaceProgram program;
		ComPtr<slang::IBlob> diagnostics;
		// Everything Slang says, with the step it said it in
		const auto collect = [&](const char* step = nullptr)
		{
			const std::string text = BlobText(diagnostics);
			if (!text.empty())
			{
				program.Diagnostics += step ? "[" + std::string(step) + "] " + text : text;
			}
			diagnostics = nullptr;
		};

		slang::IModule* material = session->loadModule(materialModule, diagnostics.writeRef());
		collect();
		if (!material)
		{
			return program;
		}
		slang::IModule* surface = FindLoadedModule(session, "nyx/surface");
		if (!surface)
		{
			program.Diagnostics += "nyx.surface isn't among the loaded modules: " + LoadedModuleNames(session) + "\n";
			return program;
		}

		ComPtr<slang::IEntryPoint> vertexEntry;
		ComPtr<slang::IEntryPoint> fragmentEntry;
		if (SLANG_FAILED(surface->findEntryPointByName("surfaceVertex", vertexEntry.writeRef())) ||
			SLANG_FAILED(surface->findEntryPointByName("surfaceFragment", fragmentEntry.writeRef())))
		{
			program.Diagnostics += "entry points not found in nyx.surface\n";
			return program;
		}

		slang::TypeReflection* type = material->getLayout()->findTypeByName(materialType);
		if (!type)
		{
			program.Diagnostics += std::string("type ") + materialType + " not found\n";
			return program;
		}

		// Vertex: not generic, links on its own
		{
			slang::IComponentType* parts[] = { surface, vertexEntry };
			ComPtr<slang::IComponentType> composite;
			ComPtr<slang::IComponentType> linked;
			ComPtr<slang::IBlob> code;
			session->createCompositeComponentType(parts, 2, composite.writeRef(), diagnostics.writeRef());
			collect("vertex, compose");
			if (!composite || SLANG_FAILED(composite->link(linked.writeRef(), diagnostics.writeRef())))
			{
				collect("vertex, link");
				return program;
			}
			collect("vertex, link");
			if (SLANG_FAILED(linked->getEntryPointCode(0, 0, code.writeRef(), diagnostics.writeRef())))
			{
				collect("vertex, code");
				return program;
			}
			collect("vertex, code");
			program.Vertex = BlobWords(code);
		}

		// Fragment: the domain's module, the material's, the generic entry point, then S = the
		// material. The domain's module must come first: without it, or after the material,
		// Slang 2026.1 fails with an internal error while generating code (check 0).
		{
			slang::IComponentType* parts[] = { surface, material, fragmentEntry };
			ComPtr<slang::IComponentType> composite;
			ComPtr<slang::IComponentType> specialized;
			ComPtr<slang::IComponentType> linked;
			ComPtr<slang::IBlob> code;
			session->createCompositeComponentType(parts, 3, composite.writeRef(), diagnostics.writeRef());
			collect("fragment, compose");
			if (!composite)
			{
				return program;
			}
			const slang::SpecializationArg argument = slang::SpecializationArg::fromType(type);
			if (SLANG_FAILED(composite->specialize(&argument, 1, specialized.writeRef(), diagnostics.writeRef())))
			{
				collect("fragment, specialize");
				return program;
			}
			collect("fragment, specialize");
			if (SLANG_FAILED(specialized->link(linked.writeRef(), diagnostics.writeRef())))
			{
				collect("fragment, link");
				return program;
			}
			collect("fragment, link");
			if (SLANG_FAILED(linked->getEntryPointCode(0, 0, code.writeRef(), diagnostics.writeRef())))
			{
				collect("fragment, code");
				return program;
			}
			collect("fragment, code");
			program.Fragment = BlobWords(code);
		}

		program.bOk = true;
		return program;
	}

	// -------------------------------------------------------------------------
	// Reflection
	// -------------------------------------------------------------------------

	std::string TypeName(slang::TypeReflection* type)
	{
		ComPtr<slang::IBlob> name;
		if (SLANG_SUCCEEDED(type->getFullName(name.writeRef())))
		{
			return BlobText(name);
		}
		return type->getName() ? type->getName() : "?";
	}

	std::string DescribeAttribute(slang::Attribute* attribute)
	{
		std::string text = attribute->getName();
		const uint32_t count = attribute->getArgumentCount();
		if (count == 0)
		{
			return text;
		}
		std::vector<std::string> arguments;
		for (uint32_t index = 0; index < count; ++index)
		{
			size_t size = 0;
			float floatValue = 0.0f;
			int intValue = 0;
			if (const char* stringValue = attribute->getArgumentValueString(index, &size))
			{
				arguments.push_back("\"" + std::string(stringValue, size) + "\"");
			}
			else if (SLANG_SUCCEEDED(attribute->getArgumentValueFloat(index, &floatValue)))
			{
				std::ostringstream number;
				number << floatValue;
				arguments.push_back(number.str());
			}
			else if (SLANG_SUCCEEDED(attribute->getArgumentValueInt(index, &intValue)))
			{
				arguments.push_back(std::to_string(intValue));
			}
			else
			{
				arguments.push_back("?");
			}
		}
		return text + "(" + Join(arguments) + ")";
	}

	// One scalar of a struct, flattened: where it lives and what kind it is
	struct Leaf
	{
		std::string Path;
		size_t Offset = 0;
		slang::TypeReflection::ScalarType Scalar = slang::TypeReflection::ScalarType::None;
	};

	void Flatten(slang::TypeLayoutReflection* layout, size_t base, const std::string& path, std::vector<Leaf>& leaves)
	{
		using Kind = slang::TypeReflection::Kind;
		switch (layout->getKind())
		{
		case Kind::Struct:
			for (unsigned index = 0; index < layout->getFieldCount(); ++index)
			{
				slang::VariableLayoutReflection* field = layout->getFieldByIndex(index);
				const std::string name = path.empty() ? field->getName() : path + "." + field->getName();
				Flatten(field->getTypeLayout(), base + field->getOffset(), name, leaves);
			}
			break;
		case Kind::Vector:
		{
			const size_t count = layout->getElementCount();
			const size_t stride = layout->getSize() / count;
			for (size_t index = 0; index < count; ++index)
			{
				leaves.push_back({ path + "[" + std::to_string(index) + "]", base + index * stride, layout->getScalarType() });
			}
			break;
		}
		case Kind::Array:
		{
			const size_t count = layout->getElementCount();
			const size_t stride = layout->getElementStride(SLANG_PARAMETER_CATEGORY_UNIFORM);
			for (size_t index = 0; index < count; ++index)
			{
				Flatten(layout->getElementTypeLayout(), base + index * stride, path + "[" + std::to_string(index) + "]", leaves);
			}
			break;
		}
		case Kind::Scalar:
			leaves.push_back({ path, base, layout->getScalarType() });
			break;
		default:
			// A descriptor handle is two uints: the index into the heap, then a spare word
			if (layout->getSize() == 8)
			{
				leaves.push_back({ path + ".x", base, slang::TypeReflection::ScalarType::UInt32 });
				leaves.push_back({ path + ".y", base + 4, slang::TypeReflection::ScalarType::UInt32 });
			}
			else
			{
				throw std::runtime_error("Unexpected field kind at " + path + ": " + TypeName(layout->getType()));
			}
			break;
		}
	}

	// -------------------------------------------------------------------------
	// GPU: runs probeLayout once and reads ProbeOut back
	// -------------------------------------------------------------------------

	struct ObjectConstants
	{
		glm::mat4 Model{ 1.0f };
		uint32_t MaterialOffset = 0;
		uint32_t EntityId = 0;
		uint32_t Padding[2]{};
		glm::vec4 CustomData{ 0.0f };
	};
	static_assert(sizeof(ObjectConstants) == 96);

	uint32_t FindMemoryType(vk::PhysicalDevice physicalDevice, uint32_t typeFilter, vk::MemoryPropertyFlags properties)
	{
		const vk::PhysicalDeviceMemoryProperties memoryProperties = physicalDevice.getMemoryProperties();
		for (uint32_t typeIndex = 0; typeIndex < memoryProperties.memoryTypeCount; ++typeIndex)
		{
			if ((typeFilter & (1u << typeIndex)) && (memoryProperties.memoryTypes[typeIndex].propertyFlags & properties) == properties)
			{
				return typeIndex;
			}
		}
		throw std::runtime_error("No suitable memory type");
	}

	struct HostBuffer
	{
		vk::raii::Buffer Buffer{ nullptr };
		vk::raii::DeviceMemory Memory{ nullptr };
		vk::DeviceSize Size = 0;

		HostBuffer(Nyx::VulkanContext& context, vk::DeviceSize size)
			: Size(size)
		{
			Buffer = vk::raii::Buffer(context.GetDevice(), vk::BufferCreateInfo({}, size, vk::BufferUsageFlagBits::eStorageBuffer));
			const vk::MemoryRequirements requirements = Buffer.getMemoryRequirements();
			const uint32_t type = FindMemoryType(*context.GetPhysicalDevice(), requirements.memoryTypeBits,
				vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent);
			Memory = vk::raii::DeviceMemory(context.GetDevice(), vk::MemoryAllocateInfo(requirements.size, type));
			Buffer.bindMemory(*Memory, 0);
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

	std::vector<uint32_t> RunProbe(Nyx::VulkanContext& context, const std::vector<uint32_t>& spirv, const char* entryName,
		const std::vector<uint8_t>& materialBytes, const ObjectConstants& constants, size_t outputCount)
	{
		vk::raii::Device& device = context.GetDevice();

		HostBuffer materials(context, std::max<size_t>(materialBytes.size(), 16));
		materials.Write(materialBytes);
		HostBuffer output(context, outputCount * sizeof(uint32_t));
		output.Write(std::vector<uint8_t>(outputCount * sizeof(uint32_t), 0xCD));

		// Set 0: binding 2 the material buffer, binding 3 the probe's output
		const std::array<vk::DescriptorSetLayoutBinding, 2> bindings{
			vk::DescriptorSetLayoutBinding(2, vk::DescriptorType::eStorageBuffer, 1, vk::ShaderStageFlagBits::eCompute),
			vk::DescriptorSetLayoutBinding(3, vk::DescriptorType::eStorageBuffer, 1, vk::ShaderStageFlagBits::eCompute),
		};
		vk::raii::DescriptorSetLayout setLayout(device, vk::DescriptorSetLayoutCreateInfo({}, bindings));
		const vk::PushConstantRange pushRange(vk::ShaderStageFlagBits::eCompute, 0, sizeof(ObjectConstants));
		vk::raii::PipelineLayout pipelineLayout(device, vk::PipelineLayoutCreateInfo({}, *setLayout, pushRange));

		vk::raii::ShaderModule module(device, vk::ShaderModuleCreateInfo({}, spirv.size() * sizeof(uint32_t), spirv.data()));
		vk::ComputePipelineCreateInfo pipelineInfo{};
		pipelineInfo.stage = vk::PipelineShaderStageCreateInfo({}, vk::ShaderStageFlagBits::eCompute, *module, entryName);
		pipelineInfo.layout = *pipelineLayout;
		vk::raii::Pipeline pipeline(device, nullptr, pipelineInfo);

		const vk::DescriptorPoolSize poolSize(vk::DescriptorType::eStorageBuffer, 2);
		vk::raii::DescriptorPool pool(device, vk::DescriptorPoolCreateInfo(vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet, 1, poolSize));
		vk::raii::DescriptorSet set = std::move(vk::raii::DescriptorSets(device, vk::DescriptorSetAllocateInfo(*pool, *setLayout)).front());
		const vk::DescriptorBufferInfo materialInfo(*materials.Buffer, 0, VK_WHOLE_SIZE);
		const vk::DescriptorBufferInfo outputInfo(*output.Buffer, 0, VK_WHOLE_SIZE);
		const std::array<vk::WriteDescriptorSet, 2> writes{
			vk::WriteDescriptorSet(*set, 2, 0, vk::DescriptorType::eStorageBuffer, {}, materialInfo),
			vk::WriteDescriptorSet(*set, 3, 0, vk::DescriptorType::eStorageBuffer, {}, outputInfo),
		};
		device.updateDescriptorSets(writes, {});

		vk::CommandBufferAllocateInfo allocInfo(*context.GetGraphicsCommandPool(), vk::CommandBufferLevel::ePrimary, 1);
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
		context.GetGraphicsQueue().submit(submit);
		device.waitIdle();

		return output.ReadWords(outputCount);
	}

	// A device with a hidden window, for the probe
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
			Window = glfwCreateWindow(64, 64, "NyxSlangSpike", nullptr, nullptr);
			if (!Window)
			{
				throw std::runtime_error("No window for the Vulkan device");
			}
			Context.Initialize("NyxSlangSpike", Window);
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

		Nyx::VulkanContext Context;

	private:
		GLFWwindow* Window = nullptr;
	};

	// -------------------------------------------------------------------------
	// The checks
	// -------------------------------------------------------------------------

	// The ways to put the generic fragment entry point and a material together. Empty when the
	// fragment's code came out valid, otherwise what went wrong.
	std::string TryFragment(slang::IGlobalSession* globalSession, bool bScalarLayout, int way)
	{
		ComPtr<slang::ISession> session = MakeSession(globalSession, bScalarLayout);
		ComPtr<slang::IBlob> diagnostics;
		slang::IModule* material = session->loadModule("Stone", diagnostics.writeRef());
		slang::IModule* surface = FindLoadedModule(session, "nyx/surface");
		ComPtr<slang::IEntryPoint> entry;
		surface->findEntryPointByName("surfaceFragment", entry.writeRef());
		const slang::SpecializationArg argument = slang::SpecializationArg::fromType(material->getLayout()->findTypeByName("Stone"));

		ComPtr<slang::IComponentType> program;
		if (way == 0 || way == 1 || way == 2)
		{
			std::vector<slang::IComponentType*> parts;
			if (way == 0) parts = { material, entry };
			if (way == 1) parts = { surface, material, entry };
			if (way == 2) parts = { entry, material };
			ComPtr<slang::IComponentType> composite;
			session->createCompositeComponentType(parts.data(), static_cast<SlangInt>(parts.size()), composite.writeRef(), diagnostics.writeRef());
			if (!composite || SLANG_FAILED(composite->specialize(&argument, 1, program.writeRef(), diagnostics.writeRef())))
			{
				return "specialize: " + BlobText(diagnostics);
			}
		}
		else
		{
			// Specialise the entry point on its own, then add the material's module (way 3) or not (way 4)
			ComPtr<slang::IComponentType> specialized;
			if (SLANG_FAILED(entry->specialize(&argument, 1, specialized.writeRef(), diagnostics.writeRef())))
			{
				return "specialize: " + BlobText(diagnostics);
			}
			if (way == 3)
			{
				slang::IComponentType* parts[] = { material, specialized };
				session->createCompositeComponentType(parts, 2, program.writeRef(), diagnostics.writeRef());
			}
			else
			{
				program = specialized;
			}
		}
		ComPtr<slang::IComponentType> linked;
		ComPtr<slang::IBlob> code;
		if (!program || SLANG_FAILED(program->link(linked.writeRef(), diagnostics.writeRef())))
		{
			return "link: " + BlobText(diagnostics);
		}
		if (SLANG_FAILED(linked->getEntryPointCode(0, 0, code.writeRef(), diagnostics.writeRef())))
		{
			const std::string text = BlobText(diagnostics);
			const size_t error = text.find("error");
			return "code: " + (error != std::string::npos ? text.substr(error, text.find('\n', error) - error) : text);
		}
		const std::string problem = ValidateSpirv(BlobWords(code));
		return problem.empty() ? std::string() : "invalid SPIR-V: " + problem;
	}

	void CheckSpecialisation(slang::IGlobalSession* globalSession)
	{
		// Informational: which work in this Slang version, to see at once when an update changes it
		Section("0. Ways to specialise the fragment entry point (informational)");
		const char* ways[] = {
			"compose [material, entry], specialise",
			"compose [surface, material, entry], specialise",
			"compose [entry, material], specialise",
			"specialise entry, compose [material, entry]",
			"specialise entry alone",
		};
		for (bool bScalar : { false, true })
		{
			for (int way = 0; way < 5; ++way)
			{
				const std::string result = TryFragment(globalSession, bScalar, way);
				std::cout << "         " << (bScalar ? "scalar  " : "default ") << ways[way] << ": " << (result.empty() ? "works" : result) << "\n";
			}
		}

		Section("1. Generic entry point specialised with the material");
		ComPtr<slang::ISession> session = MakeSession(globalSession);
		const SurfaceProgram program = CompileSurface(session, "Stone", "Stone");
		Check(program.bOk, "surfaceVertex and surfaceFragment<Stone> compile", program.bOk ? "" : program.Diagnostics);
		if (!program.bOk)
		{
			return;
		}
		if (!program.Diagnostics.empty())
		{
			std::cout << "         Slang said:\n"
					  << program.Diagnostics;
		}

		const std::string vertexProblem = ValidateSpirv(program.Vertex);
		const std::string fragmentProblem = ValidateSpirv(program.Fragment);
		Check(vertexProblem.empty(), "vertex SPIR-V is valid for Vulkan 1.3 with scalar block layout", vertexProblem);
		Check(fragmentProblem.empty(), "fragment SPIR-V is valid for Vulkan 1.3 with scalar block layout", fragmentProblem);

		std::cout << "         vertex: " << program.Vertex.size() * 4 << " bytes, entry " << Join(EntryPointNames(program.Vertex))
				  << ", bindings " << Join(DescriptorBindings(program.Vertex)) << "\n";
		std::cout << "         fragment: " << program.Fragment.size() * 4 << " bytes, entry " << Join(EntryPointNames(program.Fragment))
				  << ", bindings " << Join(DescriptorBindings(program.Fragment)) << "\n";

		const std::vector<std::string> fragmentBindings = DescriptorBindings(program.Fragment);
		const auto has = [&](const char* binding)
		{
			return std::find(fragmentBindings.begin(), fragmentBindings.end(), binding) != fragmentBindings.end();
		};
		Check(has("0.0") && has("0.1") && has("0.2") && has("1.0") && fragmentBindings.size() == 4,
			"fragment uses exactly the planned bindings", "0.0 textures, 0.1 samplers, 0.2 materials, 1.0 view");
	}

	void CheckReflection(slang::IGlobalSession* globalSession)
	{
		Section("2. Reflection of the material's fields");
		ComPtr<slang::ISession> session = MakeSession(globalSession);
		ComPtr<slang::IBlob> diagnostics;
		slang::IModule* module = session->loadModule("Stone", diagnostics.writeRef());
		Check(module != nullptr, "Stone loads", BlobText(diagnostics));
		if (!module)
		{
			return;
		}
		slang::TypeReflection* type = module->getLayout()->findTypeByName("Stone");
		slang::TypeLayoutReflection* layout = session->getTypeLayout(type);
		std::cout << "         Stone: " << layout->getSize() << " bytes, alignment " << layout->getAlignment() << "\n";

		bool bAllAttributes = true;
		bool bAnyDefault = false;
		for (unsigned index = 0; index < layout->getFieldCount(); ++index)
		{
			slang::VariableLayoutReflection* field = layout->getFieldByIndex(index);
			slang::VariableReflection* variable = field->getVariable();
			std::vector<std::string> attributes;
			for (unsigned attributeIndex = 0; attributeIndex < variable->getUserAttributeCount(); ++attributeIndex)
			{
				attributes.push_back(DescribeAttribute(variable->getUserAttributeByIndex(attributeIndex)));
			}
			std::string defaultValue = variable->hasDefaultValue() ? "has default" : "no default";
			float floatDefault = 0.0f;
			if (variable->hasDefaultValue() && SLANG_SUCCEEDED(variable->getDefaultValueFloat(&floatDefault)))
			{
				defaultValue = "default " + std::to_string(floatDefault);
				bAnyDefault = true;
			}
			std::cout << "         " << field->getOffset() << "\t" << field->getTypeLayout()->getSize() << "\t" << TypeName(field->getType())
					  << " " << field->getName() << "  [" << Join(attributes) << "]  " << defaultValue << "\n";
			bAllAttributes = bAllAttributes && !attributes.empty();
		}
		Check(bAllAttributes, "every field's attributes arrive, with their arguments");

		slang::VariableReflection* roughness = layout->getFieldByIndex(1)->getVariable();
		slang::Attribute* range = roughness->findUserAttributeByName(globalSession, "Range");
		float min = -1.0f;
		float max = -1.0f;
		const bool bRange = range && SLANG_SUCCEEDED(range->getArgumentValueFloat(0, &min)) && SLANG_SUCCEEDED(range->getArgumentValueFloat(1, &max));
		Check(bRange && min == 0.0f && max == 1.0f, "Range(0, 1) on Roughness reads back as numbers", std::to_string(min) + ", " + std::to_string(max));
		std::cout << "         default values through reflection: " << (bAnyDefault ? "yes, for scalars" : "no") << " (informational)\n";
	}

	// An entry point of the probe, compiled in `session`
	std::vector<uint32_t> CompileProbe(slang::ISession* session, const char* entryName, std::string& outDiagnostics)
	{
		ComPtr<slang::IBlob> diagnostics;
		slang::IModule* module = session->loadModule("LayoutProbe", diagnostics.writeRef());
		outDiagnostics += BlobText(diagnostics);
		if (!module)
		{
			return {};
		}
		ComPtr<slang::IEntryPoint> entry;
		module->findEntryPointByName(entryName, entry.writeRef());
		slang::IComponentType* parts[] = { module, entry };
		ComPtr<slang::IComponentType> composite;
		ComPtr<slang::IComponentType> linked;
		ComPtr<slang::IBlob> code;
		session->createCompositeComponentType(parts, 2, composite.writeRef(), diagnostics.writeRef());
		if (!composite || SLANG_FAILED(composite->link(linked.writeRef(), diagnostics.writeRef())) ||
			SLANG_FAILED(linked->getEntryPointCode(0, 0, code.writeRef(), diagnostics.writeRef())))
		{
			outDiagnostics += BlobText(diagnostics);
			return {};
		}
		return BlobWords(code);
	}

	void CheckLayoutOnGpu(slang::IGlobalSession* globalSession, GpuDevice& gpu)
	{
		Section("5. Material layout: reflection against what the GPU loads");
		ComPtr<slang::ISession> session = MakeSession(globalSession, false);
		ComPtr<slang::ISession> scalarSession = MakeSession(globalSession, true);
		std::string diagnostics;
		const std::vector<uint32_t> spirv = CompileProbe(session, "probeLayout", diagnostics);
		CompileProbe(scalarSession, "probeLayout", diagnostics);
		Check(!spirv.empty(), "probeLayout compiles", diagnostics);
		if (spirv.empty())
		{
			return;
		}
		const std::string problem = ValidateSpirv(spirv);
		Check(problem.empty(), "probe SPIR-V is valid", problem);

		// A non-zero offset, as a material that isn't the first in the buffer has
		constexpr uint32_t MaterialOffset = 64;
		constexpr uint32_t BufferSize = 1024;
		ObjectConstants constants;
		constants.MaterialOffset = MaterialOffset;
		constants.Model = glm::translate(glm::mat4(1.0f), glm::vec3(10.0f, 20.0f, 30.0f)) * glm::rotate(glm::mat4(1.0f), glm::radians(90.0f), glm::vec3(0.0f, 0.0f, 1.0f));
		const glm::vec4 expectedMoved = constants.Model * glm::vec4(1.0f, 2.0f, 3.0f, 1.0f);

		// The layouts reflection offers: plain, and as the element of a structured or constant
		// buffer, in a default session and in one with scalar layout
		slang::TypeReflection* type = FindLoadedModule(session, "LayoutProbe")->getLayout()->findTypeByName("LayoutProbe");
		slang::TypeReflection* scalarType = FindLoadedModule(scalarSession, "LayoutProbe")->getLayout()->findTypeByName("LayoutProbe");
		struct Candidate
		{
			const char* Name;
			slang::TypeLayoutReflection* Layout;
		};
		const Candidate candidates[] = {
			{ "getTypeLayout(type)", session->getTypeLayout(type) },
			{ "StructuredBuffer element", session->getTypeLayout(session->getContainerType(type, slang::ContainerType::StructuredBuffer))->getElementTypeLayout() },
			{ "ConstantBuffer element", session->getTypeLayout(session->getContainerType(type, slang::ContainerType::ConstantBuffer))->getElementTypeLayout() },
			{ "scalar session: getTypeLayout(type)", scalarSession->getTypeLayout(scalarType) },
			{ "scalar session: StructuredBuffer element", scalarSession->getTypeLayout(scalarSession->getContainerType(scalarType, slang::ContainerType::StructuredBuffer))->getElementTypeLayout() },
		};

		std::vector<Leaf> leaves;
		Flatten(candidates[0].Layout, 0, "", leaves);

		// Where the GPU reads each scalar: every word of the buffer holds its own offset from the
		// material's start
		std::vector<uint8_t> marked(BufferSize, 0);
		for (uint32_t offset = MaterialOffset; offset + 4 <= BufferSize; offset += 4)
		{
			const uint32_t word = 0xA0000000u | (offset - MaterialOffset);
			std::memcpy(&marked[offset], &word, 4);
		}
		const std::vector<uint32_t> found = RunProbe(gpu.Context, spirv, "probeLayout", marked, constants, leaves.size() + 4);
		std::vector<int> gpuOffsets; // -1 where the scalar is a bool, which only reads back as 0 or 1
		std::cout << "         the GPU reads:";
		for (size_t index = 0; index < leaves.size(); ++index)
		{
			const bool bBool = leaves[index].Scalar == slang::TypeReflection::ScalarType::Bool;
			gpuOffsets.push_back(bBool ? -1 : static_cast<int>(found[index] & 0xFFFFF));
			std::cout << (index % 6 == 0 ? "\n           " : "  ") << leaves[index].Path << "@" << (bBool ? std::string("?") : std::to_string(gpuOffsets.back()));
		}
		std::cout << "\n";

		glm::vec4 moved;
		std::memcpy(&moved, &found[leaves.size()], sizeof(moved));
		Check(glm::all(glm::lessThan(glm::abs(moved - expectedMoved), glm::vec4(1e-4f))), "push-constant model matrix matches glm (column-major session)",
			"(" + std::to_string(moved.x) + ", " + std::to_string(moved.y) + ", " + std::to_string(moved.z) + ")");

		const Candidate* matched = nullptr;
		for (const Candidate& candidate : candidates)
		{
			std::vector<Leaf> candidateLeaves;
			Flatten(candidate.Layout, 0, "", candidateLeaves);
			size_t firstDifference = candidateLeaves.size();
			for (size_t index = 0; index < candidateLeaves.size(); ++index)
			{
				if (gpuOffsets[index] >= 0 && static_cast<size_t>(gpuOffsets[index]) != candidateLeaves[index].Offset)
				{
					firstDifference = index;
					break;
				}
			}
			const bool bMatches = firstDifference == candidateLeaves.size();
			std::cout << "         " << candidate.Name << ": " << candidate.Layout->getSize() << " bytes, "
					  << (bMatches ? std::string("matches the GPU")
								   : "differs at " + candidateLeaves[firstDifference].Path + " (" + std::to_string(candidateLeaves[firstDifference].Offset) + ")")
					  << "\n";
			if (bMatches && !matched)
			{
				matched = &candidate;
			}
		}
		Check(matched != nullptr, "a layout from reflection is what the GPU loads", matched ? matched->Name : "none");
		Check(matched == &candidates[3], "it is the plain type layout of a scalar-layout session, as the engine's session will be");
		if (!matched)
		{
			return;
		}

		// The matching layout once more with real values, bools included: each scalar gets a
		// value no other has, written where the layout says it lives
		std::vector<Leaf> matchedLeaves;
		Flatten(matched->Layout, 0, "", matchedLeaves);
		std::vector<uint8_t> bytes(BufferSize, 0);
		std::vector<uint32_t> expected;
		for (size_t index = 0; index < matchedLeaves.size(); ++index)
		{
			uint32_t word = 0;
			switch (matchedLeaves[index].Scalar)
			{
			case slang::TypeReflection::ScalarType::Float32:
			{
				const float value = 1000.0f + static_cast<float>(index);
				std::memcpy(&word, &value, 4);
				break;
			}
			case slang::TypeReflection::ScalarType::Int32:
				word = static_cast<uint32_t>(-static_cast<int32_t>(index) - 1);
				break;
			case slang::TypeReflection::ScalarType::Bool:
				word = 1;
				break;
			default:
				word = 100u + static_cast<uint32_t>(index);
				break;
			}
			std::memcpy(&bytes[MaterialOffset + matchedLeaves[index].Offset], &word, 4);
			expected.push_back(word);
		}
		const std::vector<uint32_t> actual = RunProbe(gpu.Context, spirv, "probeLayout", bytes, constants, matchedLeaves.size() + 4);
		bool bValues = true;
		for (size_t index = 0; index < matchedLeaves.size(); ++index)
		{
			bValues = bValues && actual[index] == expected[index];
		}
		Check(bValues, "values written at the reflected offsets read back exactly, bools included");

		// Load, then Store<LayoutProbe>: every value must land at its reflected offset again
		const std::vector<uint32_t> storeSpirv = CompileProbe(scalarSession, "probeStore", diagnostics);
		Check(!storeSpirv.empty(), "probeStore compiles", diagnostics);
		if (!storeSpirv.empty())
		{
			const std::vector<uint32_t> stored = RunProbe(gpu.Context, storeSpirv, "probeStore", bytes, constants, (matched->Layout->getSize() + 3) / 4);
			size_t firstDifference = matchedLeaves.size();
			for (size_t index = 0; index < matchedLeaves.size(); ++index)
			{
				if (stored[matchedLeaves[index].Offset / 4] != expected[index])
				{
					firstDifference = index;
					break;
				}
			}
			Check(firstDifference == matchedLeaves.size(), "Store<T> writes every value at its reflected offset too",
				firstDifference == matchedLeaves.size() ? "" : "first wrong: " + matchedLeaves[firstDifference].Path);
		}

		// Slang's DescriptorHandle at offset 4: informational, it shows why nyx::Texture holds a
		// plain index. Reflection puts the handle at 4; the GPU should read 77 there.
		const std::vector<uint32_t> rawSpirv = CompileProbe(scalarSession, "probeRawHandle", diagnostics);
		if (!rawSpirv.empty())
		{
			std::vector<uint8_t> rawBytes(BufferSize, 0);
			const float a = 1.5f;
			const uint32_t handle[2] = { 77, 0 };
			std::memcpy(&rawBytes[MaterialOffset], &a, 4);
			std::memcpy(&rawBytes[MaterialOffset + 4], handle, 8);
			const std::vector<uint32_t> raw = RunProbe(gpu.Context, rawSpirv, "probeRawHandle", rawBytes, constants, 3);
			std::cout << "         DescriptorHandle at offset 4 (informational): reads back (" << raw[1] << ", " << raw[2] << "), "
					  << (raw[1] == 77 ? "correct: Slang has fixed this, nyx::Texture could hold a handle again" : "wrong: loaded from offset 0, as a uint2 array of stride 8")
					  << "\n";
		}
	}

	// A compute shader, generated for one material, that stores the material made with its
	// defaults: the bytes a new material starts with, in the layout the GPU reads
	std::vector<uint32_t> CompileDefaultsWriter(slang::ISession* session, const std::string& materialModule, const std::string& materialType, std::string& outDiagnostics)
	{
		const std::string moduleName = materialModule + "Defaults";
		const std::string source =
			"import " + materialModule + ";\n"
										 "[vk::binding(3, 0)] RWByteAddressBuffer DefaultsOut;\n"
										 "[shader(\"compute\")]\n"
										 "[numthreads(1, 1, 1)]\n"
										 "void writeDefaults() { DefaultsOut.Store<" +
			materialType + ">(0, " + materialType + "()); }\n";

		ComPtr<slang::IBlob> diagnostics;
		slang::IModule* module = session->loadModuleFromSourceString(moduleName.c_str(), (moduleName + ".slang").c_str(), source.c_str(), diagnostics.writeRef());
		outDiagnostics += BlobText(diagnostics);
		if (!module)
		{
			return {};
		}
		ComPtr<slang::IEntryPoint> entry;
		module->findEntryPointByName("writeDefaults", entry.writeRef());
		slang::IComponentType* parts[] = { module, entry };
		ComPtr<slang::IComponentType> composite;
		ComPtr<slang::IComponentType> linked;
		ComPtr<slang::IBlob> code;
		session->createCompositeComponentType(parts, 2, composite.writeRef(), diagnostics.writeRef());
		if (!composite || SLANG_FAILED(composite->link(linked.writeRef(), diagnostics.writeRef())) ||
			SLANG_FAILED(linked->getEntryPointCode(0, 0, code.writeRef(), diagnostics.writeRef())))
		{
			outDiagnostics += BlobText(diagnostics);
			return {};
		}
		return BlobWords(code);
	}

	void CheckDefaultsOnGpu(slang::IGlobalSession* globalSession, GpuDevice& gpu)
	{
		Section("6. Default values: a generated shader stores the material made with its defaults");
		ComPtr<slang::ISession> session = MakeSession(globalSession);
		std::string diagnostics;
		const std::vector<uint32_t> spirv = CompileDefaultsWriter(session, "Stone", "Stone", diagnostics);
		Check(!spirv.empty(), "the defaults writer for Stone compiles", diagnostics);
		if (spirv.empty())
		{
			return;
		}

		slang::TypeLayoutReflection* layout = session->getTypeLayout(FindLoadedModule(session, "Stone")->getLayout()->findTypeByName("Stone"));
		std::vector<Leaf> leaves;
		Flatten(layout, 0, "", leaves);
		std::cout << "         Expected: the validation layer reports this shader. Store<T> writes through an array\n"
					 "         with a 12-byte stride, which needs scalarBlockLayout; the device turns that on in change 5.\n";
		const std::vector<uint32_t> stored = RunProbe(gpu.Context, spirv, "writeDefaults", std::vector<uint8_t>(16, 0), ObjectConstants{}, (layout->getSize() + 3) / 4);

		const auto valueOf = [&](const std::string& path)
		{
			const auto leaf = std::find_if(leaves.begin(), leaves.end(), [&](const Leaf& l)
				{
					return l.Path == path;
				});
			if (leaf == leaves.end())
			{
				throw std::runtime_error("No field " + path);
			}
			const uint32_t word = stored[leaf->Offset / 4];
			float value = 0.0f;
			if (leaf->Scalar == slang::TypeReflection::ScalarType::Float32)
			{
				std::memcpy(&value, &word, 4);
			}
			else
			{
				value = static_cast<float>(word);
			}
			return value;
		};
		std::cout << "        ";
		for (const Leaf& leaf : leaves)
		{
			std::cout << " " << leaf.Path << "@" << leaf.Offset << "=" << valueOf(leaf.Path);
		}
		std::cout << "\n         stored words:";
		for (uint32_t word : stored)
		{
			std::cout << " " << std::hex << word << std::dec;
		}
		std::cout << "\n";

		const auto near = [&](const char* path, float expected)
		{
			return std::abs(valueOf(path) - expected) < 1e-6f;
		};
		Check(near("BaseColor[0]", 0.62f) && near("BaseColor[1]", 0.60f) && near("BaseColor[2]", 0.56f) && near("Roughness", 0.78f) &&
				near("Wetness", 0.3f) && near("Albedo.Index", 0.0f) && near("Tiling[0]", 1.0f) && near("Tiling[1]", 1.0f),
			"Stone's defaults come back as the shader declares them, vectors included");

		// A field without a default is allowed; with zero-initialisation on, it starts at 0
		ComPtr<slang::ISession> otherSession = MakeSession(globalSession);
		std::string noDefaultDiagnostics;
		const std::vector<uint32_t> noDefaultSpirv = CompileDefaultsWriter(otherSession, "NoDefault", "NoDefault", noDefaultDiagnostics);
		Check(!noDefaultSpirv.empty(), "a material with a field without a default can be made with defaults", noDefaultDiagnostics);
		if (!noDefaultSpirv.empty())
		{
			// Filled with a pattern first, so a field the shader leaves alone doesn't read as 0
			const std::vector<uint32_t> words = RunProbe(gpu.Context, noDefaultSpirv, "writeDefaults", std::vector<uint8_t>(16, 0), ObjectConstants{}, 2);
			float metallic = -1.0f;
			float roughness = -1.0f;
			std::memcpy(&metallic, &words[0], 4);
			std::memcpy(&roughness, &words[1], 4);
			Check(metallic == 0.0f && roughness == 0.5f, "the field without a default is 0, the other keeps its default",
				"Metallic " + std::to_string(metallic) + ", Roughness " + std::to_string(roughness));
		}
	}

	void CheckDiagnostics(slang::IGlobalSession* globalSession)
	{
		Section("3. Diagnostics");
		ComPtr<slang::ISession> session = MakeSession(globalSession);
		const SurfaceProgram program = CompileSurface(session, "Broken", "Broken");
		Check(!program.bOk, "a material with an error doesn't compile");
		const std::vector<Diagnostic> diagnostics = ParseDiagnostics(program.Diagnostics);
		const auto error = std::find_if(diagnostics.begin(), diagnostics.end(), [](const Diagnostic& d)
			{
				return d.Severity == "error";
			});
		if (error == diagnostics.end())
		{
			Check(false, "an error is reported", program.Diagnostics);
			return;
		}
		std::cout << "         " << error->File << " line " << error->Line << " column " << error->Column << ", " << error->Severity << " "
				  << error->Code << ": " << error->Message << "\n";
		Check(std::filesystem::path(error->File).filename() == "Broken.slang", "the file is named");
		Check(error->Line == 13 && error->Column == 32, "line and column point at 'Tilling'",
			"line " + std::to_string(error->Line) + ", column " + std::to_string(error->Column));
	}

	void CheckDependencies(slang::IGlobalSession* globalSession)
	{
		Section("4. Dependencies");
		ComPtr<slang::ISession> session = MakeSession(globalSession);
		slang::IModule* module = session->loadModule("Stone");
		std::vector<std::string> files;
		for (SlangInt32 index = 0; index < module->getDependencyFileCount(); ++index)
		{
			files.push_back(std::filesystem::path(module->getDependencyFilePath(index)).filename().string());
		}
		std::cout << "         Stone depends on: " << Join(files) << "\n";
		const auto has = [&](const char* name)
		{
			return std::find(files.begin(), files.end(), name) != files.end();
		};
		Check(has("Stone.slang") && has("surface.slang"), "the module lists itself and what it imports");
	}

	void CheckDeviceFeatures(GpuDevice& gpu)
	{
		Section("7. This GPU and bindless (informational; nothing is enabled yet)");
		const auto chain = gpu.Context.GetPhysicalDevice().getFeatures2<vk::PhysicalDeviceFeatures2, vk::PhysicalDeviceVulkan12Features>();
		const vk::PhysicalDeviceVulkan12Features& features = chain.get<vk::PhysicalDeviceVulkan12Features>();
		const auto line = [](const char* name, vk::Bool32 value)
		{
			std::cout << "         " << name << ": " << (value ? "yes" : "NO") << "\n";
		};
		std::cout << "         " << gpu.Context.GetPhysicalDevice().getProperties().deviceName.data() << "\n";
		line("runtimeDescriptorArray", features.runtimeDescriptorArray);
		line("descriptorBindingPartiallyBound", features.descriptorBindingPartiallyBound);
		line("descriptorBindingSampledImageUpdateAfterBind", features.descriptorBindingSampledImageUpdateAfterBind);
		line("descriptorBindingVariableDescriptorCount", features.descriptorBindingVariableDescriptorCount);
		line("shaderSampledImageArrayNonUniformIndexing", features.shaderSampledImageArrayNonUniformIndexing);
		line("scalarBlockLayout", features.scalarBlockLayout);
		const auto properties = gpu.Context.GetPhysicalDevice().getProperties2<vk::PhysicalDeviceProperties2, vk::PhysicalDeviceVulkan12Properties>();
		std::cout << "         maxDescriptorSetUpdateAfterBindSampledImages: "
				  << properties.get<vk::PhysicalDeviceVulkan12Properties>().maxDescriptorSetUpdateAfterBindSampledImages << "\n";
		std::cout << "         maxPushConstantsSize: " << properties.get<vk::PhysicalDeviceProperties2>().properties.limits.maxPushConstantsSize << "\n";
		Check(features.runtimeDescriptorArray && features.descriptorBindingPartiallyBound && features.descriptorBindingSampledImageUpdateAfterBind,
			"this GPU has what bindless needs");
	}

	void MeasureTimes(double globalSessionMilliseconds, slang::IGlobalSession* globalSession)
	{
		Section("8. Times");
		std::cout << "         global session (once per program): " << FormatMilliseconds(globalSessionMilliseconds) << "\n";

		// What a save costs: a fresh session (Slang keeps loaded modules per session), loading the
		// material and the library, both entry points
		std::vector<double> times;
		for (int run = 0; run < 10; ++run)
		{
			const Clock::time_point start = Clock::now();
			ComPtr<slang::ISession> session = MakeSession(globalSession);
			const SurfaceProgram program = CompileSurface(session, "Stone", "Stone");
			times.push_back(MillisecondsSince(start));
			if (!program.bOk)
			{
				Check(false, "Stone compiles every time");
				return;
			}
		}
		std::cout << "         first compile in the program: " << FormatMilliseconds(times.front()) << "\n";
		std::vector<double> later(times.begin() + 1, times.end());
		std::sort(later.begin(), later.end());
		std::cout << "         later compiles: median " << FormatMilliseconds(later[later.size() / 2]) << ", fastest "
				  << FormatMilliseconds(later.front()) << ", slowest " << FormatMilliseconds(later.back()) << "\n";

		// Recompiling in the same session, where the modules are already loaded: linking and code
		// generation alone
		ComPtr<slang::ISession> session = MakeSession(globalSession);
		CompileSurface(session, "Stone", "Stone");
		const Clock::time_point start = Clock::now();
		CompileSurface(session, "Stone", "Stone");
		std::cout << "         again in the same session (link and code only): " << FormatMilliseconds(MillisecondsSince(start)) << "\n";
		Check(later[later.size() / 2] < 250.0, "a save compiles within the plan's 250 ms");
	}
}

int main()
{
	try
	{
		std::cout << "NyxSlangSpike: Slang " << SLANG_TAG_VERSION << ", shaders in " << ShaderDir.string() << "\n";

		const Clock::time_point start = Clock::now();
		ComPtr<slang::IGlobalSession> globalSession;
		if (SLANG_FAILED(slang::createGlobalSession(globalSession.writeRef())))
		{
			std::cout << "Slang's global session could not be made\n";
			return 1;
		}
		const double globalSessionMilliseconds = MillisecondsSince(start);

		CheckSpecialisation(globalSession);
		CheckReflection(globalSession);
		CheckDiagnostics(globalSession);
		CheckDependencies(globalSession);
		{
			GpuDevice gpu;
			CheckLayoutOnGpu(globalSession, gpu);
			CheckDefaultsOnGpu(globalSession, gpu);
			CheckDeviceFeatures(gpu);
		}
		MeasureTimes(globalSessionMilliseconds, globalSession);
	}
	catch (const std::exception& exception)
	{
		std::cout << "\nStopped: " << exception.what() << "\n";
		return 1;
	}

	std::cout << "\n"
			  << (FailedChecks == 0 ? "All checks passed." : std::to_string(FailedChecks) + " check(s) failed.") << "\n";
	return FailedChecks == 0 ? 0 : 1;
}
