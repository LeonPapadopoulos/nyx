#include "Lexer.h"

#include <cctype>

namespace Nyx::HeaderTool
{
	Lexer::Lexer(std::string_view sourceText)
		: Source(sourceText)
	{
	}

	std::vector<Token> Lexer::LexAll()
	{
		std::vector<Token> tokens;

		while (true)
		{
			SkipWhitespaceAndComments();

			if (IsAtEnd())
			{
				tokens.push_back(MakeToken(ETokenKind::EndOfFile, "", Line, Column));
				break;
			}

			const char c = Peek();

			if (IsIdentifierStart(c))
			{
				tokens.push_back(LexIdentifier());
				continue;
			}

			if (std::isdigit(static_cast<unsigned char>(c)))
			{
				tokens.push_back(LexNumber());
				continue;
			}

			if (c == '"')
			{
				tokens.push_back(LexStringLiteral());
				continue;
			}

			tokens.push_back(LexPunctuation());
		}

		return tokens;
	}

	bool Lexer::IsAtEnd() const
	{
		return Index >= Source.size();
	}

	char Lexer::Peek(size_t lookahead) const
	{
		const size_t target = Index + lookahead;
		if (target >= Source.size())
		{
			return '\0';
		}
		return Source[target];
	}

	char Lexer::Advance()
	{
		if (IsAtEnd())
		{
			return '\0';
		}

		const char c = Source[Index++];
		if (c == '\n')
		{
			++Line;
			Column = 1;
		}
		else
		{
			++Column;
		}

		return c;
	}

	bool Lexer::Match(char expected)
	{
		if (IsAtEnd() || Peek() != expected)
		{
			return false;
		}

		Advance();
		return true;
	}

	void Lexer::SkipWhitespaceAndComments()
	{
		while (!IsAtEnd())
		{
			const char c = Peek();

			if (std::isspace(static_cast<unsigned char>(c)))
			{
				Advance();
				continue;
			}

			if (c == '/' && Peek(1) == '/')
			{
				Advance();
				Advance();

				while (!IsAtEnd() && Peek() != '\n')
				{
					Advance();
				}

				continue;
			}

			if (c == '/' && Peek(1) == '*')
			{
				Advance();
				Advance();

				while (!IsAtEnd())
				{
					if (Peek() == '*' && Peek(1) == '/')
					{
						Advance();
						Advance();
						break;
					}

					Advance();
				}

				continue;
			}

			break;
		}
	}

	Token Lexer::MakeToken(ETokenKind kind, std::string text, int line, int column) const
	{
		Token token{};
		token.Kind = kind;
		token.Text = std::move(text);
		token.Line = line;
		token.Column = column;
		return token;
	}

	Token Lexer::LexIdentifier()
	{
		const int startLine = Line;
		const int startColumn = Column;
		const size_t startIndex = Index;

		Advance();

		while (!IsAtEnd() && IsIdentifierPart(Peek()))
		{
			Advance();
		}

		return MakeToken(
			ETokenKind::Identifier,
			std::string(Source.substr(startIndex, Index - startIndex)),
			startLine,
			startColumn
		);
	}

	Token Lexer::LexNumber()
	{
		const int startLine = Line;
		const int startColumn = Column;
		const size_t startIndex = Index;

		bool bSawDot = false;

		while (!IsAtEnd())
		{
			const char c = Peek();

			if (std::isdigit(static_cast<unsigned char>(c)))
			{
				Advance();
				continue;
			}

			if (c == '.' && !bSawDot)
			{
				bSawDot = true;
				Advance();
				continue;
			}

			if ((c == 'f' || c == 'F') && bSawDot)
			{
				Advance();
			}

			break;
		}

		return MakeToken(
			bSawDot ? ETokenKind::FloatLiteral : ETokenKind::IntegerLiteral,
			std::string(Source.substr(startIndex, Index - startIndex)),
			startLine,
			startColumn
		);
	}

	Token Lexer::LexStringLiteral()
	{
		const int startLine = Line;
		const int startColumn = Column;
		const size_t startIndex = Index;

		Advance(); // opening quote

		bool bEscape = false;
		while (!IsAtEnd())
		{
			const char c = Advance();

			if (bEscape)
			{
				bEscape = false;
				continue;
			}

			if (c == '\\')
			{
				bEscape = true;
				continue;
			}

			if (c == '"')
			{
				break;
			}
		}

		return MakeToken(
			ETokenKind::StringLiteral,
			std::string(Source.substr(startIndex, Index - startIndex)),
			startLine,
			startColumn
		);
	}

	Token Lexer::LexPunctuation()
	{
		const int startLine = Line;
		const int startColumn = Column;

		const char c = Advance();

		switch (c)
		{
		case '(': return MakeToken(ETokenKind::LParen, "(", startLine, startColumn);
		case ')': return MakeToken(ETokenKind::RParen, ")", startLine, startColumn);
		case '{': return MakeToken(ETokenKind::LBrace, "{", startLine, startColumn);
		case '}': return MakeToken(ETokenKind::RBrace, "}", startLine, startColumn);
		case '[': return MakeToken(ETokenKind::LBracket, "[", startLine, startColumn);
		case ']': return MakeToken(ETokenKind::RBracket, "]", startLine, startColumn);
		case ',': return MakeToken(ETokenKind::Comma, ",", startLine, startColumn);
		case ';': return MakeToken(ETokenKind::Semicolon, ";", startLine, startColumn);
		case '.': return MakeToken(ETokenKind::Dot, ".", startLine, startColumn);
		case '*': return MakeToken(ETokenKind::Star, "*", startLine, startColumn);
		case '&': return MakeToken(ETokenKind::Ampersand, "&", startLine, startColumn);
		case '=': return MakeToken(ETokenKind::Equals, "=", startLine, startColumn);
		case '<': return MakeToken(ETokenKind::Less, "<", startLine, startColumn);
		case '>': return MakeToken(ETokenKind::Greater, ">", startLine, startColumn);
		case '+': return MakeToken(ETokenKind::Plus, "+", startLine, startColumn);
		case '-': return MakeToken(ETokenKind::Minus, "-", startLine, startColumn);
		case '/': return MakeToken(ETokenKind::Slash, "/", startLine, startColumn);
		case '%': return MakeToken(ETokenKind::Percent, "%", startLine, startColumn);
		case '#': return MakeToken(ETokenKind::Hash, "#", startLine, startColumn);
		case '?': return MakeToken(ETokenKind::Question, "?", startLine, startColumn);
		case '!': return MakeToken(ETokenKind::Exclamation, "!", startLine, startColumn);

		case ':':
			if (Match(':'))
			{
				return MakeToken(ETokenKind::Scope, "::", startLine, startColumn);
			}
			return MakeToken(ETokenKind::Colon, ":", startLine, startColumn);

		default:
			return MakeToken(ETokenKind::Unknown, std::string(1, c), startLine, startColumn);
		}
	}

	bool Lexer::IsIdentifierStart(char c)
	{
		return std::isalpha(static_cast<unsigned char>(c)) || c == '_';
	}

	bool Lexer::IsIdentifierPart(char c)
	{
		return std::isalnum(static_cast<unsigned char>(c)) || c == '_';
	}
}