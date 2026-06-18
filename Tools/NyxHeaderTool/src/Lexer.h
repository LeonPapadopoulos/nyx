#pragma once

#include "Token.h"

#include <string_view>
#include <vector>

namespace Nyx::HeaderTool
{
	class Lexer
	{
	public:
		explicit Lexer(std::string_view sourceText);

		std::vector<Token> LexAll();

	private:
		bool IsAtEnd() const;
		char Peek(size_t lookahead = 0) const;
		char Advance();
		bool Match(char expected);

		void SkipWhitespaceAndComments();

		Token MakeToken(ETokenKind kind, std::string text, int line, int column) const;
		Token LexIdentifier();
		Token LexNumber();
		Token LexStringLiteral();
		Token LexPunctuation();

		static bool IsIdentifierStart(char c);
		static bool IsIdentifierPart(char c);

	private:
		std::string_view Source;
		size_t Index = 0;
		int Line = 1;
		int Column = 1;
	};
}