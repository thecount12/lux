#ifndef lux_scanner_h
#define lux_scanner_h

/* 1. Fully define the enum first */
enum TTypeTag {
	TOKEN_LEFT_PAREN, TOKEN_RIGHT_PAREN,
	TOKEN_LEFT_BRACE, TOKEN_RIGHT_BRACE,
	TOKEN_COMMA, TOKEN_DOT, TOKEN_MINUS, TOKEN_PLUS,
	TOKEN_SEMICOLON, TOKEN_SLASH, TOKEN_STAR,
	TOKEN_BANG, TOKEN_BANG_EQUAL,
	TOKEN_EQUAL, TOKEN_EQUAL_EQUAL,
	TOKEN_GREATER, TOKEN_GREATER_EQUAL,
	TOKEN_LESS, TOKEN_LESS_EQUAL,
	TOKEN_IDENTIFIER, TOKEN_STRING, TOKEN_NUMBER,
	TOKEN_AND, TOKEN_CLASS, TOKEN_ELSE, TOKEN_FALSE,
	TOKEN_FOR, TOKEN_FUN, TOKEN_IF, TOKEN_NIL, TOKEN_OR,
	TOKEN_PRINT, TOKEN_RETURN, TOKEN_SUPER, TOKEN_THIS,
	TOKEN_TRUE, TOKEN_VAR, TOKEN_WHILE,
	TOKEN_ERROR, TOKEN_EOF  /* NO trailing comma here */
};
typedef enum TTypeTag TokenType;

/* 2. Fully define the struct BEFORE any typedef or usage */
struct TokenTag {
	TokenType type;
	char* start;
	int length;
	int line;
}; /* Ensure this semicolon exists */

/* 3. Create the alias name */
typedef struct TokenTag Token;

/* 4. Prototypes using the finalized names */
void	initScanner(char* source);
Token	scanToken(void);

#endif
