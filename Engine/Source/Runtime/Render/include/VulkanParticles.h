#pragma once

#include "ParticleEmitterComponent.h"
#include "Particles.h"

#include <vulkan/vulkan_raii.hpp>

#include <array>
#include <cstdint>
#include <functional>
#include <span>
#include <utility>
#include <vector>

namespace Nyx
{
	class Mesh;
	class ShaderPrograms;
	class VulkanContext;
	class VulkanViewportTarget;
	struct SceneViewInstance;

	// One draw of a view's particles: an emitter's, InstanceCount from FirstInstance on in this
	// frame's particle stream, each a billboard or a mesh, blended its way
	struct SceneViewParticleDraw
	{
		uint32_t FirstInstance = 0;
		uint32_t InstanceCount = 0;
		EParticleBlend Blend = EParticleBlend::Additive;
		EParticleShape Shape = EParticleShape::Billboard;
		// For the Mesh shape
		const Mesh* MeshAsset = nullptr;
		float SoftDistance = 0.0f;
		bool bLit = false;
	};

	// A view's particles: what it draws this frame, the alpha blended back to front and the
	// additive after them, and the pass's framebuffer over its HDR scene, made for its size
	struct SceneViewParticles
	{
		std::vector<SceneViewParticleDraw> Draws;
		vk::Extent2D Extent{ 0, 0 };
		vk::raii::Framebuffer Framebuffer{ nullptr };
	};

	// Particles on the GPU (Docs/Design/lights-and-particles.md): each frame's instances of every
	// view in one vertex stream, and per view one pass after its ambient occlusion and before its
	// bloom, over its HDR scene with no depth buffer: Particle.slang tests each pixel against the
	// scene's depth, which it reads, and fades it in front of what is behind it. Four pipelines:
	// billboards and meshes, each additive or alpha blended, with the scene pipelines' layout.
	class VulkanParticles
	{
	public:
		using BindSceneSetsFunction = std::function<void(SceneViewInstance& view, vk::raii::CommandBuffer& cmd)>;

		// Both must outlive it
		VulkanParticles(VulkanContext& context, ShaderPrograms& programs);

		// Its render pass and pipelines; drawn with the scene pipelines' layout. Both must outlive it.
		void Initialize(vk::PipelineLayout scenePipelineLayout, BindSceneSetsFunction bindSceneSets);
		void Shutdown();
		// Once, and again when Particle.slang changed
		void CreatePipelines();

		// While no frame is in flight: the view's framebuffer for its size, made again when it
		// changed or `bTargetsRemade` (the view's own targets were made again)
		void Prepare(SceneViewParticles& particles, const VulkanViewportTarget& target, bool bTargetsRemade);
		void Release(SceneViewParticles& particles);

		// This frame's particles of every view, into the stream, made bigger when they don't fit;
		// while no frame reads it (one frame in flight)
		void Upload(std::span<const ParticleInstanceData> instances);
		// After the last frame's fence: the streams it outgrew
		void ReleaseRetired();

		// After the view's ambient occlusion, outside a render pass: its draws over its HDR scene,
		// which stays in ShaderReadOnlyOptimal layout; its depth read, and back in
		// DepthStencilAttachmentOptimal. Nothing for a view without draws.
		void Draw(SceneViewInstance& view, vk::raii::CommandBuffer& cmd);

	private:
		VulkanContext& Context;
		ShaderPrograms& Programs;
		vk::PipelineLayout ScenePipelineLayout;
		BindSceneSetsFunction BindSceneSets;

		vk::raii::RenderPass Pass{ nullptr };
		// By shape, then by blend
		std::array<std::array<vk::raii::Pipeline, 2>, 2> Pipelines{ { { vk::raii::Pipeline{ nullptr }, vk::raii::Pipeline{ nullptr } },
			{ vk::raii::Pipeline{ nullptr }, vk::raii::Pipeline{ nullptr } } } };

		vk::raii::Buffer Stream{ nullptr };
		vk::raii::DeviceMemory StreamMemory{ nullptr };
		size_t StreamCapacity = 0;
		std::vector<std::pair<vk::raii::Buffer, vk::raii::DeviceMemory>> RetiredStreams;
	};
}
