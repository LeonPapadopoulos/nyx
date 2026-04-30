#include "NyxPCH.h"
#include "GltfImporter.h"

#include "cgltf.h"

#include <glm/gtc/type_ptr.hpp>
#include <filesystem>
#include <unordered_map>

namespace
{
	const cgltf_attribute* FindAttribute(const cgltf_primitive& primitive, cgltf_attribute_type type)
	{
		for (cgltf_size i = 0; i < primitive.attributes_count; ++i)
		{
			if (primitive.attributes[i].type == type)
			{
				return &primitive.attributes[i];
			}
		}
		return nullptr;
	}

	glm::mat4 Mat4FromCglTF(const float* m)
	{
		// cgltf gives column-major floats compatible with glm::make_mat4
		return glm::make_mat4(m);
	}

	std::string ResolveImagePath(const std::filesystem::path& gltfPath, const cgltf_image* image)
	{
		if (!image || !image->uri)
		{
			return {};
		}

		return (gltfPath.parent_path() / image->uri).string();
	}
}

namespace Nyx
{
	bool LoadStaticGltfScene(const std::string& filePath, ImportedScene& outScene)
	{
		outScene = {};

		cgltf_options options{};
		cgltf_data* data = nullptr;

		cgltf_result result = cgltf_parse_file(&options, filePath.c_str(), &data);
		if (result != cgltf_result_success || !data)
		{
			return false;
		}

		auto freeData = [&]()
			{
				if (data)
				{
					cgltf_free(data);
					data = nullptr;
				}
			};

		result = cgltf_load_buffers(&options, data, filePath.c_str());
		if (result != cgltf_result_success)
		{
			freeData();
			return false;
		}

		result = cgltf_validate(data);
		if (result != cgltf_result_success)
		{
			freeData();
			return false;
		}

		// Materials
		for (cgltf_size i = 0; i < data->materials_count; ++i)
		{
			const cgltf_material& srcMat = data->materials[i];

			ImportedMaterial dstMat{};

			if (srcMat.has_pbr_metallic_roughness)
			{
				const cgltf_pbr_metallic_roughness& pbr = srcMat.pbr_metallic_roughness;
				dstMat.BaseColorFactor = glm::vec4(
					pbr.base_color_factor[0],
					pbr.base_color_factor[1],
					pbr.base_color_factor[2],
					pbr.base_color_factor[3]
				);

				if (pbr.base_color_texture.texture &&
					pbr.base_color_texture.texture->image)
				{
					dstMat.BaseColorTexturePath =
						ResolveImagePath(std::filesystem::path(filePath), pbr.base_color_texture.texture->image);
				}
			}

			outScene.Materials.push_back(std::move(dstMat));
		}

		// Build primitive table so nodes can reference imported primitives by index
		std::unordered_map<const cgltf_primitive*, int> primitiveToImportedIndex;

		for (cgltf_size meshIndex = 0; meshIndex < data->meshes_count; ++meshIndex)
		{
			const cgltf_mesh& mesh = data->meshes[meshIndex];

			for (cgltf_size primIndex = 0; primIndex < mesh.primitives_count; ++primIndex)
			{
				const cgltf_primitive& primitive = mesh.primitives[primIndex];

				if (primitive.type != cgltf_primitive_type_triangles)
				{
					continue;
				}

				const cgltf_attribute* posAttr = FindAttribute(primitive, cgltf_attribute_type_position);
				if (!posAttr || !posAttr->data)
				{
					continue;
				}

				const cgltf_attribute* normalAttr = FindAttribute(primitive, cgltf_attribute_type_normal);
				const cgltf_attribute* uvAttr = FindAttribute(primitive, cgltf_attribute_type_texcoord);

				const cgltf_accessor* posAccessor = posAttr->data;
				const cgltf_accessor* normalAccessor = normalAttr ? normalAttr->data : nullptr;
				const cgltf_accessor* uvAccessor = uvAttr ? uvAttr->data : nullptr;

				const cgltf_size vertexCount = posAccessor->count;

				ImportedPrimitive imported{};
				imported.Mesh.Vertices.resize(static_cast<size_t>(vertexCount));

				for (cgltf_size v = 0; v < vertexCount; ++v)
				{
					float p[3]{};
					float n[3]{ 0.0f, 1.0f, 0.0f };
					float uv[2]{ 0.0f, 0.0f };

					cgltf_accessor_read_float(posAccessor, v, p, 3);

					if (normalAccessor)
					{
						cgltf_accessor_read_float(normalAccessor, v, n, 3);
					}

					if (uvAccessor)
					{
						cgltf_accessor_read_float(uvAccessor, v, uv, 2);
					}

					Vertex vert{};
					vert.Position = glm::vec3(p[0], p[1], p[2]);
					vert.Normal = glm::vec3(n[0], n[1], n[2]);
					vert.UV = glm::vec2(uv[0], uv[1]);
					vert.Color = glm::vec3(1.0f); // default white
					imported.Mesh.Vertices[static_cast<size_t>(v)] = vert;
				}

				if (primitive.indices)
				{
					const cgltf_accessor* indexAccessor = primitive.indices;
					imported.Mesh.Indices.resize(static_cast<size_t>(indexAccessor->count));

					for (cgltf_size i = 0; i < indexAccessor->count; ++i)
					{
						imported.Mesh.Indices[static_cast<size_t>(i)] =
							static_cast<uint32_t>(cgltf_accessor_read_index(indexAccessor, i));
					}
				}
				else
				{
					imported.Mesh.Indices.resize(static_cast<size_t>(vertexCount));
					for (uint32_t i = 0; i < static_cast<uint32_t>(vertexCount); ++i)
					{
						imported.Mesh.Indices[i] = i;
					}
				}

				if (primitive.material)
				{
					imported.MaterialIndex = static_cast<int>(primitive.material - data->materials);
				}

				const int importedPrimitiveIndex = static_cast<int>(outScene.Primitives.size());
				primitiveToImportedIndex[&primitive] = importedPrimitiveIndex;
				outScene.Primitives.push_back(std::move(imported));
			}
		}

		// Node instances with world transforms
		for (cgltf_size nodeIndex = 0; nodeIndex < data->nodes_count; ++nodeIndex)
		{
			const cgltf_node& node = data->nodes[nodeIndex];
			if (!node.mesh)
			{
				continue;
			}

			float worldM[16]{};
			cgltf_node_transform_world(&node, worldM);
			const glm::mat4 worldTransform = Mat4FromCglTF(worldM);

			for (cgltf_size primIndex = 0; primIndex < node.mesh->primitives_count; ++primIndex)
			{
				const cgltf_primitive& primitive = node.mesh->primitives[primIndex];

				auto it = primitiveToImportedIndex.find(&primitive);
				if (it == primitiveToImportedIndex.end())
				{
					continue;
				}

				ImportedNodeInstance instance{};
				instance.PrimitiveIndex = it->second;
				instance.WorldTransform = worldTransform;
				outScene.Instances.push_back(instance);
			}
		}

		freeData();
		return true;
	}
}