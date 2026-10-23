#include "NyxPCH.h"
#include "SampleGround.h"

#include "RuntimeMeshComponent.h"

#include <glm/gtc/constants.hpp>

#include <algorithm>
#include <cmath>
#include <memory>

namespace Nyx
{
	namespace
	{
		// The ground's height and how fast it rises along x and z
		struct GroundPoint
		{
			float Height = 0.0f;
			float AlongX = 0.0f;
			float AlongZ = 0.0f;
		};

		// Bumps in a grid, and a long wave across them, so the ground doesn't look tiled
		GroundPoint Sample(const SampleGroundComponent& ground, float x, float z)
		{
			const float k = glm::two_pi<float>() / std::max(ground.BumpSpacing, 0.01f);
			const float wave = 0.5f * k * (x + z) + 1.0f;
			return GroundPoint{
				.Height = ground.BumpHeight * (0.7f * std::sin(k * x) * std::sin(k * z) + 0.3f * std::sin(wave)),
				.AlongX = ground.BumpHeight * (0.7f * k * std::cos(k * x) * std::sin(k * z) + 0.15f * k * std::cos(wave)),
				.AlongZ = ground.BumpHeight * (0.7f * k * std::sin(k * x) * std::cos(k * z) + 0.15f * k * std::cos(wave)),
			};
		}

		bool SameValues(const SampleGroundComponent& a, const SampleGroundComponent& b)
		{
			return a.Size == b.Size && a.Cells == b.Cells && a.BumpHeight == b.BumpHeight && a.BumpSpacing == b.BumpSpacing;
		}
	}

	float GetSampleGroundHeight(const SampleGroundComponent& ground, float x, float z)
	{
		return Sample(ground, x, z).Height;
	}

	MeshData MakeSampleGroundMesh(const SampleGroundComponent& ground)
	{
		const uint32_t cells = std::clamp(ground.Cells, 1u, 512u);
		const float size = std::max(ground.Size, 0.01f);
		MeshData mesh;
		mesh.Vertices.reserve(static_cast<size_t>(cells + 1) * (cells + 1));
		for (uint32_t row = 0; row <= cells; ++row)
		{
			const float z = (static_cast<float>(row) / static_cast<float>(cells) - 0.5f) * size;
			for (uint32_t column = 0; column <= cells; ++column)
			{
				const float x = (static_cast<float>(column) / static_cast<float>(cells) - 0.5f) * size;
				const GroundPoint point = Sample(ground, x, z);
				const glm::vec3 normal = glm::normalize(glm::vec3(-point.AlongX, 1.0f, -point.AlongZ));
				// Along rising u, which is x; v rises with z, so the bitangent, up the image, points to -z
				const glm::vec3 tangent = glm::normalize(glm::vec3(1.0f, point.AlongX, 0.0f));
				mesh.Vertices.push_back(Vertex{ .Position = glm::vec3(x, point.Height, z),
					.UV = glm::vec2(x, z) * 0.25f,
					.Normal = normal,
					.Tangent = glm::vec4(glm::normalize(tangent - normal * glm::dot(normal, tangent)), 1.0f) });
			}
		}

		// Counter-clockwise from above
		mesh.Indices.reserve(static_cast<size_t>(cells) * cells * 6);
		for (uint32_t row = 0; row < cells; ++row)
		{
			for (uint32_t column = 0; column < cells; ++column)
			{
				const uint32_t nearLeft = (row + 1) * (cells + 1) + column;
				const uint32_t farLeft = row * (cells + 1) + column;
				mesh.Indices.insert(mesh.Indices.end(), { nearLeft, nearLeft + 1, farLeft + 1, farLeft + 1, farLeft, nearLeft });
			}
		}
		return mesh;
	}

	void SampleGroundSystem::Step(const SimContext& context)
	{
		Registry& world = context.World;
		world.Each<SampleGroundComponent>([&](Entity entity, SampleGroundComponent& ground)
			{
				const auto made = Made.find(entity.Value);
				if (world.Has<RuntimeMeshComponent>(entity) && made != Made.end() && SameValues(made->second, ground))
				{
					return;
				}
				RuntimeMeshComponent& runtimeMesh = world.Has<RuntimeMeshComponent>(entity) ? world.Get<RuntimeMeshComponent>(entity)
																							 : world.Add<RuntimeMeshComponent>(entity);
				SetRuntimeMesh(runtimeMesh, std::make_shared<MeshData>(MakeSampleGroundMesh(ground)));
				Made[entity.Value] = ground;
			});

		// Grounds gone
		std::erase_if(Made, [&world](const auto& made)
			{
				const Entity entity{ made.first };
				return !world.IsAlive(entity) || !world.Has<SampleGroundComponent>(entity);
			});
	}
}
