#include "ReflectedPropertyDrawer.h"

#include "PropertyWidgetRegistry.h"
#include "ReflectionUtils.h"

#include <map>
#include <string_view>

#include <imgui.h>
#include <imgui_internal.h>

namespace
{
	static constexpr float DetailsRowHeight = 24.0f;
	static constexpr float PropertyIndentPerDepth = 16.0f;
	static constexpr float PropertyLabelPaddingX = 8.0f;

	static float GetDetailsFramePaddingY()
	{
		return ImMax(3.0f, floorf((DetailsRowHeight - ImGui::GetFontSize()) * 0.5f));
	}

	struct ScopedDetailsFramePadding
	{
		ScopedDetailsFramePadding()
		{
			ImGui::PushStyleVar(
				ImGuiStyleVar_FramePadding,
				ImVec2(6.0f, GetDetailsFramePaddingY()));
		}

		~ScopedDetailsFramePadding()
		{
			ImGui::PopStyleVar();
		}
	};

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

	static void DrawPropertyTooltipIfHovered(const Nyx::Reflection::PropertyMetadata& property)
	{
		if (ImGui::IsItemHovered())
		{
			if (const char* tooltip = GetPropertyTooltip(property))
			{
				ImGui::SetTooltip("%s", tooltip);
			}
		}
	}

	static void DrawIndentedLabel(const char* label, int depth)
	{
		const float offset = PropertyLabelPaddingX + PropertyIndentPerDepth * static_cast<float>(depth);

		ImGui::Dummy(ImVec2(offset, 0.0f));
		ImGui::SameLine(0.0f, 0.0f);
		ImGui::AlignTextToFramePadding();
		ImGui::TextUnformatted(label);
	}

	struct ScopedCategoryHeaderStyle
	{
		ScopedCategoryHeaderStyle()
		{
			ImGui::PushStyleColor(ImGuiCol_Header, IM_COL32(55, 55, 55, 180));
			ImGui::PushStyleColor(ImGuiCol_HeaderHovered, IM_COL32(72, 72, 72, 220));
			ImGui::PushStyleColor(ImGuiCol_HeaderActive, IM_COL32(82, 82, 82, 220));
			ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(6.0f, 4.0f));
		}

		~ScopedCategoryHeaderStyle()
		{
			ImGui::PopStyleVar();
			ImGui::PopStyleColor(3);
		}
	};

	static void ApplyHoveredRowHighlight(float rowTopY, float rowBottomY)
	{
		ImGuiTable* table = ImGui::GetCurrentTable();
		if (!table)
		{
			return;
		}

		const ImVec2 rowMin(table->OuterRect.Min.x, rowTopY);
		const ImVec2 rowMax(table->OuterRect.Max.x, rowBottomY);

		if (ImGui::IsMouseHoveringRect(rowMin, rowMax, false))
		{
			ImGui::TableSetBgColor(
				ImGuiTableBgTarget_RowBg0,
				IM_COL32(255, 255, 255, 18));
		}
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

	static bool DrawSingleLeafPropertyRow(
		void* object,
		const Nyx::Reflection::PropertyMetadata& property,
		Nyx::Editor::InspectorDrawContext& drawContext,
		const Nyx::Reflection::TypeMetadata& ownerType,
		int depth)
	{
		bool bAnyChanged = false;

		ImGui::PushID(property.Name);

		ImGui::TableNextRow(0, DetailsRowHeight);
		const float rowTopY = ImGui::GetCursorScreenPos().y;
		float rowBottomY = rowTopY + DetailsRowHeight;

		ImGui::TableSetColumnIndex(0);

		const char* displayName =
			(property.DisplayName && property.DisplayName[0]) ? property.DisplayName : property.Name;

		DrawIndentedLabel(displayName, depth);

		rowBottomY = (std::max)(rowBottomY, ImGui::GetItemRectMax().y);

		ImGui::TableSetColumnIndex(1);

		const bool bReadOnly = IsPropertyReadOnly(property);
		if (bReadOnly)
		{
			ImGui::BeginDisabled();
		}

		ImGui::SetNextItemWidth(-FLT_MIN);

		if (Nyx::Editor::PropertyWidgetDrawFn drawFn =
			Nyx::Editor::PropertyWidgetRegistry::Get().Find(property.Kind))
		{
			Nyx::Editor::PropertyWidgetArgs args{};
			args.Object = object;
			args.Property = &property;
			args.OwnerType = &ownerType;
			args.DrawContext = &drawContext;

			bAnyChanged = drawFn(args);
			rowBottomY = (std::max)(rowBottomY, ImGui::GetItemRectMax().y);
		}
		else
		{
			ImGui::TextDisabled("<unsupported>");
			rowBottomY = (std::max)(rowBottomY, ImGui::GetItemRectMax().y);
		}

		if (bReadOnly)
		{
			ImGui::EndDisabled();
		}

		DrawPropertyTooltipIfHovered(property);
		ApplyHoveredRowHighlight(rowTopY, rowBottomY);

		ImGui::PopID();
		return bAnyChanged;
	}

	static bool DrawReflectedTypeRows(
		void* object,
		const Nyx::Reflection::TypeMetadata& typeMetadata,
		Nyx::Editor::InspectorDrawContext& drawContext,
		int depth);

	static bool DrawStructPropertyRow(
		void* object,
		const Nyx::Reflection::PropertyMetadata& property,
		Nyx::Editor::InspectorDrawContext& drawContext,
		int depth);

	static void DrawCategoryRow(const std::string& category, int depth)
	{
		ImGui::TableNextRow();
		ImGui::TableSetColumnIndex(0);

		ImGui::Dummy(ImVec2(PropertyIndentPerDepth * static_cast<float>(depth), 0.0f));
		ImGui::SameLine(0.0f, 0.0f);
		ImGui::SeparatorText(category.c_str());

		ImGui::TableSetColumnIndex(1);
		ImGui::TextUnformatted("");
	}

	static bool DrawPropertyRow(
		void* object,
		const Nyx::Reflection::PropertyMetadata& property,
		Nyx::Editor::InspectorDrawContext& drawContext,
		const Nyx::Reflection::TypeMetadata& ownerType,
		int depth)
	{
		if (Nyx::Reflection::IsStructProperty(property))
		{
			return DrawStructPropertyRow(object, property, drawContext, depth);
		}

		return DrawSingleLeafPropertyRow(object, property, drawContext, ownerType, depth);
	}

	static bool DrawCategoryHeader(const std::string& category)
	{
		ImGui::TableNextRow();
		ImGui::TableSetColumnIndex(0);

		const ImGuiTreeNodeFlags flags =
			ImGuiTreeNodeFlags_SpanFullWidth |
			ImGuiTreeNodeFlags_DefaultOpen;

		const bool bOpen = ImGui::TreeNodeEx(category.c_str(), flags, "%s", category.c_str());

		ImGui::TableSetColumnIndex(1);
		ImGui::TextUnformatted("");

		return bOpen;
	}

	static bool DrawReflectedTypeRows(
		void* object,
		const Nyx::Reflection::TypeMetadata& typeMetadata,
		Nyx::Editor::InspectorDrawContext& drawContext,
		int depth)
	{
		bool bAnyChanged = false;

		const PropertyBuckets buckets = BuildPropertyBuckets(typeMetadata);

		// Uncategorized properties first
		for (size_t propertyIndex : buckets.Uncategorized)
		{
			bAnyChanged |= DrawPropertyRow(
				object,
				typeMetadata.Properties[propertyIndex],
				drawContext,
				typeMetadata,
				depth);
		}

		// Only top-level categories become collapsible blocks.
		if (depth == 0)
		{
			for (const auto& [category, indices] : buckets.ByCategory)
			{
				if (DrawCategoryHeader(category))
				{
					for (size_t propertyIndex : indices)
					{
						bAnyChanged |= DrawPropertyRow(
							object,
							typeMetadata.Properties[propertyIndex],
							drawContext,
							typeMetadata,
							depth + 1);
					}

					ImGui::TreePop();
				}
			}
		}
		else
		{
			// Nested structs' category grouping is kept flat
			for (const auto& [category, indices] : buckets.ByCategory)
			{
				ImGui::TableNextRow();
				ImGui::TableSetColumnIndex(0);

				ImGui::Dummy(ImVec2(PropertyIndentPerDepth * static_cast<float>(depth), 0.0f));
				ImGui::SameLine(0.0f, 0.0f);
				ImGui::TextDisabled("%s", category.c_str());

				ImGui::TableSetColumnIndex(1);
				ImGui::TextUnformatted("");

				for (size_t propertyIndex : indices)
				{
					bAnyChanged |= DrawPropertyRow(
						object,
						typeMetadata.Properties[propertyIndex],
						drawContext,
						typeMetadata,
						depth + 1);
				}
			}
		}

		return bAnyChanged;
	}

	static bool DrawPropertyTableForIndices(
		void* object,
		const Nyx::Reflection::TypeMetadata& typeMetadata,
		Nyx::Editor::InspectorDrawContext& drawContext,
		const std::vector<size_t>& propertyIndices,
		int depth)
	{
		bool bAnyChanged = false;

		//ImGui::PushStyleColor(ImGuiCol_TableBorderStrong, IM_COL32(95, 95, 95, 255));
		ImGui::PushStyleColor(ImGuiCol_TableBorderLight, IM_COL32(70, 70, 70, 70));

		const ImGuiTableFlags tableFlags =
			//ImGuiTableFlags_Borders |
			ImGuiTableFlags_BordersInnerV |
			ImGuiTableFlags_BordersInnerH |
			ImGuiTableFlags_SizingStretchProp;

		if (ImGui::BeginTable("##Properties", 2, tableFlags))
		{
			ImGui::TableSetupColumn("Property", ImGuiTableColumnFlags_WidthStretch, 0.45f);
			ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch, 0.55f);

			for (size_t propertyIndex : propertyIndices)
			{
				bAnyChanged |= DrawPropertyRow(
					object,
					typeMetadata.Properties[propertyIndex],
					drawContext,
					typeMetadata,
					depth);
			}

			ImGui::EndTable();
		}

		ImGui::PopStyleColor(1); // 2

		return bAnyChanged;
	}

	static bool DrawNestedStructRows(
		void* object,
		const Nyx::Reflection::TypeMetadata& typeMetadata,
		Nyx::Editor::InspectorDrawContext& drawContext,
		int depth)
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
		//			typeMetadata,
		//			depth);
		//	}

		//	for (const auto& [category, indices] : buckets.ByCategory)
		//	{
		//		// Nested categories stay lightweight for now.
		//		ImGui::TableNextRow();
		//		ImGui::TableSetColumnIndex(0);

		//		ImGui::Dummy(ImVec2(PropertyIndentPerDepth * static_cast<float>(depth), 0.0f));
		//		ImGui::SameLine(0.0f, 0.0f);
		//		ImGui::TextDisabled("%s", category.c_str());

		//		ImGui::TableSetColumnIndex(1);
		//		ImGui::TextUnformatted("");

		//		for (size_t propertyIndex : indices)
		//		{
		//			bAnyChanged |= DrawPropertyRow(
		//				object,
		//				typeMetadata.Properties[propertyIndex],
		//				drawContext,
		//				typeMetadata,
		//				depth + 1);
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
				typeMetadata,
				depth);
		}

		return bAnyChanged;
	}

	static bool DrawStructPropertyRow(
		void* object,
		const Nyx::Reflection::PropertyMetadata& property,
		Nyx::Editor::InspectorDrawContext& drawContext,
		int depth)
	{
		bool bAnyChanged = false;

		const Nyx::Reflection::TypeMetadata* nestedType =
			Nyx::Reflection::TryGetNestedType(property);

		if (!nestedType)
		{
			return false;
		}

		ImGui::PushID(property.Name);

		ImGui::TableNextRow(0, DetailsRowHeight);
		const float rowTopY = ImGui::GetCursorScreenPos().y;
		float rowBottomY = rowTopY + DetailsRowHeight;

		ImGui::TableSetColumnIndex(0);

		const char* displayName =
			(property.DisplayName && property.DisplayName[0]) ? property.DisplayName : property.Name;

		const float offset = PropertyLabelPaddingX + PropertyIndentPerDepth * static_cast<float>(depth);
		ImGui::Dummy(ImVec2(offset, 0.0f));
		ImGui::SameLine(0.0f, 0.0f);

		ScopedDetailsFramePadding rowPadding;

		ImGui::PushStyleColor(ImGuiCol_Header, IM_COL32(0, 0, 0, 0));
		ImGui::PushStyleColor(ImGuiCol_HeaderHovered, IM_COL32(0, 0, 0, 0));
		ImGui::PushStyleColor(ImGuiCol_HeaderActive, IM_COL32(0, 0, 0, 0));

		const ImGuiTreeNodeFlags flags =
			ImGuiTreeNodeFlags_SpanAvailWidth |
			ImGuiTreeNodeFlags_DefaultOpen;

		const bool bOpen = ImGui::TreeNodeEx("##StructNode", flags, "%s", displayName);

		ImGui::PopStyleColor(3);

		rowBottomY = (std::max)(rowBottomY, ImGui::GetItemRectMax().y);

		ImGui::TableSetColumnIndex(1);
		ImGui::TextUnformatted("");
		rowBottomY = (std::max)(rowBottomY, ImGui::GetItemRectMax().y);

		DrawPropertyTooltipIfHovered(property);
		ApplyHoveredRowHighlight(rowTopY, rowBottomY);

		if (bOpen)
		{
			void* nestedObject = Nyx::Reflection::GetPropertyAddress(object, property);
			bAnyChanged |= DrawNestedStructRows(nestedObject, *nestedType, drawContext, depth + 1);
			ImGui::TreePop();
		}

		ImGui::PopID();
		return bAnyChanged;
	}

	static bool DrawCategorySectionHeader(const std::string& category)
	{
		ImGui::PushID(category.c_str());

		ImGuiStorage* storage = ImGui::GetStateStorage();
		const ImGuiID openId = ImGui::GetID("##Open");
		bool bOpen = storage->GetBool(openId, true);

		const float fullWidth = ImGui::GetContentRegionAvail().x;
		const ImVec2 rowSize(fullWidth, DetailsRowHeight);

		ImGui::InvisibleButton("##CategoryRow", rowSize);

		const ImVec2 rowMin = ImGui::GetItemRectMin();
		const ImVec2 rowMax = ImGui::GetItemRectMax();
		const bool bHovered = ImGui::IsItemHovered();
		const bool bClicked = ImGui::IsItemClicked();

		if (bClicked)
		{
			bOpen = !bOpen;
			storage->SetBool(openId, bOpen);
		}

		ImDrawList* drawList = ImGui::GetWindowDrawList();

		if (bHovered)
		{
			drawList->AddRectFilled(rowMin, rowMax, IM_COL32(255, 255, 255, 10));
		}

		const float arrowX = rowMin.x + 8.0f;
		const float arrowCenterY = rowMin.y + DetailsRowHeight * 0.5f;
		const ImGuiDir arrowDir = bOpen ? ImGuiDir_Down : ImGuiDir_Right;

		ImGui::RenderArrow(
			drawList,
			ImVec2(arrowX, arrowCenterY - 4.0f),
			IM_COL32(220, 220, 220, 255),
			arrowDir,
			0.80f);

		const ImVec2 textSize = ImGui::CalcTextSize(category.c_str());
		const float textX = rowMin.x + 22.0f;
		const float textY = rowMin.y + floorf((DetailsRowHeight - textSize.y) * 0.5f);

		drawList->AddText(
			ImVec2(textX, textY),
			IM_COL32(220, 220, 220, 255),
			category.c_str());

		ImGui::PopID();
		return bOpen;
	}
}

bool Nyx::Editor::DrawReflectedTypeTable(
	void* object,
	const Nyx::Reflection::TypeMetadata& typeMetadata,
	Nyx::Editor::InspectorDrawContext& drawContext)
{
	bool bAnyChanged = false;

	const PropertyBuckets buckets = BuildPropertyBuckets(typeMetadata);

	if (!buckets.Uncategorized.empty())
	{
		bAnyChanged |= DrawPropertyTableForIndices(
			object,
			typeMetadata,
			drawContext,
			buckets.Uncategorized,
			0);
	}

	for (const auto& [category, indices] : buckets.ByCategory)
	{
		if (DrawCategorySectionHeader(category))
		{
			const ImVec2 oldItemSpacing = ImGui::GetStyle().ItemSpacing;
			ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(oldItemSpacing.x, 0.0f));

			bAnyChanged |= DrawPropertyTableForIndices(
				object,
				typeMetadata,
				drawContext,
				indices,
				0);

			ImGui::PopStyleVar();
		}
	}

	return bAnyChanged;
}