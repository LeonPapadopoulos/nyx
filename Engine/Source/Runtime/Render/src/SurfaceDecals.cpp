#include "NyxPCH.h"
#include "SurfaceDecals.h"

#include "EntityGuidLookup.h"
#include "PreviousTransformComponent.h"
#include "SurfaceAnchor.h"
#include "SurfaceDecalComponent.h"
#include "TransformComponent.h"

#include <optional>

namespace Nyx
{
	void ExtractSurfaceDecals(const Registry& world, std::optional<float> interpolation, std::vector<RenderDecal>& outDecals)
	{
		outDecals.clear();

		// Made at the first decal that is drawn, so a world without decals goes through no guids
		std::optional<EntityGuidLookup> lookup;
		world.Each<SurfaceDecalComponent>([&](Entity entity, const SurfaceDecalComponent& decal)
			{
				const float reveal = glm::clamp(decal.Reveal, 0.0f, 1.0f);
				if (!decal.bVisible || !decal.MaterialAsset || reveal <= 0.0f)
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
				drawn.MaterialAsset = decal.MaterialAsset;
				drawn.Model = MakeSurfaceDecalMatrix(frame, decal.Size);
				drawn.WorldModel = MakeSurfaceDecalMatrix(worldFrame, decal.Size);
				drawn.WorldCorners = GetSurfaceDecalCorners(worldFrame, decal.Size);
				drawn.CustomData = glm::vec4(decal.Color, decal.Intensity);
				drawn.CustomData2 = glm::vec2(reveal, glm::clamp(decal.Pulse, 0.0f, 1.0f));
			});
	}
}
