#include "NyxPCH.h"
#include "VulkanMaterials.h"

#include "ImageFile.h"
#include "Log.h"
#include "Mesh.h"
#include "Paths.h"
#include "StringUtil.h"
#include "VulkanBindless.h"
#include "VulkanContext.h"
#include "VulkanUtil.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstddef>
#include <cstring>
#include <span>
#include <stdexcept>

namespace Nyx
{
	namespace
	{
		constexpr const char* MissingProgram = "MissingMaterial.slang";

		// Paths as Windows compares them: whole, normalised, without regard to case
		std::string PathKey(const std::filesystem::path& path)
		{
			std::string key = ToLower(path.lexically_normal().generic_string());
			while (!key.empty() && key.back() == '/')
			{
				key.pop_back();
			}
			return key;
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

	void VulkanMaterials::Initialize(Nyx::VulkanContext& context, VulkanBindless& bindless, ShaderPrograms& programs, std::vector<std::filesystem::path> assetFolders, vk::PipelineLayout layout,
		std::function<vk::RenderPass()> renderPass, std::function<vk::RenderPass()> pickingRenderPass, VertexFunctionPasses vertexPasses)
	{
		Context = &context;
		Bindless = &bindless;
		Programs = &programs;
		AssetFolders = std::move(assetFolders);
		Watchers.clear();
		Layout = layout;
		RenderPass = std::move(renderPass);
		PickingRenderPass = std::move(pickingRenderPass);
		VertexPasses = std::move(vertexPasses);
		RebuildMissingPipeline();

		for (const std::filesystem::path& folder : AssetFolders)
		{
			auto watcher = std::make_unique<FileWatcher>();
			if (std::filesystem::is_directory(folder) && !watcher->Watch(folder))
			{
				LOG_WARNING("Saved material files in {} won't be reloaded: {}", folder.string(), watcher->GetProblem());
			}
			Watchers.push_back(std::move(watcher));
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
		MissingDecalPipeline = nullptr;
		MissingDecalPickingPipeline = nullptr;
		Context = nullptr;
	}

	void VulkanMaterials::RebuildMissingPipeline()
	{
		const std::string vertex = "missingVertex";
		const std::string fragment = "missingFragment";
		const std::string pickingFragment = "missingPickingFragment";
		const CompiledEntryPoint vertexCode{ vertex, EShaderStage::Vertex, Programs->GetSpirv(MissingProgram, vertex) };
		const CompiledEntryPoint fragmentCode{ fragment, EShaderStage::Fragment, Programs->GetSpirv(MissingProgram, fragment) };
		const CompiledEntryPoint pickingFragmentCode{ pickingFragment, EShaderStage::Fragment, Programs->GetSpirv(MissingProgram, pickingFragment) };
		vk::raii::Pipeline pipeline = MakePipeline(vertexCode, fragmentCode, EPipelineKind::Opaque);
		// As a decal, the checker is opaque too: its alpha is 1
		vk::raii::Pipeline decalPipeline = MakePipeline(vertexCode, fragmentCode, EPipelineKind::Decal);
		vk::raii::Pipeline decalPickingPipeline = MakePipeline(vertexCode, pickingFragmentCode, EPipelineKind::DecalPicking);
		// The handles point at these objects, so they are replaced in place
		Context->WaitIdle();
		MissingPipeline = std::move(pipeline);
		MissingDecalPipeline = std::move(decalPipeline);
		MissingDecalPickingPipeline = std::move(decalPickingPipeline);
	}

	void VulkanMaterials::RebuildVertexFunctionPipelines()
	{
		Context->WaitIdle();
		for (auto& [key, shader] : Shaders)
		{
			if (!shader->DisplacedVertex)
			{
				continue;
			}
			try
			{
				// In place: the handles point at these objects
				shader->PickingPipeline = VertexPasses.Picking(*shader->DisplacedVertex);
				shader->SelectionMaskVisiblePipeline = VertexPasses.SelectionMaskVisible(*shader->DisplacedVertex);
				shader->SelectionMaskFullPipeline = VertexPasses.SelectionMaskFull(*shader->DisplacedVertex);
			}
			catch (const std::exception& error)
			{
				LOG_ERROR("Material shader {}: its vertex function's editor pipelines can't be made again: {}", shader->Path, error.what());
			}
		}
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
			entry->File = FindAssetFile(path);
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

		// The farthest parent's values and options first, each nearer one's over them, the
		// material's own last
		entry.Inherited = MaterialAsset{};
		entry.Options = MaterialOptions{};
		if (!entry.Parents.empty())
		{
			entry.Inherited.SetShader(shader);
			for (auto parent = entry.Parents.rbegin(); parent != entry.Parents.rend(); ++parent)
			{
				for (const auto& [name, value] : (*parent)->Asset.GetValues())
				{
					entry.Inherited.SetValue(name, value);
				}
				for (const auto& [name, value] : (*parent)->Asset.GetOptionValues())
				{
					entry.Inherited.SetValue(name, value);
				}
				ReadMaterialOptions((*parent)->Asset, entry.Options);
			}
		}
		for (const MaterialValueProblem& problem : ReadMaterialOptions(entry.Asset, entry.Options))
		{
			LOG_WARNING("Material {}, {}: {}", entry.Path, problem.Name, problem.Message);
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
			shader->MaterialRequest.File = FindAssetFile(path);
			shader->MaterialRequest.Domain = ShaderDomain::Surface();
			shader->DefaultsRequest.Kind = EShaderCompileKind::DefaultsWriter;
			shader->DefaultsRequest.File = shader->MaterialRequest.File;
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
		const CompiledEntryPoint* decalFragment = FindEntryPoint(*material, "decalFragment");
		const CompiledEntryPoint* decalPickingFragment = FindEntryPoint(*material, "decalPickingFragment");
		const CompiledEntryPoint* writer = FindEntryPoint(*defaults, "writeDefaults");
		if (!vertex || !fragment || !decalFragment || !decalPickingFragment || !writer)
		{
			LOG_ERROR("Material shader {} lacks an entry point of its domain", shader.Path);
			return shader.bReady;
		}
		// With a vertex function, both of its vertex shaders, or neither
		const CompiledEntryPoint* displacedVertex = FindEntryPoint(*material, "displacedSurfaceVertex");
		const CompiledEntryPoint* displacedShadowVertex = FindEntryPoint(*material, "displacedShadowVertex");
		if (!displacedVertex != !displacedShadowVertex)
		{
			LOG_ERROR("Material shader {} lacks one of its vertex function's entry points", shader.Path);
			return shader.bReady;
		}

		try
		{
			// The mesh moved by the vertex function; a decal lies flat on what it decals, as ever
			vk::raii::Pipeline pipeline = MakePipeline(displacedVertex ? *displacedVertex : *vertex, *fragment, EPipelineKind::Opaque);
			vk::raii::Pipeline decalPipeline = MakePipeline(*vertex, *decalFragment, EPipelineKind::Decal);
			vk::raii::Pipeline decalPickingPipeline = MakePipeline(*vertex, *decalPickingFragment, EPipelineKind::DecalPicking);
			vk::raii::Pipeline shadowPipeline{ nullptr };
			vk::raii::Pipeline pickingPipeline{ nullptr };
			vk::raii::Pipeline selectionMaskVisiblePipeline{ nullptr };
			vk::raii::Pipeline selectionMaskFullPipeline{ nullptr };
			if (displacedVertex)
			{
				shadowPipeline = VertexPasses.Shadow(*displacedShadowVertex);
				pickingPipeline = VertexPasses.Picking(*displacedVertex);
				selectionMaskVisiblePipeline = VertexPasses.SelectionMaskVisible(*displacedVertex);
				selectionMaskFullPipeline = VertexPasses.SelectionMaskFull(*displacedVertex);
			}
			std::vector<std::byte> bytes = RunDefaultsWriter(*writer, material->MaterialLayout->Size);
			Context->WaitIdle();
			shader.Pipeline = std::move(pipeline);
			shader.DecalPipeline = std::move(decalPipeline);
			shader.DecalPickingPipeline = std::move(decalPickingPipeline);
			shader.DisplacedVertex = displacedVertex ? std::optional<CompiledEntryPoint>(*displacedVertex) : std::nullopt;
			shader.DisplacedShadowVertex = displacedShadowVertex ? std::optional<CompiledEntryPoint>(*displacedShadowVertex) : std::nullopt;
			shader.ShadowPipeline = std::move(shadowPipeline);
			shader.PickingPipeline = std::move(pickingPipeline);
			shader.SelectionMaskVisiblePipeline = std::move(selectionMaskVisiblePipeline);
			shader.SelectionMaskFullPipeline = std::move(selectionMaskFullPipeline);
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
		const bool bResolved = entry.bLoaded && Resolve(entry);
		// Its options, also while it draws as the missing material
		handle.bTwoSided = bResolved && entry.Options.bTwoSided;
		handle.bCastShadows = !bResolved || entry.Options.bCastShadows;
		handle.MaxVertexOffset = bResolved ? std::max(entry.Options.MaxVertexOffset, 0.0f) : 0.0f;
		// Until it has a shader with a vertex function, the passes' own pipelines draw it
		handle.ShadowPipeline = nullptr;
		handle.PickingPipeline = nullptr;
		handle.SelectionMaskVisiblePipeline = nullptr;
		handle.SelectionMaskFullPipeline = nullptr;
		if (!bResolved || !Prepare(*entry.UsedShader))
		{
			handle.Pipeline = &MissingPipeline;
			handle.DecalPipeline = &MissingDecalPipeline;
			handle.DecalPickingPipeline = &MissingDecalPickingPipeline;
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
				handle.DecalPipeline = &MissingDecalPipeline;
				handle.DecalPickingPipeline = &MissingDecalPickingPipeline;
				handle.MaterialOffset = 0;
				return;
			}
		}
		Bindless->WriteMaterial(*entry.Offset, bytes);
		handle.Pipeline = &shader.Pipeline;
		handle.DecalPipeline = &shader.DecalPipeline;
		handle.DecalPickingPipeline = &shader.DecalPickingPipeline;
		if (shader.DisplacedVertex)
		{
			handle.ShadowPipeline = &shader.ShadowPipeline;
			handle.PickingPipeline = &shader.PickingPipeline;
			handle.SelectionMaskVisiblePipeline = &shader.SelectionMaskVisiblePipeline;
			handle.SelectionMaskFullPipeline = &shader.SelectionMaskFullPipeline;
		}
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
		if (LoadRgbaImage(FindAssetFile(path), image, error))
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
		std::vector<std::filesystem::path> changes;
		for (const std::unique_ptr<FileWatcher>& watcher : Watchers)
		{
			std::vector<std::filesystem::path> folderChanges = watcher->TakeChanges();
			changes.insert(changes.end(), folderChanges.begin(), folderChanges.end());
		}
		for (const std::filesystem::path& changed : changes)
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

	std::filesystem::path VulkanMaterials::FindAssetFile(const std::string& path) const
	{
		return (Nyx::Paths::FindAssetDirIn(AssetFolders, path) / path).make_preferred();
	}

	vk::raii::Pipeline VulkanMaterials::MakePipeline(const CompiledEntryPoint& vertex, const CompiledEntryPoint& fragment, EPipelineKind kind)
	{
		vk::raii::Device& device = Context->GetDevice();
		vk::raii::ShaderModule vertexModule(device, vk::ShaderModuleCreateInfo({}, vertex.Spirv.size() * sizeof(uint32_t), vertex.Spirv.data()));
		vk::raii::ShaderModule fragmentModule(device, vk::ShaderModuleCreateInfo({}, fragment.Spirv.size() * sizeof(uint32_t), fragment.Spirv.data()));
		const std::array<vk::PipelineShaderStageCreateInfo, 2> stages = {
			vk::PipelineShaderStageCreateInfo({}, vk::ShaderStageFlagBits::eVertex, *vertexModule, vertex.Name.c_str()),
			vk::PipelineShaderStageCreateInfo({}, vk::ShaderStageFlagBits::eFragment, *fragmentModule, fragment.Name.c_str()),
		};

		// The mesh's vertices and each instance from the second stream, those parts the vertex shader
		// reads
		const std::array<vk::VertexInputBindingDescription, 2> bindingDescriptions = { Nyx::GetVertexBindingDescription(), Nyx::GetInstanceBindingDescription() };
		const std::vector<vk::VertexInputAttributeDescription> attributeDescriptions = Nyx::GetMeshAttributesRead(vertex.Spirv);
		vk::PipelineVertexInputStateCreateInfo vertexInput{};
		vertexInput.vertexBindingDescriptionCount = static_cast<uint32_t>(bindingDescriptions.size());
		vertexInput.pVertexBindingDescriptions = bindingDescriptions.data();
		vertexInput.vertexAttributeDescriptionCount = static_cast<uint32_t>(attributeDescriptions.size());
		vertexInput.pVertexAttributeDescriptions = attributeDescriptions.data();

		vk::PipelineInputAssemblyStateCreateInfo inputAssembly({}, vk::PrimitiveTopology::eTriangleList);
		vk::PipelineViewportStateCreateInfo viewportState{};
		viewportState.viewportCount = 1;
		viewportState.scissorCount = 1;

		// A mesh's back faces aren't drawn, unless its material is two-sided: the draw says which
		// (SetMeshFaces()). A decal is a quad on what it lies on, seen from wherever.
		vk::PipelineRasterizationStateCreateInfo rasterizer{};
		rasterizer.polygonMode = vk::PolygonMode::eFill;
		rasterizer.lineWidth = 1.0f;
		rasterizer.cullMode = kind == EPipelineKind::Opaque ? vk::CullModeFlagBits::eBack : vk::CullModeFlagBits::eNone;
		rasterizer.frontFace = vk::FrontFace::eCounterClockwise;

		vk::PipelineMultisampleStateCreateInfo multisampling{};
		multisampling.rasterizationSamples = vk::SampleCountFlagBits::e1;

		// Not blended in the picking pass: its target holds ids, written as they are
		vk::PipelineColorBlendAttachmentState colorBlendAttachment{};
		colorBlendAttachment.colorWriteMask = vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG | vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA;
		if (kind == EPipelineKind::Decal)
		{
			// Premultiplied alpha (decalFragment): what is beneath is kept by one minus the decal's
			// opacity, and the decal's colour, already weighed by its opacity, and its light are
			// added. So a decal can cover what is beneath, glow on it without covering it (opacity 0),
			// or both. The alpha is kept as the scene left it.
			colorBlendAttachment.blendEnable = VK_TRUE;
			colorBlendAttachment.srcColorBlendFactor = vk::BlendFactor::eOne;
			colorBlendAttachment.dstColorBlendFactor = vk::BlendFactor::eOneMinusSrcAlpha;
			colorBlendAttachment.colorBlendOp = vk::BlendOp::eAdd;
			colorBlendAttachment.srcAlphaBlendFactor = vk::BlendFactor::eZero;
			colorBlendAttachment.dstAlphaBlendFactor = vk::BlendFactor::eOne;
			colorBlendAttachment.alphaBlendOp = vk::BlendOp::eAdd;
		}
		// The scene pass has two targets: the light, and the sky's light apart for the ambient
		// occlusion (nyx.surface), blended alike; the picking pass one
		const std::array<vk::PipelineColorBlendAttachmentState, 2> sceneBlendAttachments = { colorBlendAttachment, colorBlendAttachment };
		vk::PipelineColorBlendStateCreateInfo colorBlending{};
		colorBlending.attachmentCount = kind == EPipelineKind::DecalPicking ? 1u : static_cast<uint32_t>(sceneBlendAttachments.size());
		colorBlending.pAttachments = sceneBlendAttachments.data();

		const std::span<const vk::DynamicState> dynamicStates = kind == EPipelineKind::Opaque ? std::span<const vk::DynamicState>(MeshDynamicStates)
																							  : std::span<const vk::DynamicState>(MeshDynamicStates).first(2);
		vk::PipelineDynamicStateCreateInfo dynamicState({}, static_cast<uint32_t>(dynamicStates.size()), dynamicStates.data());

		// A decal is hidden by what is in front of it, and hides nothing itself. In the picking pass
		// it hides what is behind it: the nearest decal under a click is picked.
		vk::PipelineDepthStencilStateCreateInfo depthStencil{};
		depthStencil.depthTestEnable = VK_TRUE;
		depthStencil.depthWriteEnable = kind == EPipelineKind::Decal ? VK_FALSE : VK_TRUE;
		depthStencil.depthCompareOp = kind == EPipelineKind::Opaque ? DepthTestNearer : DepthTestNearerOrEqual;

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
		pipelineInfo.renderPass = kind == EPipelineKind::DecalPicking ? PickingRenderPass() : RenderPass();
		pipelineInfo.subpass = 0;
		return vk::raii::Pipeline(device, Context->GetPipelineCache(), pipelineInfo);
	}

	std::vector<std::byte> VulkanMaterials::RunDefaultsWriter(const CompiledEntryPoint& writer, uint32_t size)
	{
		vk::raii::Device& device = Context->GetDevice();
		const vk::DeviceSize byteCount = std::max<vk::DeviceSize>((size + 3) / 4 * 4, 4);

		// The writer stores at set 0, binding 3 (ShaderCompiler::CompileDefaultsWriter())
		vk::raii::Buffer output{ nullptr };
		vk::raii::DeviceMemory outputMemory{ nullptr };
		CreateBuffer(*Context, byteCount, vk::BufferUsageFlagBits::eStorageBuffer,
			vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent, output, outputMemory);

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
