#include "lux.h"
#include "common.h"
#include "value.h"
#include "chunk.h"
#include "vm.h"
#include "memory.h"
#include "object.h"
#include "scanner.h"

// prototypes
static struct TokenTag makeToken(enum TTypeTag type);
static struct TokenTag errorToken(char* message);

typedef struct Scanner_s Scanner;
struct Scanner_s {
	char*	start;
	char*	current;
	int	line;
};

Scanner scanner;

void
initScanner(char* source)
{
	scanner.start = source;
	scanner.current  = source;
	scanner.line = 1;
}

static int
isAlpha(char c)
{
	return 	(c >= 'a' && c <= 'z') ||
			(c >= 'A' && c <= 'Z') ||
			c == '_';
}

static int
isDigit(char c)
{
	return c >= '0' && c <= '9';
}

static int
isAtEnd(void)
{
	return *scanner.current == '\0';
}

static char
advance(void)
{
	scanner.current++;
	return scanner.current[-1];
}

static char
peek(void)
{
	return *scanner.current;
}

static char
peekNext(void)
{
	if (isAtEnd()) return '\0';
	return scanner.current[1];
}

static bool
match(char expected)
{
	if (isAtEnd()) return false;
	if (*scanner.current != expected) return false;
	scanner.current++;
	return true;
}

static struct TokenTag
makeToken(enum TTypeTag type)
{
	struct TokenTag token;

	token.type = type;
	token.start = scanner.start;
	token.length = (int)(scanner.current - scanner.start);
	token.line = scanner.line;
	return token;
}

static struct TokenTag
errorToken(char* message)
{
	struct TokenTag token;

	token.type = TOKEN_ERROR;
	token.start = message;
	token.length = (int)strlen(message);
	token.line = scanner.line;
	return token;
}

static void
skipWhitespace(void)
{
	char c;

	for (;;) {
		c = peek();
		switch (c) {
		case ' ':
		case '\r':
		case '\t':
			advance();
			break;
		case '\n':
			scanner.line++;
			advance();
			break;
		case '/':
			if (peekNext() == '/') {
				while (peek() != '\n' && !isAtEnd())
					advance();
			} else {
				return;
			}
			break;
		default:
			return;
		}
	}
}

static TokenType
checkKeyword(int start, int length, char* rest, TokenType type)
{
	if (scanner.current - scanner.start == start + length &&
		memcmp(scanner.start + start, rest, length) == 0) {
		return type;
	}
	return TOKEN_IDENTIFIER;
}

static TokenType
identifierType(void)
{
	switch (scanner.start[0]) {
	case 'a': return checkKeyword(1, 2, "nd", TOKEN_AND);
	case 'c': return checkKeyword(1, 4, "lass", TOKEN_CLASS);
	case 'e': return checkKeyword(1, 3, "lse", TOKEN_ELSE);
	case 'f':
		if (scanner.current - scanner.start > 1) {
			switch (scanner.start[1]) {
			case 'a': return checkKeyword(1, 4, "alse", TOKEN_FALSE);
			case 'o': return checkKeyword(1, 2, "or", TOKEN_FOR);
			case 'u': return checkKeyword(1, 2, "un", TOKEN_FUN);
			}
		}
		break;
	case 'i': return checkKeyword(1, 1, "f", TOKEN_IF);
	case 'n': return checkKeyword(1, 2, "il", TOKEN_NIL);
	case 'o': return checkKeyword(1, 1, "r", TOKEN_OR);
	case 'p': return checkKeyword(1, 4, "rint", TOKEN_PRINT);
	case 'r': return checkKeyword(1, 5, "eturn", TOKEN_RETURN);
	case 's': return checkKeyword(1, 4, "uper", TOKEN_SUPER);
	case 't':
		if (scanner.current - scanner.start > 1) {
			switch (scanner.start[1]) {
			case 'h': return checkKeyword(2, 2, "is", TOKEN_THIS);
			case 'r': return checkKeyword(2, 2, "ue", TOKEN_TRUE);
			}
		}
		break;
	case 'v': return checkKeyword(1, 2, "ar", TOKEN_VAR);
	case 'w': return checkKeyword(1, 4, "hile", TOKEN_WHILE);
	}

	return TOKEN_IDENTIFIER;
}

static Token
identifier(void)
{
	while (isAlpha(peek()) || isDigit(peek()))
		advance();
	return makeToken(identifierType());
}

static Token
number(void)
{
	while (isDigit(peek()))
		advance();

	if (peek() == '.' && isDigit(peekNext())) {
		advance();
		while (isDigit(peek()))
			advance();
	}
	return makeToken(TOKEN_NUMBER);
}

static Token
string(void)
{
	while (peek() != '"' && !isAtEnd()) {
		if (peek() == '\n')
			scanner.line++;
		advance();
	}

	if (isAtEnd())
		return errorToken("Unterminated string.");

	advance();
	return makeToken(TOKEN_STRING);
}

Token
scanToken(void)
{
	char c;

	skipWhitespace();
	scanner.start = scanner.current;
	
	if (isAtEnd())
		return makeToken(TOKEN_EOF);
	
	c = advance();
	if(isAlpha(c)) return identifier();
	if (isDigit(c)) return number();

	switch (c) {
	case '(': return makeToken(TOKEN_LEFT_PAREN);
	case ')': return makeToken(TOKEN_RIGHT_PAREN);
	case '{': return makeToken(TOKEN_LEFT_BRACE);
	case '}': return makeToken(TOKEN_RIGHT_BRACE);
	case '[': return makeToken(TOKEN_LEFT_BRACKET);
	case ']': return makeToken(TOKEN_RIGHT_BRACKET);
	case ';': return makeToken(TOKEN_SEMICOLON);
	case ',': return makeToken(TOKEN_COMMA);
	case '.': return makeToken(TOKEN_DOT);
	case '-': return makeToken(TOKEN_MINUS);
	case '+': return makeToken(TOKEN_PLUS);
	case '/': return makeToken(TOKEN_SLASH);
	case '*': return makeToken(TOKEN_STAR);
	case '!':
		return makeToken(match('=') ? TOKEN_BANG_EQUAL : TOKEN_BANG);
	case '=':
		return makeToken(match('=') ? TOKEN_EQUAL_EQUAL : TOKEN_EQUAL);
	case '<':
		return makeToken(match('=') ? TOKEN_LESS_EQUAL : TOKEN_LESS);
	case '>':
		return makeToken(match('=') ? TOKEN_GREATER_EQUAL : TOKEN_GREATER);
	case '"': return string();
	}

	return errorToken("Unexpected character.");
}
