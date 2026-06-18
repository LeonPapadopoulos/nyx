#pragma once

#include "ReflectionAst.h"
#include "Token.h"

#include <string>
#include <string_view>
#include <vector>

namespace Nyx::HeaderTool
{
	class Parser
	{
	public:
		explicit Parser(std::vector<Token> tokens);

		ParsedHeader ParseHeader();

	private:
		bool IsAtEnd() const;
		const Token& Peek(size_t lookahead = 0) const;
		const Token& Previous() const;
		const Token& Advance();

		bool Check(ETokenKind kind) const;
		bool Match(ETokenKind kind);

		bool CheckIdentifier(std::string_view text) const;
		bool MatchIdentifier(std::string_view text);

		const Token& Expect(ETokenKind kind, std::string_view message);
		const Token& ExpectIdentifier(std::string_view message);

		void ParseDeclarationInCurrentScope(ParsedHeader& outHeader);
		void ParseNamespace(ParsedHeader& outHeader);
		ParsedType ParseReflectedType();

		std::vector<ParsedProperty> ParseReflectedStructBody();
		ParsedProperty ParseProperty();

		std::vector<std::vector<Token>> ParseCommaSeparatedClausesUntil(ETokenKind terminator);
		std::vector<Token> CollectDeclarationTokensUntilSemicolon();

		void SkipBalanced(ETokenKind openKind, ETokenKind closeKind);

		std::vector<std::string> ParseNamespaceName();
		std::string BuildQualifiedTypeName(std::string_view localName) const;

		static std::string DecodeStringLiteralToken(const Token& token);
		static std::string NormalizeNumberLiteral(std::string_view text);
		static std::string JoinTypeTokens(const std::vector<Token>& tokens);

		static std::pair<std::string, std::string> ExtractTypeAndNameFromDeclarationTokens(
			const std::vector<Token>& declarationTokens);

		static std::string MapTypeToKind(const std::string& typeName);

		static void ApplyReflectArgs(const std::vector<std::vector<Token>>& clauses, ParsedType& parsedType);
		static void ApplyPropertyArgs(const std::vector<std::vector<Token>>& clauses, ParsedProperty& parsedProperty);

		[[noreturn]] void ErrorHere(std::string_view message) const;

	private:
		std::vector<Token> Tokens;
		size_t Current = 0;
		std::vector<std::string> NamespaceStack;
	};
}