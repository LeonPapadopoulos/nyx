#include "InputTuningView.h"

#include "ImGuiSource.h"
#include "InputAssetDocument.h"
#include "InputSystem.h"
#include "ReflectedPropertyAccess.h"
#include "ReflectedPropertyDrawer.h"
#include "ReflectionUtils.h"
#include "TransactionSystem.h"
#include "UITheme.h"
#include "UIWidgets.h"
#include "WindowPainter.h"

#include <imgui.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>

namespace Nyx::Editor
{
	namespace
	{
		using namespace Nyx::Input;
		using Nyx::UI::EPainterLineCap;
		using Nyx::UI::PainterRect;
		using Nyx::UI::PainterStyle;

		const Nyx::Reflection::TypeMetadata& AssetType()
		{
			return Nyx::Reflection::GetTypeMetadata<InputAssetData>();
		}

		size_t PropertyIndex(const Nyx::Reflection::TypeMetadata& type, const char* name)
		{
			return Nyx::Reflection::FindPropertyIndexByName(type, name).value_or(0);
		}

		// Where an element of one of the asset's arrays is, for the details table's edits
		SubobjectPath ElementPath(const char* arrayName, size_t index)
		{
			return SubobjectPath{ .SubobjectType = &AssetType(), .PropertyIndices = { PropertyIndex(AssetType(), arrayName), index } };
		}

		SubobjectPath BindingPath(size_t context, size_t binding)
		{
			SubobjectPath path = ElementPath("Contexts", context);
			path.PropertyIndices.push_back(PropertyIndex(Nyx::Reflection::GetTypeMetadata<InputContextData>(), "Bindings"));
			path.PropertyIndices.push_back(binding);
			return path;
		}

		// One of the asset's arrays as it is now, to record its change as one undo step
		ReplaceArrayChange BeginArrayChange(const void* owner, const Nyx::Reflection::TypeMetadata& ownerType, const char* arrayName, SubobjectPath location)
		{
			const size_t propertyIndex = PropertyIndex(ownerType, arrayName);
			return ReplaceArrayChange{ .Target = InputAssetDocument::GetRef(),
				.TypeMetadata = &ownerType,
				.PropertyIndex = propertyIndex,
				.Location = std::move(location),
				.Before = SaveReflectedProperty(owner, ownerType, propertyIndex) };
		}

		Change EndArrayChange(ReplaceArrayChange change, const void* owner)
		{
			change.After = SaveReflectedProperty(owner, *change.TypeMetadata, change.PropertyIndex);
			return Change{ EChangeKind::ReplaceArray, std::move(change) };
		}

		PainterRect Reserve(glm::vec2 size)
		{
			ImGui::Dummy(ImVec2(size.x, size.y));
			const ImVec2 min = ImGui::GetItemRectMin();
			const ImVec2 max = ImGui::GetItemRectMax();
			return PainterRect{ { min.x, min.y }, { max.x, max.y } };
		}

		std::string Format(const char* format, double value)
		{
			char text[64];
			std::snprintf(text, sizeof(text), format, value);
			return text;
		}

		void TextWarning(const std::string& text)
		{
			ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(Nyx::UI::GetTheme().Accent.r, Nyx::UI::GetTheme().Accent.g, Nyx::UI::GetTheme().Accent.b, 1.0f));
			ImGui::TextWrapped("%s", text.c_str());
			ImGui::PopStyleColor();
		}

		const InputActionData* FindActionData(const InputAssetData& asset, const std::string& name)
		{
			const auto found = std::find_if(asset.Actions.begin(), asset.Actions.end(),
				[&name](const InputActionData& action)
				{
					return action.Name == name;
				});
			return found != asset.Actions.end() ? &*found : nullptr;
		}

		// Whether the input system has the asset's bindings of this context at the same indices:
		// not if some couldn't be used and were left out
		bool HasAllBindings(const InputAssetDocument& document, const InputContextData& context)
		{
			const std::string prefix = context.Name + ", binding ";
			return std::none_of(document.GetProblems().begin(), document.GetProblems().end(),
				[&prefix](const InputAssetProblem& problem)
				{
					return problem.Where.starts_with(prefix);
				});
		}

		const BindingTrace* FindTrace(const InputAssetDocument& document, const InputSystem& input, size_t contextIndex, size_t bindingIndex)
		{
			const InputContextData& context = document.GetAsset().Contexts[contextIndex];
			const ContextHandle handle = input.FindContext(context.Name);
			if (!handle.IsValid() || !HasAllBindings(document, context))
			{
				return nullptr;
			}
			for (const BindingTrace& trace : input.GetUser().GetTraces())
			{
				if (trace.Context == handle && trace.BindingIndex == bindingIndex)
				{
					return &trace;
				}
			}
			return nullptr;
		}

		bool SameResponse(const ValuePipeline& a, const ValuePipeline& b)
		{
			const DeadZoneSettings& x = a.DeadZone;
			const DeadZoneSettings& y = b.DeadZone;
			return x.bEnabled == y.bEnabled && x.Shape == y.Shape && x.Inner == y.Inner && x.Outer == y.Outer && a.Curve.bEnabled == b.Curve.bEnabled &&
				a.Curve.Kind == b.Curve.Kind && a.Curve.Exponent == b.Curve.Exponent && a.Curve.Points == b.Curve.Points;
		}

		constexpr std::array<EGamepadModel, 4> LayerModels = { EGamepadModel::Unknown, EGamepadModel::XboxOne, EGamepadModel::DualShock4,
			EGamepadModel::DualSense };
	}

	void InputTuningView::SelectBinding(size_t context, size_t binding)
	{
		Selected = ESelection::Binding;
		SelectedContext = context;
		SelectedBinding = binding;
	}

	EGamepadModel InputTuningView::GetShownModel(const InputSystem& input) const
	{
		if (LayerModel > 0)
		{
			return LayerModels[static_cast<size_t>(LayerModel - 1)];
		}
		const InputFrame::DeviceState* device = input.GetFrame().FindDevice(input.GetUser().GetLastUsedDevice());
		return device && device->Info.Kind == EDeviceKind::Gamepad ? device->Info.Model : EGamepadModel::Unknown;
	}

	// ---------------------------------------------------------------------------------------
	// Tuning

	void InputTuningView::DrawTuning(InputAssetDocument& document, TransactionSystem& transactions, InputSystem& input)
	{
		if (!document.IsLoaded())
		{
			ImGui::TextDisabled("No input asset: %s could not be read", GetDefaultInputAssetPath().string().c_str());
			return;
		}

		DrawContext.Transactions = &transactions;
		DrawContext.CurrentObjectRef = InputAssetDocument::GetRef();

		// Undo can take away what was selected
		InputAssetData& asset = document.GetAsset();
		if ((Selected == ESelection::Action && SelectedAction >= asset.Actions.size()) ||
			((Selected == ESelection::Context || Selected == ESelection::Binding) && SelectedContext >= asset.Contexts.size()) ||
			(Selected == ESelection::Binding && SelectedBinding >= asset.Contexts[SelectedContext].Bindings.size()))
		{
			Selected = ESelection::Contexts;
		}

		DrawToolbar(document, transactions, input);

		const float treeWidth = std::max(ImGui::GetContentRegionAvail().x * 0.32f, ImGui::GetFontSize() * 12.0f);
		if (ImGui::BeginChild("##TuningTree", ImVec2(treeWidth, 0.0f), ImGuiChildFlags_None))
		{
			DrawTree(document);
		}
		ImGui::EndChild();
		ImGui::SameLine();
		if (ImGui::BeginChild("##TuningDetails", ImVec2(0.0f, 0.0f), ImGuiChildFlags_None))
		{
			DrawSelection(document, transactions, input);
		}
		ImGui::EndChild();
	}

	void InputTuningView::DrawToolbar(InputAssetDocument& document, TransactionSystem& transactions, InputSystem& input)
	{
		ImGui::BeginDisabled(!document.IsDirty());
		if (NYX_UI(Nyx::UI::Button("Save")))
		{
			document.Save();
		}
		ImGui::EndDisabled();
		ImGui::SameLine();
		if (NYX_UI(Nyx::UI::Button("Add Action...")))
		{
			NewActionName.clear();
			NewActionContext = SelectedContext < document.GetAsset().Contexts.size() ? static_cast<int>(SelectedContext) : 0;
			ImGui::OpenPopup("Add Action");
		}
		DrawAddAction(document, transactions);

		ImGui::SameLine();
		ImGui::AlignTextToFramePadding();
		ImGui::TextDisabled("%s%s", document.GetPath().filename().string().c_str(), document.IsDirty() ? " (changed)" : "");

		// A/B
		ImGui::SameLine();
		if (NYX_UI(Nyx::UI::Button(document.HasA() ? "Keep as A again" : "Keep as A")))
		{
			document.KeepAsA();
		}
		ImGui::SetItemTooltip("Remembers the tuning as it is as A; edits after this are B");
		if (document.HasA())
		{
			ImGui::SameLine();
			const bool bShowingA = document.IsShowingA();
			if (NYX_UI(Nyx::UI::Button(bShowingA ? "Showing A###SwapAB" : "Showing B###SwapAB", glm::vec2(0.0f),
					bShowingA ? Nyx::UI::EButtonTone::Go : Nyx::UI::EButtonTone::Neutral)))
			{
				document.SetShowingA(!bShowingA);
			}
			ImGui::SetItemTooltip("Swaps A and B, here and in every running game. While playing: hold the pad's Back (View, Share, "
								  "Create) and press its top face button.");
			ImGui::SameLine();
			if (NYX_UI(Nyx::UI::Button("Forget A")))
			{
				document.ForgetA();
			}
		}

		// Which profile the values are shown with; the pad in hand by default
		const EGamepadModel inHand = LayerModel == 0 ? GetShownModel(input) : EGamepadModel::Unknown;
		const std::string handLabel = std::string("Pad in hand (") + (inHand == EGamepadModel::Unknown ? "none" : GetGamepadModelName(inHand)) + ")";
		const std::array<const char*, 5> models = { handLabel.c_str(), "No pad profile", "Xbox One", "DualShock 4", "DualSense" };
		ImGui::AlignTextToFramePadding();
		ImGui::TextUnformatted("Layers for");
		ImGui::SameLine();
		ImGui::SetNextItemWidth(ImGui::GetFontSize() * 11.0f);
		NYX_UI(Nyx::UI::Combo("##LayerModel", &LayerModel, models));

		if (document.GetPlayerSettings())
		{
			ImGui::SameLine();
			bool bPlayer = document.IsApplyingPlayerSettings();
			if (NYX_UI(Nyx::UI::Checkbox("With the player's settings", &bPlayer)))
			{
				document.SetApplyPlayerSettings(bPlayer);
			}
		}
		Nyx::UI::Separator();
	}

	void InputTuningView::DrawAddAction(InputAssetDocument& document, TransactionSystem& transactions)
	{
		if (!ImGui::BeginPopup("Add Action"))
		{
			return;
		}

		InputAssetData& asset = document.GetAsset();
		ImGui::TextUnformatted("What is it for?");
		for (int index = 0; index < static_cast<int>(EInputRecipe::Count); ++index)
		{
			const EInputRecipe recipe = static_cast<EInputRecipe>(index);
			if (NYX_UI(Nyx::UI::Selectable(GetRecipeName(recipe), NewActionRecipe == index, ImGuiSelectableFlags_NoAutoClosePopups)))
			{
				NewActionRecipe = index;
			}
		}
		ImGui::PushTextWrapPos(ImGui::GetFontSize() * 26.0f);
		ImGui::TextDisabled("%s", GetRecipeDescription(static_cast<EInputRecipe>(NewActionRecipe)));
		ImGui::PopTextWrapPos();

		ImGui::SetNextItemWidth(ImGui::GetFontSize() * 14.0f);
		NYX_UI(Nyx::UI::InputText("Name", &NewActionName, 0, "e.g. Jump"));

		std::vector<const char*> contexts;
		for (const InputContextData& context : asset.Contexts)
		{
			contexts.push_back(context.Name.c_str());
		}
		ImGui::SetNextItemWidth(ImGui::GetFontSize() * 14.0f);
		NYX_UI(Nyx::UI::Combo("In context", &NewActionContext, contexts));

		const bool bTaken = FindActionData(asset, NewActionName) != nullptr;
		if (bTaken)
		{
			TextWarning("An action is called that already");
		}
		ImGui::BeginDisabled(NewActionName.empty() || bTaken || contexts.empty());
		if (NYX_UI(Nyx::UI::Button("Add")))
		{
			const size_t contextIndex = static_cast<size_t>(std::clamp(NewActionContext, 0, static_cast<int>(asset.Contexts.size()) - 1));

			// The actions and the context's bindings, as one undo step
			ReplaceArrayChange actions = BeginArrayChange(&asset, AssetType(), "Actions", {});
			ReplaceArrayChange bindings = BeginArrayChange(&asset.Contexts[contextIndex], Nyx::Reflection::GetTypeMetadata<InputContextData>(), "Bindings",
				ElementPath("Contexts", contextIndex));
			const size_t firstBinding = asset.Contexts[contextIndex].Bindings.size();
			if (!AddRecipe(asset, contextIndex, static_cast<EInputRecipe>(NewActionRecipe), NewActionName).empty())
			{
				Transaction transaction;
				transaction.Label = "Add Action";
				transaction.Changes.push_back(EndArrayChange(std::move(actions), &asset));
				transaction.Changes.push_back(EndArrayChange(std::move(bindings), &asset.Contexts[contextIndex]));
				transactions.Push(std::move(transaction));
				SelectBinding(contextIndex, firstBinding);
			}
			ImGui::CloseCurrentPopup();
		}
		ImGui::EndDisabled();
		ImGui::EndPopup();
	}

	void InputTuningView::DrawTree(InputAssetDocument& document)
	{
		const InputAssetData& asset = document.GetAsset();
		const std::vector<InputCheckFinding>& findings = document.GetFindings();

		Nyx::UI::TreeRowOptions groupOptions;
		groupOptions.bDefaultOpen = true;

		groupOptions.bSelected = Selected == ESelection::Actions;
		const Nyx::UI::TreeRowResult actionsRow = NYX_UI(Nyx::UI::TreeRow("Actions", groupOptions));
		if (actionsRow.bClicked)
		{
			Selected = ESelection::Actions;
		}
		if (actionsRow.bOpen)
		{
			for (size_t index = 0; index < asset.Actions.size(); ++index)
			{
				Nyx::UI::TreeRowOptions options;
				options.bLeaf = true;
				options.bSelected = Selected == ESelection::Action && SelectedAction == index;
				const std::string label = asset.Actions[index].Name + "##Action" + std::to_string(index);
				if (NYX_UI(Nyx::UI::TreeRow(label.c_str(), options)).bClicked)
				{
					Selected = ESelection::Action;
					SelectedAction = index;
				}
			}
			Nyx::UI::TreeRowEnd();
		}

		groupOptions.bSelected = Selected == ESelection::Contexts;
		const Nyx::UI::TreeRowResult contextsRow = NYX_UI(Nyx::UI::TreeRow("Contexts", groupOptions));
		if (contextsRow.bClicked)
		{
			Selected = ESelection::Contexts;
		}
		if (contextsRow.bOpen)
		{
			for (size_t contextIndex = 0; contextIndex < asset.Contexts.size(); ++contextIndex)
			{
				const InputContextData& context = asset.Contexts[contextIndex];
				Nyx::UI::TreeRowOptions options;
				options.bSelected = Selected == ESelection::Context && SelectedContext == contextIndex;
				const std::string label = context.Name + " (" + std::to_string(context.Priority) + ")##Context" + std::to_string(contextIndex);
				const Nyx::UI::TreeRowResult row = NYX_UI(Nyx::UI::TreeRow(label.c_str(), options));
				if (row.bClicked)
				{
					Selected = ESelection::Context;
					SelectedContext = contextIndex;
				}
				if (!row.bOpen)
				{
					continue;
				}

				for (size_t bindingIndex = 0; bindingIndex < context.Bindings.size(); ++bindingIndex)
				{
					const InputBindingData& binding = context.Bindings[bindingIndex];
					const size_t found = FindingsFor(findings, contextIndex, bindingIndex).size();
					Nyx::UI::TreeRowOptions bindingOptions;
					bindingOptions.bLeaf = true;
					bindingOptions.bSelected = Selected == ESelection::Binding && SelectedContext == contextIndex && SelectedBinding == bindingIndex;
					const std::string bindingLabel = binding.Action + ": " + DescribeControls(binding) + (found > 0 ? "  (!)" : "") + "##Binding" +
						std::to_string(bindingIndex);
					if (NYX_UI(Nyx::UI::TreeRow(bindingLabel.c_str(), bindingOptions)).bClicked)
					{
						SelectBinding(contextIndex, bindingIndex);
					}
				}
				Nyx::UI::TreeRowEnd();
			}
			Nyx::UI::TreeRowEnd();
		}

		Nyx::UI::TreeRowOptions profilesOptions;
		profilesOptions.bLeaf = true;
		profilesOptions.bSelected = Selected == ESelection::PadProfiles;
		const std::string profilesLabel = "Pad profiles (" + std::to_string(asset.PadProfiles.size()) + ")###PadProfiles";
		if (NYX_UI(Nyx::UI::TreeRow(profilesLabel.c_str(), profilesOptions)).bClicked)
		{
			Selected = ESelection::PadProfiles;
		}
	}

	void InputTuningView::DrawSelection(InputAssetDocument& document, TransactionSystem& transactions, InputSystem& input)
	{
		InputAssetData& asset = document.GetAsset();
		const Nyx::Reflection::TypeMetadata& contextType = Nyx::Reflection::GetTypeMetadata<InputContextData>();

		switch (Selected)
		{
		case ESelection::Actions:
			DrawContext.CurrentLocation = {};
			ImGui::TextDisabled("Every action. Right-click one to remove or move it.");
			DrawReflectedProperty(&asset, AssetType(), PropertyIndex(AssetType(), "Actions"), DrawContext);
			break;

		case ESelection::Action:
		{
			InputActionData& action = asset.Actions[SelectedAction];
			DrawContext.CurrentLocation = ElementPath("Actions", SelectedAction);
			DrawReflectedTypeTable(&action, Nyx::Reflection::GetTypeMetadata<InputActionData>(), DrawContext);

			Nyx::UI::SectionHeader("Bound in");
			for (const InputContextData& context : asset.Contexts)
			{
				for (const InputBindingData& binding : context.Bindings)
				{
					if (binding.Action == action.Name)
					{
						ImGui::Text("- %s: %s", context.Name.c_str(), DescribeControls(binding).c_str());
					}
				}
			}
			break;
		}

		case ESelection::Contexts:
			DrawContext.CurrentLocation = {};
			ImGui::TextDisabled("Every context, with its bindings. Right-click one to remove or move it.");
			DrawReflectedProperty(&asset, AssetType(), PropertyIndex(AssetType(), "Contexts"), DrawContext);
			break;

		case ESelection::Context:
			DrawContext.CurrentLocation = ElementPath("Contexts", SelectedContext);
			DrawReflectedTypeTable(&asset.Contexts[SelectedContext], contextType, DrawContext);
			break;

		case ESelection::Binding:
			DrawBinding(document, transactions, input);
			break;

		case ESelection::PadProfiles:
			DrawContext.CurrentLocation = {};
			ImGui::PushTextWrapPos(0.0f);
			ImGui::TextDisabled("Overrides while a pad of a model drives a binding. Field is the value's path in a binding, e.g. "
								"Pipeline.DeadZone.Inner; Context, Action and Control narrow down which bindings, empty for any. Bools "
								"are 0 or 1, enums their number.");
			ImGui::PopTextWrapPos();
			DrawReflectedProperty(&asset, AssetType(), PropertyIndex(AssetType(), "PadProfiles"), DrawContext);
			break;
		}
	}

	void InputTuningView::DrawFix(InputAssetDocument& document, TransactionSystem& transactions, const InputCheckFinding& finding, size_t id)
	{
		if (!finding.Fix)
		{
			return;
		}
		InputAssetData& asset = document.GetAsset();
		const InputCheckFix fix = *finding.Fix;
		const std::string label = fix.Label + "##Fix" + std::to_string(id);
		if (NYX_UI(Nyx::UI::Button(label.c_str())) && fix.Context < asset.Contexts.size())
		{
			ReplaceArrayChange bindings = BeginArrayChange(&asset.Contexts[fix.Context], Nyx::Reflection::GetTypeMetadata<InputContextData>(),
				"Bindings", ElementPath("Contexts", fix.Context));
			if (ApplyFix(asset, fix))
			{
				Transaction transaction;
				transaction.Label = fix.Label;
				transaction.Changes.push_back(EndArrayChange(std::move(bindings), &asset.Contexts[fix.Context]));
				transactions.Push(std::move(transaction));
			}
		}
	}

	void InputTuningView::DrawBinding(InputAssetDocument& document, TransactionSystem& transactions, InputSystem& input)
	{
		InputAssetData& asset = document.GetAsset();
		InputContextData& context = asset.Contexts[SelectedContext];
		InputBindingData& binding = context.Bindings[SelectedBinding];
		const InputActionData* action = FindActionData(asset, binding.Action);

		ImGui::Text("%s, binding %zu: %s", context.Name.c_str(), SelectedBinding, DescribeControls(binding).c_str());

		// What the checks find, with their fixes
		size_t id = 0;
		for (const InputCheckFinding* finding : FindingsFor(document.GetFindings(), SelectedContext, SelectedBinding))
		{
			TextWarning(std::string(GetInputCheckName(finding->Check)) + ": " + finding->Message);
			DrawFix(document, transactions, *finding, id++);
		}
		const std::string problemPrefix = context.Name + ", binding " + std::to_string(SelectedBinding);
		for (const InputAssetProblem& problem : document.GetProblems())
		{
			if (problem.Where == problemPrefix)
			{
				TextWarning("Can't be used: " + problem.What);
			}
		}

		if (document.IsShowingA())
		{
			TextWarning("A is what is felt now; these are B's values. An edit shows B again.");
		}
		DrawContext.CurrentLocation = BindingPath(SelectedContext, SelectedBinding);
		DrawReflectedTypeTable(&binding, Nyx::Reflection::GetTypeMetadata<InputBindingData>(), DrawContext);

		// What its values mean, as the layers make them for the pad model shown
		const EGamepadModel model = GetShownModel(input);
		const InputPlayerSettingsData* player = document.IsApplyingPlayerSettings() ? document.GetPlayerSettings() : nullptr;
		const InputAssetData resolved = ResolveLayers(asset, player, model);
		const InputBindingData& tuned = resolved.Contexts[SelectedContext].Bindings[SelectedBinding];

		Nyx::UI::SectionHeader("What it means");
		ImGui::TextDisabled("With the layers for %s%s", model == EGamepadModel::Unknown ? "no pad profile" : GetGamepadModelName(model),
			player ? " and the player's settings" : "");
		ImGui::PushTextWrapPos(0.0f);
		for (const TuningExplanation& line : ExplainBinding(tuned, action))
		{
			std::string text = line.Text;
			const std::optional<LayeredValue> value = FindTuning(asset, player, SelectedContext, SelectedBinding, line.Field, model);
			if (value && value->Layer != ETuningLayer::Asset)
			{
				const std::optional<LayeredValue> own = FindTuning(asset, nullptr, SelectedContext, SelectedBinding, line.Field, EGamepadModel::Unknown);
				text += "  (from the " + std::string(GetTuningLayerName(value->Layer)) + " layer; the asset has " + Format("%g", own ? own->Value : 0.0f) + ")";
			}
			ImGui::TextWrapped("- %s", text.c_str());
		}
		ImGui::PopTextWrapPos();

		const BindingTrace* trace = FindTrace(document, input, SelectedContext, SelectedBinding);
		const std::optional<ControlId> control = binding.Control.empty() ? std::nullopt : ParseControlPath(binding.Control);
		const bool bAnalog = control && !IsButton(*control) && !IsRelative(*control);
		if (bAnalog)
		{
			Nyx::UI::SectionHeader("Response");
			std::optional<float> liveIn;
			if (trace)
			{
				liveIn = std::min(glm::length(trace->Raw), 1.0f);
			}

			// The curve before the last edit: what it was when nothing was being dragged
			const size_t curveOf = SelectedContext * 100000 + SelectedBinding;
			if (CurveOf != curveOf)
			{
				CurveOf = curveOf;
				StablePipeline = tuned.Pipeline;
				GhostPipeline.reset();
			}
			else if (!ImGui::IsAnyItemActive() && !SameResponse(StablePipeline, tuned.Pipeline))
			{
				GhostPipeline = StablePipeline;
				StablePipeline = tuned.Pipeline;
			}
			DrawResponseCurve(tuned.Pipeline, liveIn);
		}

		if (tuned.Trigger.Kind != ETriggerKind::Down || tuned.Trigger.BufferSeconds > 0.0f)
		{
			Nyx::UI::SectionHeader("Trigger");
			DrawTriggerTimeline(tuned.Trigger, trace);
		}
	}

	void InputTuningView::DrawResponseCurve(const ValuePipeline& pipeline, std::optional<float> liveIn)
	{
		const Nyx::UI::Theme& theme = Nyx::UI::GetTheme();
		const float width = std::min(ImGui::GetContentRegionAvail().x, ImGui::GetFontSize() * 16.0f);
		const PainterRect area = Reserve({ width, width * 0.6f });
		Nyx::UI::WindowPainter& painter = Nyx::UI::GetWindowPainter();
		painter.AddRoundedRect(area, theme.FrameRounding, PainterStyle::Filled(theme.Field));

		const float pad = 6.0f;
		const auto pointOf = [&](float in, float out)
		{
			return glm::vec2(area.Min.x + pad + in * (area.GetWidth() - 2.0f * pad), area.Max.y - pad - out * (area.GetHeight() - 2.0f * pad));
		};
		const auto paintCurve = [&](const ValuePipeline& curve, float thickness, const glm::vec4& color)
		{
			constexpr int Steps = 64;
			for (int step = 1; step <= Steps; ++step)
			{
				const float a = static_cast<float>(step - 1) / Steps;
				const float b = static_cast<float>(step) / Steps;
				painter.AddLine(pointOf(a, SampleResponse(curve, a)), pointOf(b, SampleResponse(curve, b)), thickness, EPainterLineCap::Round,
					PainterStyle::Filled(color));
			}
		};

		// In equals out, for comparison
		painter.AddLine(pointOf(0.0f, 0.0f), pointOf(1.0f, 1.0f), 1.0f, EPainterLineCap::Flat, PainterStyle::Filled(theme.Separator));
		if (GhostPipeline)
		{
			paintCurve(*GhostPipeline, 1.0f, theme.TextMuted);
		}
		paintCurve(pipeline, 2.0f, theme.Accent);
		if (liveIn)
		{
			painter.AddCircle(pointOf(*liveIn, SampleResponse(pipeline, *liveIn)), 4.0f, PainterStyle::Filled(theme.Text));
		}

		ImGui::TextDisabled("In (how far the control is pushed) against out, after the dead zone and the curve%s",
			GhostPipeline ? "; grey: before the last edit" : "");
		if (liveIn)
		{
			ImGui::Text("Now: in %.2f, out %.2f", *liveIn, SampleResponse(pipeline, *liveIn));
		}
	}

	void InputTuningView::DrawTriggerTimeline(const TriggerSettings& trigger, const BindingTrace* trace)
	{
		const Nyx::UI::Theme& theme = Nyx::UI::GetTheme();

		// The last press: how long it waited, and whether it fired, from the trace each frame
		if (trace)
		{
			const bool bNowWaiting = trace->Trigger.Phase == Nyx::Input::EActionPhase::Started || trace->Trigger.Phase == Nyx::Input::EActionPhase::Ongoing;
			bFiredDuringPress = (bWaiting && bFiredDuringPress) || trace->Trigger.bTriggered;
			if (bNowWaiting)
			{
				WaitedSeconds = trace->Trigger.WaitedSeconds;
			}
			else if (bWaiting)
			{
				LastPressSeconds = WaitedSeconds;
				bLastPressFired = bFiredDuringPress || trace->Trigger.bTriggered;
				bFiredDuringPress = false;
			}
			bWaiting = bNowWaiting;
		}

		// What the trigger waits for, in seconds from the press
		float span = 0.5f;
		switch (trigger.Kind)
		{
		case ETriggerKind::Hold:      span = trigger.HoldSeconds; break;
		case ETriggerKind::Tap:       span = trigger.TapMaxSeconds; break;
		case ETriggerKind::DoubleTap: span = trigger.DoubleTapMaxGapSeconds; break;
		case ETriggerKind::Repeat:    span = trigger.RepeatDelaySeconds + 3.0f * trigger.RepeatIntervalSeconds; break;
		default:                      break;
		}
		span = std::max({ span + trigger.BufferSeconds, LastPressSeconds.value_or(0.0f), 0.2f }) * 1.3f;

		const PainterRect area = Reserve({ ImGui::GetContentRegionAvail().x, ImGui::GetFontSize() * 2.0f });
		Nyx::UI::WindowPainter& painter = Nyx::UI::GetWindowPainter();
		painter.AddRoundedRect(area, theme.FrameRounding, PainterStyle::Filled(theme.Field));
		const float pad = 8.0f;
		const auto xOf = [&](float seconds)
		{
			return area.Min.x + pad + std::clamp(seconds / span, 0.0f, 1.0f) * (area.GetWidth() - 2.0f * pad);
		};
		const float middle = (area.Min.y + area.Max.y) * 0.5f;
		const auto mark = [&](float seconds, const glm::vec4& color, float height)
		{
			painter.AddLine({ xOf(seconds), middle - height }, { xOf(seconds), middle + height }, 2.0f, EPainterLineCap::Round, PainterStyle::Filled(color));
		};
		const auto window = [&](float from, float to, const glm::vec4& color)
		{
			painter.AddRoundedRect(PainterRect{ { xOf(from), middle - 4.0f }, { xOf(to), middle + 4.0f } }, 3.0f, PainterStyle::Filled(color));
		};

		painter.AddLine({ xOf(0.0f), middle }, { xOf(span), middle }, 1.0f, EPainterLineCap::Flat, PainterStyle::Filled(theme.Separator));
		mark(0.0f, theme.TextMuted, 8.0f);

		std::string text;
		float firesAt = 0.0f;
		switch (trigger.Kind)
		{
		case ETriggerKind::Hold:
			firesAt = trigger.HoldSeconds;
			mark(firesAt, theme.Accent, 10.0f);
			text = "Press at the left; it fires at the orange mark, " + Format("%.0f ms", trigger.HoldSeconds * 1000.0) + " later";
			break;
		case ETriggerKind::Tap:
			window(0.0f, trigger.TapMaxSeconds, theme.RowSelected);
			text = "Let go inside the bar for a tap (" + Format("%.0f ms", trigger.TapMaxSeconds * 1000.0) + "); it fires when let go";
			break;
		case ETriggerKind::DoubleTap:
			window(0.0f, trigger.DoubleTapMaxGapSeconds, theme.RowSelected);
			text = "The second press must come inside the bar (" + Format("%.0f ms", trigger.DoubleTapMaxGapSeconds * 1000.0) + ")";
			break;
		case ETriggerKind::Repeat:
			mark(0.0f, theme.Accent, 10.0f);
			for (float at = trigger.RepeatDelaySeconds; at <= span && trigger.RepeatIntervalSeconds > 0.0f; at += trigger.RepeatIntervalSeconds)
			{
				mark(at, theme.Accent, 7.0f);
			}
			text = "Fires at the press, after " + Format("%.0f ms", trigger.RepeatDelaySeconds * 1000.0) + ", then every " +
				Format("%.0f ms", trigger.RepeatIntervalSeconds * 1000.0);
			break;
		default:
			mark(0.0f, theme.Accent, 10.0f);
			text = std::string("Fires: ") + GetTriggerKindName(trigger.Kind);
			break;
		}
		if (trigger.BufferSeconds > 0.0f)
		{
			window(firesAt, firesAt + trigger.BufferSeconds, theme.TabUnderlineDimmed);
			text += "; the firing is kept " + Format("%.0f ms", trigger.BufferSeconds * 1000.0) + " (grey) for code that asks later";
		}

		// The press now, and the last one
		if (bWaiting)
		{
			painter.AddCircle({ xOf(WaitedSeconds), middle }, 4.0f, PainterStyle::Filled(theme.Text));
		}
		else if (LastPressSeconds)
		{
			painter.AddCircle({ xOf(*LastPressSeconds), middle }, 4.0f, PainterStyle::Outlined(theme.Text, 1.5f));
		}

		ImGui::PushTextWrapPos(0.0f);
		ImGui::TextDisabled("%s", text.c_str());
		if (LastPressSeconds && !bWaiting)
		{
			ImGui::Text("Last press: held %.0f ms, %s", *LastPressSeconds * 1000.0f, bLastPressFired ? "fired" : "didn't fire");
		}
		ImGui::PopTextWrapPos();
	}

	// ---------------------------------------------------------------------------------------
	// Checks

	void InputTuningView::DrawChecks(InputAssetDocument& document, TransactionSystem& transactions, InputSystem& /*input*/)
	{
		if (!document.IsLoaded())
		{
			ImGui::TextDisabled("No input asset: %s could not be read", GetDefaultInputAssetPath().string().c_str());
			return;
		}

		InputAssetData& asset = document.GetAsset();
		const std::vector<InputCheckFinding>& findings = document.GetFindings();
		const std::vector<InputAssetProblem>& problems = document.GetProblems();

		ImGui::TextDisabled("Checked with the contexts that are on now and the actions the code has taken once");
		if (findings.empty() && problems.empty())
		{
			ImGui::TextUnformatted("Nothing found");
		}

		const ImGuiTableFlags flags = ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable | ImGuiTableFlags_SizingStretchProp;
		if (!findings.empty() && Nyx::UI::BeginTable("##Findings", 4, flags))
		{
			ImGui::TableSetupColumn("Check", ImGuiTableColumnFlags_WidthStretch, 0.8f);
			ImGui::TableSetupColumn("Where", ImGuiTableColumnFlags_WidthStretch, 1.4f);
			ImGui::TableSetupColumn("What", ImGuiTableColumnFlags_WidthStretch, 2.0f);
			ImGui::TableSetupColumn("Fix", ImGuiTableColumnFlags_WidthStretch, 0.8f);
			Nyx::UI::TableHeadersRow();

			// A fix changes the findings: draw from a copy
			const std::vector<InputCheckFinding> shown = findings;
			for (size_t index = 0; index < shown.size(); ++index)
			{
				const InputCheckFinding& finding = shown[index];
				Nyx::UI::TableNextRow();
				ImGui::TableNextColumn();
				ImGui::TextUnformatted(GetInputCheckName(finding.Check));
				ImGui::TableNextColumn();
				const std::string where = DescribeWhere(asset, finding) + "##Where" + std::to_string(index);
				if (NYX_UI(Nyx::UI::Selectable(where.c_str())) && finding.Context && finding.Binding)
				{
					SelectBinding(*finding.Context, *finding.Binding);
					bShowTuningRequested = true;
				}
				ImGui::TableNextColumn();
				ImGui::PushTextWrapPos(0.0f);
				ImGui::TextUnformatted(finding.Message.c_str());
				ImGui::PopTextWrapPos();
				ImGui::TableNextColumn();
				DrawFix(document, transactions, finding, index);
			}
			Nyx::UI::EndTable();
		}

		if (!problems.empty())
		{
			Nyx::UI::SectionHeader("Can't be used");
			for (const InputAssetProblem& problem : problems)
			{
				ImGui::TextWrapped("- %s: %s", problem.Where.c_str(), problem.What.c_str());
			}
		}

		Nyx::UI::SectionHeader("Coverage");
		const std::vector<CoverageRow> rows = ComputeCoverage(asset);
		constexpr int Columns = static_cast<int>(ECoverageColumn::Count);
		if (Nyx::UI::BeginTable("##Coverage", Columns + 1, flags))
		{
			ImGui::TableSetupColumn("Action", ImGuiTableColumnFlags_WidthStretch, 1.4f);
			for (int column = 0; column < Columns; ++column)
			{
				ImGui::TableSetupColumn(GetCoverageColumnName(static_cast<ECoverageColumn>(column)), ImGuiTableColumnFlags_WidthStretch, 1.0f);
			}
			Nyx::UI::TableHeadersRow();

			for (const CoverageRow& row : rows)
			{
				Nyx::UI::TableNextRow();
				ImGui::TableNextColumn();
				ImGui::TextUnformatted(row.Action.c_str());
				for (int column = 0; column < Columns; ++column)
				{
					ImGui::TableNextColumn();
					const ECoverageColumn which = static_cast<ECoverageColumn>(column);
					if (row.bCovered[static_cast<size_t>(column)])
					{
						ImGui::TextUnformatted("yes");
					}
					else if (row.IsExpected(which))
					{
						TextWarning("missing");
					}
					else
					{
						ImGui::TextDisabled("not needed");
					}
				}
			}
			Nyx::UI::EndTable();
		}
	}
}
