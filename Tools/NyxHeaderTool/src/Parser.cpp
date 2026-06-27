#include "Parser.h"

#include <sstream>
#include <stdexcept>

// Local Helper
namespace Nyx::HeaderTool
{
	static std::string ClauseToString(const std::vector<Token>& clause)
	{
		std::ostringstream out;
		for (const Token& token : clause)
		{
			out << token.Text;
		}
		return out.str();
	}
}

namespace Nyx::HeaderTool
{
	Parser::Parser(std::vector<Token> tokens, std::string sourceFilePath)
		: Tokens(std::move(tokens))
		, SourceFilePath(std::move(sourceFilePath))
	{
	}

	ParsedHeader Parser::ParseHeader()
	{
		ParsedHeader parsedHeader{};

		while (!IsAtEnd())
		{
			ParseDeclarationInCurrentScope(parsedHeader);
		}

		return parsedHeader;
	}

	bool Parser::IsAtEnd() const
	{
		return Peek().Kind == ETokenKind::EndOfFile;
	}

	const Token& Parser::Peek(size_t lookahead) const
	{
		const size_t index = Current + lookahead;
		if (index >= Tokens.size())
		{
			return Tokens.back();
		}
		return Tokens[index];
	}

	const Token& Parser::Previous() const
	{
		return Tokens[Current - 1];
	}

	const Token& Parser::Advance()
	{
		if (!IsAtEnd())
		{
			++Current;
		}
		return Previous();
	}

	bool Parser::Check(ETokenKind kind) const
	{
		return Peek().Kind == kind;
	}

	bool Parser::Match(ETokenKind kind)
	{
		if (!Check(kind))
		{
			return false;
		}

		Advance();
		return true;
	}

	bool Parser::CheckIdentifier(std::string_view text) const
	{
		return Peek().Kind == ETokenKind::Identifier && Peek().Text == text;
	}

	bool Parser::MatchIdentifier(std::string_view text)
	{
		if (!CheckIdentifier(text))
		{
			return false;
		}

		Advance();
		return true;
	}

	const Token& Parser::Expect(ETokenKind kind, std::string_view message)
	{
		if (!Check(kind))
		{
			ErrorHere(message);
		}
		return Advance();
	}

	const Token& Parser::ExpectIdentifier(std::string_view message)
	{
		if (!Check(ETokenKind::Identifier))
		{
			ErrorHere(message);
		}
		return Advance();
	}

	void Parser::ParseDeclarationInCurrentScope(ParsedHeader& outHeader)
	{
		if (MatchIdentifier("namespace"))
		{
			ParseNamespace(outHeader);
			return;
		}

		if (CheckIdentifier("NYX_REFLECT"))
		{
			outHeader.Types.push_back(ParseReflectedType());
			return;
		}

		if (Check(ETokenKind::LBrace))
		{
			SkipBalanced(ETokenKind::LBrace, ETokenKind::RBrace);
			return;
		}

		Advance();
	}

	void Parser::ParseNamespace(ParsedHeader& outHeader)
	{
		const std::vector<std::string> namespaceNames = ParseNamespaceName();

		if (!Match(ETokenKind::LBrace))
		{
			while (!IsAtEnd() && !Match(ETokenKind::Semicolon))
			{
				Advance();
			}
			return;
		}

		const size_t oldDepth = NamespaceStack.size();
		for (const std::string& ns : namespaceNames)
		{
			NamespaceStack.push_back(ns);
		}

		while (!IsAtEnd() && !Check(ETokenKind::RBrace))
		{
			ParseDeclarationInCurrentScope(outHeader);
		}

		Expect(ETokenKind::RBrace, "Expected '}' to close namespace.");
		Match(ETokenKind::Semicolon);

		NamespaceStack.resize(oldDepth);
	}

	ParsedType Parser::ParseReflectedType()
	{
		ExpectIdentifier("Expected NYX_REFLECT.");
		Expect(ETokenKind::LParen, "Expected '(' after NYX_REFLECT.");

		ParsedType parsedType{};
		parsedType.RawArguments = ParseMacroArguments();

		if (!(MatchIdentifier("struct") || MatchIdentifier("class")))
		{
			ErrorHere("Expected 'struct' or 'class' after NYX_REFLECT(...).");
		}

		parsedType.Name = ExpectIdentifier("Expected reflected type name.").Text;
		parsedType.QualifiedName = BuildQualifiedTypeName(parsedType.Name);

		while (!IsAtEnd() && !Check(ETokenKind::LBrace))
		{
			Advance();
		}

		Expect(ETokenKind::LBrace, "Expected '{' to begin reflected type body.");
		parsedType.Properties = ParseReflectedStructBody();

		Match(ETokenKind::Semicolon);
		return parsedType;
	}

	std::vector<ParsedProperty> Parser::ParseReflectedStructBody()
	{
		std::vector<ParsedProperty> properties;

		while (!IsAtEnd() && !Check(ETokenKind::RBrace))
		{
			if (CheckIdentifier("NYX_PROPERTY"))
			{
				properties.push_back(ParseProperty());
				continue;
			}

			if (Check(ETokenKind::LBrace))
			{
				SkipBalanced(ETokenKind::LBrace, ETokenKind::RBrace);
				continue;
			}

			Advance();
		}

		Expect(ETokenKind::RBrace, "Expected '}' to close reflected type body.");
		return properties;
	}

	ParsedProperty Parser::ParseProperty()
	{
		ExpectIdentifier("Expected NYX_PROPERTY.");
		Expect(ETokenKind::LParen, "Expected '(' after NYX_PROPERTY.");

		ParsedProperty property{};
		property.RawArguments = ParseMacroArguments();

		const std::vector<Token> declarationTokens = CollectDeclarationTokensUntilSemicolon();
		auto [typeName, propertyName] = ExtractTypeAndNameFromDeclarationTokens(declarationTokens);

		property.Type = typeName;
		property.Name = propertyName;
		return property;
	}

	ParsedMacroArguments Parser::ParseMacroArguments()
	{
		const std::vector<std::vector<Token>> clauses = ParseCommaSeparatedClausesUntil(ETokenKind::RParen);
		return ParseMacroArgumentsFromClauses(clauses);
	}

	ParsedMacroArguments Parser::ParseMacroArgumentsFromClauses(const std::vector<std::vector<Token>>& clauses)
	{
		ParsedMacroArguments args{};

		for (const std::vector<Token>& clause : clauses)
		{
			if (clause.empty())
			{
				continue;
			}

			if (clause[0].Kind != ETokenKind::Identifier)
			{
				throw std::runtime_error("Expected macro argument clause to begin with an identifier.");
			}

			const std::string name = clause[0].Text;

			// Bare specifier: Edit / Undo / Serialize / Component / Hidden / ReadOnly
			if (clause.size() == 1)
			{
				args.Specifiers.push_back(ParsedMacroEntry{
					.Name = name,
					.Value = std::nullopt
					});
				continue;
			}

			// meta=(...)
			if (name == "meta" &&
				clause.size() >= 4 &&
				clause[1].Kind == ETokenKind::Equals &&
				clause[2].Kind == ETokenKind::LParen &&
				clause.back().Kind == ETokenKind::RParen)
			{
				const std::vector<Token> inner(clause.begin() + 3, clause.end() - 1);
				const std::vector<std::vector<Token>> metaClauses = SplitTopLevelClauses(inner);

				for (const std::vector<Token>& metaClause : metaClauses)
				{
					if (metaClause.empty())
					{
						continue;
					}

					if (metaClause[0].Kind != ETokenKind::Identifier)
					{
						throw std::runtime_error("Expected metadata entry to begin with an identifier.");
					}

					ParsedMacroEntry entry{};
					entry.Name = metaClause[0].Text;

					if (metaClause.size() == 1)
					{
						entry.Value.reset();
					}
					else if (metaClause.size() >= 3 && metaClause[1].Kind == ETokenKind::Equals)
					{
						std::vector<Token> valueTokens(metaClause.begin() + 2, metaClause.end());
						entry.Value = ParseValueTokens(valueTokens);
					}
					else
					{
						throw std::runtime_error("Unsupported metadata clause syntax.");
					}

					args.Metadata.push_back(std::move(entry));
				}

				continue;
			}

			// Generic key=value specifier
			if (clause.size() >= 3 && clause[1].Kind == ETokenKind::Equals)
			{
				std::vector<Token> valueTokens(clause.begin() + 2, clause.end());

				args.Specifiers.push_back(ParsedMacroEntry{
					.Name = name,
					.Value = ParseValueTokens(valueTokens)
					});
				continue;
			}

			ErrorAtToken(
				clause.front(),
				"Unsupported macro argument clause syntax: " + ClauseToString(clause));
		}

		return args;
	}

	std::vector<std::vector<Token>> Parser::ParseCommaSeparatedClausesUntil(ETokenKind terminator)
	{
		std::vector<std::vector<Token>> clauses;
		std::vector<Token> currentClause;

		int parenDepth = 0;
		int braceDepth = 0;
		int bracketDepth = 0;
		int angleDepth = 0;

		while (!IsAtEnd())
		{
			if (Check(terminator) &&
				parenDepth == 0 &&
				braceDepth == 0 &&
				bracketDepth == 0 &&
				angleDepth == 0)
			{
				Advance();

				if (!currentClause.empty())
				{
					clauses.push_back(std::move(currentClause));
				}

				return clauses;
			}

			const Token token = Advance();

			if (token.Kind == ETokenKind::Comma &&
				parenDepth == 0 &&
				braceDepth == 0 &&
				bracketDepth == 0 &&
				angleDepth == 0)
			{
				if (!currentClause.empty())
				{
					clauses.push_back(std::move(currentClause));
					currentClause.clear();
				}
				continue;
			}

			switch (token.Kind)
			{
			case ETokenKind::LParen:   ++parenDepth; break;
			case ETokenKind::RParen:   --parenDepth; break;
			case ETokenKind::LBrace:   ++braceDepth; break;
			case ETokenKind::RBrace:   --braceDepth; break;
			case ETokenKind::LBracket: ++bracketDepth; break;
			case ETokenKind::RBracket: --bracketDepth; break;
			case ETokenKind::Less:     ++angleDepth; break;
			case ETokenKind::Greater:  --angleDepth; break;
			default: break;
			}

			currentClause.push_back(token);
		}

		ErrorHere("Unterminated macro argument list.");
	}

	std::vector<std::vector<Token>> Parser::SplitTopLevelClauses(const std::vector<Token>& tokens)
	{
		std::vector<std::vector<Token>> clauses;
		std::vector<Token> currentClause;

		int parenDepth = 0;
		int braceDepth = 0;
		int bracketDepth = 0;
		int angleDepth = 0;

		for (const Token& token : tokens)
		{
			if (token.Kind == ETokenKind::Comma &&
				parenDepth == 0 &&
				braceDepth == 0 &&
				bracketDepth == 0 &&
				angleDepth == 0)
			{
				if (!currentClause.empty())
				{
					clauses.push_back(std::move(currentClause));
					currentClause.clear();
				}
				continue;
			}

			switch (token.Kind)
			{
			case ETokenKind::LParen:   ++parenDepth; break;
			case ETokenKind::RParen:   --parenDepth; break;
			case ETokenKind::LBrace:   ++braceDepth; break;
			case ETokenKind::RBrace:   --braceDepth; break;
			case ETokenKind::LBracket: ++bracketDepth; break;
			case ETokenKind::RBracket: --bracketDepth; break;
			case ETokenKind::Less:     ++angleDepth; break;
			case ETokenKind::Greater:  --angleDepth; break;
			default: break;
			}

			currentClause.push_back(token);
		}

		if (!currentClause.empty())
		{
			clauses.push_back(std::move(currentClause));
		}

		return clauses;
	}

	std::vector<Token> Parser::CollectDeclarationTokensUntilSemicolon()
	{
		std::vector<Token> tokens;

		int parenDepth = 0;
		int braceDepth = 0;
		int bracketDepth = 0;
		int angleDepth = 0;

		while (!IsAtEnd())
		{
			if (Check(ETokenKind::Semicolon) &&
				parenDepth == 0 &&
				braceDepth == 0 &&
				bracketDepth == 0 &&
				angleDepth == 0)
			{
				Advance();
				return tokens;
			}

			const Token token = Advance();

			switch (token.Kind)
			{
			case ETokenKind::LParen:   ++parenDepth; break;
			case ETokenKind::RParen:   --parenDepth; break;
			case ETokenKind::LBrace:   ++braceDepth; break;
			case ETokenKind::RBrace:   --braceDepth; break;
			case ETokenKind::LBracket: ++bracketDepth; break;
			case ETokenKind::RBracket: --bracketDepth; break;
			case ETokenKind::Less:     ++angleDepth; break;
			case ETokenKind::Greater:  --angleDepth; break;
			default: break;
			}

			tokens.push_back(token);
		}

		ErrorHere("Unterminated declaration.");
	}

	void Parser::SkipBalanced(ETokenKind openKind, ETokenKind closeKind)
	{
		Expect(openKind, "Expected opening token.");

		int depth = 1;
		while (!IsAtEnd() && depth > 0)
		{
			const Token token = Advance();

			if (token.Kind == openKind)
			{
				++depth;
			}
			else if (token.Kind == closeKind)
			{
				--depth;
			}
		}
	}

	std::vector<std::string> Parser::ParseNamespaceName()
	{
		std::vector<std::string> names;
		names.push_back(ExpectIdentifier("Expected namespace identifier.").Text);

		while (Match(ETokenKind::Scope))
		{
			names.push_back(ExpectIdentifier("Expected namespace identifier after '::'.").Text);
		}

		return names;
	}

	std::string Parser::BuildQualifiedTypeName(std::string_view localName) const
	{
		if (NamespaceStack.empty())
		{
			return std::string(localName);
		}

		std::ostringstream out;
		for (size_t i = 0; i < NamespaceStack.size(); ++i)
		{
			if (i > 0)
			{
				out << "::";
			}
			out << NamespaceStack[i];
		}
		out << "::" << localName;
		return out.str();
	}

	std::string Parser::DecodeStringLiteralToken(const Token& token)
	{
		if (token.Kind != ETokenKind::StringLiteral || token.Text.size() < 2)
		{
			throw std::runtime_error("Expected string literal token.");
		}

		std::string result;
		result.reserve(token.Text.size());

		for (size_t i = 1; i + 1 < token.Text.size(); ++i)
		{
			const char c = token.Text[i];

			if (c == '\\' && i + 1 < token.Text.size() - 1)
			{
				const char next = token.Text[++i];
				switch (next)
				{
				case 'n':  result.push_back('\n'); break;
				case 'r':  result.push_back('\r'); break;
				case 't':  result.push_back('\t'); break;
				case '\\': result.push_back('\\'); break;
				case '"':  result.push_back('"'); break;
				default:   result.push_back(next); break;
				}
			}
			else
			{
				result.push_back(c);
			}
		}

		return result;
	}

	std::string Parser::NormalizeNumberText(std::string_view text)
	{
		std::string value(text);

		if (!value.empty() && (value.back() == 'f' || value.back() == 'F'))
		{
			value.pop_back();
		}

		return value;
	}

	ParsedValue Parser::ParseValueTokens(const std::vector<Token>& tokens)
	{
		if (tokens.empty())
		{
			throw std::runtime_error("Expected macro value tokens.");
		}

		// String literal
		if (tokens.size() == 1 && tokens[0].Kind == ETokenKind::StringLiteral)
		{
			ParsedValue value{};
			value.Kind = EMacroValueKind::String;
			value.Text = DecodeStringLiteralToken(tokens[0]);
			return value;
		}

		// Pure numeric literal
		if (tokens.size() == 1 &&
			(tokens[0].Kind == ETokenKind::IntegerLiteral || tokens[0].Kind == ETokenKind::FloatLiteral))
		{
			ParsedValue value{};
			value.Kind = EMacroValueKind::Number;
			value.Text = NormalizeNumberText(tokens[0].Text);
			value.Number = std::stod(value.Text);
			return value;
		}

		// Treat all remaining flattened tokens as an identifier-like symbolic value.
		// This supports things like:
		//   Degrees
		//   MyNamespace::Foo
		ParsedValue value{};
		value.Kind = EMacroValueKind::Identifier;
		value.Text = FlattenValueTokens(tokens);
		return value;
	}

	std::string Parser::FlattenValueTokens(const std::vector<Token>& tokens)
	{
		std::ostringstream out;
		for (const Token& token : tokens)
		{
			out << token.Text;
		}
		return out.str();
	}

	std::string Parser::JoinTypeTokens(const std::vector<Token>& tokens)
	{
		std::string out;

		auto needsSpaceBefore = [&](const Token& token) -> bool
			{
				if (out.empty())
				{
					return false;
				}

				const char prev = out.back();
				if (prev == ' ' || prev == ':' || prev == '<' || prev == '(' || prev == '[')
				{
					return false;
				}

				if (token.Kind == ETokenKind::Scope ||
					token.Kind == ETokenKind::Greater ||
					token.Kind == ETokenKind::Comma ||
					token.Kind == ETokenKind::RParen ||
					token.Kind == ETokenKind::RBracket)
				{
					return false;
				}

				return true;
			};

		for (const Token& token : tokens)
		{
			switch (token.Kind)
			{
			case ETokenKind::Scope:
				out += "::";
				break;

			case ETokenKind::Less:
				out += "<";
				break;

			case ETokenKind::Greater:
				out += ">";
				break;

			case ETokenKind::Comma:
				out += ", ";
				break;

			case ETokenKind::Star:
			case ETokenKind::Ampersand:
				if (!out.empty() && out.back() != ' ' && out.back() != ':' && out.back() != '<')
				{
					out += ' ';
				}
				out += token.Text;
				break;

			default:
				if (needsSpaceBefore(token))
				{
					out += ' ';
				}
				out += token.Text;
				break;
			}
		}

		return out;
	}

	std::pair<std::string, std::string> Parser::ExtractTypeAndNameFromDeclarationTokens(
		const std::vector<Token>& declarationTokens)
	{
		if (declarationTokens.empty())
		{
			throw std::runtime_error("Property declaration is empty.");
		}

		size_t endIndex = declarationTokens.size();
		for (size_t i = 0; i < declarationTokens.size(); ++i)
		{
			if (declarationTokens[i].Kind == ETokenKind::Equals ||
				declarationTokens[i].Kind == ETokenKind::LBrace)
			{
				endIndex = i;
				break;
			}
		}

		if (endIndex == 0)
		{
			throw std::runtime_error("Could not parse reflected property declaration.");
		}

		size_t nameIndex = std::string::npos;
		for (size_t i = endIndex; i-- > 0;)
		{
			if (declarationTokens[i].Kind == ETokenKind::Identifier)
			{
				nameIndex = i;
				break;
			}
		}

		if (nameIndex == std::string::npos || nameIndex == 0)
		{
			throw std::runtime_error("Could not determine reflected property type/name.");
		}

		const std::string propertyName = declarationTokens[nameIndex].Text;

		std::vector<Token> typeTokens;
		typeTokens.reserve(nameIndex);
		for (size_t i = 0; i < nameIndex; ++i)
		{
			typeTokens.push_back(declarationTokens[i]);
		}

		const std::string propertyType = JoinTypeTokens(typeTokens);
		return { propertyType, propertyName };
	}

	[[noreturn]] void Parser::ErrorHere(std::string_view message) const
	{
		const Token& token = Peek();

		std::ostringstream out;

		if (!SourceFilePath.empty())
		{
			out << SourceFilePath << "(" << token.Line << "," << token.Column << "): ";
		}
		else
		{
			out << token.Line << ":" << token.Column << ": ";
		}

		out << message;

		if (!token.Text.empty())
		{
			out << " near token '" << token.Text << "'";
		}

		throw std::runtime_error(out.str());
	}

	[[noreturn]] void Parser::ErrorAtToken(const Token& token, std::string_view message) const
	{
		std::ostringstream out;

		if (!SourceFilePath.empty())
		{
			out << SourceFilePath << "(" << token.Line << "," << token.Column << "): ";
		}
		else
		{
			out << token.Line << ":" << token.Column << ": ";
		}

		out << message;

		if (!token.Text.empty())
		{
			out << " near token '" << token.Text << "'";
		}

		throw std::runtime_error(out.str());
	}
}