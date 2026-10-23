#include "ReflectedPropertyDrawer.h"
#include "AssetDatabase.h"
#include "AssetDragDrop.h"
#include "AssetReference.h"
#include "ReflectedPropertyAccess.h"
#include "TransactionDiffUtil.h"

#include "PropertyWidgetRegistry.h"
#include "ReflectionUtils.h"
#include "ImGuiSource.h"
#include "PropertyValueAccess.h"
#include "ReflectionSourceRegistry.h"
#include "UIWidgets.h"

#include <functional>
#include <map>
#include <optional>
#include <string>
#include <string_view>

#include <imgui.h>

// The rows are painted (Nyx::UI, UIWidgets.h): a property row per property, with its label in a
// column on the left and its value's widget on the right, and a tree row for each category and
// each struct, which opens and closes the rows inside it. An array is a tree row too, with a row
// per element (a tree row for a struct) and an Add row; each element's context menu inserts,
// removes and moves elements.

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

	static const char* GetPropertyDisplayName(const Nyx::Reflection::PropertyMetadata& property)
	{
		return (property.DisplayName && property.DisplayName[0]) ? property.DisplayName : property.Name;
	}

	static void DrawPropertyTooltipIfHovered(const Nyx::Reflection::PropertyMetadata& property)
	{
		// Read-only properties are drawn disabled, and their tooltips should show too
		if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
		{
			if (const char* tooltip = GetPropertyTooltip(property))
			{
				ImGui::SetTooltip("%s", tooltip);
			}
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
		ImGui::PopID();
		return bChanged;
	}

	static bool DrawArrayPropertyRow(
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
		Nyx::UI::SourceDeclarationScope declaration(Nyx::Editor::ReflectionSourceRegistry::Get().Find(property));
		if (Nyx::Reflection::IsStructProperty(property) &&
			Nyx::Reflection::TryGetNestedType(property) == &Nyx::Reflection::GetTypeMetadata<Nyx::AssetReference>())
		{
			return DrawAssetReferenceRow(object, property, drawContext, ownerType);
		}
		if (Nyx::Reflection::IsStructProperty(property))
		{
			return DrawStructPropertyRow(object, property, drawContext, ownerType);
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
			if (bReadOnly)
			{
				ImGui::BeginDisabled();
			}

			// Each element's edits record where it is: the array's owner, the array, the element
			const Nyx::Editor::SubobjectPath outerLocation = drawContext.CurrentLocation;
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
				if (bStructs)
				{
					const std::string summary = GetElementSummary(element, *elementType);
					const std::string label = elementLabel + (summary.empty() ? "" : " " + summary) + "###Element";
					const bool bElementOpen = BeginPropertyGroupRow(label.c_str());
					DrawElementMenu(index, count, edit);
					if (bElementOpen)
					{
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
	for (const auto& [category, indices] : buckets.ByCategory)
	{
		if (BeginPropertyGroupRow(category.c_str()))
		{
			for (size_t propertyIndex : indices)
			{
				bAnyChanged |= DrawPropertyRow(object, typeMetadata.Properties[propertyIndex], drawContext, typeMetadata);
			}

			Nyx::UI::TreeRowEnd();
		}
	}
	ImGui::PopID();

	return bAnyChanged;
}
