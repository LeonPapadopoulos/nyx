#include "ImGuiSource.h"
#include "PropertyWidgetRegistry.h"

#include "ReflectionUtils.h"
#include "UIWidgets.h"

#include <cstdlib>
#include <string_view>
#include <unordered_map>

#include <imgui.h>

namespace
{
	static float WrapDegrees180(float degrees)
	{
		while (degrees > 180.0f)
		{
			degrees -= 360.0f;
		}

		while (degrees < -180.0f)
		{
			degrees += 360.0f;
		}

		return degrees;
	}

	static glm::vec3 WrapEulerDegrees180(const glm::vec3& degrees)
	{
		return glm::vec3(
			WrapDegrees180(degrees.x),
			WrapDegrees180(degrees.y),
			WrapDegrees180(degrees.z));
	}
}

namespace
{
	struct EnumClassHash
	{
		template<typename T>
		size_t operator()(T value) const noexcept
		{
			return static_cast<size_t>(value);
		}
	};

	using namespace Nyx::Editor;

	static std::unordered_map<Nyx::Reflection::EPropertyKind, PropertyWidgetDrawFn, EnumClassHash> GPropertyWidgets;

	static float GetPropertyDragSpeed(const Nyx::Reflection::PropertyMetadata& property, float defaultValue = 0.1f)
	{
		if (const char* value = Nyx::Reflection::FindMetadataValue(property, "DragSpeed"))
		{
			const float parsed = std::strtof(value, nullptr);
			if (parsed > 0.0f)
			{
				return parsed;
			}
		}

		return defaultValue;
	}

	// Whether the property's "UI" metadata asks for `ui`, e.g. UI = Degrees for rotations, UI = Color for colours
	static bool PropertyUsesUI(const Nyx::Reflection::PropertyMetadata& property, std::string_view ui)
	{
		if (const char* value = Nyx::Reflection::FindMetadataValue(property, "UI"))
		{
			return std::string_view(value) == ui;
		}

		return false;
	}

	static void BeginPropertyEdit(
		PropertyEditTransactionState& state,
		Nyx::Editor::InspectorDrawContext& drawContext,
		void* object,
		const Nyx::Reflection::TypeMetadata& typeMetadata)
	{
		if (!state.bEditing || state.Target != drawContext.CurrentObjectRef)
		{
			state.bEditing = true;
			state.Target = drawContext.CurrentObjectRef;
			state.PendingDiff.emplace();
			state.PendingDiff->TakeSnapshot(drawContext.CurrentObjectRef, object, typeMetadata, drawContext.CurrentLocation);
		}
	}

	static void CommitPropertyEdit(
		PropertyEditTransactionState& state,
		Nyx::Editor::InspectorDrawContext& drawContext,
		const char* label)
	{
		if (state.PendingDiff.has_value() && drawContext.Transactions)
		{
			state.PendingDiff->CommitChanges(label, *drawContext.Transactions);
		}

		state.PendingDiff.reset();
		state.bEditing = false;
	}

	static bool DrawBoolWidget(const PropertyWidgetArgs& args)
	{
		bool& value = Nyx::Reflection::AccessProperty<bool>(args.Object, *args.Property);
		bool editedValue = value;

		if (NYX_UI(Nyx::UI::Checkbox("##Field", &editedValue)))
		{
			PropertyEditTransactionState immediateEditState{};
			immediateEditState.Target = args.DrawContext->CurrentObjectRef;
			immediateEditState.PendingDiff.emplace();
			immediateEditState.PendingDiff->TakeSnapshot(
				args.DrawContext->CurrentObjectRef,
				args.Object,
				*args.OwnerType,
				args.DrawContext->CurrentLocation);

			value = editedValue;

			if (args.DrawContext->Transactions)
			{
				immediateEditState.PendingDiff->CommitChanges("Edit Property", *args.DrawContext->Transactions);
			}

			return true;
		}

		return false;
	}

	static bool DrawInt32Widget(const PropertyWidgetArgs& args)
	{
		int32_t& value = Nyx::Reflection::AccessProperty<int32_t>(args.Object, *args.Property);
		const float speed = GetPropertyDragSpeed(*args.Property, 1.0f);

		const bool bChanged = NYX_UI(Nyx::UI::DragScalar("##Field", ImGuiDataType_S32, &value, speed));

		if (ImGui::IsItemActivated())
		{
			BeginPropertyEdit(args.DrawContext->GenericPropertyEdit, *args.DrawContext, args.Object, *args.OwnerType);
		}

		if (ImGui::IsItemDeactivatedAfterEdit())
		{
			CommitPropertyEdit(args.DrawContext->GenericPropertyEdit, *args.DrawContext, "Edit Property");
		}

		return bChanged;
	}

	static bool DrawUInt32Widget(const PropertyWidgetArgs& args)
	{
		uint32_t& value = Nyx::Reflection::AccessProperty<uint32_t>(args.Object, *args.Property);
		const float speed = GetPropertyDragSpeed(*args.Property, 1.0f);

		const bool bChanged = NYX_UI(Nyx::UI::DragScalar("##Field", ImGuiDataType_U32, &value, speed));

		if (ImGui::IsItemActivated())
		{
			BeginPropertyEdit(args.DrawContext->GenericPropertyEdit, *args.DrawContext, args.Object, *args.OwnerType);
		}

		if (ImGui::IsItemDeactivatedAfterEdit())
		{
			CommitPropertyEdit(args.DrawContext->GenericPropertyEdit, *args.DrawContext, "Edit Property");
		}

		return bChanged;
	}

	// 64-bit values are IDs, so they are shown and typed in hex instead of dragged
	static bool DrawUInt64Widget(const PropertyWidgetArgs& args)
	{
		uint64_t& value = Nyx::Reflection::AccessProperty<uint64_t>(args.Object, *args.Property);

		const bool bChanged = NYX_UI(Nyx::UI::InputScalar(
			"##Field", ImGuiDataType_U64, &value, "%016llX", ImGuiInputTextFlags_CharsHexadecimal));

		if (ImGui::IsItemActivated())
		{
			BeginPropertyEdit(args.DrawContext->GenericPropertyEdit, *args.DrawContext, args.Object, *args.OwnerType);
		}

		if (ImGui::IsItemDeactivatedAfterEdit())
		{
			CommitPropertyEdit(args.DrawContext->GenericPropertyEdit, *args.DrawContext, "Edit Property");
		}

		return bChanged;
	}

	static bool DrawFloatWidget(const PropertyWidgetArgs& args)
	{
		float& value = Nyx::Reflection::AccessProperty<float>(args.Object, *args.Property);
		const float speed = GetPropertyDragSpeed(*args.Property);

		const bool bChanged = NYX_UI(Nyx::UI::DragFloat("##Field", &value, speed));

		if (ImGui::IsItemActivated())
		{
			BeginPropertyEdit(args.DrawContext->GenericPropertyEdit, *args.DrawContext, args.Object, *args.OwnerType);
		}

		if (ImGui::IsItemDeactivatedAfterEdit())
		{
			CommitPropertyEdit(args.DrawContext->GenericPropertyEdit, *args.DrawContext, "Edit Property");
		}

		return bChanged;
	}

	static bool DrawVec2Widget(const PropertyWidgetArgs& args)
	{
		glm::vec2& value = Nyx::Reflection::AccessProperty<glm::vec2>(args.Object, *args.Property);
		const float speed = GetPropertyDragSpeed(*args.Property);

		const bool bChanged = NYX_UI(Nyx::UI::DragVector("##Field", &value.x, 2, speed));

		if (ImGui::IsItemActivated())
		{
			BeginPropertyEdit(args.DrawContext->GenericPropertyEdit, *args.DrawContext, args.Object, *args.OwnerType);
		}

		if (ImGui::IsItemDeactivatedAfterEdit())
		{
			CommitPropertyEdit(args.DrawContext->GenericPropertyEdit, *args.DrawContext, "Edit Property");
		}

		return bChanged;
	}

	static bool DrawVec3Widget(const PropertyWidgetArgs& args)
	{
		glm::vec3& value = Nyx::Reflection::AccessProperty<glm::vec3>(args.Object, *args.Property);

		// A colour: a swatch with a picker, in which each drag is one edit
		if (PropertyUsesUI(*args.Property, "Color"))
		{
			const Nyx::UI::ColorSwatchResult swatch = NYX_UI(Nyx::UI::ColorSwatch("##Field", &value.x));
			if (swatch.bEditStarted)
			{
				BeginPropertyEdit(args.DrawContext->GenericPropertyEdit, *args.DrawContext, args.Object, *args.OwnerType);
			}
			if (swatch.bEditEnded)
			{
				CommitPropertyEdit(args.DrawContext->GenericPropertyEdit, *args.DrawContext, "Edit Property");
			}
			return swatch.bChanged;
		}

		const float speed = GetPropertyDragSpeed(*args.Property);
		const bool bChanged = NYX_UI(Nyx::UI::DragVector("##Field", &value.x, 3, speed));

		if (ImGui::IsItemActivated())
		{
			BeginPropertyEdit(args.DrawContext->GenericPropertyEdit, *args.DrawContext, args.Object, *args.OwnerType);
		}

		if (ImGui::IsItemDeactivatedAfterEdit())
		{
			CommitPropertyEdit(args.DrawContext->GenericPropertyEdit, *args.DrawContext, "Edit Property");
		}

		return bChanged;
	}

	static bool DrawVec4Widget(const PropertyWidgetArgs& args)
	{
		glm::vec4& value = Nyx::Reflection::AccessProperty<glm::vec4>(args.Object, *args.Property);
		const float speed = GetPropertyDragSpeed(*args.Property);

		const bool bChanged = NYX_UI(Nyx::UI::DragVector("##Field", &value.x, 4, speed));

		if (ImGui::IsItemActivated())
		{
			BeginPropertyEdit(args.DrawContext->GenericPropertyEdit, *args.DrawContext, args.Object, *args.OwnerType);
		}

		if (ImGui::IsItemDeactivatedAfterEdit())
		{
			CommitPropertyEdit(args.DrawContext->GenericPropertyEdit, *args.DrawContext, "Edit Property");
		}

		return bChanged;
	}

	static bool DrawQuatWidget(const PropertyWidgetArgs& args)
	{
		glm::quat& value = Nyx::Reflection::AccessProperty<glm::quat>(args.Object, *args.Property);

		if (!PropertyUsesUI(*args.Property, "Degrees"))
		{
			NYX_UI(ImGui::TextDisabled("<quat unsupported>"));
			return false;
		}

		auto& rotState = args.DrawContext->TransformRotationEdit;

		if (!rotState.bEditing || rotState.Target != args.DrawContext->CurrentObjectRef)
		{
			rotState.Target = args.DrawContext->CurrentObjectRef;
			rotState.CachedDegrees =
				WrapEulerDegrees180(glm::degrees(glm::eulerAngles(glm::normalize(value))));
		}

		const bool bChanged = NYX_UI(Nyx::UI::DragVector("##Field", &rotState.CachedDegrees.x, 3, 1.0f));
		if (bChanged)
		{
			value = glm::normalize(glm::quat(glm::radians(rotState.CachedDegrees)));
		}

		if (ImGui::IsItemActivated())
		{
			BeginPropertyEdit(rotState, *args.DrawContext, args.Object, *args.OwnerType);
			rotState.Target = args.DrawContext->CurrentObjectRef;
		}

		if (ImGui::IsItemDeactivatedAfterEdit())
		{
			CommitPropertyEdit(rotState, *args.DrawContext, "Edit Property");
			rotState.CachedDegrees =
				WrapEulerDegrees180(glm::degrees(glm::eulerAngles(glm::normalize(value))));
		}

		return bChanged;
	}

	static bool DrawStringWidget(const PropertyWidgetArgs& args)
	{
		std::string& value = Nyx::Reflection::AccessProperty<std::string>(args.Object, *args.Property);

		const bool bChanged = NYX_UI(Nyx::UI::InputText("##Field", &value));

		if (ImGui::IsItemActivated())
		{
			BeginPropertyEdit(args.DrawContext->GenericPropertyEdit, *args.DrawContext, args.Object, *args.OwnerType);
		}

		if (ImGui::IsItemDeactivatedAfterEdit())
		{
			CommitPropertyEdit(args.DrawContext->GenericPropertyEdit, *args.DrawContext, "Edit Property");
		}

		return bChanged;
	}
}

namespace Nyx::Editor
{
	PropertyWidgetRegistry& PropertyWidgetRegistry::Get()
	{
		static PropertyWidgetRegistry Instance;
		return Instance;
	}

	void PropertyWidgetRegistry::Register(Nyx::Reflection::EPropertyKind kind, PropertyWidgetDrawFn drawFn)
	{
		GPropertyWidgets[kind] = drawFn;
	}

	PropertyWidgetDrawFn PropertyWidgetRegistry::Find(Nyx::Reflection::EPropertyKind kind) const
	{
		const auto it = GPropertyWidgets.find(kind);
		return it != GPropertyWidgets.end() ? it->second : nullptr;
	}

	void RegisterDefaultPropertyWidgets()
	{
		static bool bRegistered = false;
		if (bRegistered)
		{
			return;
		}
		bRegistered = true;

		auto& registry = PropertyWidgetRegistry::Get();

		registry.Register(Nyx::Reflection::EPropertyKind::Bool, &DrawBoolWidget);
		registry.Register(Nyx::Reflection::EPropertyKind::Int32, &DrawInt32Widget);
		registry.Register(Nyx::Reflection::EPropertyKind::UInt32, &DrawUInt32Widget);
		registry.Register(Nyx::Reflection::EPropertyKind::UInt64, &DrawUInt64Widget);
		registry.Register(Nyx::Reflection::EPropertyKind::Float, &DrawFloatWidget);
		registry.Register(Nyx::Reflection::EPropertyKind::Vec2, &DrawVec2Widget);
		registry.Register(Nyx::Reflection::EPropertyKind::Vec3, &DrawVec3Widget);
		registry.Register(Nyx::Reflection::EPropertyKind::Vec4, &DrawVec4Widget);
		registry.Register(Nyx::Reflection::EPropertyKind::Quat, &DrawQuatWidget);
		registry.Register(Nyx::Reflection::EPropertyKind::String, &DrawStringWidget);
	}
}