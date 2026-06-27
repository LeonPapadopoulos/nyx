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
		explicit Parser(std::vector<Token> tokens, std::string sourceFilePath);

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

		ParsedMacroArguments ParseMacroArguments();
		ParsedMacroArguments ParseMacroArgumentsFromClauses(const std::vector<std::vector<Token>>& clauses);

		std::vector<std::vector<Token>> ParseCommaSeparatedClausesUntil(ETokenKind terminator);
		static std::vector<std::vector<Token>> SplitTopLevelClauses(const std::vector<Token>& tokens);

		std::vector<Token> CollectDeclarationTokensUntilSemicolon();

		void SkipBalanced(ETokenKind openKind, ETokenKind closeKind);

		std::vector<std::string> ParseNamespaceName();
		std::string BuildQualifiedTypeName(std::string_view localName) const;

		static std::string DecodeStringLiteralToken(const Token& token);
		static ParsedValue ParseValueTokens(const std::vector<Token>& tokens);
		static std::string NormalizeNumberText(std::string_view text);
		static std::string FlattenValueTokens(const std::vector<Token>& tokens);
		static std::string JoinTypeTokens(const std::vector<Token>& tokens);

		static std::pair<std::string, std::string> ExtractTypeAndNameFromDeclarationTokens(
			const std::vector<Token>& declarationTokens);

		[[noreturn]] void ErrorHere(std::string_view message) const;
		[[noreturn]] void ErrorAtToken(const Token& token, std::string_view message) const;

	private:
		std::vector<Token> Tokens;
		size_t Current = 0;
		std::vector<std::string> NamespaceStack;
		std::string SourceFilePath;
	};
}