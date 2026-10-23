#include "InputDebugView.h"

#include "UITheme.h"
#include "UIWidgets.h"
#include "WindowPainter.h"

#include <imgui.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <span>

namespace Nyx::Input
{
	namespace
	{
		using Nyx::UI::EPainterLineCap;
		using Nyx::UI::PainterRect;
		using Nyx::UI::PainterStyle;

		// Reserves room in the window, as an item, and returns it for painting
		PainterRect Reserve(glm::vec2 size)
		{
			ImGui::Dummy(ImVec2(size.x, size.y));
			const ImVec2 min = ImGui::GetItemRectMin();
			const ImVec2 max = ImGui::GetItemRectMax();
			return PainterRect{ { min.x, min.y }, { max.x, max.y } };
		}

		bool IsMouseIn(const PainterRect& rect)
		{
			const ImVec2 mouse = ImGui::GetMousePos();
			return ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows) && mouse.x >= rect.Min.x && mouse.x < rect.Max.x &&
				mouse.y >= rect.Min.y && mouse.y < rect.Max.y;
		}

		std::string Format(const char* format, double a, double b = 0.0, double c = 0.0)
		{
			char text[128];
			std::snprintf(text, sizeof(text), format, a, b, c);
			return text;
		}

		std::string SecondsAgo(uint64_t nowUs, uint64_t thenUs)
		{
			if (thenUs == 0)
			{
				return "-";
			}
			return Format("%.2f s ago", nowUs > thenUs ? static_cast<double>(nowUs - thenUs) / 1'000'000.0 : 0.0);
		}

		std::string DescribeValue(EActionValue type, const glm::vec3& value)
		{
			switch (type)
			{
			case EActionValue::Button: return value.x != 0.0f ? "on" : "off";
			case EActionValue::Axis1D: return Format("%.3f", value.x);
			case EActionValue::Axis2D: return Format("(%.3f, %.3f)", value.x, value.y);
			case EActionValue::Axis3D: return Format("(%.2f, %.2f, %.2f)", value.x, value.y, value.z);
			}
			return "";
		}

		std::string DescribeDevices(uint32_t devices)
		{
			if (devices == 0)
			{
				return "";
			}
			if (devices == AllDevices)
			{
				return "everything";
			}
			std::string text;
			for (const EDeviceKind kind : { EDeviceKind::Keyboard, EDeviceKind::Mouse, EDeviceKind::Gamepad })
			{
				if ((devices & DeviceBit(kind)) != 0)
				{
					text += (text.empty() ? "the " : ", the ") + std::string(GetDeviceKindName(kind));
				}
			}
			return text;
		}

		// "Move in EditorFlyCamera", or the context that takes the whole device
		std::string DescribeUse(const InputSystem& input, const ControlUse& use)
		{
			const InputContextDef& context = input.GetContext(use.Context);
			if (use.BindingIndex == NoBinding)
			{
				return "taken by " + context.Name + " (" + std::to_string(context.Priority) + ")";
			}
			return input.GetAction(use.Action).Name + " in " + context.Name + " (" + std::to_string(context.Priority) + ")";
		}

		std::string DeviceName(const InputFrame& frame, InputDeviceId device)
		{
			const InputFrame::DeviceState* state = frame.FindDevice(device);
			return state ? state->Info.Name : (device.IsValid() ? "Device " + std::to_string(device.Value) : "-");
		}

		bool IsSensor(ControlId control)
		{
			return control == Gamepad(EGamepadControl::Gyro) || control == Gamepad(EGamepadControl::Accel);
		}

		// The binding of the stack that reads a stick as one control, and its trace this frame:
		// for the dead zone rings and the value after the pipeline
		struct StickBinding
		{
			const InputBinding* Binding = nullptr;
			const BindingTrace* Trace = nullptr;
		};

		StickBinding FindStickBinding(const InputSystem& input, ControlId stick)
		{
			for (const BindingTrace& trace : input.GetUser().GetTraces())
			{
				if (trace.Outcome == EBindingOutcome::ContextOff || trace.Outcome == EBindingOutcome::Taken)
				{
					continue;
				}
				const InputContextDef& context = input.GetContext(trace.Context);
				if (trace.BindingIndex < context.Bindings.size() && context.Bindings[trace.BindingIndex].Control == stick)
				{
					return { &context.Bindings[trace.BindingIndex], &trace };
				}
			}
			return {};
		}

		// ---------------------------------------------------------------------------------------
		// The pad, painted: where each control is, in units of the drawing's height (1.8 wide)

		enum class EShape : uint8_t
		{
			Button,
			Round,
			Stick,
			Trigger,
		};

		struct Element
		{
			EGamepadControl Control;
			EShape Shape;
			glm::vec2 Center;
			glm::vec2 Size;
		};

		constexpr float DiagramAspect = 1.8f;
		constexpr float StickRadius = 0.13f;

		constexpr std::array<Element, 27> Elements = { {
			{ EGamepadControl::LeftTrigger, EShape::Trigger, { 0.40f, 0.06f }, { 0.30f, 0.08f } },
			{ EGamepadControl::RightTrigger, EShape::Trigger, { 1.40f, 0.06f }, { 0.30f, 0.08f } },
			{ EGamepadControl::LeftShoulder, EShape::Button, { 0.40f, 0.15f }, { 0.30f, 0.05f } },
			{ EGamepadControl::RightShoulder, EShape::Button, { 1.40f, 0.15f }, { 0.30f, 0.05f } },
			{ EGamepadControl::LeftStick, EShape::Stick, { 0.45f, 0.46f }, { 2.0f * StickRadius, 2.0f * StickRadius } },
			{ EGamepadControl::RightStick, EShape::Stick, { 1.15f, 0.72f }, { 2.0f * StickRadius, 2.0f * StickRadius } },
			{ EGamepadControl::LeftStickButton, EShape::Round, { 0.45f, 0.46f }, { 0.07f, 0.07f } },
			{ EGamepadControl::RightStickButton, EShape::Round, { 1.15f, 0.72f }, { 0.07f, 0.07f } },
			{ EGamepadControl::DpadUp, EShape::Button, { 0.65f, 0.64f }, { 0.07f, 0.07f } },
			{ EGamepadControl::DpadDown, EShape::Button, { 0.65f, 0.80f }, { 0.07f, 0.07f } },
			{ EGamepadControl::DpadLeft, EShape::Button, { 0.57f, 0.72f }, { 0.07f, 0.07f } },
			{ EGamepadControl::DpadRight, EShape::Button, { 0.73f, 0.72f }, { 0.07f, 0.07f } },
			{ EGamepadControl::FaceNorth, EShape::Round, { 1.35f, 0.36f }, { 0.09f, 0.09f } },
			{ EGamepadControl::FaceSouth, EShape::Round, { 1.35f, 0.56f }, { 0.09f, 0.09f } },
			{ EGamepadControl::FaceWest, EShape::Round, { 1.25f, 0.46f }, { 0.09f, 0.09f } },
			{ EGamepadControl::FaceEast, EShape::Round, { 1.45f, 0.46f }, { 0.09f, 0.09f } },
			{ EGamepadControl::Back, EShape::Button, { 0.72f, 0.42f }, { 0.08f, 0.04f } },
			{ EGamepadControl::Start, EShape::Button, { 1.08f, 0.42f }, { 0.08f, 0.04f } },
			{ EGamepadControl::Guide, EShape::Round, { 0.90f, 0.52f }, { 0.08f, 0.08f } },
			{ EGamepadControl::Misc, EShape::Button, { 0.90f, 0.62f }, { 0.06f, 0.03f } },
			{ EGamepadControl::TouchpadButton, EShape::Button, { 0.90f, 0.31f }, { 0.26f, 0.12f } },
			{ EGamepadControl::LeftPaddle1, EShape::Button, { 0.62f, 0.92f }, { 0.06f, 0.04f } },
			{ EGamepadControl::LeftPaddle2, EShape::Button, { 0.72f, 0.92f }, { 0.06f, 0.04f } },
			{ EGamepadControl::RightPaddle2, EShape::Button, { 1.08f, 0.92f }, { 0.06f, 0.04f } },
			{ EGamepadControl::RightPaddle1, EShape::Button, { 1.18f, 0.92f }, { 0.06f, 0.04f } },
			{ EGamepadControl::Touchpad0, EShape::Button, { 0.90f, 0.31f }, { 0.0f, 0.0f } },
			{ EGamepadControl::Touchpad1, EShape::Button, { 0.90f, 0.31f }, { 0.0f, 0.0f } },
		} };

		struct Diagram
		{
			PainterRect Area;
			float Unit = 1.0f;

			glm::vec2 Point(glm::vec2 at) const
			{
				return Area.Min + at * Unit;
			}

			PainterRect RectOf(const Element& element) const
			{
				const glm::vec2 center = Point(element.Center);
				const glm::vec2 half = element.Size * Unit * 0.5f;
				return PainterRect{ center - half, center + half };
			}
		};

		Diagram ReserveDiagram()
		{
			const float width = std::min(ImGui::GetContentRegionAvail().x, ImGui::GetFontSize() * 30.0f);
			const float height = width / DiagramAspect;
			return Diagram{ Reserve({ width, height }), height };
		}

		void PaintBody(Nyx::UI::WindowPainter& painter, const Diagram& diagram)
		{
			const Nyx::UI::Theme& theme = Nyx::UI::GetTheme();
			const PainterRect body{ diagram.Point({ 0.12f, 0.22f }), diagram.Point({ 1.68f, 0.96f }) };
			painter.AddRoundedRect(body, diagram.Unit * 0.28f, PainterStyle::Filled(theme.Field));
			painter.AddRoundedRect(body, diagram.Unit * 0.28f, PainterStyle::Outlined(theme.FieldBorder, 1.0f));
		}

		// One control in a colour: a stick as its gate, the rest as their shape
		void PaintElement(Nyx::UI::WindowPainter& painter, const Diagram& diagram, const Element& element, const glm::vec4& color, bool bOutlined = false)
		{
			if (element.Size == glm::vec2(0.0f))
			{
				return;
			}
			const PainterRect rect = diagram.RectOf(element);
			const PainterStyle style = bOutlined ? PainterStyle::Outlined(color, 2.0f) : PainterStyle::Filled(color);
			switch (element.Shape)
			{
			case EShape::Round:
			case EShape::Stick:
				painter.AddCircle(diagram.Point(element.Center), rect.GetWidth() * 0.5f, style);
				break;
			case EShape::Button:
			case EShape::Trigger:
				painter.AddRoundedRect(rect, std::min(rect.GetHeight(), rect.GetWidth()) * 0.3f, style);
				break;
			}
		}

		const Element& FindElement(EGamepadControl control)
		{
			return *std::find_if(Elements.begin(), Elements.end(),
				[control](const Element& element)
				{
					return element.Control == control;
				});
		}

		const Element* FindElementAt(const Diagram& diagram, glm::vec2 point)
		{
			// Small controls first: a stick's button lies on its stick
			const Element* found = nullptr;
			for (const Element& element : Elements)
			{
				const PainterRect rect = diagram.RectOf(element);
				const bool bInside = point.x >= rect.Min.x && point.x < rect.Max.x && point.y >= rect.Min.y && point.y < rect.Max.y;
				if (bInside && (!found || rect.GetWidth() < diagram.RectOf(*found).GetWidth()))
				{
					found = &element;
				}
			}
			return found;
		}

		// A stick's place on its gate: y up on the stick, down on the screen
		glm::vec2 StickPoint(glm::vec2 center, float radius, const glm::vec3& value)
		{
			return center + glm::vec2(value.x, -value.y) * radius;
		}

		// The rings of a binding's dead zone around a stick, if it has one
		void PaintDeadZone(Nyx::UI::WindowPainter& painter, glm::vec2 center, float radius, const InputBinding* binding)
		{
			const Nyx::UI::Theme& theme = Nyx::UI::GetTheme();
			if (!binding || !binding->Pipeline.DeadZone.bEnabled)
			{
				return;
			}
			const DeadZoneSettings& deadZone = binding->Pipeline.DeadZone;
			painter.AddCircle(center, std::max(deadZone.Inner * radius, 1.0f), PainterStyle::Outlined(theme.TextMuted, 1.0f));
			painter.AddCircle(center, deadZone.Outer * radius, PainterStyle::Outlined(theme.TextMuted, 1.0f));
		}

		// A bar of `fraction`, e.g. a calibration's progress
		void PaintProgress(float fraction)
		{
			const Nyx::UI::Theme& theme = Nyx::UI::GetTheme();
			const PainterRect bar = Reserve({ std::min(ImGui::GetContentRegionAvail().x, 200.0f), ImGui::GetFontSize() * 0.5f });
			Nyx::UI::WindowPainter& painter = Nyx::UI::GetWindowPainter();
			painter.AddRoundedRect(bar, bar.GetHeight() * 0.5f, PainterStyle::Filled(theme.Track));
			PainterRect done = bar;
			done.Max.x = bar.Min.x + bar.GetWidth() * std::clamp(fraction, 0.0f, 1.0f);
			if (!done.IsEmpty())
			{
				painter.AddRoundedRect(done, bar.GetHeight() * 0.5f, PainterStyle::Filled(theme.Accent));
			}
		}

		const std::array<TriggerEffect, 4> TriggerEffects = { {
			{ .Kind = ETriggerEffectKind::Resistance, .Start = 0.0f, .Strength = 0.5f },
			{ .Kind = ETriggerEffectKind::Weapon, .Start = 0.3f, .End = 0.6f, .Strength = 0.8f },
			{ .Kind = ETriggerEffectKind::Vibration, .Start = 0.5f, .Strength = 0.25f, .FrequencyHz = 15.0f },
			{ .Kind = ETriggerEffectKind::Off },
		} };

		const std::array<glm::vec3, 4> LightColors = { { { 1.0f, 0.2f, 0.1f }, { 0.1f, 1.0f, 0.2f }, { 0.1f, 0.3f, 1.0f }, { 1.0f, 1.0f, 1.0f } } };

		std::array<glm::vec4, 6> SeriesColors()
		{
			const Nyx::UI::Theme& theme = Nyx::UI::GetTheme();
			return { theme.Accent, theme.TextInfo, theme.AxisY, theme.AxisX, theme.AxisZ, theme.FolderIcon };
		}
	}

	const char* InputDebugView::GetTabName(ETab tab)
	{
		switch (tab)
		{
		case ETab::Devices:    return "Devices";
		case ETab::Actions:    return "Actions";
		case ETab::Contexts:   return "Contexts";
		case ETab::ControlMap: return "Control map";
		case ETab::Events:     return "Events";
		case ETab::Timeline:   return "Timeline";
		case ETab::Count:      break;
		}
		return "";
	}

	void InputDebugView::Draw(InputSystem& input, InputHistory& history)
	{
		Calibration.Sample(input.GetFrame());

		if (!ImGui::BeginTabBar("##InputTabs"))
		{
			return;
		}

		for (int index = 0; index < static_cast<int>(ETab::Count); ++index)
		{
			const ETab tab = static_cast<ETab>(index);
			const ImGuiTabItemFlags flags = TabToSelect == tab ? ImGuiTabItemFlags_SetSelected : 0;
			if (!ImGui::BeginTabItem(GetTabName(tab), nullptr, flags))
			{
				continue;
			}

			if (ImGui::BeginChild("##InputTab", ImVec2(0.0f, 0.0f), ImGuiChildFlags_None))
			{
				switch (tab)
				{
				case ETab::Devices:    DrawDevices(input, history); break;
				case ETab::Actions:    DrawActions(input, history); break;
				case ETab::Contexts:   DrawContexts(input); break;
				case ETab::ControlMap: DrawControlMap(input); break;
				case ETab::Events:     DrawEvents(input, history); break;
				case ETab::Timeline:   DrawTimeline(input, history); break;
				case ETab::Count:      break;
				}
			}
			ImGui::EndChild();
			ImGui::EndTabItem();
		}
		TabToSelect.reset();

		ImGui::EndTabBar();
	}

	// ---------------------------------------------------------------------------------------
	// Devices

	void InputDebugView::DrawDevices(InputSystem& input, const InputHistory& history)
	{
		const InputFrame& frame = input.GetFrame();
		if (frame.GetDevices().empty())
		{
			ImGui::TextDisabled("No devices");
			return;
		}

		for (const InputFrame::DeviceState& device : frame.GetDevices())
		{
			const InputDeviceInfo& info = device.Info;
			const std::string header = info.Name + (info.Id == input.GetUser().GetLastUsedDevice() ? "  (used last)" : "") + "###Device" +
				std::to_string(info.Id.Value);
			if (!Nyx::UI::CollapsingHeader(header.c_str()))
			{
				continue;
			}

			ImGui::PushID(static_cast<int>(info.Id.Value));
			ImGui::TextDisabled("%s through %s", GetDeviceKindName(info.Kind), info.Backend.c_str());
			if (info.Kind == EDeviceKind::Gamepad)
			{
				DrawGamepad(input, history, device);
			}
			else
			{
				DrawKeyboardAndMouse(device, frame);
			}
			ImGui::PopID();
		}
	}

	void InputDebugView::DrawGamepad(InputSystem& input, const InputHistory& history, const InputFrame::DeviceState& device)
	{
		const Nyx::UI::Theme& theme = Nyx::UI::GetTheme();
		const InputDeviceInfo& info = device.Info;
		const InputFrame& frame = input.GetFrame();

		ImGui::Text("%s, %s, battery %s", GetGamepadModelName(info.Model), GetConnectionName(info.Connection),
			info.BatteryPercent ? (std::to_string(*info.BatteryPercent) + "%").c_str() : "unknown");
		std::string capabilities;
		const std::array<std::pair<EDeviceCapability, const char*>, 8> names = { {
			{ EDeviceCapability::Rumble, "rumble" },
			{ EDeviceCapability::TriggerRumble, "trigger rumble" },
			{ EDeviceCapability::AdaptiveTriggers, "adaptive triggers" },
			{ EDeviceCapability::Gyro, "gyro" },
			{ EDeviceCapability::Accelerometer, "accelerometer" },
			{ EDeviceCapability::Touchpad, "touchpad" },
			{ EDeviceCapability::Lightbar, "light" },
			{ EDeviceCapability::PlayerLeds, "player LEDs" },
		} };
		for (const auto& [capability, name] : names)
		{
			if (HasCapability(info.Capabilities, capability))
			{
				capabilities += (capabilities.empty() ? "" : ", ") + std::string(name);
			}
		}
		ImGui::TextDisabled("Can: %s", capabilities.empty() ? "nothing more" : capabilities.c_str());

		// The pad, live: buttons light up, sticks show their raw value (white) and the value after
		// the pipeline of the binding that reads them (accent), with its dead zone's rings
		const Diagram diagram = ReserveDiagram();
		Nyx::UI::WindowPainter& painter = Nyx::UI::GetWindowPainter();
		PaintBody(painter, diagram);
		for (const Element& element : Elements)
		{
			const ControlId control = Gamepad(element.Control);
			const ControlState state = frame.GetState(info.Id, control);
			switch (element.Shape)
			{
			case EShape::Button:
			case EShape::Round:
				if (element.Control == EGamepadControl::LeftStickButton || element.Control == EGamepadControl::RightStickButton)
				{
					if (state.Value.x != 0.0f)
					{
						PaintElement(painter, diagram, element, theme.Accent);
					}
					break;
				}
				PaintElement(painter, diagram, element, state.Value.x != 0.0f ? theme.Accent : theme.Frame);
				break;

			case EShape::Trigger:
			{
				PaintElement(painter, diagram, element, theme.Frame);
				PainterRect fill = diagram.RectOf(element);
				fill.Max.x = fill.Min.x + fill.GetWidth() * std::clamp(state.Value.x, 0.0f, 1.0f);
				if (!fill.IsEmpty())
				{
					painter.AddRoundedRect(fill, fill.GetHeight() * 0.3f, PainterStyle::Filled(theme.Accent));
				}
				break;
			}

			case EShape::Stick:
			{
				const glm::vec2 center = diagram.Point(element.Center);
				const float radius = StickRadius * diagram.Unit;
				painter.AddCircle(center, radius, PainterStyle::Filled(theme.Frame));
				const StickBinding binding = FindStickBinding(input, control);
				PaintDeadZone(painter, center, radius, binding.Binding);
				painter.AddCircle(StickPoint(center, radius, state.Value), 3.0f, PainterStyle::Filled(theme.Text));
				if (binding.Trace)
				{
					painter.AddCircle(StickPoint(center, radius, binding.Trace->Value), 3.0f, PainterStyle::Filled(theme.Accent));
				}
				break;
			}
			}
		}

		// Touchpad fingers
		const PainterRect touchpad = diagram.RectOf(FindElement(EGamepadControl::TouchpadButton));
		for (const EGamepadControl finger : { EGamepadControl::Touchpad0, EGamepadControl::Touchpad1 })
		{
			const glm::vec3 value = frame.GetState(info.Id, Gamepad(finger)).Value;
			if (value.z > 0.0f)
			{
				const glm::vec2 at = touchpad.Min + glm::vec2(value.x * touchpad.GetWidth(), value.y * touchpad.GetHeight());
				painter.AddCircle(at, 3.0f, PainterStyle::Filled(theme.TextInfo));
			}
		}

		if (HasCapability(info.Capabilities, EDeviceCapability::Gyro))
		{
			const glm::vec3 gyro = frame.GetState(info.Id, Gamepad(EGamepadControl::Gyro)).Value;
			const glm::vec3 accel = frame.GetState(info.Id, Gamepad(EGamepadControl::Accel)).Value;
			ImGui::Text("Gyro (%.2f, %.2f, %.2f) rad/s, accelerometer (%.2f, %.2f, %.2f) m/s2", gyro.x, gyro.y, gyro.z, accel.x, accel.y, accel.z);
		}

		// Stick plot: every place each stick reported in the last seconds, with the dead zone's
		// rings. Drift shows as a cloud off the middle; turning the stick round its edge shows
		// where full deflection ends.
		Nyx::UI::SectionHeader("Sticks");
		const float plotSize = std::min(ImGui::GetFontSize() * 10.0f, (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) * 0.5f);
		for (const EGamepadControl stickControl : { EGamepadControl::LeftStick, EGamepadControl::RightStick })
		{
			const ControlId stick = Gamepad(stickControl);
			ImGui::BeginGroup();
			ImGui::TextUnformatted(stickControl == EGamepadControl::LeftStick ? "Left stick" : "Right stick");
			const PainterRect plot = Reserve({ plotSize, plotSize });
			const glm::vec2 center = (plot.Min + plot.Max) * 0.5f;
			const float radius = plotSize * 0.45f;
			painter.AddRoundedRect(plot, theme.FrameRounding, PainterStyle::Filled(theme.Field));
			painter.AddCircle(center, radius, PainterStyle::Outlined(theme.Separator, 1.0f));
			PaintDeadZone(painter, center, radius, FindStickBinding(input, stick).Binding);

			const bool bCalibrating = Calibration.IsRunning() && Calibration.GetDevice() == info.Id && Calibration.GetStick() == stick;
			const std::vector<glm::vec3> trail = history.GetTrail(info.Id, stick);
			for (const glm::vec3& point : trail)
			{
				painter.AddCircle(StickPoint(center, radius, point), 1.5f, PainterStyle::Filled(theme.TextMuted));
			}
			if (bCalibrating)
			{
				for (const glm::vec2& point : Calibration.GetSamples())
				{
					painter.AddCircle(StickPoint(center, radius, glm::vec3(point, 0.0f)), 1.5f, PainterStyle::Filled(theme.TextInfo));
				}
			}
			painter.AddCircle(StickPoint(center, radius, frame.GetState(info.Id, stick).Value), 3.0f, PainterStyle::Filled(theme.Text));

			// Calibration
			const char* suffix = stickControl == EGamepadControl::LeftStick ? "##Left" : "##Right";
			if (bCalibrating)
			{
				PaintProgress(Calibration.GetProgress(frame.GetTimeUs()));
				if (Nyx::UI::Button((std::string("Cancel") + suffix).c_str()))
				{
					Calibration.Cancel();
				}
			}
			else
			{
				if (Nyx::UI::Button((std::string("Measure drift") + suffix).c_str()))
				{
					Calibration.Start(StickCalibration::EMeasure::Drift, info.Id, stick, frame.GetTimeUs(), 3.0f);
				}
				ImGui::SetItemTooltip("Leave the stick alone for 3 seconds: suggests an inner dead zone");
				if (Nyx::UI::Button((std::string("Measure range") + suffix).c_str()))
				{
					Calibration.Start(StickCalibration::EMeasure::Range, info.Id, stick, frame.GetTimeUs(), 5.0f);
				}
				ImGui::SetItemTooltip("Turn the stick around its edge for 5 seconds: suggests an outer dead zone");
			}
			ImGui::EndGroup();
			if (stickControl == EGamepadControl::LeftStick)
			{
				ImGui::SameLine();
			}
		}

		const std::optional<StickCalibration::Result>& result = Calibration.GetResult();
		if (result && Calibration.GetDevice() == info.Id && !Calibration.IsRunning())
		{
			const char* measure = result->Measure == StickCalibration::EMeasure::Drift ? "Inner dead zone" : "Outer dead zone";
			if (result->Suggested)
			{
				ImGui::Text("%s: %.2f suggested", measure, *result->Suggested);
			}
			else
			{
				ImGui::Text("%s: no suggestion", measure);
			}
			ImGui::TextDisabled("%s", result->Explanation.c_str());
		}

		// Output: what the user's pads can do, tried on all of them
		Nyx::UI::SectionHeader("Try");
		InputUser& user = input.GetUser();
		if (Nyx::UI::Button("Rumble"))
		{
			user.Rumble({ .Low = 0.6f, .High = 0.3f, .DurationMs = 250 });
		}
		ImGui::SameLine();
		if (Nyx::UI::Button("Trigger rumble"))
		{
			user.TriggerRumble({ .Left = 0.5f, .Right = 0.5f, .DurationMs = 250 });
		}
		ImGui::SameLine();
		const TriggerEffect& effect = TriggerEffects[NextTriggerEffect];
		if (Nyx::UI::Button((std::string("Trigger effect: ") + GetTriggerEffectKindName(effect.Kind) + "###TriggerEffect").c_str()))
		{
			user.SetTriggerEffect(ETrigger::Left, effect);
			user.SetTriggerEffect(ETrigger::Right, effect);
			NextTriggerEffect = (NextTriggerEffect + 1) % TriggerEffects.size();
		}
		ImGui::SameLine();
		if (Nyx::UI::Button("Light colour"))
		{
			user.SetLightColor(LightColors[NextLightColor]);
			NextLightColor = (NextLightColor + 1) % LightColors.size();
		}
		ImGui::TextDisabled("Sent to every pad; what a pad can't do shows in the Events tab with the reason");
	}

	void InputDebugView::DrawKeyboardAndMouse(const InputFrame::DeviceState& device, const InputFrame& frame)
	{
		std::string held;
		for (uint16_t index = 0; index < device.Controls.size(); ++index)
		{
			const ControlId control{ device.Info.Kind, index };
			if (IsButton(control) && device.Controls[index].Value.x != 0.0f)
			{
				held += (held.empty() ? "" : ", ") + GetControlPath(control);
			}
		}
		ImGui::Text("Held: %s", held.empty() ? "nothing" : held.c_str());

		if (device.Info.Kind == EDeviceKind::Mouse)
		{
			const glm::vec3 delta = frame.GetState(device.Info.Id, Mouse(EMouseControl::Delta)).Value;
			const glm::vec3 wheel = frame.GetState(device.Info.Id, Mouse(EMouseControl::Wheel)).Value;
			ImGui::Text("Moved (%.0f, %.0f) px, wheel (%.1f, %.1f) this frame", delta.x, delta.y, wheel.x, wheel.y);
		}
	}

	// ---------------------------------------------------------------------------------------
	// Actions

	void InputDebugView::DrawActions(const InputSystem& input, const InputHistory& history)
	{
		const Nyx::UI::Theme& theme = Nyx::UI::GetTheme();
		const InputUser& user = input.GetUser();
		const InputFrame& frame = input.GetFrame();

		const float tableHeight = std::max(ImGui::GetContentRegionAvail().y * 0.5f, ImGui::GetFontSize() * 8.0f);
		const ImGuiTableFlags flags = ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY | ImGuiTableFlags_Resizable | ImGuiTableFlags_SizingStretchProp;
		if (Nyx::UI::BeginTable("##Actions", 6, flags, glm::vec2(0.0f, tableHeight)))
		{
			ImGui::TableSetupScrollFreeze(0, 1);
			ImGui::TableSetupColumn("Action", ImGuiTableColumnFlags_WidthStretch, 1.0f);
			ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch, 1.2f);
			ImGui::TableSetupColumn("Phase", ImGuiTableColumnFlags_WidthStretch, 0.9f);
			ImGui::TableSetupColumn("From", ImGuiTableColumnFlags_WidthStretch, 1.6f);
			ImGui::TableSetupColumn("Fired", ImGuiTableColumnFlags_WidthStretch, 0.8f);
			ImGui::TableSetupColumn("Last 2 s", ImGuiTableColumnFlags_WidthStretch, 1.0f);
			Nyx::UI::TableHeadersRow();

			for (uint32_t index = 0; index < input.GetActionCount(); ++index)
			{
				const ActionHandle action = ActionHandle::Make(index, 1);
				const InputActionDef& def = input.GetAction(action);
				const ActionState& state = user.Get(action);

				Nyx::UI::TableNextRow();
				ImGui::TableNextColumn();
				if (Nyx::UI::Selectable((def.Name + "##Action" + std::to_string(index)).c_str(), SelectedAction == action, ImGuiSelectableFlags_SpanAllColumns))
				{
					SelectedAction = action;
				}

				ImGui::TableNextColumn();
				ImGui::TextUnformatted(DescribeValue(def.ValueType, state.Value).c_str());

				ImGui::TableNextColumn();
				ImGui::Text("%s%s", GetActionPhaseName(state.Phase), state.bTriggered ? ", fired" : "");

				ImGui::TableNextColumn();
				if (state.Context.IsValid() && state.Phase != EActionPhase::Idle)
				{
					const InputContextDef& context = input.GetContext(state.Context);
					const std::string source =
						state.BindingIndex < context.Bindings.size() ? DescribeBindingSource(context.Bindings[state.BindingIndex]) : std::string();
					ImGui::Text("%s - %s - %s", context.Name.c_str(), source.c_str(), DeviceName(frame, state.Device).c_str());
				}

				ImGui::TableNextColumn();
				ImGui::TextUnformatted(SecondsAgo(frame.GetTimeUs(), history.GetLastTriggeredUs(action)).c_str());

				// A small graph of the value's length
				ImGui::TableNextColumn();
				const PainterRect graph = Reserve({ ImGui::GetContentRegionAvail().x, ImGui::GetTextLineHeight() });
				const std::deque<InputHistory::Frame>& frames = history.GetFrames();
				const size_t shown = std::min<size_t>(frames.size(), 120);
				float highest = 1e-6f;
				for (size_t at = frames.size() - shown; at < frames.size(); ++at)
				{
					highest = std::max(highest, index < frames[at].Actions.size() ? glm::length(frames[at].Actions[index].Value) : 0.0f);
				}
				Nyx::UI::WindowPainter& painter = Nyx::UI::GetWindowPainter();
				for (size_t at = frames.size() - shown + 1; at < frames.size() && shown > 1; ++at)
				{
					const auto pointOf = [&](size_t frameIndex)
					{
						const float value = index < frames[frameIndex].Actions.size() ? glm::length(frames[frameIndex].Actions[index].Value) : 0.0f;
						const float x = graph.Min.x + graph.GetWidth() * static_cast<float>(frameIndex - (frames.size() - shown)) / static_cast<float>(shown - 1);
						return glm::vec2(x, graph.Max.y - 1.0f - (graph.GetHeight() - 2.0f) * value / highest);
					};
					painter.AddLine(pointOf(at - 1), pointOf(at), 1.0f, EPainterLineCap::Round, PainterStyle::Filled(theme.Accent));
				}
			}
			Nyx::UI::EndTable();
		}

		if (!SelectedAction.IsValid())
		{
			ImGui::TextDisabled("Select an action to see why it fired, or why not");
			return;
		}

		// Why (not): every binding of the action, and where it stopped
		Nyx::UI::SectionHeader(("Bindings of " + input.GetAction(SelectedAction).Name).c_str());
		const std::vector<const BindingTrace*> traces = user.GetTraces(SelectedAction);
		if (traces.empty())
		{
			ImGui::TextDisabled("No context binds it");
		}
		for (const BindingTrace* trace : traces)
		{
			const bool bStopped = trace->Outcome != EBindingOutcome::Contributed && trace->Outcome != EBindingOutcome::Waiting;
			ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(bStopped ? ImGuiCol_TextDisabled : ImGuiCol_Text));
			ImGui::TextWrapped("%s", FormatTrace(input, *trace).c_str());
			ImGui::PopStyleColor();
			Nyx::UI::Separator();
		}
	}

	// ---------------------------------------------------------------------------------------
	// Contexts

	void InputDebugView::DrawContexts(const InputSystem& input)
	{
		const InputUser& user = input.GetUser();
		const std::vector<InputUser::StackEntry> stack = user.GetStack();

		const CursorRequest& cursor = user.GetCursor();
		ImGui::Text("Cursor: %s%s%s", GetCursorModeName(cursor.Mode), cursor.Owner.empty() ? "" : ", by ", cursor.Owner.c_str());

		Nyx::UI::SectionHeader("On, the highest first");
		const ImGuiTableFlags flags = ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable | ImGuiTableFlags_SizingStretchProp;
		if (Nyx::UI::BeginTable("##Stack", 5, flags))
		{
			ImGui::TableSetupColumn("Context", ImGuiTableColumnFlags_WidthStretch, 1.0f);
			ImGui::TableSetupColumn("Priority", ImGuiTableColumnFlags_WidthStretch, 0.5f);
			ImGui::TableSetupColumn("Why it is on", ImGuiTableColumnFlags_WidthStretch, 1.6f);
			ImGui::TableSetupColumn("Takes", ImGuiTableColumnFlags_WidthStretch, 1.0f);
			ImGui::TableSetupColumn("Acting now", ImGuiTableColumnFlags_WidthStretch, 1.4f);
			Nyx::UI::TableHeadersRow();

			for (const InputUser::StackEntry& entry : stack)
			{
				const InputContextDef& def = input.GetContext(entry.Context);
				Nyx::UI::TableNextRow();
				ImGui::TableNextColumn();
				ImGui::Text("%s%s", def.Name.c_str(), def.Cursor ? (std::string(" (cursor ") + GetCursorModeName(*def.Cursor) + ")").c_str() : "");
				ImGui::TableNextColumn();
				ImGui::Text("%d", def.Priority);
				ImGui::TableNextColumn();
				ImGui::TextUnformatted(entry.Activation.Reason.c_str());
				ImGui::TableNextColumn();
				const std::string takes = DescribeDevices(entry.Activation.TakesDevices.value_or(def.TakesDevices));
				ImGui::TextUnformatted(takes.empty() ? "its bindings' controls" : takes.c_str());

				// The actions its bindings drive in this frame
				ImGui::TableNextColumn();
				std::string acting;
				for (const BindingTrace& trace : user.GetTraces())
				{
					if (trace.Context == entry.Context && (trace.Outcome == EBindingOutcome::Contributed || trace.Outcome == EBindingOutcome::Waiting))
					{
						acting += (acting.empty() ? "" : ", ") + input.GetAction(trace.Action).Name;
					}
				}
				ImGui::TextUnformatted(acting.c_str());
			}
			Nyx::UI::EndTable();
		}

		Nyx::UI::SectionHeader("Off");
		for (uint32_t index = 0; index < input.GetContextCount(); ++index)
		{
			const ContextHandle context = ContextHandle::Make(index, 1);
			if (!user.IsContextActive(context))
			{
				const InputContextDef& def = input.GetContext(context);
				ImGui::TextDisabled("%s (%d), %zu bindings", def.Name.c_str(), def.Priority, def.Bindings.size());
			}
		}
	}

	// ---------------------------------------------------------------------------------------
	// Control map

	void InputDebugView::DrawControlMap(const InputSystem& input)
	{
		const Nyx::UI::Theme& theme = Nyx::UI::GetTheme();
		const InputUser& user = input.GetUser();
		const InputFrame& frame = input.GetFrame();

		static constexpr std::array<const char*, 3> DeviceNames = { "Keyboard", "Mouse", "Gamepad" };
		ImGui::SetNextItemWidth(ImGui::GetFontSize() * 9.0f);
		Nyx::UI::Combo("Device", &MapDevice, DeviceNames);
		const EDeviceKind kind = static_cast<EDeviceKind>(std::clamp(MapDevice, 0, 2));

		ImGui::SameLine();
		if (Nyx::UI::Checkbox("What if", &bWhatIf) && bWhatIf)
		{
			// Start from the stack as it is
			WhatIfContexts.assign(input.GetContextCount(), false);
			for (const InputUser::StackEntry& entry : user.GetStack())
			{
				WhatIfContexts[entry.Context.Index()] = true;
			}
		}
		ImGui::SetItemTooltip("Shows the map for other contexts than those on now, e.g. with a menu open");

		ImGui::SameLine();
		if (Nyx::UI::Button(bPressToFind ? "Press a key or button...###PressToFind" : "Press to find###PressToFind"))
		{
			bPressToFind = !bPressToFind;
		}
		ImGui::SetItemTooltip("Lists every binding of the next key, button or stick used");

		// The stack the map is for
		std::vector<InputUser::StackEntry> stack = user.GetStack();
		if (bWhatIf)
		{
			WhatIfContexts.resize(input.GetContextCount(), false);
			std::vector<ContextHandle> chosen;
			for (uint32_t index = 0; index < input.GetContextCount(); ++index)
			{
				const ContextHandle context = ContextHandle::Make(index, 1);
				bool bOn = WhatIfContexts[index];
				const InputContextDef& def = input.GetContext(context);
				if (Nyx::UI::Checkbox((def.Name + " (" + std::to_string(def.Priority) + ")##WhatIf").c_str(), &bOn))
				{
					WhatIfContexts[index] = bOn;
				}
				if (index + 1 < input.GetContextCount() && ImGui::GetContentRegionAvail().x > ImGui::GetFontSize() * 14.0f)
				{
					ImGui::SameLine();
				}
				if (WhatIfContexts[index])
				{
					chosen.push_back(context);
				}
			}
			ImGui::NewLine();
			stack = MakeStack(input, chosen);
		}

		// Press to find: the next control pressed, or a stick or trigger pushed past half
		if (bPressToFind)
		{
			for (const InputEvent& event : frame.GetEvents())
			{
				const bool bPressed = event.Kind == EInputEventKind::Control && !IsSensor(event.Control) && !IsRelative(event.Control) &&
					(IsButton(event.Control) ? event.Value.x != 0.0f : glm::length(event.Value) > 0.5f);
				if (bPressed)
				{
					FoundControl = event.Control;
					bPressToFind = false;
					break;
				}
			}
		}
		if (FoundControl)
		{
			Nyx::UI::SectionHeader(("Bindings of " + GetControlPath(*FoundControl)).c_str());
			const std::vector<ControlUse> gets = ResolveControl(input, stack, *FoundControl);
			const std::vector<ControlUse> all = FindBindingsOf(input, *FoundControl);
			if (all.empty())
			{
				ImGui::TextDisabled("No context binds it");
			}
			for (const ControlUse& use : all)
			{
				const bool bGets = std::any_of(gets.begin(), gets.end(),
					[&use](const ControlUse& got)
					{
						return got.Context == use.Context && got.BindingIndex == use.BindingIndex;
					});
				const bool bOn = std::any_of(stack.begin(), stack.end(),
					[&use](const InputUser::StackEntry& entry)
					{
						return entry.Context == use.Context;
					});
				ImGui::Text("%s%s", DescribeUse(input, use).c_str(), bGets ? ": gets it now" : bOn ? ": doesn't get it, a context above takes it"
																								   : ": context off");
			}
			if (!gets.empty() && gets.back().BindingIndex == NoBinding)
			{
				ImGui::Text("%s", DescribeUse(input, gets.back()).c_str());
			}
		}

		// Who gets each control: the first use is the context that takes it
		const auto describeUses = [&input](const std::vector<ControlUse>& uses)
		{
			std::string text;
			for (const ControlUse& use : uses)
			{
				text += (text.empty() ? "" : "; ") + DescribeUse(input, use);
			}
			return text;
		};

		if (kind == EDeviceKind::Gamepad)
		{
			// Bound controls in the accent, controls nothing gets dark, a context taking the pad
			// muted, several actions on one control in one context red
			const Diagram diagram = ReserveDiagram();
			Nyx::UI::WindowPainter& painter = Nyx::UI::GetWindowPainter();
			PaintBody(painter, diagram);
			for (const Element& element : Elements)
			{
				const std::vector<ControlUse> uses = ResolveControl(input, stack, Gamepad(element.Control));
				glm::vec4 color = theme.Frame;
				if (!uses.empty())
				{
					size_t actions = 0;
					for (const ControlUse& use : uses)
					{
						actions += use.Context == uses.back().Context && use.BindingIndex != NoBinding ? 1 : 0;
					}
					color = uses.back().BindingIndex == NoBinding ? theme.TextMuted : actions > 1 ? theme.ButtonStop
																								  : theme.Accent;
				}
				PaintElement(painter, diagram, element, color, element.Shape == EShape::Stick);
			}

			if (IsMouseIn(diagram.Area))
			{
				const ImVec2 mouse = ImGui::GetMousePos();
				if (const Element* hovered = FindElementAt(diagram, { mouse.x, mouse.y }))
				{
					const ControlId control = Gamepad(hovered->Control);
					const std::string uses = describeUses(ResolveControl(input, stack, control));
					ImGui::SetTooltip("%s\n%s", GetControlPath(control).c_str(), uses.empty() ? "Nothing gets it" : uses.c_str());
				}
			}
		}

		const ImGuiTableFlags flags = ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable | ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_ScrollY;
		if (Nyx::UI::BeginTable("##ControlMap", 3, flags))
		{
			ImGui::TableSetupScrollFreeze(0, 1);
			ImGui::TableSetupColumn("Control", ImGuiTableColumnFlags_WidthStretch, 0.8f);
			ImGui::TableSetupColumn("Gets it", ImGuiTableColumnFlags_WidthStretch, 1.6f);
			ImGui::TableSetupColumn("Bound, but not reached", ImGuiTableColumnFlags_WidthStretch, 1.6f);
			Nyx::UI::TableHeadersRow();

			for (uint16_t index = 0; index < GetControlCount(kind); ++index)
			{
				const ControlId control{ kind, index };
				const std::vector<ControlUse> gets = ResolveControl(input, stack, control);
				const std::vector<ControlUse> all = FindBindingsOf(input, control);
				if (gets.empty() && all.empty())
				{
					continue;
				}

				std::vector<ControlUse> unreached;
				for (const ControlUse& use : all)
				{
					const bool bGets = std::any_of(gets.begin(), gets.end(),
						[&use](const ControlUse& got)
						{
							return got.Context == use.Context && got.BindingIndex == use.BindingIndex;
						});
					if (!bGets)
					{
						unreached.push_back(use);
					}
				}

				Nyx::UI::TableNextRow();
				ImGui::TableNextColumn();
				ImGui::TextUnformatted(GetControlPath(control).c_str());
				ImGui::TableNextColumn();
				ImGui::TextUnformatted(gets.empty() ? "-" : describeUses(gets).c_str());
				ImGui::TableNextColumn();
				ImGui::TextDisabled("%s", describeUses(unreached).c_str());
			}
			Nyx::UI::EndTable();
		}
	}

	// ---------------------------------------------------------------------------------------
	// Events

	void InputDebugView::DrawEvents(const InputSystem& input, InputHistory& history)
	{
		const InputFrame& frame = input.GetFrame();

		if (Nyx::UI::Button(history.IsPaused() ? "Resume###Pause" : "Pause###Pause"))
		{
			history.SetPaused(!history.IsPaused());
		}
		ImGui::SetItemTooltip("While paused, the events, the timeline and the stick plots stay as they are");
		ImGui::SameLine();
		if (Nyx::UI::Button("Clear"))
		{
			history.Clear();
		}

		// The rows as text, newest first, for the table and for Copy
		struct Row
		{
			std::string Time;
			std::string Device;
			std::string What;
		};
		std::vector<Row> rows;
		for (auto entry = history.GetEntries().rbegin(); entry != history.GetEntries().rend(); ++entry)
		{
			Row row;
			row.Time = Format("-%.3f", frame.GetTimeUs() > entry->TimeUs ? static_cast<double>(frame.GetTimeUs() - entry->TimeUs) / 1'000'000.0 : 0.0);
			if (entry->Output)
			{
				row.Device = DeviceName(frame, entry->Output->Device);
				row.What = (entry->Output->bSent ? "Sent: " : "Not sent: ") + entry->Output->What +
					(entry->Output->Reason.empty() ? "" : " (" + entry->Output->Reason + ")");
			}
			else
			{
				const InputEvent& event = *entry->Event;
				if (!bShowSensors && event.Kind == EInputEventKind::Control && IsSensor(event.Control))
				{
					continue;
				}
				row.Device = event.Kind == EInputEventKind::DeviceAdded ? event.Device.Name : DeviceName(frame, event.Device.Id);
				switch (event.Kind)
				{
				case EInputEventKind::DeviceAdded:   row.What = "connected"; break;
				case EInputEventKind::DeviceRemoved: row.What = "disconnected"; break;
				case EInputEventKind::DeviceChanged: row.What = "changed"; break;
				case EInputEventKind::FocusChanged:  row.What = event.bFocused ? "focus gained" : "focus lost"; break;
				case EInputEventKind::Control:
					row.What = GetControlPath(event.Control) + " " +
						(IsButton(event.Control) ? std::string(event.Value.x != 0.0f ? "down" : "up") : FormatValue(event.Value));
					break;
				}
			}
			if (!EventFilter.empty() && (row.Device + " " + row.What).find(EventFilter) == std::string::npos)
			{
				continue;
			}
			rows.push_back(std::move(row));
		}

		ImGui::SameLine();
		if (Nyx::UI::Button("Copy"))
		{
			std::string text;
			for (const Row& row : rows)
			{
				text += row.Time + "\t" + row.Device + "\t" + row.What + "\n";
			}
			ImGui::SetClipboardText(text.c_str());
		}
		ImGui::SetItemTooltip("Copies the rows shown, as text");
		ImGui::SameLine();
		Nyx::UI::Checkbox("Sensors", &bShowSensors);
		ImGui::SetItemTooltip("Gyro and accelerometer, once per frame");
		ImGui::SameLine();
		ImGui::SetNextItemWidth(ImGui::GetFontSize() * 12.0f);
		Nyx::UI::SearchField("##EventFilter", &EventFilter, "Filter");

		const ImGuiTableFlags flags = ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY | ImGuiTableFlags_Resizable | ImGuiTableFlags_SizingStretchProp;
		if (Nyx::UI::BeginTable("##Events", 3, flags))
		{
			ImGui::TableSetupScrollFreeze(0, 1);
			ImGui::TableSetupColumn("Seconds", ImGuiTableColumnFlags_WidthFixed, ImGui::CalcTextSize("-000.000").x);
			ImGui::TableSetupColumn("Device", ImGuiTableColumnFlags_WidthStretch, 1.0f);
			ImGui::TableSetupColumn("What", ImGuiTableColumnFlags_WidthStretch, 3.0f);
			Nyx::UI::TableHeadersRow();

			ImGuiListClipper clipper;
			clipper.Begin(static_cast<int>(rows.size()));
			while (Nyx::UI::TableClipperStep(clipper))
			{
				for (int index = clipper.DisplayStart; index < clipper.DisplayEnd; ++index)
				{
					const Row& row = rows[static_cast<size_t>(index)];
					Nyx::UI::TableNextRow();
					ImGui::TableNextColumn();
					ImGui::TextUnformatted(row.Time.c_str());
					ImGui::TableNextColumn();
					ImGui::TextUnformatted(row.Device.c_str());
					ImGui::TableNextColumn();
					ImGui::TextUnformatted(row.What.c_str());
				}
			}
			Nyx::UI::EndTable();
		}
	}

	// ---------------------------------------------------------------------------------------
	// Timeline

	void InputDebugView::DrawTimeline(const InputSystem& input, InputHistory& history)
	{
		const Nyx::UI::Theme& theme = Nyx::UI::GetTheme();
		const std::array<glm::vec4, 6> colors = SeriesColors();

		if (Nyx::UI::Button(history.IsPaused() ? "Resume###Pause" : "Pause###Pause"))
		{
			history.SetPaused(!history.IsPaused());
		}
		ImGui::SetItemTooltip("Freezes the last seconds; hover the graph to see each frame's traces");

		// The actions to show, as boxes in rows
		TimelineActions.resize(input.GetActionCount(), false);
		std::vector<uint32_t> shown;
		for (uint32_t index = 0; index < input.GetActionCount(); ++index)
		{
			const std::string label = input.GetAction(ActionHandle::Make(index, 1)).Name + "##Timeline";
			const float width = ImGui::CalcTextSize(label.c_str(), nullptr, true).x + ImGui::GetFontSize() * 2.5f;
			if (index == 0 || ImGui::GetContentRegionAvail().x < width)
			{
				ImGui::NewLine();
			}
			else
			{
				ImGui::SameLine();
			}
			bool bShown = TimelineActions[index];
			if (Nyx::UI::Checkbox(label.c_str(), &bShown))
			{
				TimelineActions[index] = bShown;
			}
			if (bShown)
			{
				shown.push_back(index);
			}
		}

		const std::deque<InputHistory::Frame>& frames = history.GetFrames();
		const PainterRect area = Reserve({ ImGui::GetContentRegionAvail().x, ImGui::GetFontSize() * 10.0f });
		Nyx::UI::WindowPainter& painter = Nyx::UI::GetWindowPainter();
		painter.AddRoundedRect(area, theme.FrameRounding, PainterStyle::Filled(theme.Field));
		if (frames.size() < 2)
		{
			return;
		}

		const float step = area.GetWidth() / static_cast<float>(frames.size() - 1);
		const auto xOf = [&](size_t frameIndex)
		{
			return area.Min.x + static_cast<float>(frameIndex) * step;
		};

		// Where the stack changed
		for (size_t index = 1; index < frames.size(); ++index)
		{
			const auto contextsOf = [](const InputHistory::Frame& frame)
			{
				std::vector<uint64_t> contexts;
				for (const InputUser::StackEntry& entry : frame.Stack)
				{
					contexts.push_back(entry.Context.Value);
				}
				return contexts;
			};
			if (contextsOf(frames[index]) != contextsOf(frames[index - 1]))
			{
				painter.AddLine({ xOf(index), area.Min.y + 2.0f }, { xOf(index), area.Max.y - 2.0f }, 1.0f, EPainterLineCap::Flat,
					PainterStyle::Filled(theme.TextMuted));
			}
		}

		// Each action's value as its length, scaled to the largest shown; buttons 0 or 1. A dot
		// where it fired.
		for (size_t series = 0; series < shown.size(); ++series)
		{
			const uint32_t action = shown[series];
			const glm::vec4& color = colors[series % colors.size()];
			float highest = 1e-6f;
			for (const InputHistory::Frame& frame : frames)
			{
				highest = std::max(highest, action < frame.Actions.size() ? glm::length(frame.Actions[action].Value) : 0.0f);
			}
			const auto pointOf = [&](size_t frameIndex)
			{
				const InputHistory::Frame& frame = frames[frameIndex];
				const float value = action < frame.Actions.size() ? glm::length(frame.Actions[action].Value) / highest : 0.0f;
				return glm::vec2(xOf(frameIndex), area.Max.y - 4.0f - value * (area.GetHeight() - 8.0f));
			};
			for (size_t index = 1; index < frames.size(); ++index)
			{
				painter.AddLine(pointOf(index - 1), pointOf(index), 1.5f, EPainterLineCap::Round, PainterStyle::Filled(color));
			}
			for (size_t index = 0; index < frames.size(); ++index)
			{
				if (action < frames[index].Actions.size() && frames[index].Actions[action].bTriggered)
				{
					painter.AddCircle(pointOf(index), 2.5f, PainterStyle::Filled(color));
				}
			}
		}

		if (shown.empty())
		{
			ImGui::TextDisabled("Choose actions above");
			return;
		}

		// Legend
		for (size_t series = 0; series < shown.size(); ++series)
		{
			if (series > 0)
			{
				ImGui::SameLine();
			}
			const PainterRect swatch = Reserve({ ImGui::GetFontSize() * 0.7f, ImGui::GetFontSize() * 0.7f });
			painter.AddRoundedRect(swatch, 2.0f, PainterStyle::Filled(colors[series % colors.size()]));
			ImGui::SameLine();
			ImGui::TextUnformatted(input.GetAction(ActionHandle::Make(shown[series], 1)).Name.c_str());
		}

		// The frame under the mouse, with its traces
		if (!IsMouseIn(area))
		{
			ImGui::TextDisabled("Hover the graph to see a frame's traces");
			return;
		}
		const float mouseX = ImGui::GetMousePos().x;
		const size_t hovered = std::min(static_cast<size_t>(std::max(mouseX - area.Min.x + step * 0.5f, 0.0f) / step), frames.size() - 1);
		painter.AddLine({ xOf(hovered), area.Min.y }, { xOf(hovered), area.Max.y }, 1.0f, EPainterLineCap::Flat, PainterStyle::Filled(theme.Text));

		const InputHistory::Frame& frame = frames[hovered];
		const uint64_t newest = frames.back().TimeUs;
		ImGui::Text("Frame %llu, %.3f s before the newest", static_cast<unsigned long long>(frame.FrameIndex),
			static_cast<double>(newest - frame.TimeUs) / 1'000'000.0);
		for (const uint32_t action : shown)
		{
			for (const BindingTrace& trace : frame.Traces)
			{
				if (trace.Action.Index() == action)
				{
					ImGui::TextWrapped("%s", FormatTrace(input, trace).c_str());
				}
			}
		}
	}
}
