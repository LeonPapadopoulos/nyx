// Finding the right setup without a window: each check on an asset that has its mistake and one
// that doesn't, with its fix; the coverage table; every recipe passing every check; tuning
// explained in units; the response curve.
#include "InputChecks.h"
#include "InputRecipes.h"
#include "Log.h"

#include <cmath>
#include <iostream>
#include <stdexcept>
#include <string>

namespace
{
	using namespace Nyx::Input;

	void Require(bool condition, const std::string& message)
	{
		if (!condition)
		{
			throw std::runtime_error(message);
		}
	}

	bool Near(float a, float b, float tolerance = 1e-4f)
	{
		return std::abs(a - b) <= tolerance;
	}

	std::string Describe(const InputAssetData& asset, const std::vector<InputCheckFinding>& findings)
	{
		std::string text;
		for (const InputCheckFinding& finding : findings)
		{
			text += std::string("\n  ") + GetInputCheckName(finding.Check) + ": " + DescribeWhere(asset, finding) + ": " + finding.Message;
		}
		return text;
	}

	size_t Count(const std::vector<InputCheckFinding>& findings, EInputCheck check)
	{
		size_t count = 0;
		for (const InputCheckFinding& finding : findings)
		{
			count += finding.Check == check;
		}
		return count;
	}

	const InputCheckFinding* Find(const std::vector<InputCheckFinding>& findings, EInputCheck check)
	{
		for (const InputCheckFinding& finding : findings)
		{
			if (finding.Check == check)
			{
				return &finding;
			}
		}
		return nullptr;
	}

	// A context "Play" with the recipe's bindings, for keyboard and mouse and pads: no findings
	InputAssetData MakeRecipeAsset(EInputRecipe recipe, const std::string& name)
	{
		InputAssetData asset;
		asset.Contexts.push_back({ .Name = "Play" });
		Require(!AddRecipe(asset, 0, recipe, name).empty(), std::string("The recipe adds its action: ") + GetRecipeName(recipe));
		return asset;
	}

	// The asset has the check's mistake once (with the runtime given); its fix, if it has one,
	// takes it away
	void RequireFinds(InputAssetData asset, EInputCheck check, const InputCheckRuntime* runtime = nullptr, const std::string& contains = {})
	{
		const std::vector<InputCheckFinding> findings = RunInputChecks(asset, runtime);
		const std::string name = GetInputCheckName(check);
		Require(Count(findings, check) == 1, name + ": found once" + Describe(asset, findings));
		const InputCheckFinding& finding = *Find(findings, check);
		Require(contains.empty() || finding.Message.find(contains) != std::string::npos, name + ": says \"" + contains + "\": " + finding.Message);

		if (finding.Fix)
		{
			Require(ApplyFix(asset, *finding.Fix), name + ": the fix applies");
			const std::vector<InputCheckFinding> after = RunInputChecks(asset, runtime);
			Require(Count(after, check) == 0, name + ": fixed by \"" + finding.Fix->Label + "\"" + Describe(asset, after));
		}
	}

	void RequireClean(const InputAssetData& asset, const InputCheckRuntime* runtime, const std::string& what)
	{
		const std::vector<InputCheckFinding> findings = RunInputChecks(asset, runtime);
		Require(findings.empty(), what + ": no findings" + Describe(asset, findings));
	}

	// ---------------------------------------------------------------------------------------
	// Checks

	void TestDefaultAssetIsClean()
	{
		RequireClean(MakeDefaultInputAsset(), nullptr, "The default asset");

		const InputCheckRuntime runtime{ .ActiveContexts = { "EditorFlyCamera", "EditorMouseLook" } };
		const std::vector<InputCheckFinding> findings = RunInputChecks(MakeDefaultInputAsset(), &runtime);
		Require(findings.empty(), "Also with the mouse look on: it takes only the mouse's movement" + Describe(MakeDefaultInputAsset(), findings));
	}

	void TestEachRecipePassesEveryCheck()
	{
		for (int index = 0; index < static_cast<int>(EInputRecipe::Count); ++index)
		{
			const EInputRecipe recipe = static_cast<EInputRecipe>(index);
			const InputAssetData asset = MakeRecipeAsset(recipe, "Act");
			// The code takes the one-shot recipes' firings once
			const bool bOneShot = recipe == EInputRecipe::Press || recipe == EInputRecipe::Charge || recipe == EInputRecipe::TapAndHold;
			InputCheckRuntime runtime{ .ActiveContexts = { "Play" } };
			if (bOneShot)
			{
				runtime.ConsumedActions = { "Act", "ActHold" };
			}
			RequireClean(asset, &runtime, GetRecipeName(recipe));
			Require(GetRecipeDescription(recipe)[0] != '\0', "It says what it writes");

			// And the input system takes every binding
			InputSystem input;
			const std::vector<InputAssetProblem> problems = ApplyInputAsset(input, asset);
			Require(problems.empty(), std::string(GetRecipeName(recipe)) + ": every binding usable" + (problems.empty() ? "" : ": " + problems[0].Where + ": " + problems[0].What));
		}

		InputAssetData asset = MakeRecipeAsset(EInputRecipe::TapAndHold, "Use");
		Require(asset.Actions.size() == 2 && asset.Actions[1].Name == "UseHold", "Tap and hold: two actions");
		Require(AddRecipe(asset, 0, EInputRecipe::Press, "Use").empty(), "Not a name that's taken");
		Require(AddRecipe(asset, 3, EInputRecipe::Press, "Jump").empty(), "Nor a context that isn't there");
	}

	void TestConflictAndTapHold()
	{
		InputAssetData asset = MakeRecipeAsset(EInputRecipe::Press, "Jump");
		asset.Actions.push_back({ "Dodge", EActionValue::Button });
		asset.Contexts[0].Bindings.push_back({ .Action = "Dodge", .Control = "Gamepad/FaceSouth" });
		RequireFinds(asset, EInputCheck::Conflict, nullptr, "drives Jump too");

		// With Ctrl, it's another chord of keys
		InputAssetData withCtrl = MakeRecipeAsset(EInputRecipe::Press, "Jump");
		withCtrl.Actions.push_back({ "Dodge", EActionValue::Button });
		withCtrl.Contexts[0].Bindings.push_back({ .Action = "Dodge", .Control = "Keyboard/Space", .bCtrl = true });
		withCtrl.Contexts[0].Bindings.push_back({ .Action = "Dodge", .Control = "Gamepad/FaceEast" });
		RequireClean(withCtrl, nullptr, "Modifiers make it another control");

		InputAssetData overlap = MakeRecipeAsset(EInputRecipe::TapAndHold, "Use");
		overlap.Contexts[0].Bindings[1].Trigger.HoldSeconds = 0.15f;
		RequireFinds(overlap, EInputCheck::TapHoldOverlap, nullptr, "fires before a tap");
	}

	void TestUnreachable()
	{
		InputAssetData asset = MakeRecipeAsset(EInputRecipe::Press, "Jump");
		asset.Contexts.push_back({ .Name = "Menu", .Priority = 50, .bTakesGamepad = true });
		const InputCheckRuntime both{ .ActiveContexts = { "Play", "Menu" } };
		RequireFinds(asset, EInputCheck::Unreachable, &both, "takes the whole pad");

		const InputCheckRuntime playOnly{ .ActiveContexts = { "Play" } };
		RequireClean(asset, &playOnly, "Menu off");
		RequireClean(asset, nullptr, "Without knowing what is on");

		InputAssetData byBinding = MakeRecipeAsset(EInputRecipe::Press, "Jump");
		byBinding.Actions.push_back({ "Confirm", EActionValue::Button, EActionDevices::KeyboardMouse });
		byBinding.Contexts.push_back({ .Name = "Menu", .Priority = 50 });
		byBinding.Contexts[1].Bindings.push_back({ .Action = "Confirm", .Control = "Keyboard/Space" });
		RequireFinds(byBinding, EInputCheck::Unreachable, &both, "Menu (50) is on and takes Keyboard/Space");
	}

	void TestCoverage()
	{
		InputAssetData asset = MakeRecipeAsset(EInputRecipe::Press, "Jump");
		asset.Contexts[0].Bindings.pop_back();
		RequireFinds(asset, EInputCheck::Coverage, nullptr, "Jump has no binding for pads");

		asset.Actions[0].Devices = EActionDevices::KeyboardMouse;
		RequireClean(asset, nullptr, "Keyboard and mouse only, on purpose");

		InputAssetData gyro = MakeRecipeAsset(EInputRecipe::Look, "Aim");
		gyro.Contexts[0].Bindings[0].Control = "Gamepad/Gyro";
		gyro.Contexts[0].Bindings[0].Pipeline.LookAcceleration.bEnabled = false;
		RequireFinds(gyro, EInputCheck::DeviceOnlyControl, nullptr, "Xbox One");

		const std::vector<CoverageRow> rows = ComputeCoverage(gyro);
		Require(rows.size() == 1 && rows[0].HasGap(), "The table has the gap");
		Require(rows[0].bCovered[static_cast<size_t>(ECoverageColumn::KeyboardMouse)] &&
				!rows[0].bCovered[static_cast<size_t>(ECoverageColumn::Xbox)] &&
				rows[0].bCovered[static_cast<size_t>(ECoverageColumn::DualSense)],
			"Keyboard and mouse, the DualSense, not Xbox pads");

		const std::vector<CoverageRow> defaults = ComputeCoverage(MakeDefaultInputAsset());
		for (const CoverageRow& row : defaults)
		{
			Require(!row.HasGap(), "The defaults have no gap: " + row.Action);
		}
		Require(!defaults[4].IsExpected(ECoverageColumn::Xbox), "LookButton is for the mouse only");
	}

	void TestPipelineChecks()
	{
		InputAssetData look = MakeRecipeAsset(EInputRecipe::Look, "Look");
		look.Contexts[0].Bindings[1].Pipeline.bMultiplyByDeltaTime = true;
		RequireFinds(look, EInputCheck::DeltaTimeOnMouse);

		look = MakeRecipeAsset(EInputRecipe::Look, "Look");
		look.Contexts[0].Bindings[0].Pipeline.bMultiplyByDeltaTime = false;
		RequireFinds(look, EInputCheck::NoDeltaTimeOnStickRate);

		InputAssetData move = MakeRecipeAsset(EInputRecipe::Move, "Move");
		move.Contexts[0].Bindings[0].Composite.bNormalize = false;
		RequireFinds(move, EInputCheck::CompositeNotNormalized, nullptr, "faster diagonally");

		move = MakeRecipeAsset(EInputRecipe::Move, "Move");
		move.Contexts[0].Bindings[1].Pipeline.DeadZone.Inner = 0.96f;
		RequireFinds(move, EInputCheck::DeadZone, nullptr, "at or past outer");
		move.Contexts[0].Bindings[1].Pipeline.DeadZone.Inner = 0.6f;
		RequireFinds(move, EInputCheck::DeadZone, nullptr, "more than half");

		move = MakeRecipeAsset(EInputRecipe::Move, "Move");
		move.Contexts[0].Bindings[1].Pipeline.Smoothing = { .bEnabled = true, .TimeConstantSeconds = 0.08f };
		RequireFinds(move, EInputCheck::SmoothingLatency, nullptr, "80 ms");
		move.Contexts[0].Bindings[1].Pipeline.Smoothing.TimeConstantSeconds = 0.03f;
		RequireClean(move, nullptr, "30 ms is fine");
	}

	void TestTriggerChecks()
	{
		InputAssetData charge = MakeRecipeAsset(EInputRecipe::Charge, "Shoot");
		charge.Contexts[0].Bindings[1].Trigger.ReleaseThreshold = 0.5f;
		RequireFinds(charge, EInputCheck::ThresholdWithoutHysteresis, nullptr, "flickers");

		InputAssetData jump = MakeRecipeAsset(EInputRecipe::Hold, "Jump");
		jump.Contexts[0].Bindings.pop_back();
		jump.Contexts[0].Bindings.push_back({ .Action = "Jump", .Control = "Gamepad/FaceSouth" });
		const InputCheckRuntime consumed{ .ConsumedActions = { "Jump" } };
		const std::vector<InputCheckFinding> findings = RunInputChecks(jump, &consumed);
		Require(Count(findings, EInputCheck::DownOnOneShot) == 2, "Both Down bindings of an action the code takes once" + Describe(jump, findings));
		Require(findings[0].Fix && findings[0].Fix->Field == "Trigger.Kind", "Fixed with Pressed");
		Require(ApplyFix(jump, *findings[0].Fix) && jump.Contexts[0].Bindings[0].Trigger.Kind == ETriggerKind::Pressed, "Which sets it");
		RequireClean(MakeRecipeAsset(EInputRecipe::Hold, "Sprint"), nullptr, "Down is right for a held action nobody takes once");
	}

	void TestWhere()
	{
		InputAssetData asset = MakeRecipeAsset(EInputRecipe::Move, "Move");
		asset.Contexts[0].Bindings[0].Composite.bNormalize = false;
		const std::vector<InputCheckFinding> findings = RunInputChecks(asset);
		Require(DescribeWhere(asset, findings[0]) == "Play, binding 0 (Move: Keyboard/D, Keyboard/A, Keyboard/W, Keyboard/S)",
			"Where, with the controls: " + DescribeWhere(asset, findings[0]));
		Require(FindingsFor(findings, 0, 0).size() == 1 && FindingsFor(findings, 0, 1).empty(), "By binding");
	}

	// ---------------------------------------------------------------------------------------
	// Seeing what a setting means

	bool Says(const std::vector<TuningExplanation>& lines, const std::string& field, const std::string& text)
	{
		for (const TuningExplanation& line : lines)
		{
			if (line.Field == field && line.Text.find(text) != std::string::npos)
			{
				return true;
			}
		}
		return false;
	}

	void TestExplanations()
	{
		const InputAssetData look = MakeRecipeAsset(EInputRecipe::Look, "Look");
		const std::vector<TuningExplanation> stick = ExplainBinding(look.Contexts[0].Bindings[0], &look.Actions[0]);
		Require(Says(stick, "Pipeline.DeadZone.Inner", "ignores the first 15 % of the stick's travel"), "The inner dead zone in percent");
		Require(Says(stick, "Pipeline.Curve.Exponent", "half way gives 25 %"), "The curve");
		Require(Says(stick, "Pipeline.Sensitivity.Scale", "full stick turns 143 degrees a second"), "The turn rate in degrees");
		Require(Says(stick, "Pipeline.LookAcceleration.MaxMultiplier", "Held past 90 % for 250 ms"), "Look acceleration");

		const std::vector<TuningExplanation> mouse = ExplainBinding(look.Contexts[0].Bindings[1], &look.Actions[0]);
		Require(Says(mouse, "Pipeline.Sensitivity.Scale", "moving the mouse 1000 pixels turns 172 degrees"), "Mouse sensitivity");
		Require(!Says(mouse, "Trigger.PressThreshold", ""), "No thresholds for the mouse's movement");

		const InputAssetData use = MakeRecipeAsset(EInputRecipe::TapAndHold, "Use");
		Require(Says(ExplainBinding(use.Contexts[0].Bindings[1], &use.Actions[1]), "Trigger.HoldSeconds", "300 ms held: 18 frames at 60 fps"),
			"A hold in frames");
		const InputAssetData jump = MakeRecipeAsset(EInputRecipe::Press, "Jump");
		Require(Says(ExplainBinding(jump.Contexts[0].Bindings[0], &jump.Actions[0]), "Trigger.BufferSeconds", "kept 100 ms"), "The buffer");
		const InputAssetData shoot = MakeRecipeAsset(EInputRecipe::Charge, "Shoot");
		Require(Says(ExplainBinding(shoot.Contexts[0].Bindings[1], &shoot.Actions[0]), "Trigger.PressThreshold", "from 50 %, let go below 40 %"),
			"The trigger's thresholds");
	}

	void TestResponseCurve()
	{
		ValuePipeline pipeline;
		Require(Near(SampleResponse(pipeline, 0.3f), 0.3f), "Nothing on: straight");

		pipeline.DeadZone = { .bEnabled = true, .Shape = EDeadZoneShape::ScaledRadial, .Inner = 0.2f, .Outer = 0.8f };
		Require(Near(SampleResponse(pipeline, 0.1f), 0.0f), "Inside the dead zone: nothing");
		Require(Near(SampleResponse(pipeline, 0.5f), 0.5f), "Half way between inner and outer: half");
		Require(Near(SampleResponse(pipeline, 0.9f), 1.0f), "Past outer: full");

		pipeline.Curve = { .bEnabled = true, .Kind = ECurveKind::Power, .Exponent = 2.0f };
		Require(Near(SampleResponse(pipeline, 0.5f), 0.25f), "Then the curve");
	}
}

int main()
{
	Nyx::Logger::Get().Init();

	try
	{
		TestDefaultAssetIsClean();
		TestEachRecipePassesEveryCheck();
		TestConflictAndTapHold();
		TestUnreachable();
		TestCoverage();
		TestPipelineChecks();
		TestTriggerChecks();
		TestWhere();
		TestExplanations();
		TestResponseCurve();
	}
	catch (const std::exception& error)
	{
		std::cerr << "Input checks test failed: " << error.what() << "\n";
		return 1;
	}

	std::cout << "All input checks tests passed.\n";
	return 0;
}
