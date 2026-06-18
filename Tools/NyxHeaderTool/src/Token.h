#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace Nyx::HeaderTool
{
	enum class ETokenKind : uint16_t
	{
		Invalid = 0,
		EndOfFile,

		Identifier,
		StringLiteral,
		IntegerLiteral,
		FloatLiteral,

		LParen,          // (
		RParen,          // )
		LBrace,          // {
		RBrace,          // }
		LBracket,        // [
		RBracket,        // ]

		Comma,           // ,
		Semicolon,       // ;
		Colon,           // :
		Scope,           // ::
		Dot,             // .
		Star,            // *
		Ampersand,       // &
		Equals,          // =
		Less,            // <
		Greater,         // >
		Plus,            // +
		Minus,           // -
		Slash,           // /
		Percent,         // %
		Hash,            // #
		Question,        // ?
		Exclamation,     // !

		Unknown
	};

	struct Token
	{
		ETokenKind Kind = ETokenKind::Invalid;
		std::string Text;

		int Line = 1;
		int Column = 1;
	};

	inline const char* ToString(ETokenKind kind)
	{
		switch (kind)
		{
		case ETokenKind::Invalid: return "Invalid";
		case ETokenKind::EndOfFile: return "EndOfFile";
		case ETokenKind::Identifier: return "Identifier";
		case ETokenKind::StringLiteral: return "StringLiteral";
		case ETokenKind::IntegerLiteral: return "IntegerLiteral";
		case ETokenKind::FloatLiteral: return "FloatLiteral";
		case ETokenKind::LParen: return "LParen";
		case ETokenKind::RParen: return "RParen";
		case ETokenKind::LBrace: return "LBrace";
		case ETokenKind::RBrace: return "RBrace";
		case ETokenKind::LBracket: return "LBracket";
		case ETokenKind::RBracket: return "RBracket";
		case ETokenKind::Comma: return "Comma";
		case ETokenKind::Semicolon: return "Semicolon";
		case ETokenKind::Colon: return "Colon";
		case ETokenKind::Scope: return "Scope";
		case ETokenKind::Dot: return "Dot";
		case ETokenKind::Star: return "Star";
		case ETokenKind::Ampersand: return "Ampersand";
		case ETokenKind::Equals: return "Equals";
		case ETokenKind::Less: return "Less";
		case ETokenKind::Greater: return "Greater";
		case ETokenKind::Plus: return "Plus";
		case ETokenKind::Minus: return "Minus";
		case ETokenKind::Slash: return "Slash";
		case ETokenKind::Percent: return "Percent";
		case ETokenKind::Hash: return "Hash";
		case ETokenKind::Question: return "Question";
		case ETokenKind::Exclamation: return "Exclamation";
		case ETokenKind::Unknown: return "Unknown";
		default: return "Unrecognized";
		}
	}
}