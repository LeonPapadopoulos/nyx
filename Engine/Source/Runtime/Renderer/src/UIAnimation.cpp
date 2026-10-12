#include "UIAnimation.h"

#include <imgui_internal.h> // ImGui::AddContextHook()

#include <cmath>
#include <iterator>
#include <unordered_map>

namespace Nyx::UI
{
	namespace
	{
		struct AnimatedValue
		{
			float Value = 0.0f;
			// The frame it last moved in
			int Frame = 0;
		};

		struct AnimationStore
		{
			std::unordered_map<ImGuiID, AnimatedValue> Values;
			ImGuiContext* Context = nullptr;
			int Frame = -1;
		};

		AnimationStore& GetStore()
		{
			static AnimationStore store;
			return store;
		}

		// Forgets everything when the ImGui context the values belong to shuts down
		void OnContextShutdown(ImGuiContext*, ImGuiContextHook*)
		{
			AnimationStore& store = GetStore();
			store.Values.clear();
			store.Context = nullptr;
			store.Frame = -1;
		}

		// On the first call in a frame, forgets the values not asked for in a while; with another
		// ImGui context, forgets them all.
		void StartFrameIfNew(AnimationStore& store)
		{
			ImGuiContext* context = ImGui::GetCurrentContext();
			const int frame = ImGui::GetFrameCount();
			if (store.Context != context)
			{
				store.Values.clear();
				store.Context = context;
				store.Frame = -1;

				// A later context may get this one's address; forgetting at shutdown keeps them apart.
				ImGuiContextHook hook;
				hook.Type = ImGuiContextHookType_Shutdown;
				hook.Callback = &OnContextShutdown;
				ImGui::AddContextHook(context, &hook);
			}
			if (store.Frame == frame)
			{
				return;
			}
			store.Frame = frame;

			for (auto value = store.Values.begin(); value != store.Values.end();)
			{
				value = frame - value->second.Frame > AnimationForgetFrames ? store.Values.erase(value) : std::next(value);
			}
		}
	}

	float Animate(ImGuiID id, float target, float speed)
	{
		AnimationStore& store = GetStore();
		StartFrameIfNew(store);

		const auto [found, bNew] = store.Values.try_emplace(id, AnimatedValue{ target, store.Frame });
		AnimatedValue& value = found->second;
		if (bNew || value.Frame == store.Frame)
		{
			return value.Value;
		}

		// value(t) = target + (start - target) * e^(-speed * t), one frame's step of it
		const float deltaTime = ImGui::GetIO().DeltaTime;
		value.Value = target + (value.Value - target) * std::exp(-speed * deltaTime);
		if (std::abs(value.Value - target) < 0.001f)
		{
			value.Value = target;
		}
		value.Frame = store.Frame;
		return value.Value;
	}

	size_t GetAnimationCount()
	{
		AnimationStore& store = GetStore();
		if (ImGui::GetCurrentContext() != nullptr)
		{
			StartFrameIfNew(store);
		}
		return store.Values.size();
	}
}
