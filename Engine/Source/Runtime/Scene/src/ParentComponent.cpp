#include "NyxPCH.h"
#include "ParentComponent.h"

#include "EntityGuidLookup.h"
#include "TransformComponent.h"

#include <algorithm>
#include <cstdint>
#include <optional>

namespace Nyx
{
	namespace
	{
		// Divides axis by axis; an axis divided by 0 gives 0
		glm::vec3 DivideOrZero(const glm::vec3& value, const glm::vec3& divisor)
		{
			glm::vec3 result{ 0.0f };
			for (int axis = 0; axis < 3; ++axis)
			{
				result[axis] = divisor[axis] != 0.0f ? value[axis] / divisor[axis] : 0.0f;
			}
			return result;
		}

		// One child whose parent is there, and how deep it is below a root
		struct Link
		{
			Entity Child;
			Entity Parent;
			// The parent's own link, if the parent is a child too
			int32_t ParentLink = -1;
			// Unknown, being worked out, or in or below a circle, until it is 0 or more
			int32_t Depth = -1;
		};

		constexpr int32_t DepthUnknown = -1;
		constexpr int32_t DepthVisiting = -2;
		constexpr int32_t DepthInCircle = -3;

		// How deep each link is, walking up to a root or to a link already known. A walk that comes
		// back to a link it is on has found a circle: every link on it, and every link below one, moves
		// nothing.
		void FindDepths(std::vector<Link>& links)
		{
			std::vector<int32_t> walk;
			for (size_t start = 0; start < links.size(); ++start)
			{
				int32_t current = static_cast<int32_t>(start);
				while (current >= 0 && links[current].Depth == DepthUnknown)
				{
					links[current].Depth = DepthVisiting;
					walk.push_back(current);
					current = links[current].ParentLink;
				}

				// Where the walk stopped: a root's link has no parent link
				int32_t depth = current < 0 ? -1 : links[current].Depth;
				const bool bBroken = depth == DepthVisiting || depth == DepthInCircle;

				while (!walk.empty())
				{
					Link& link = links[walk.back()];
					walk.pop_back();
					link.Depth = bBroken ? DepthInCircle : ++depth;
				}
			}
		}
	}

	TransformComponent GetChildWorldTransform(const TransformComponent& parentWorld, const ParentComponent& link)
	{
		TransformComponent world;
		world.Position = parentWorld.Position + parentWorld.Rotation * (parentWorld.Scale * link.LocalPosition);
		world.Rotation = glm::normalize(parentWorld.Rotation * link.LocalRotation);
		world.Scale = parentWorld.Scale * link.LocalScale;
		return world;
	}

	void SetLocalTransform(ParentComponent& link, const TransformComponent& parentWorld, const TransformComponent& world)
	{
		const glm::quat inverseRotation = glm::inverse(parentWorld.Rotation);
		link.LocalPosition = DivideOrZero(inverseRotation * (world.Position - parentWorld.Position), parentWorld.Scale);
		link.LocalRotation = glm::normalize(inverseRotation * world.Rotation);
		link.LocalScale = DivideOrZero(world.Scale, parentWorld.Scale);
	}

	void UpdateTransformHierarchy(Registry& world, const EntityGuidLookup& guids, std::vector<Entity>* outMoved)
	{
		if (outMoved)
		{
			outMoved->clear();
		}

		std::vector<Link> links;
		uint32_t highestChildIndex = 0;
		world.Each<ParentComponent>(
			[&](Entity child, const ParentComponent& link)
			{
				const std::optional<Entity> parent = link.Parent.IsValid() ? FindEntityByGuid(world, guids, link.Parent) : std::nullopt;
				if (parent && *parent != child && world.Has<TransformComponent>(*parent) && world.Has<TransformComponent>(child))
				{
					links.push_back(Link{ child, *parent });
					highestChildIndex = std::max(highestChildIndex, child.Index());
				}
			});

		if (links.empty())
		{
			return;
		}

		// Each child's link by its entity's index, to find a parent's own link
		std::vector<int32_t> linkByIndex(static_cast<size_t>(highestChildIndex) + 1, -1);
		for (size_t index = 0; index < links.size(); ++index)
		{
			linkByIndex[links[index].Child.Index()] = static_cast<int32_t>(index);
		}
		for (Link& link : links)
		{
			const uint32_t parentIndex = link.Parent.Index();
			link.ParentLink = parentIndex < linkByIndex.size() ? linkByIndex[parentIndex] : -1;
		}

		FindDepths(links);

		// Parents before their children; the same depth in the order of the links, so every run
		// writes in the same order
		std::vector<int32_t> order;
		order.reserve(links.size());
		for (size_t index = 0; index < links.size(); ++index)
		{
			if (links[index].Depth >= 0)
			{
				order.push_back(static_cast<int32_t>(index));
			}
		}
		std::stable_sort(order.begin(), order.end(),
			[&](int32_t left, int32_t right)
			{
				return links[left].Depth < links[right].Depth;
			});

		for (const int32_t index : order)
		{
			const Link& link = links[index];
			const TransformComponent placed =
				GetChildWorldTransform(world.Get<TransformComponent>(link.Parent), world.Get<ParentComponent>(link.Child));

			TransformComponent& transform = world.Get<TransformComponent>(link.Child);
			if (transform.Position != placed.Position || transform.Rotation != placed.Rotation || transform.Scale != placed.Scale)
			{
				transform.Position = placed.Position;
				transform.Rotation = placed.Rotation;
				transform.Scale = placed.Scale;
				if (outMoved)
				{
					outMoved->push_back(link.Child);
				}
			}
		}
	}
}
