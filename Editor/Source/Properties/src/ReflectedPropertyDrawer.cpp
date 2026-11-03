#include "ReflectedPropertyDrawer.h"
#include "AssetDatabase.h"
#include "AssetDragDrop.h"
#include "AssetReference.h"
#include "ReflectedPropertyAccess.h"
#include "TransactionDiffUtil.h"

#include "PropertyWidgetRegistry.h"
#include "ReflectionUtils.h"
#include "SearchRow.h"
#include "ImGuiSource.h"
#include "OverrideRows.h"
#include "PropertyValueAccess.h"
#include "ReflectionSourceRegistry.h"
#include "UITheme.h"
#include "UIWidgets.h"
#include "WindowPainter.h"

#include <imgui_internal.h>

#include <algorithm>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include <imgui.h>

// The rows are painted (Nyx::UI, UIWidgets.h): a property row per property, with its label in a
// column on the left and its value's widget on the right, and a tree row for each category and
// each struct, which opens and closes the rows inside it. An array is a tree row too, with a row
// per element (a tree row for a struct) and an Add row; each element's context menu inserts,
// removes and moves elements; a component's overrides of values it inherits are a row per value
// instead (OverrideRows.h). A problem the draw context has about a property (InspectorDrawContext::
// Problems, e.g. an asset's) is a row of its own right under the property's row.

namespace
{
	static const char* GetPropertyTooltip(const Nyx::Reflection::PropertyMetadata& property)
	{
		return Nyx::Reflection::FindMetadataValue(property, "Tooltip");
	}

	static const char* GetPropertyCategory(const Nyx::Reflection::PropertyMetadata& property)
	{
		return Nyx::Reflection::FindMetadataValue(property, "Category");
	}

	static bool IsPropertyHidden(const Nyx::Reflection::PropertyMetadata& property)
	{
		return Nyx::Reflection::HasFlag(property.Flags, Nyx::Reflection::EPropertyFlags::Hidden);
	}

	static bool IsPropertyReadOnly(const Nyx::Reflection::PropertyMetadata& property)
	{
		return Nyx::Reflection::HasFlag(property.Flags, Nyx::Reflection::EPropertyFlags::ReadOnly);
	}

	// How many structs and arrays deep a search looks
	static constexpr int MaxFilterDepth = 8;

	static const char* GetPropertyDisplayName(const Nyx::Reflection::PropertyMetadata& property)
	{
		return (property.DisplayName && property.DisplayName[0]) ? property.DisplayName : property.Name;
	}

	static bool TypeHasFilterMatch(const Nyx::Reflection::TypeMetadata& typeMetadata, std::string_view filter, int depth);

	// Whether a search shows the property: by its name, or by one inside it. An array can hold its
	// own type, so this looks only so deep.
	static bool PropertyHasFilterMatch(const Nyx::Reflection::PropertyMetadata& property, std::string_view filter, int depth)
	{
		if (Nyx::Editor::NameMatchesFilter(GetPropertyDisplayName(property), filter))
		{
			return true;
		}
		if (depth <= 0)
		{
			return false;
		}
		if (const Nyx::Reflection::TypeMetadata* nestedType = Nyx::Reflection::TryGetNestedType(property))
		{
			return TypeHasFilterMatch(*nestedType, filter, depth - 1);
		}
		if (const auto* array = Nyx::Reflection::TryGetArray(property); array && Nyx::Reflection::IsStructProperty(array->Element))
		{
			const Nyx::Reflection::TypeMetadata* elementType = Nyx::Editor::GetArrayElementType(*array);
			return elementType && TypeHasFilterMatch(*elementType, filter, depth - 1);
		}
		return false;
	}

	static bool TypeHasFilterMatch(const Nyx::Reflection::TypeMetadata& typeMetadata, std::string_view filter, int depth)
	{
		for (size_t index = 0; index < typeMetadata.PropertyCount; ++index)
		{
			const Nyx::Reflection::PropertyMetadata& property = typeMetadata.Properties[index];
			if (!IsPropertyHidden(property) && PropertyHasFilterMatch(property, filter, depth))
			{
				return true;
			}
		}
		return false;
	}

	static void DrawPropertyTooltipIfHovered(const Nyx::Reflection::PropertyMetadata& property)
	{
		// Read-only properties are drawn disabled, and their tooltips should show too. Under a
		// tooltip the widget showed itself, such as an entity reference's guid.
		if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
		{
			if (const char* tooltip = GetPropertyTooltip(property))
			{
				if (ImGui::BeginTooltip())
				{
					ImGui::TextUnformatted(tooltip);
					ImGui::EndTooltip();
				}
			}
		}
	}

	// The path of the property being drawn (InspectorDrawContext::CurrentPath) while it lives
	struct PropertyPathScope
	{
		PropertyPathScope(Nyx::Editor::InspectorDrawContext& drawContext, std::string path)
			: DrawContext(drawContext)
			, Outer(std::exchange(drawContext.CurrentPath, std::move(path)))
		{
		}

		~PropertyPathScope()
		{
			DrawContext.CurrentPath = std::move(Outer);
		}

		PropertyPathScope(const PropertyPathScope&) = delete;
		PropertyPathScope& operator=(const PropertyPathScope&) = delete;

		Nyx::Editor::InspectorDrawContext& DrawContext;
		std::string Outer;
	};

	// The problems about the property being drawn, each a row in the colour of errors
	static void DrawPropertyProblems(Nyx::Editor::InspectorDrawContext& drawContext)
	{
		if (!drawContext.Problems)
		{
			return;
		}
		const std::vector<Nyx::PropertyProblem>& problems = *drawContext.Problems;
		for (size_t index = 0; index < problems.size(); ++index)
		{
			if (problems[index].Path != drawContext.CurrentPath)
			{
				continue;
			}
			drawContext.ShownProblems.push_back(index);
			ImGui::PushID(static_cast<int>(index));
			Nyx::UI::BeginPropertyRow("##Problem");
			const glm::vec4& error = Nyx::UI::GetTheme().StatusError;
			ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(error.r, error.g, error.b, error.a));
			NYX_UI(ImGui::TextWrapped("%s", problems[index].Message.c_str()));
			ImGui::PopStyleColor();
			Nyx::UI::EndPropertyRow();
			ImGui::PopID();
		}
	}

	// A tree row that a click anywhere opens and closes, for a category or a struct; open the
	// first time it shows
	static bool BeginPropertyGroupRow(const char* label)
	{
		Nyx::UI::TreeRowOptions options;
		options.bDefaultOpen = true;
		options.bToggleOnClick = true;
		return NYX_UI(Nyx::UI::TreeRow(label, options)).bOpen;
	}

	struct PropertyBuckets
	{
		std::vector<size_t> Uncategorized;
		std::map<std::string, std::vector<size_t>> ByCategory;
	};

	static PropertyBuckets BuildPropertyBuckets(const Nyx::Reflection::TypeMetadata& typeMetadata)
	{
		PropertyBuckets buckets{};

		for (size_t propertyIndex = 0; propertyIndex < typeMetadata.PropertyCount; ++propertyIndex)
		{
			const Nyx::Reflection::PropertyMetadata& property = typeMetadata.Properties[propertyIndex];

			if (IsPropertyHidden(property))
			{
				continue;
			}

			if (const char* category = GetPropertyCategory(property))
			{
				if (category[0] != '\0')
				{
					buckets.ByCategory[category].push_back(propertyIndex);
					continue;
				}
			}

			buckets.Uncategorized.push_back(propertyIndex);
		}

		return buckets;
	}

	// label: instead of the property's name, e.g. an array element's "[2]". afterLabel: called
	// right after the label, e.g. for its context menu.
	static bool DrawSingleLeafPropertyRow(
		void* object,
		const Nyx::Reflection::PropertyMetadata& property,
		Nyx::Editor::InspectorDrawContext& drawContext,
		const Nyx::Reflection::TypeMetadata& ownerType,
		const char* label = nullptr,
		const std::function<void()>& afterLabel = {})
	{
		bool bAnyChanged = false;

		ImGui::PushID(property.Name);

		// The row's own ID is "##Row", so the value's widgets keep the IDs they had
		const std::string rowLabel = std::string(label ? label : GetPropertyDisplayName(property)) + "##Row";
		Nyx::UI::BeginPropertyRow(rowLabel.c_str());
		DrawPropertyTooltipIfHovered(property);
		if (afterLabel)
		{
			afterLabel();
		}

		const bool bReadOnly = IsPropertyReadOnly(property);
		if (bReadOnly)
		{
			ImGui::BeginDisabled();
		}

		if (Nyx::Editor::PropertyWidgetDrawFn drawFn =
				Nyx::Editor::PropertyWidgetRegistry::Get().Find(property.Kind))
		{
			Nyx::Editor::PropertyWidgetArgs args{};
			args.Object = object;
			args.Property = &property;
			args.OwnerType = &ownerType;
			args.DrawContext = &drawContext;

			bAnyChanged = drawFn(args);
		}
		else
		{
			ImGui::AlignTextToFramePadding();
			NYX_UI(ImGui::TextDisabled("<unsupported>"));
		}

		if (bReadOnly)
		{
			ImGui::EndDisabled();
		}

		DrawPropertyTooltipIfHovered(property);
		Nyx::UI::EndPropertyRow();
		DrawPropertyProblems(drawContext);

		ImGui::PopID();
		return bAnyChanged;
	}

	static bool DrawStructPropertyRow(
		void* object,
		const Nyx::Reflection::PropertyMetadata& property,
		Nyx::Editor::InspectorDrawContext& drawContext,
		const Nyx::Reflection::TypeMetadata& ownerType);

	// An asset reference as one row: a combo of the assets of its type (the property's AssetType,
	// else the reference's own), which also takes an asset of that type dragged from the asset
	// browser. Each choice is one undo step.
	static bool DrawAssetReferenceRow(
		void* object,
		const Nyx::Reflection::PropertyMetadata& property,
		Nyx::Editor::InspectorDrawContext& drawContext,
		const Nyx::Reflection::TypeMetadata& ownerType)
	{
		Nyx::AssetReference& reference = Nyx::Reflection::AccessProperty<Nyx::AssetReference>(object, property);
		const char* assetType = Nyx::Reflection::FindMetadataValue(property, "AssetType");
		const std::string typeId = assetType ? std::string(assetType) : reference.Type;

		ImGui::PushID(property.Name);
		const std::string rowLabel = std::string(GetPropertyDisplayName(property)) + "##Row";
		Nyx::UI::BeginPropertyRow(rowLabel.c_str());
		DrawPropertyTooltipIfHovered(property);

		const bool bReadOnly = IsPropertyReadOnly(property);
		std::optional<std::string> chosen;
		ImGui::BeginDisabled(bReadOnly);
		const std::string preview = reference.Path.empty() ? std::string("None") : reference.Path;
		if (Nyx::UI::BeginCombo("##Asset", preview.c_str()))
		{
			if (NYX_UI(Nyx::UI::Selectable("None", reference.Path.empty())))
			{
				chosen = std::string();
			}
			if (drawContext.Assets)
			{
				for (const Nyx::Editor::AssetEntry& entry : drawContext.Assets->GetAllByType(typeId))
				{
					const std::string path = entry.RelativePath.generic_string();
					if (NYX_UI(Nyx::UI::Selectable(path.c_str(), path == reference.Path)))
					{
						chosen = path;
					}
				}
			}
			Nyx::UI::EndCombo();
		}
		ImGui::EndDisabled();
		if (!bReadOnly && ImGui::BeginDragDropTarget())
		{
			if (std::optional<std::string> dropped = drawContext.Assets ? Nyx::Editor::AcceptAssetDrop(drawContext.Assets->GetTypes(), typeId) : std::nullopt)
			{
				chosen = std::move(dropped);
			}
			ImGui::EndDragDropTarget();
		}

		bool bChanged = false;
		if (chosen && *chosen != reference.Path)
		{
			// The reference is a struct inside the component, or inside a struct of it: where it
			// is, so undo finds it
			Nyx::Editor::SubobjectPath location = drawContext.CurrentLocation;
			if (!location.SubobjectType)
			{
				location.SubobjectType = &ownerType;
			}
			location.PropertyIndices.push_back(static_cast<size_t>(&property - ownerType.Properties));
			Nyx::Editor::TransactionDiffUtil diff;
			diff.TakeSnapshot(drawContext.CurrentObjectRef, &reference, Nyx::Reflection::GetTypeMetadata<Nyx::AssetReference>(), location);
			reference.Type = chosen->empty() ? std::string() : typeId;
			reference.Path = *chosen;
			if (drawContext.Transactions)
			{
				const std::string label = "Assign " + std::string(GetPropertyDisplayName(property));
				diff.CommitChanges(label.c_str(), *drawContext.Transactions);
			}
			bChanged = true;
		}

		Nyx::UI::EndPropertyRow();
		DrawPropertyProblems(drawContext);
		ImGui::PopID();
		return bChanged;
	}

	static bool DrawArrayPropertyRow(
		void* object,
		const Nyx::Reflection::PropertyMetadata& property,
		Nyx::Editor::InspectorDrawContext& drawContext,
		const Nyx::Reflection::TypeMetadata& ownerType);

	// A prefab instance's value that differs from its prefab's: the accent at the left of its rows,
	// and their right-click menu
	static void DrawPrefabOverrideMark(Nyx::Editor::InspectorDrawContext& drawContext, size_t propertyIndex, float top, float bottom)
	{
		const ImGuiWindow* window = ImGui::GetCurrentWindow();
		const float left = window->WorkRect.Min.x - ImGui::GetStyle().WindowPadding.x * 0.5f - 1.5f;
		Nyx::UI::GetWindowPainter().AddRoundedRect(Nyx::UI::PainterRect{ { left, top + 1.0f }, { left + 3.0f, bottom - 1.0f } }, 1.5f,
			Nyx::UI::PainterStyle::Filled(Nyx::UI::GetTheme().Accent));

		ImGui::PushID(static_cast<int>(propertyIndex));
		ImGui::PushID("##PrefabValue");
		const bool bHovered = ImGui::IsWindowHovered() && ImGui::IsMouseHoveringRect(ImVec2(window->WorkRect.Min.x - 8.0f, top), ImVec2(window->WorkRect.Max.x, bottom), false);
		if (bHovered && ImGui::GetIO().MousePos.x < window->WorkRect.Min.x + 8.0f)
		{
			ImGui::SetTooltip("Differs from the prefab; right-click to revert it or apply it to the prefab");
		}
		if (bHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right))
		{
			ImGui::OpenPopup("##Menu");
		}
		if (Nyx::UI::BeginPopup("##Menu"))
		{
			if (NYX_UI(Nyx::UI::MenuItem("Revert to Prefab")) && drawContext.PrefabValueAction)
			{
				drawContext.PrefabValueAction(propertyIndex, false);
			}
			if (NYX_UI(Nyx::UI::MenuItem("Apply to Prefab")) && drawContext.PrefabValueAction)
			{
				drawContext.PrefabValueAction(propertyIndex, true);
			}
			ImGui::EndPopup();
		}
		ImGui::PopID();
		ImGui::PopID();
	}

	static bool DrawPropertyRowContents(
		void* object,
		const Nyx::Reflection::PropertyMetadata& property,
		Nyx::Editor::InspectorDrawContext& drawContext,
		const Nyx::Reflection::TypeMetadata& ownerType);

	static bool DrawPropertyRow(
		void* object,
		const Nyx::Reflection::PropertyMetadata& property,
		Nyx::Editor::InspectorDrawContext& drawContext,
		const Nyx::Reflection::TypeMetadata& ownerType)
	{
		// A search: a property with it in its name is drawn whole, another only for what it holds
		const std::string_view filter = drawContext.PropertyFilter;
		if (!filter.empty())
		{
			if (Nyx::Editor::NameMatchesFilter(GetPropertyDisplayName(property), filter))
			{
				drawContext.PropertyFilter = {};
				const bool bChanged = DrawPropertyRow(object, property, drawContext, ownerType);
				drawContext.PropertyFilter = filter;
				return bChanged;
			}
			if (!PropertyHasFilterMatch(property, filter, MaxFilterDepth))
			{
				return false;
			}
		}

		// A component's own property that differs from its prefab's is marked over all its rows
		const size_t index = static_cast<size_t>(&property - ownerType.Properties);
		const bool bOverridden = drawContext.PrefabOverrides && drawContext.PrefabOverridesType == &ownerType && !drawContext.CurrentLocation.SubobjectType &&
			std::find(drawContext.PrefabOverrides->begin(), drawContext.PrefabOverrides->end(), index) != drawContext.PrefabOverrides->end();
		if (!bOverridden)
		{
			return DrawPropertyRowContents(object, property, drawContext, ownerType);
		}
		const float top = ImGui::GetCursorScreenPos().y;
		const bool bChanged = DrawPropertyRowContents(object, property, drawContext, ownerType);
		DrawPrefabOverrideMark(drawContext, index, top, ImGui::GetCursorScreenPos().y);
		return bChanged;
	}

	static bool DrawPropertyRowContents(
		void* object,
		const Nyx::Reflection::PropertyMetadata& property,
		Nyx::Editor::InspectorDrawContext& drawContext,
		const Nyx::Reflection::TypeMetadata& ownerType)
	{
		Nyx::UI::SourceDeclarationScope declaration(Nyx::Editor::ReflectionSourceRegistry::Get().Find(property));
		const PropertyPathScope path(drawContext, drawContext.CurrentPath.empty() ? std::string(property.Name) : drawContext.CurrentPath + "." + property.Name);
		if (Nyx::Reflection::IsStructProperty(property) &&
			Nyx::Reflection::TryGetNestedType(property) == &Nyx::Reflection::GetTypeMetadata<Nyx::AssetReference>())
		{
			return DrawAssetReferenceRow(object, property, drawContext, ownerType);
		}
		if (Nyx::Reflection::IsStructProperty(property))
		{
			return DrawStructPropertyRow(object, property, drawContext, ownerType);
		}
		if (const Nyx::ComponentTypeOps* provider = Nyx::Editor::FindOverridableValuesProvider(property, ownerType))
		{
			return Nyx::Editor::DrawOverrideRows(object, property, ownerType, *provider, drawContext);
		}
		if (Nyx::Reflection::TryGetArray(property))
		{
			return DrawArrayPropertyRow(object, property, drawContext, ownerType);
		}

		return DrawSingleLeafPropertyRow(object, property, drawContext, ownerType);
	}

	static bool DrawNestedStructRows(
		void* object,
		const Nyx::Reflection::TypeMetadata& typeMetadata,
		Nyx::Editor::InspectorDrawContext& drawContext)
	{
		bool bAnyChanged = false;

		// Disabled until we settle on a whether we want to show the struct's properties' categories too
		//{
		//	const PropertyBuckets buckets = BuildPropertyBuckets(typeMetadata);

		//	for (size_t propertyIndex : buckets.Uncategorized)
		//	{
		//		bAnyChanged |= DrawPropertyRow(
		//			object,
		//			typeMetadata.Properties[propertyIndex],
		//			drawContext,
		//			typeMetadata);
		//	}

		//	for (const auto& [category, indices] : buckets.ByCategory)
		//	{
		//		// Nested categories stay lightweight for now.
		//		ImGui::TextDisabled("%s", category.c_str());

		//		for (size_t propertyIndex : indices)
		//		{
		//			bAnyChanged |= DrawPropertyRow(
		//				object,
		//				typeMetadata.Properties[propertyIndex],
		//				drawContext,
		//				typeMetadata);
		//		}
		//	}
		//}

		for (size_t i = 0; i < typeMetadata.PropertyCount; ++i)
		{
			const auto& property = typeMetadata.Properties[i];
			if (IsPropertyHidden(property))
			{
				continue;
			}

			bAnyChanged |= DrawPropertyRow(
				object,
				property,
				drawContext,
				typeMetadata);
		}

		return bAnyChanged;
	}

	static bool DrawStructPropertyRow(
		void* object,
		const Nyx::Reflection::PropertyMetadata& property,
		Nyx::Editor::InspectorDrawContext& drawContext,
		const Nyx::Reflection::TypeMetadata& ownerType)
	{
		bool bAnyChanged = false;

		const Nyx::Reflection::TypeMetadata* nestedType =
			Nyx::Reflection::TryGetNestedType(property);

		if (!nestedType)
		{
			return false;
		}

		ImGui::PushID(property.Name);

		const std::string rowLabel = std::string(GetPropertyDisplayName(property)) + "##StructNode";
		const bool bOpen = BeginPropertyGroupRow(rowLabel.c_str());
		DrawPropertyTooltipIfHovered(property);
		if (bOpen)
		{
			DrawPropertyProblems(drawContext);
		}

		if (bOpen)
		{
			// Edits of the struct's fields record where it is, so undo can find it: the component
			// (the owner of the outermost struct), then each struct property on the way
			const Nyx::Editor::SubobjectPath outerLocation = drawContext.CurrentLocation;
			if (!drawContext.CurrentLocation.SubobjectType)
			{
				drawContext.CurrentLocation.SubobjectType = &ownerType;
			}
			drawContext.CurrentLocation.PropertyIndices.push_back(static_cast<size_t>(&property - ownerType.Properties));

			void* nestedObject = Nyx::Reflection::GetPropertyAddress(object, property);
			bAnyChanged |= DrawNestedStructRows(nestedObject, *nestedType, drawContext);

			drawContext.CurrentLocation = outerLocation;
			Nyx::UI::TreeRowEnd();
		}

		ImGui::PopID();
		return bAnyChanged;
	}

	// What an element's context menu or the Add row asks for
	struct ArrayEdit
	{
		enum class EKind : uint8_t
		{
			Add,
			InsertAbove,
			Remove,
			MoveUp,
			MoveDown
		};

		EKind Kind = EKind::Add;
		size_t Index = 0;
	};

	// A struct element's name for its row: the value of its first string property, e.g. a
	// binding's action
	static std::string GetElementSummary(const void* element, const Nyx::Reflection::TypeMetadata& elementType)
	{
		for (size_t i = 0; i < elementType.PropertyCount; ++i)
		{
			const Nyx::Reflection::PropertyMetadata& property = elementType.Properties[i];
			if (property.Kind == Nyx::Reflection::EPropertyKind::String && !IsPropertyHidden(property))
			{
				return std::get<std::string>(Nyx::Reflection::GetPropertyValue(element, property));
			}
		}
		return {};
	}

	static void DrawElementMenu(size_t index, size_t count, std::optional<ArrayEdit>& outEdit)
	{
		if (ImGui::BeginPopupContextItem("##ElementMenu"))
		{
			if (Nyx::UI::MenuItem("Insert Above"))
			{
				outEdit = ArrayEdit{ ArrayEdit::EKind::InsertAbove, index };
			}
			if (Nyx::UI::MenuItem("Move Up", nullptr, false, index > 0))
			{
				outEdit = ArrayEdit{ ArrayEdit::EKind::MoveUp, index };
			}
			if (Nyx::UI::MenuItem("Move Down", nullptr, false, index + 1 < count))
			{
				outEdit = ArrayEdit{ ArrayEdit::EKind::MoveDown, index };
			}
			Nyx::UI::Separator();
			if (Nyx::UI::MenuItem("Remove"))
			{
				outEdit = ArrayEdit{ ArrayEdit::EKind::Remove, index };
			}
			ImGui::EndPopup();
		}
	}

	// Adds, removes or moves elements as one undo step: the whole array before and after
	static void ApplyArrayEdit(
		void* object,
		const Nyx::Reflection::PropertyMetadata& property,
		const Nyx::Reflection::ArrayMetadata& array,
		Nyx::Editor::InspectorDrawContext& drawContext,
		const Nyx::Reflection::TypeMetadata& ownerType,
		const ArrayEdit& edit)
	{
		const size_t propertyIndex = static_cast<size_t>(&property - ownerType.Properties);
		std::vector<std::byte> before = Nyx::Editor::SaveReflectedProperty(object, ownerType, propertyIndex);

		void* arrayAddress = Nyx::Reflection::GetPropertyAddress(object, property);
		const size_t count = array.GetSize(arrayAddress);
		switch (edit.Kind)
		{
		case ArrayEdit::EKind::Add:         array.Insert(arrayAddress, count); break;
		case ArrayEdit::EKind::InsertAbove: array.Insert(arrayAddress, edit.Index); break;
		case ArrayEdit::EKind::Remove:      array.Erase(arrayAddress, edit.Index); break;
		case ArrayEdit::EKind::MoveUp:      array.Swap(arrayAddress, edit.Index, edit.Index - 1); break;
		case ArrayEdit::EKind::MoveDown:    array.Swap(arrayAddress, edit.Index, edit.Index + 1); break;
		}

		// A named entry put in gets a name of its own
		const bool bPutIn = edit.Kind == ArrayEdit::EKind::Add || edit.Kind == ArrayEdit::EKind::InsertAbove;
		const Nyx::Reflection::PropertyMetadata* nameProperty = Nyx::FindEntryNameProperty(array);
		if (bPutIn && drawContext.bNameNewEntries && nameProperty)
		{
			std::string name = Nyx::MakeEntryName(arrayAddress, array, "Entry");
			Nyx::Reflection::AccessProperty<std::string>(array.GetElement(arrayAddress, edit.Kind == ArrayEdit::EKind::Add ? count : edit.Index), *nameProperty) =
				std::move(name);
		}

		if (!drawContext.Transactions || !drawContext.CurrentObjectRef.IsValid())
		{
			return;
		}

		Nyx::Editor::Transaction transaction;
		switch (edit.Kind)
		{
		case ArrayEdit::EKind::Remove:   transaction.Label = "Remove Element"; break;
		case ArrayEdit::EKind::MoveUp:
		case ArrayEdit::EKind::MoveDown: transaction.Label = "Move Element"; break;
		default:                         transaction.Label = "Add Element"; break;
		}
		transaction.Changes.push_back({ Nyx::Editor::EChangeKind::ReplaceArray,
			Nyx::Editor::ReplaceArrayChange{ .Target = drawContext.CurrentObjectRef,
				.TypeMetadata = &ownerType,
				.PropertyIndex = propertyIndex,
				.Location = drawContext.CurrentLocation,
				.Before = std::move(before),
				.After = Nyx::Editor::SaveReflectedProperty(object, ownerType, propertyIndex) } });
		drawContext.Transactions->Push(std::move(transaction));
	}

	static bool DrawArrayPropertyRow(
		void* object,
		const Nyx::Reflection::PropertyMetadata& property,
		Nyx::Editor::InspectorDrawContext& drawContext,
		const Nyx::Reflection::TypeMetadata& ownerType)
	{
		const Nyx::Reflection::ArrayMetadata& array = *Nyx::Reflection::TryGetArray(property);
		const Nyx::Reflection::TypeMetadata* elementType = Nyx::Editor::GetArrayElementType(array);
		void* arrayAddress = Nyx::Reflection::GetPropertyAddress(object, property);
		const size_t count = array.GetSize(arrayAddress);
		const bool bStructs = Nyx::Reflection::IsStructProperty(array.Element);
		const bool bReadOnly = IsPropertyReadOnly(property);

		bool bAnyChanged = false;
		ImGui::PushID(property.Name);

		const std::string rowLabel = std::string(GetPropertyDisplayName(property)) + " (" + std::to_string(count) + ")###ArrayNode";
		const bool bOpen = BeginPropertyGroupRow(rowLabel.c_str());
		DrawPropertyTooltipIfHovered(property);

		if (bOpen)
		{
			DrawPropertyProblems(drawContext);

			if (bReadOnly)
			{
				ImGui::BeginDisabled();
			}

			// Each element's edits record where it is: the array's owner, the array, the element
			const Nyx::Editor::SubobjectPath outerLocation = drawContext.CurrentLocation;
			const std::string arrayPath = drawContext.CurrentPath;
			std::optional<ArrayEdit> edit;
			for (size_t index = 0; index < count; ++index)
			{
				ImGui::PushID(static_cast<int>(index));
				drawContext.CurrentLocation = outerLocation;
				if (!drawContext.CurrentLocation.SubobjectType)
				{
					drawContext.CurrentLocation.SubobjectType = &ownerType;
				}
				drawContext.CurrentLocation.PropertyIndices.push_back(static_cast<size_t>(&property - ownerType.Properties));
				drawContext.CurrentLocation.PropertyIndices.push_back(index);

				void* element = array.GetElement(arrayAddress, index);
				const std::string elementLabel = "[" + std::to_string(index) + "]";
				const PropertyPathScope elementPath(drawContext, Nyx::GetElementPath(arrayPath, element, bStructs ? elementType : nullptr, index));
				if (bStructs)
				{
					const std::string summary = GetElementSummary(element, *elementType);
					const std::string label = elementLabel + (summary.empty() ? "" : " " + summary) + "###Element";
					const bool bElementOpen = BeginPropertyGroupRow(label.c_str());
					DrawElementMenu(index, count, edit);
					if (bElementOpen)
					{
						DrawPropertyProblems(drawContext);
						bAnyChanged |= DrawNestedStructRows(element, *elementType, drawContext);
						Nyx::UI::TreeRowEnd();
					}
				}
				else
				{
					bAnyChanged |= DrawSingleLeafPropertyRow(element, array.Element, drawContext, *elementType, elementLabel.c_str(),
						[&]()
						{
							DrawElementMenu(index, count, edit);
						});
				}
				ImGui::PopID();
			}
			drawContext.CurrentLocation = outerLocation;

			Nyx::UI::BeginPropertyRow("##AddRow");
			if (NYX_UI(Nyx::UI::Button("Add")))
			{
				edit = ArrayEdit{ ArrayEdit::EKind::Add, count };
			}
			Nyx::UI::EndPropertyRow();

			// After drawing, so no element is drawn after it moved
			if (edit)
			{
				ApplyArrayEdit(object, property, array, drawContext, ownerType, *edit);
				bAnyChanged = true;
			}

			if (bReadOnly)
			{
				ImGui::EndDisabled();
			}
			Nyx::UI::TreeRowEnd();
		}

		ImGui::PopID();
		return bAnyChanged;
	}
}

bool Nyx::Editor::ReflectedTypeHasMatch(const Nyx::Reflection::TypeMetadata& typeMetadata, std::string_view filter)
{
	if (TypeHasFilterMatch(typeMetadata, filter, MaxFilterDepth))
	{
		return true;
	}
	for (size_t index = 0; index < typeMetadata.PropertyCount; ++index)
	{
		const Nyx::Reflection::PropertyMetadata& property = typeMetadata.Properties[index];
		const char* category = GetPropertyCategory(property);
		if (!IsPropertyHidden(property) && category && category[0] != '\0' && Nyx::Editor::NameMatchesFilter(category, filter))
		{
			return true;
		}
	}
	return false;
}

bool Nyx::Editor::DrawReflectedProperty(
	void* object,
	const Nyx::Reflection::TypeMetadata& typeMetadata,
	size_t propertyIndex,
	Nyx::Editor::InspectorDrawContext& drawContext)
{
	if (propertyIndex >= typeMetadata.PropertyCount)
	{
		return false;
	}
	return DrawPropertyRow(object, typeMetadata.Properties[propertyIndex], drawContext, typeMetadata);
}

bool Nyx::Editor::DrawReflectedTypeTable(
	void* object,
	const Nyx::Reflection::TypeMetadata& typeMetadata,
	Nyx::Editor::InspectorDrawContext& drawContext)
{
	bool bAnyChanged = false;

	const PropertyBuckets buckets = BuildPropertyBuckets(typeMetadata);

	for (size_t propertyIndex : buckets.Uncategorized)
	{
		bAnyChanged |= DrawPropertyRow(object, typeMetadata.Properties[propertyIndex], drawContext, typeMetadata);
	}

	// A category is a tree row, with its properties inside it. In an ID scope of their own: a
	// category may be named like its component ("Rigid Body"), whose header shares the scope, or
	// like a property
	ImGui::PushID("##Categories");
	const std::string_view filter = drawContext.PropertyFilter;
	for (const auto& [category, indices] : buckets.ByCategory)
	{
		// A search: a category with it in its name is drawn whole, another for what it holds
		const bool bWhole = Nyx::Editor::NameMatchesFilter(category, filter);
		const bool bShown = bWhole || std::any_of(indices.begin(), indices.end(),
			[&](size_t propertyIndex)
			{
				return PropertyHasFilterMatch(typeMetadata.Properties[propertyIndex], filter, MaxFilterDepth);
			});
		if (bShown && BeginPropertyGroupRow(category.c_str()))
		{
			drawContext.PropertyFilter = bWhole ? std::string_view{} : filter;
			for (size_t propertyIndex : indices)
			{
				bAnyChanged |= DrawPropertyRow(object, typeMetadata.Properties[propertyIndex], drawContext, typeMetadata);
			}
			drawContext.PropertyFilter = filter;

			Nyx::UI::TreeRowEnd();
		}
	}
	ImGui::PopID();

	return bAnyChanged;
}
