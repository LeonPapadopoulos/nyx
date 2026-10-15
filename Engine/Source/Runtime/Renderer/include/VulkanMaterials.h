#pragma once

#include "FileWatcher.h"
#include "MaterialAsset.h"
#include "Material.h"
#include "ShaderPrograms.h"

#include <vulkan/vulkan_raii.hpp>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace Nyx
{
	class VulkanContext;
}

namespace Nyx::Engine
{
	class VulkanBindless;

	// Materials on the GPU (Design/shader-materials.md, stage 1, change 6). A material is a
	// .nyxmat file (MaterialAsset) in the assets folder: its values are bytes in the material
	// buffer, laid out by its shader, and it draws with its shader's pipeline. The renderer draws a
	// Nyx::Material, a handle these keep up to date:
	//   - a material whose shader hasn't compiled yet, or whose file can't be read, draws as a
	//     magenta checker (MissingMaterial.slang);
	//   - saving a material file writes its new values in the next frame;
	//   - saving its shader compiles it again (ShaderPrograms); pipeline and bytes follow, values
	//     carried over by name. A shader that stops compiling keeps its last good look.
	// A new material's bytes are its shader's defaults, from the defaults writer run once on the
	// GPU. Material textures are loaded from the assets folder, once each.
	//
	// A material instance (a Parent line instead of a Shader line) is drawn with its parents'
	// values under its own, and its shader is the one the chain of parents ends in. A parent's
	// new values, saved or set, reach its instances in the same update; a chain that goes round,
	// or ends without a shader, draws as the missing material.
	class VulkanMaterials
	{
	public:
		// `layout` and `renderPass()`: the scene pipelines' layout, and a render pass of the scene
		// views, asked for each time a pipeline is made
		void Initialize(Nyx::VulkanContext& context, VulkanBindless& bindless, ShaderPrograms& programs, std::filesystem::path assetsFolder, vk::PipelineLayout layout,
			std::function<vk::RenderPass()> renderPass);
		void Shutdown();

		// The material in the file at `path`, relative to the assets folder
		// ("Materials/Stone.nyxmat"), loaded at the first ask. Never null.
		Nyx::Material* Get(const std::string& path);

		// What an editor shows of a material (IRenderer::ViewMaterial())
		struct View
		{
			// Its values: as its file has them, or as SetValues() set them since
			const MaterialAsset* Asset = nullptr;
			// An instance's: the values it takes from its parents, all of them merged, and the
			// shader they end in; empty for a material on a shader of its own
			const MaterialAsset* Inherited = nullptr;
			// The shader it is drawn with: its own, or its parents'
			std::string Shader;
			// Its shader's parameters, and the bytes of their defaults; null and empty until the
			// shader first compiled
			const ShaderLayout* Layout = nullptr;
			std::span<const std::byte> Defaults;
			// Changes each time the file is read, e.g. after a save; 0 while it couldn't be
			uint64_t FileRevision = 0;
		};
		// Loads the material like Get(). Valid until the next Update() or SetValues().
		View Inspect(const std::string& path);

		// Draws the material with these values, as if its file said them; the file stays as it is.
		// Call while no frame is in flight. Reading the file again (after it is saved) takes the
		// file's values.
		void SetValues(const std::string& path, const MaterialAsset& asset);

		// Once per frame, while no frame is in flight: takes what finished compiling (from
		// ShaderPrograms::Update()) and the material files saved since, and makes again what they
		// change
		void Update(const std::vector<ShaderProgramUpdate>& updates);

		// MissingMaterial.slang changed
		void RebuildMissingPipeline();

		// Whether a material still waits for its shader's first compile
		bool IsWaiting() const;

	private:
		struct Shader
		{
			std::string Path;
			ShaderCompileRequest MaterialRequest;
			ShaderCompileRequest DefaultsRequest;
			vk::raii::Pipeline Pipeline{ nullptr };
			ShaderLayout Layout;
			std::vector<std::byte> Defaults;
			bool bReady = false;
			// It compiled since it was made ready: made again at the next update
			bool bChanged = false;
		};

		struct Entry
		{
			std::string Path;
			std::filesystem::path File;
			MaterialAsset Asset;
			bool bLoaded = false;
			// Counts the reads of the file (View::FileRevision)
			uint64_t FileRevision = 0;
			Shader* UsedShader = nullptr;
			// Its parents, nearest first, and what they give it (Resolve())
			std::vector<Entry*> Parents;
			MaterialAsset Inherited;
			// Its own values over the inherited ones, with the shader: what is drawn
			MaterialAsset Resolved;
			// Why the chain of parents doesn't make a material, as last logged
			std::string ChainProblem;
			// Its room in the material buffer
			std::optional<uint32_t> Offset;
			uint32_t Size = 0;
			std::unique_ptr<Nyx::Material> Handle;
		};

		// False when the file can't be read; a material read before keeps its values
		bool LoadAsset(Entry& entry);
		// The entry for `path`, loaded at the first ask
		Entry& GetEntry(const std::string& path);
		// The material's parents and what they give it, and its shader; false when the chain
		// doesn't make a material (logged once)
		bool Resolve(Entry& entry);
		// Every material with `changed` among its parents is written again
		void WriteInstancesOf(const Entry& changed);
		Shader* FindShader(const std::string& path);
		// Makes the shader's pipeline and defaults once both its compiles are in; false before
		bool Prepare(Shader& shader);
		void WriteBytes(Entry& entry);
		std::optional<uint32_t> TextureSlot(const std::string& path, bool bLinear);
		vk::raii::Pipeline MakePipeline(const CompiledEntryPoint& vertex, const CompiledEntryPoint& fragment);
		std::vector<std::byte> RunDefaultsWriter(const CompiledEntryPoint& writer, uint32_t size);

		Nyx::VulkanContext* Context = nullptr;
		VulkanBindless* Bindless = nullptr;
		ShaderPrograms* Programs = nullptr;
		std::filesystem::path AssetsFolder;
		vk::PipelineLayout Layout;
		std::function<vk::RenderPass()> RenderPass;

		vk::raii::Pipeline MissingPipeline{ nullptr };
		std::map<std::string, std::unique_ptr<Shader>> Shaders;
		std::map<std::string, std::unique_ptr<Entry>> Materials;
		// Slots of the textures loaded, by path and whether linear; nullopt for one that can't be
		std::map<std::string, std::optional<uint32_t>> Textures;

		FileWatcher Watcher;
		// Material files saved, waiting for their writes to settle
		std::vector<Entry*> Saved;
		std::chrono::steady_clock::time_point LastSave{};
	};
}
