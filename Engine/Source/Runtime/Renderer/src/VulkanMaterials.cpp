#include "NyxPCH.h"
#include "VulkanMaterials.h"

#include "ImageFile.h"
#include "Log.h"
#include "Mesh.h"
#include "VulkanBindless.h"
#include "VulkanContext.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstddef>
#include <cstring>
#include <stdexcept>

namespace Nyx::Engine
{
	namespace
	{
		constexpr const char* MissingProgram = "MissingMaterial.slang";

		// Paths as Windows compares them: whole, normalised, without regard to case
		std::string PathKey(const std::filesystem::path& path)
		{
			std::string key = path.lexically_normal().generic_string();
			std::transform(key.begin(), key.end(), key.begin(), [](unsigned char c)
				{
					return static_cast<char>(std::tolower(c));
				});
			while (!key.empty() && key.back() == '/')
			{
				key.pop_back();
			}
			return key;
		}

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
			throw std::runtime_error("Materials: no suitable memory type");
		}

		// The input locations a vertex shader reads: its Input variables' Location decorations.
		// Slang leaves out inputs nothing uses, and attributes it doesn't read only slow the GPU.
		std::vector<uint32_t> InputLocations(const std::vector<uint32_t>& spirv)
		{
			constexpr uint32_t OpVariable = 59;
			constexpr uint32_t OpDecorate = 71;
			constexpr uint32_t DecorationLocation = 30;
			constexpr uint32_t StorageClassInput = 1;
			std::map<uint32_t, uint32_t> locations;
			std::vector<uint32_t> inputs;
			for (size_t index = 5; index < spirv.size();)
			{
				const uint32_t wordCount = spirv[index] >> 16;
				const uint32_t opcode = spirv[index] & 0xFFFF;
				if (wordCount == 0)
				{
					break;
				}
				if (opcode == OpDecorate && wordCount >= 4 && spirv[index + 2] == DecorationLocation)
				{
					locations[spirv[index + 1]] = spirv[index + 3];
				}
				else if (opcode == OpVariable && wordCount >= 4 && spirv[index + 3] == StorageClassInput)
				{
					inputs.push_back(spirv[index + 2]);
				}
				index += wordCount;
			}
			std::vector<uint32_t> result;
			for (const uint32_t input : inputs)
			{
				if (const auto found = locations.find(input); found != locations.end())
				{
					result.push_back(found->second);
				}
			}
			return result;
		}

		const CompiledEntryPoint* FindEntryPoint(const ShaderCompileResult& result, const std::string& name)
		{
			for (const CompiledEntryPoint& entryPoint : result.EntryPoints)
			{
				if (entryPoint.Name == name)
				{
					return &entryPoint;
				}
			}
			return nullptr;
		}
	}

	void VulkanMaterials::Initialize(Nyx::VulkanContext& context, VulkanBindless& bindless, ShaderPrograms& programs, std::filesystem::path assetsFolder, vk::PipelineLayout layout,
		std::function<vk::RenderPass()> renderPass)
	{
		Context = &context;
		Bindless = &bindless;
		Programs = &programs;
		AssetsFolder = std::move(assetsFolder);
		Layout = layout;
		RenderPass = std::move(renderPass);
		RebuildMissingPipeline();

		if (std::filesystem::is_directory(AssetsFolder) && !Watcher.Watch(AssetsFolder))
		{
			LOG_WARNING("Saved material files won't be reloaded: {}", Watcher.GetProblem());
		}
	}

	void VulkanMaterials::Shutdown()
	{
		if (Context)
		{
			Context->WaitIdle();
		}
		Materials.clear();
		Shaders.clear();
		Textures.clear();
		Saved.clear();
		MissingPipeline = nullptr;
		Context = nullptr;
	}

	void VulkanMaterials::RebuildMissingPipeline()
	{
		const std::string vertex = "missingVertex";
		const std::string fragment = "missingFragment";
		vk::raii::Pipeline pipeline = MakePipeline(CompiledEntryPoint{ vertex, EShaderStage::Vertex, Programs->GetSpirv(MissingProgram, vertex) },
			CompiledEntryPoint{ fragment, EShaderStage::Fragment, Programs->GetSpirv(MissingProgram, fragment) });
		// The handles point at this one object, so it is replaced in place
		Context->WaitIdle();
		MissingPipeline = std::move(pipeline);
	}

	Nyx::Material* VulkanMaterials::Get(const std::string& path)
	{
		const std::string key = PathKey(path);
		return GetEntry(path).Handle.get();
	}

	VulkanMaterials::Entry& VulkanMaterials::GetEntry(const std::string& path)
	{
		const std::string key = PathKey(path);
		auto found = Materials.find(key);
		if (found == Materials.end())
		{
			auto entry = std::make_unique<Entry>();
			entry->Path = path;
			entry->File = (AssetsFolder / path).make_preferred();
			entry->Handle = std::make_unique<Nyx::Material>();
			// In the map before its parents are looked for, so a chain that comes back finds it
			found = Materials.emplace(key, std::move(entry)).first;
			LoadAsset(*found->second);
			WriteBytes(*found->second);
		}
		return *found->second;
	}

	bool VulkanMaterials::Resolve(Entry& entry)
	{
		const auto fail = [&entry](std::string problem)
		{
			if (problem != entry.ChainProblem)
			{
				LOG_ERROR("Material {}: {}; it draws as the missing material", entry.Path, problem);
				entry.ChainProblem = std::move(problem);
			}
			entry.UsedShader = nullptr;
			return false;
		};

		// Up the chain of parents, nearest first, to the one with a shader
		entry.Parents.clear();
		const Entry* last = &entry;
		while (true)
		{
			const std::string parent = last->Asset.GetParent();
			if (parent.empty())
			{
				break;
			}
			Entry& next = GetEntry(parent);
			if (&next == &entry || std::find(entry.Parents.begin(), entry.Parents.end(), &next) != entry.Parents.end())
			{
				return fail("its parents go round, back to " + next.Path);
			}
			if (!next.bLoaded)
			{
				return fail("its parent " + next.Path + " can't be read");
			}
			entry.Parents.push_back(&next);
			last = &next;
		}
		const std::string shader = last->Asset.GetShader();
		if (shader.empty())
		{
			return fail(entry.Parents.empty() ? std::string("it names no shader (Shader = Shaders/...slang) and no parent (Parent = Materials/...nyxmat)")
											  : "its parents end in " + last->Path + ", which names no shader");
		}

		// The farthest parent's values first, each nearer one's over them, the material's own last
		entry.Inherited = MaterialAsset{};
		if (!entry.Parents.empty())
		{
			entry.Inherited.SetShader(shader);
			for (auto parent = entry.Parents.rbegin(); parent != entry.Parents.rend(); ++parent)
			{
				for (const auto& [name, value] : (*parent)->Asset.GetValues())
				{
					entry.Inherited.SetValue(name, value);
				}
			}
		}
		entry.Resolved = entry.Inherited;
		entry.Resolved.SetShader(shader);
		for (const auto& [name, value] : entry.Asset.GetValues())
		{
			entry.Resolved.SetValue(name, value);
		}
		entry.UsedShader = FindShader(shader);
		entry.ChainProblem.clear();
		return true;
	}

	void VulkanMaterials::WriteInstancesOf(const Entry& changed)
	{
		for (auto& [key, entry] : Materials)
		{
			if (std::find(entry->Parents.begin(), entry->Parents.end(), &changed) != entry->Parents.end())
			{
				WriteBytes(*entry);
			}
		}
	}

	VulkanMaterials::View VulkanMaterials::Inspect(const std::string& path)
	{
		const Entry& entry = GetEntry(path);
		View view;
		view.Asset = &entry.Asset;
		view.Inherited = &entry.Inherited;
		view.Shader = entry.Resolved.GetShader();
		view.FileRevision = entry.FileRevision;
		if (entry.UsedShader && entry.UsedShader->bReady)
		{
			view.Layout = &entry.UsedShader->Layout;
			view.Defaults = entry.UsedShader->Defaults;
		}
		return view;
	}

	void VulkanMaterials::SetValues(const std::string& path, const MaterialAsset& asset)
	{
		Entry& entry = GetEntry(path);
		entry.Asset = asset;
		entry.bLoaded = true;
		WriteBytes(entry);
		WriteInstancesOf(entry);
	}

	bool VulkanMaterials::IsWaiting() const
	{
		return std::any_of(Shaders.begin(), Shaders.end(), [this](const auto& entry)
			{
				const Shader& shader = *entry.second;
				return (!shader.bReady || shader.bChanged) && (Programs->IsPending(shader.MaterialRequest) || Programs->IsPending(shader.DefaultsRequest));
			});
	}

	bool VulkanMaterials::LoadAsset(Entry& entry)
	{
		MaterialAsset asset;
		std::string error;
		if (!asset.Load(entry.File, error))
		{
			if (entry.bLoaded)
			{
				LOG_ERROR("Material {}: {}; it keeps its last values", entry.Path, error);
			}
			else
			{
				LOG_ERROR("Material {}: {}; it draws as the missing material", entry.Path, error);
			}
			return false;
		}
		entry.Asset = std::move(asset);
		entry.bLoaded = true;
		++entry.FileRevision;
		// Its shader, its own or its parents', when its bytes are written (Resolve())
		return true;
	}

	VulkanMaterials::Shader* VulkanMaterials::FindShader(const std::string& path)
	{
		const std::string key = PathKey(path);
		auto found = Shaders.find(key);
		if (found == Shaders.end())
		{
			auto shader = std::make_unique<Shader>();
			shader->Path = path;
			shader->MaterialRequest.Kind = EShaderCompileKind::Material;
			shader->MaterialRequest.File = (AssetsFolder / path).make_preferred();
			shader->MaterialRequest.Domain = ShaderDomain::Surface();
			shader->DefaultsRequest.Kind = EShaderCompileKind::DefaultsWriter;
			shader->DefaultsRequest.File = (AssetsFolder / path).make_preferred();
			found = Shaders.emplace(key, std::move(shader)).first;
		}
		return found->second.get();
	}

	bool VulkanMaterials::Prepare(Shader& shader)
	{
		if (shader.bReady && !shader.bChanged)
		{
			return true;
		}
		// The two compiles come back one after the other: a new layout waits for its defaults
		const ShaderCompileResult* material = Programs->GetResult(shader.MaterialRequest);
		const ShaderCompileResult* defaults = Programs->GetResult(shader.DefaultsRequest);
		if (shader.bReady && (Programs->IsPending(shader.MaterialRequest) || Programs->IsPending(shader.DefaultsRequest)))
		{
			return true;
		}
		if (!material || !defaults || !material->MaterialLayout)
		{
			return shader.bReady;
		}

		const CompiledEntryPoint* vertex = FindEntryPoint(*material, "surfaceVertex");
		const CompiledEntryPoint* fragment = FindEntryPoint(*material, "surfaceFragment");
		const CompiledEntryPoint* writer = FindEntryPoint(*defaults, "writeDefaults");
		if (!vertex || !fragment || !writer)
		{
			LOG_ERROR("Material shader {} lacks an entry point of its domain", shader.Path);
			return shader.bReady;
		}

		try
		{
			vk::raii::Pipeline pipeline = MakePipeline(*vertex, *fragment);
			std::vector<std::byte> bytes = RunDefaultsWriter(*writer, material->MaterialLayout->Size);
			Context->WaitIdle();
			shader.Pipeline = std::move(pipeline);
			shader.Defaults = std::move(bytes);
			shader.Layout = *material->MaterialLayout;
			shader.bReady = true;
			shader.bChanged = false;
		}
		catch (const std::exception& error)
		{
			LOG_ERROR("Material shader {} can't be drawn with: {}", shader.Path, error.what());
			shader.bChanged = false;
		}
		return shader.bReady;
	}

	void VulkanMaterials::WriteBytes(Entry& entry)
	{
		Nyx::Material& handle = *entry.Handle;
		if (!entry.bLoaded || !Resolve(entry) || !Prepare(*entry.UsedShader))
		{
			handle.Pipeline = &MissingPipeline;
			handle.MaterialOffset = 0;
			return;
		}
		Shader& shader = *entry.UsedShader;

		std::vector<std::byte> bytes = shader.Defaults;
		bytes.resize(shader.Layout.Size);
		for (const MaterialValueProblem& problem : WriteMaterialValues(shader.Layout, entry.Resolved, bytes, [this](const std::string& path, bool bLinear)
				 {
					 return TextureSlot(path, bLinear);
				 }))
		{
			LOG_WARNING("Material {}, {}: {}", entry.Path, problem.Name, problem.Message);
		}

		// Room of the size the layout has now; a new size moves the material
		if (!entry.Offset || entry.Size != shader.Layout.Size)
		{
			if (entry.Offset)
			{
				Bindless->FreeMaterial(*entry.Offset, entry.Size);
			}
			entry.Offset = Bindless->AllocateMaterial(std::max<uint32_t>(shader.Layout.Size, 4));
			entry.Size = std::max<uint32_t>(shader.Layout.Size, 4);
			if (!entry.Offset)
			{
				LOG_ERROR("Material {}: the material buffer is full; it draws as the missing material", entry.Path);
				handle.Pipeline = &MissingPipeline;
				handle.MaterialOffset = 0;
				return;
			}
		}
		Bindless->WriteMaterial(*entry.Offset, bytes);
		handle.Pipeline = &shader.Pipeline;
		handle.MaterialOffset = *entry.Offset;
	}

	std::optional<uint32_t> VulkanMaterials::TextureSlot(const std::string& path, bool bLinear)
	{
		const std::string key = PathKey(path) + (bLinear ? "|linear" : "");
		const auto found = Textures.find(key);
		if (found != Textures.end())
		{
			return found->second;
		}

		RgbaImage image;
		std::string error;
		std::optional<uint32_t> slot;
		if (LoadRgbaImage((AssetsFolder / path).make_preferred(), image, error))
		{
			// Colours are stored sRGB-encoded and decoded when sampled; data ([Linear]: normals,
			// roughness, occlusion) is read as stored. With mips, so that far away they don't shimmer.
			slot = Bindless->CreateTexture(image.Width, image.Height, image.Pixels, bLinear ? vk::Format::eR8G8B8A8Unorm : vk::Format::eR8G8B8A8Srgb, true);
		}
		else
		{
			LOG_WARNING("Texture {} can't be loaded: {}", path, error);
		}
		Textures.emplace(key, slot);
		return slot;
	}

	void VulkanMaterials::Update(const std::vector<ShaderProgramUpdate>& updates)
	{
		// Shaders that compiled
		for (const ShaderProgramUpdate& update : updates)
		{
			if (!update.bChanged || (update.Request.Kind != EShaderCompileKind::Material && update.Request.Kind != EShaderCompileKind::DefaultsWriter))
			{
				continue;
			}
			for (auto& [key, shader] : Shaders)
			{
				if (ShaderPrograms::GetResultKey(shader->MaterialRequest) == update.Name || ShaderPrograms::GetResultKey(shader->DefaultsRequest) == update.Name)
				{
					shader->bChanged = true;
				}
			}
		}
		for (auto& [key, shader] : Shaders)
		{
			if (!shader->bChanged)
			{
				continue;
			}
			Prepare(*shader);
			if (shader->bChanged)
			{
				continue; // Its other compile is still on the way
			}
			for (auto& [path, entry] : Materials)
			{
				if (entry->UsedShader == shader.get())
				{
					WriteBytes(*entry);
				}
			}
		}

		// Material files saved, once their writes settled
		const std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now();
		for (const std::filesystem::path& changed : Watcher.TakeChanges())
		{
			const std::string changedKey = PathKey(changed);
			for (auto& [path, entry] : Materials)
			{
				const std::string fileKey = PathKey(entry->File);
				const bool bConcerned = fileKey == changedKey || (fileKey.size() > changedKey.size() && fileKey.compare(0, changedKey.size(), changedKey) == 0 && fileKey[changedKey.size()] == '/');
				if (bConcerned && std::find(Saved.begin(), Saved.end(), entry.get()) == Saved.end())
				{
					Saved.push_back(entry.get());
					LastSave = now;
				}
			}
		}
		if (!Saved.empty() && now - LastSave >= ShaderPrograms::SettleTime)
		{
			for (Entry* entry : Saved)
			{
				if (LoadAsset(*entry))
				{
					LOG_INFO("Material {} reloaded", entry->Path);
				}
				WriteBytes(*entry);
				WriteInstancesOf(*entry);
			}
			Saved.clear();
		}
	}

	vk::raii::Pipeline VulkanMaterials::MakePipeline(const CompiledEntryPoint& vertex, const CompiledEntryPoint& fragment)
	{
		vk::raii::Device& device = Context->GetDevice();
		vk::raii::ShaderModule vertexModule(device, vk::ShaderModuleCreateInfo({}, vertex.Spirv.size() * sizeof(uint32_t), vertex.Spirv.data()));
		vk::raii::ShaderModule fragmentModule(device, vk::ShaderModuleCreateInfo({}, fragment.Spirv.size() * sizeof(uint32_t), fragment.Spirv.data()));
		const std::array<vk::PipelineShaderStageCreateInfo, 2> stages = {
			vk::PipelineShaderStageCreateInfo({}, vk::ShaderStageFlagBits::eVertex, *vertexModule, vertex.Name.c_str()),
			vk::PipelineShaderStageCreateInfo({}, vk::ShaderStageFlagBits::eFragment, *fragmentModule, fragment.Name.c_str()),
		};

		// The mesh's vertices, those parts the vertex shader reads (the colour, for one: SurfaceInput
		// has none)
		const auto bindingDescription = Nyx::Vertex::GetBindingDescription();
		const std::vector<uint32_t> read = InputLocations(vertex.Spirv);
		std::vector<vk::VertexInputAttributeDescription> attributeDescriptions;
		for (const vk::VertexInputAttributeDescription& attribute : Nyx::Vertex::GetAttributeDescriptions())
		{
			if (std::find(read.begin(), read.end(), attribute.location) != read.end())
			{
				attributeDescriptions.push_back(attribute);
			}
		}
		vk::PipelineVertexInputStateCreateInfo vertexInput{};
		vertexInput.vertexBindingDescriptionCount = 1;
		vertexInput.pVertexBindingDescriptions = &bindingDescription;
		vertexInput.vertexAttributeDescriptionCount = static_cast<uint32_t>(attributeDescriptions.size());
		vertexInput.pVertexAttributeDescriptions = attributeDescriptions.data();

		vk::PipelineInputAssemblyStateCreateInfo inputAssembly({}, vk::PrimitiveTopology::eTriangleList);
		vk::PipelineViewportStateCreateInfo viewportState{};
		viewportState.viewportCount = 1;
		viewportState.scissorCount = 1;

		vk::PipelineRasterizationStateCreateInfo rasterizer{};
		rasterizer.polygonMode = vk::PolygonMode::eFill;
		rasterizer.lineWidth = 1.0f;
		rasterizer.cullMode = vk::CullModeFlagBits::eNone; // @todo: back faces, once meshes agree on their winding
		rasterizer.frontFace = vk::FrontFace::eClockwise;

		vk::PipelineMultisampleStateCreateInfo multisampling{};
		multisampling.rasterizationSamples = vk::SampleCountFlagBits::e1;

		vk::PipelineColorBlendAttachmentState colorBlendAttachment{};
		colorBlendAttachment.colorWriteMask = vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG | vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA;
		vk::PipelineColorBlendStateCreateInfo colorBlending{};
		colorBlending.attachmentCount = 1;
		colorBlending.pAttachments = &colorBlendAttachment;

		const std::array<vk::DynamicState, 2> dynamicStates = { vk::DynamicState::eViewport, vk::DynamicState::eScissor };
		vk::PipelineDynamicStateCreateInfo dynamicState({}, dynamicStates);

		vk::PipelineDepthStencilStateCreateInfo depthStencil{};
		depthStencil.depthTestEnable = VK_TRUE;
		depthStencil.depthWriteEnable = VK_TRUE;
		depthStencil.depthCompareOp = vk::CompareOp::eLess;

		vk::GraphicsPipelineCreateInfo pipelineInfo{};
		pipelineInfo.stageCount = static_cast<uint32_t>(stages.size());
		pipelineInfo.pStages = stages.data();
		pipelineInfo.pVertexInputState = &vertexInput;
		pipelineInfo.pInputAssemblyState = &inputAssembly;
		pipelineInfo.pViewportState = &viewportState;
		pipelineInfo.pRasterizationState = &rasterizer;
		pipelineInfo.pMultisampleState = &multisampling;
		pipelineInfo.pDepthStencilState = &depthStencil;
		pipelineInfo.pColorBlendState = &colorBlending;
		pipelineInfo.pDynamicState = &dynamicState;
		pipelineInfo.layout = Layout;
		pipelineInfo.renderPass = RenderPass();
		pipelineInfo.subpass = 0;
		return vk::raii::Pipeline(device, Context->GetPipelineCache(), pipelineInfo);
	}

	std::vector<std::byte> VulkanMaterials::RunDefaultsWriter(const CompiledEntryPoint& writer, uint32_t size)
	{
		vk::raii::Device& device = Context->GetDevice();
		const vk::DeviceSize byteCount = std::max<vk::DeviceSize>((size + 3) / 4 * 4, 4);

		// The writer stores at set 0, binding 3 (ShaderCompiler::CompileDefaultsWriter())
		vk::BufferCreateInfo bufferInfo{};
		bufferInfo.size = byteCount;
		bufferInfo.usage = vk::BufferUsageFlagBits::eStorageBuffer;
		vk::raii::Buffer output(device, bufferInfo);
		const vk::MemoryRequirements requirements = output.getMemoryRequirements();
		vk::raii::DeviceMemory outputMemory(device, vk::MemoryAllocateInfo(requirements.size, FindMemoryType(*Context->GetPhysicalDevice(), requirements.memoryTypeBits, vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent)));
		output.bindMemory(*outputMemory, 0);

		const vk::DescriptorSetLayoutBinding binding(3, vk::DescriptorType::eStorageBuffer, 1, vk::ShaderStageFlagBits::eCompute);
		vk::raii::DescriptorSetLayout setLayout(device, vk::DescriptorSetLayoutCreateInfo({}, binding));
		vk::raii::PipelineLayout pipelineLayout(device, vk::PipelineLayoutCreateInfo({}, *setLayout));
		vk::raii::ShaderModule module(device, vk::ShaderModuleCreateInfo({}, writer.Spirv.size() * sizeof(uint32_t), writer.Spirv.data()));
		vk::ComputePipelineCreateInfo pipelineInfo{};
		pipelineInfo.stage = vk::PipelineShaderStageCreateInfo({}, vk::ShaderStageFlagBits::eCompute, *module, writer.Name.c_str());
		pipelineInfo.layout = *pipelineLayout;
		vk::raii::Pipeline pipeline(device, Context->GetPipelineCache(), pipelineInfo);

		const vk::DescriptorPoolSize poolSize(vk::DescriptorType::eStorageBuffer, 1);
		vk::raii::DescriptorPool pool(device, vk::DescriptorPoolCreateInfo(vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet, 1, poolSize));
		vk::raii::DescriptorSet set = std::move(vk::raii::DescriptorSets(device, vk::DescriptorSetAllocateInfo(*pool, *setLayout)).front());
		const vk::DescriptorBufferInfo outputInfo(*output, 0, VK_WHOLE_SIZE);
		device.updateDescriptorSets(vk::WriteDescriptorSet(*set, 3, 0, vk::DescriptorType::eStorageBuffer, {}, outputInfo), {});

		vk::raii::CommandBuffer commands = std::move(vk::raii::CommandBuffers(device, vk::CommandBufferAllocateInfo(*Context->GetGraphicsCommandPool(), vk::CommandBufferLevel::ePrimary, 1)).front());
		commands.begin(vk::CommandBufferBeginInfo(vk::CommandBufferUsageFlagBits::eOneTimeSubmit));
		commands.bindPipeline(vk::PipelineBindPoint::eCompute, *pipeline);
		commands.bindDescriptorSets(vk::PipelineBindPoint::eCompute, *pipelineLayout, 0, *set, {});
		commands.dispatch(1, 1, 1);
		const vk::MemoryBarrier toHost(vk::AccessFlagBits::eShaderWrite, vk::AccessFlagBits::eHostRead);
		commands.pipelineBarrier(vk::PipelineStageFlagBits::eComputeShader, vk::PipelineStageFlagBits::eHost, {}, toHost, {}, {});
		commands.end();

		vk::SubmitInfo submitInfo{};
		submitInfo.commandBufferCount = 1;
		submitInfo.pCommandBuffers = &*commands;
		Context->GetGraphicsQueue().submit(submitInfo);
		Context->GetGraphicsQueue().waitIdle();

		std::vector<std::byte> bytes(size);
		std::memcpy(bytes.data(), outputMemory.mapMemory(0, byteCount), size);
		outputMemory.unmapMemory();
		return bytes;
	}
}
