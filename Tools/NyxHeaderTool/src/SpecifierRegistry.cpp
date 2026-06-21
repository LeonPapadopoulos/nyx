#include "SpecifierRegistry.h"

namespace Nyx::HeaderTool
{
	void TypeSemanticContext::AddMetadata(std::string key, ParsedValue value)
	{
		Type.EmittedMetadata.push_back(ParsedMacroEntry{
			.Name = std::move(key),
			.Value = std::move(value)
			});
	}

	void TypeSemanticContext::AddStringMetadata(std::string key, std::string value)
	{
		ParsedValue parsed{};
		parsed.Kind = EMacroValueKind::String;
		parsed.Text = std::move(value);
		AddMetadata(std::move(key), std::move(parsed));
	}

	void TypeSemanticContext::AddIdentifierMetadata(std::string key, std::string value)
	{
		ParsedValue parsed{};
		parsed.Kind = EMacroValueKind::Identifier;
		parsed.Text = std::move(value);
		AddMetadata(std::move(key), std::move(parsed));
	}

	void TypeSemanticContext::AddNumberMetadata(std::string key, double value, std::string text)
	{
		ParsedValue parsed{};
		parsed.Kind = EMacroValueKind::Number;
		parsed.Number = value;
		parsed.Text = std::move(text);
		AddMetadata(std::move(key), std::move(parsed));
	}

	void TypeSemanticContext::SetRole(EParsedTypeRole role)
	{
		Type.Role = role;
	}

	void PropertySemanticContext::AddMetadata(std::string key, ParsedValue value)
	{
		Property.EmittedMetadata.push_back(ParsedMacroEntry{
			.Name = std::move(key),
			.Value = std::move(value)
			});
	}

	void PropertySemanticContext::AddStringMetadata(std::string key, std::string value)
	{
		ParsedValue parsed{};
		parsed.Kind = EMacroValueKind::String;
		parsed.Text = std::move(value);
		AddMetadata(std::move(key), std::move(parsed));
	}

	void PropertySemanticContext::AddIdentifierMetadata(std::string key, std::string value)
	{
		ParsedValue parsed{};
		parsed.Kind = EMacroValueKind::Identifier;
		parsed.Text = std::move(value);
		AddMetadata(std::move(key), std::move(parsed));
	}

	void PropertySemanticContext::AddNumberMetadata(std::string key, double value, std::string text)
	{
		ParsedValue parsed{};
		parsed.Kind = EMacroValueKind::Number;
		parsed.Number = value;
		parsed.Text = std::move(text);
		AddMetadata(std::move(key), std::move(parsed));
	}

	void PropertySemanticContext::AddFlag(EParsedPropertyFlags flag)
	{
		Property.Flags |= flag;
	}
	static void ApplyComponentToType(TypeSemanticContext& ctx, const ParsedMacroEntry&)
	{
		ctx.SetRole(EParsedTypeRole::Component);
	}

	static void ApplyEditToProperty(PropertySemanticContext& ctx, const ParsedMacroEntry&)
	{
		ctx.AddFlag(EParsedPropertyFlags::Edit);
	}

	static void ApplyUndoToProperty(PropertySemanticContext& ctx, const ParsedMacroEntry&)
	{
		ctx.AddFlag(EParsedPropertyFlags::Undo);
	}

	static void ApplySerializeToProperty(PropertySemanticContext& ctx, const ParsedMacroEntry&)
	{
		ctx.AddFlag(EParsedPropertyFlags::Serialize);
	}

	static void ApplyHiddenToProperty(PropertySemanticContext& ctx, const ParsedMacroEntry&)
	{
		ctx.AddFlag(EParsedPropertyFlags::Hidden);
	}

	static void ApplyReadOnlyToProperty(PropertySemanticContext& ctx, const ParsedMacroEntry&)
	{
		ctx.AddFlag(EParsedPropertyFlags::ReadOnly);
	}

	static void ApplyDisplayNameToType(TypeSemanticContext& ctx, const ParsedMacroEntry& entry)
	{
		ctx.Type.DisplayName = entry.Value->Text;
	}

	static void ApplyDisplayNameToProperty(PropertySemanticContext& ctx, const ParsedMacroEntry& entry)
	{
		ctx.Property.DisplayName = entry.Value->Text;
	}

	static void ApplyCategoryToProperty(PropertySemanticContext& ctx, const ParsedMacroEntry& entry)
	{
		ctx.AddStringMetadata("Category", entry.Value->Text);
	}

	static void ApplyTooltipToProperty(PropertySemanticContext& ctx, const ParsedMacroEntry& entry)
	{
		ctx.AddStringMetadata("Tooltip", entry.Value->Text);
	}

	static void ApplyDragSpeedToProperty(PropertySemanticContext& ctx, const ParsedMacroEntry& entry)
	{
		ctx.AddNumberMetadata("DragSpeed", entry.Value->Number, entry.Value->Text);
	}

	static void ApplyUIToProperty(PropertySemanticContext& ctx, const ParsedMacroEntry& entry)
	{
		ctx.AddIdentifierMetadata("UI", entry.Value->Text);
	}

	const SpecifierDefinition SpecifierRegistry::Definitions[] =
	{
		{ "Component",  EMacroEntrySource::Specifier, static_cast<uint8_t>(ESpecifierTarget::Type), ESpecifierValueKind::None, ApplyComponentToType, nullptr },

		{ "Edit",       EMacroEntrySource::Specifier, static_cast<uint8_t>(ESpecifierTarget::Property), ESpecifierValueKind::None, nullptr, ApplyEditToProperty },
		{ "Undo",       EMacroEntrySource::Specifier, static_cast<uint8_t>(ESpecifierTarget::Property), ESpecifierValueKind::None, nullptr, ApplyUndoToProperty },
		{ "Serialize",  EMacroEntrySource::Specifier, static_cast<uint8_t>(ESpecifierTarget::Property), ESpecifierValueKind::None, nullptr, ApplySerializeToProperty },
		{ "Hidden",     EMacroEntrySource::Specifier, static_cast<uint8_t>(ESpecifierTarget::Property), ESpecifierValueKind::None, nullptr, ApplyHiddenToProperty },
		{ "ReadOnly",   EMacroEntrySource::Specifier, static_cast<uint8_t>(ESpecifierTarget::Property), ESpecifierValueKind::None, nullptr, ApplyReadOnlyToProperty },

		{ "DisplayName", EMacroEntrySource::Metadata, static_cast<uint8_t>(ESpecifierTarget::Type | ESpecifierTarget::Property), ESpecifierValueKind::RequiredString, ApplyDisplayNameToType, ApplyDisplayNameToProperty },

		{ "Category",   EMacroEntrySource::Metadata, static_cast<uint8_t>(ESpecifierTarget::Property), ESpecifierValueKind::RequiredString, nullptr, ApplyCategoryToProperty },
		{ "Tooltip",    EMacroEntrySource::Metadata, static_cast<uint8_t>(ESpecifierTarget::Property), ESpecifierValueKind::RequiredString, nullptr, ApplyTooltipToProperty },
		{ "DragSpeed",  EMacroEntrySource::Metadata, static_cast<uint8_t>(ESpecifierTarget::Property), ESpecifierValueKind::RequiredNumber, nullptr, ApplyDragSpeedToProperty },
		{ "UI",         EMacroEntrySource::Metadata, static_cast<uint8_t>(ESpecifierTarget::Property), ESpecifierValueKind::RequiredIdentifier, nullptr, ApplyUIToProperty },
	};

	const size_t SpecifierRegistry::DefinitionCount = sizeof(Definitions) / sizeof(Definitions[0]);

	static bool TargetMatches(uint8_t mask, ESpecifierTarget target)
	{
		return (mask & static_cast<uint8_t>(target)) != 0;
	}

	const SpecifierDefinition* SpecifierRegistry::Find(
		EMacroEntrySource source,
		std::string_view name,
		ESpecifierTarget target)
	{
		for (size_t i = 0; i < DefinitionCount; ++i)
		{
			const SpecifierDefinition& def = Definitions[i];
			if (def.Source == source && def.Name == name && TargetMatches(def.TargetMask, target))
			{
				return &def;
			}
		}

		return nullptr;
	}
}