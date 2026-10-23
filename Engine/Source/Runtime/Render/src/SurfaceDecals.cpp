#include "NyxPCH.h"
#include "SurfaceDecals.h"

#include "EntityGuidLookup.h"
#include "IAssetResolver.h"
#include "Log.h"
#include "PreviousTransformComponent.h"
#include "SurfaceAnchor.h"
#include "SurfaceDecalComponent.h"
#include "TransformComponent.h"

#include <optional>

namespace Nyx
{
	namespace
	{
		// The material at `path`: as kept, or resolved now and kept, also when it is nothing
		Material* FindDecalMaterial(const std::string& path, IAssetResolver& assets, DecalMaterialCache& materials)
		{
			const auto found = materials.ByPath.find(path);
			if (found != materials.ByPath.end())
			{
				return found->second;
			}
			Material* material = assets.ResolveMaterial(path);
			if (!material)
			{
				LOG_WARNING("Surface decal material {} can't be found; decals with it aren't drawn", path);
			}
			materials.ByPath.emplace(path, material);
			return material;
		}
	}

	void ExtractSurfaceDecals(const Registry& world, std::optional<float> interpolation, IAssetResolver& assets, DecalMaterialCache& materials,
		std::vector<RenderDecal>& outDecals)
	{
		outDecals.clear();

		// Made at the first decal that is drawn, so a world without decals goes through no guids
		std::optional<EntityGuidLookup> lookup;
		world.Each<SurfaceDecalComponent>([&](Entity entity, const SurfaceDecalComponent& decal)
			{
				const float reveal = glm::clamp(decal.Reveal, 0.0f, 1.0f);
				if (!decal.bVisible || reveal <= 0.0f)
				{
					return;
				}
				// By its path where it has one: a scene's load gave its MaterialAsset from the same path,
				// but a decal made while the game runs has none, and a path changed since isn't in it
				Material* material = decal.Material.IsValid() ? FindDecalMaterial(decal.Material.Path, assets, materials) : decal.MaterialAsset;
				if (!material)
				{
					return;
				}

				// On its host where the host is drawn, and where it is; without a host, in world space
				TransformComponent host;
				TransformComponent hostWhereItIs;
				if (decal.Anchor.Host.IsValid())
				{
					if (!lookup)
					{
						lookup = MakeEntityGuidLookup(world);
					}
					const std::optional<Entity> hostEntity = FindEntityByGuid(world, *lookup, decal.Anchor.Host);
					// Its host is gone: nothing to draw on
					if (!hostEntity)
					{
						return;
					}
					host = GetDrawnTransform(world, *hostEntity, interpolation);
					hostWhereItIs = GetDrawnTransform(world, *hostEntity, std::nullopt);
				}
				const SurfaceFrame frame = ResolveSurfaceAnchor(host, decal.Anchor);
				const SurfaceFrame worldFrame = interpolation ? ResolveSurfaceAnchor(hostWhereItIs, decal.Anchor) : frame;

				RenderDecal& drawn = outDecals.emplace_back();
				drawn.SourceEntity = entity;
				drawn.MaterialAsset = material;
				drawn.Model = MakeSurfaceDecalMatrix(frame, decal.Size);
				drawn.WorldModel = MakeSurfaceDecalMatrix(worldFrame, decal.Size);
				drawn.WorldCorners = GetSurfaceDecalCorners(worldFrame, decal.Size);
				drawn.CustomData = glm::vec4(decal.Color, decal.Intensity);
				drawn.CustomData2 = glm::vec2(reveal, glm::clamp(decal.Pulse, 0.0f, 1.0f));
			});
	}
}
