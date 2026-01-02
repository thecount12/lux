#include "lux.h"
#include "common.h"
#include "scanner.h"
#include "compiler.h"
#include "scanner.h"

#ifdef DEBUG_PRINT_CODE
#include "debug.h"
#endif

typedef struct Parser Parser;
struct Parser {
	Token	current;
	Token	previous;
	int	hadError;
	int	panicMode;
};

enum Precedence {
	PREC_NONE,
	PREC_ASSIGNMENT, // =
	PREC_OR,		 // or
	PREC_AND,		 // and
	PREC_EQUALITY,	 // == !=
	PREC_COMPARISON, // < > <= >=
	PREC_TERM,		 // + -
	PREC_FACTOR,	 // * /
	PREC_UNARY,		 // ! -
	PREC_CALL,		 // . ()
	PREC_PRIMARY
};

typedef enum Precedence Precedence;

typedef void (*ParseFn)(void);

typedef struct ParseRule ParseRule;
struct ParseRule {
	ParseFn		prefix;
	ParseFn		infix;
	Precedence	precedence;
};

Parser parser;
Chunk* compilingChunk;

static Chunk*
currentChunk(void)
{
	return compilingChunk;
}

static void
errorAt(Token* token, char* message)
{
	if(parser.panicMode)
		return;
	parser.panicMode = 1;


	print("[line %d] Error", token->line);

	if(token->type == TOKEN_EOF){
		print(" at end");
	}else if(token->type == TOKEN_ERROR){
		/* No additional output for scan errors */
	}else{
		print(" at '%.*s'", token->length, token->start);
	}

	print(": %s\n", message);
	parser.hadError = 1;
}

static void
error(char* message)
{
	errorAt(&parser.previous, message);
}

static void
errorAtCurrent(char* message)
{
	errorAt(&parser.current, message);
}

static void
advance(void)
{
	parser.previous = parser.current;

	for(;;){
		parser.current = scanToken();
		if(parser.current.type != TOKEN_ERROR)
			break;

		errorAtCurrent(parser.current.start);
	}
}

static void
consume(TokenType type, char* message)
{
	if(parser.current.type == type){
		advance();
		return;
	}

	errorAtCurrent(message);
}

static void
emitByte(uchar byte)
{
	writeChunk(currentChunk(), byte, parser.previous.line);
}

static void
emitBytes(uchar byte1, uchar byte2)
{
	emitByte(byte1);
	emitByte(byte2);
}

static void
emitReturn(void)
{
	emitByte(OP_RETURN);
}

static uchar
makeConstant(Value value)
{
	int constant = addConstant(currentChunk(), value);
	if(constant > 255){
		error("Too many constants in one chunk.");
		return 0;
	}

	return (uchar)constant;
}

static void
emitConstant(Value value)
{
	emitBytes(OP_CONSTANT, makeConstant(value));
}

static void
endCompiler(void)
{
	emitReturn();
#ifdef DEBUG_PRINT_CODE
	if(!parser.hadError){
		disassembleChunk(currentChunk(), "code");
	}
#endif
}

static void expression(void);
static ParseRule* getRule(TokenType type);
static void parsePrecedence(Precedence precedence);

static void
binary(void)
{
	TokenType operatorType = parser.previous.type;
	ParseRule* rule = getRule(operatorType);
	parsePrecedence((Precedence)(rule->precedence + 1));
	
	switch(operatorType){
	case TOKEN_PLUS:	emitByte(OP_ADD); break;
	case TOKEN_MINUS:	emitByte(OP_SUBTRACT); break;
	case TOKEN_STAR:	emitByte(OP_MULTIPLY); break;
	case TOKEN_SLASH:	emitByte(OP_DIVIDE); break;
	default:
		return; /* Unreachable */
	}
}

static void
grouping(void)
{
	expression();
	consume(TOKEN_RIGHT_PAREN, "expect ')' after expression.");
}

static void
number(void)
{
	/* libc provides strtod; nil is the Plan 9 null pointer */
	double value = strtod(parser.previous.start, nil);
	emitConstant(value);
}

static void
unary(void)
{
	TokenType operatorType = parser.previous.type;

	/* Compile the operand */
	parsePrecedence(PREC_UNARY);

	/* Emit the operator instruction */
	switch(operatorType){
	case TOKEN_MINUS:	emitByte(OP_NEGATE); break;
	default:		return; /* Unreachable */
	}
}

/* Plan 9 C99-ish style table */
ParseRule rules[] = {
	[TOKEN_LEFT_PAREN] 		= {grouping, nil, PREC_NONE},
	[TOKEN_RIGHT_PAREN] 	= {nil, nil, PREC_NONE},
	[TOKEN_LEFT_BRACE] 		= {nil, nil, PREC_NONE},
	[TOKEN_RIGHT_BRACE] 	= {nil, nil, PREC_NONE},
	[TOKEN_COMMA] 			= {nil, nil, PREC_NONE},
	[TOKEN_DOT] 			= {nil, nil, PREC_NONE},
	[TOKEN_MINUS] 			= {unary, binary, PREC_TERM},
	[TOKEN_PLUS] 			= {nil, binary, PREC_TERM},
	[TOKEN_SEMICOLON] 		= {nil, nil, PREC_NONE},
	[TOKEN_SLASH] 			= {nil, binary, PREC_FACTOR},
	[TOKEN_STAR] 			= {nil, binary, PREC_FACTOR},
	[TOKEN_BANG] 			= {grouping, nil, PREC_NONE},
	[TOKEN_BANG_EQUAL] 		= {nil, nil, PREC_NONE},
	[TOKEN_EQUAL] 			= {nil, nil, PREC_NONE},
	[TOKEN_EQUAL_EQUAL] 	= {nil, nil, PREC_NONE},
	[TOKEN_GREATER] 		= {nil, nil, PREC_NONE},
	[TOKEN_GREATER_EQUAL] 	= {nil, nil, PREC_NONE},
	[TOKEN_LESS] 			= {nil, nil, PREC_NONE},
	[TOKEN_LESS_EQUAL] 		= {nil, nil, PREC_NONE},
	[TOKEN_IDENTIFIER] 		= {nil, nil, PREC_NONE},
	[TOKEN_STRING] 			= {nil, nil, PREC_NONE},
	[TOKEN_NUMBER] 			= {number, nil, PREC_NONE},
	[TOKEN_AND] 			= {nil, nil, PREC_TERM},
	[TOKEN_CLASS] 			= {nil, nil, PREC_TERM},
	[TOKEN_ELSE] 			= {nil, nil, PREC_NONE},
	[TOKEN_FALSE] 			= {nil, nil, PREC_NONE},
	[TOKEN_FOR] 			= {nil, nil, PREC_NONE},
	[TOKEN_FUN] 			= {nil, nil, PREC_NONE},
	[TOKEN_IF] 				= {nil, nil, PREC_NONE},
	[TOKEN_NIL] 			= {nil, nil, PREC_NONE},
	[TOKEN_OR] 				= {nil, nil, PREC_NONE},
	[TOKEN_PRINT] 			= {nil, nil, PREC_NONE},
	[TOKEN_RETURN] 			= {nil, nil, PREC_NONE},
	[TOKEN_SUPER] 			= {nil, nil, PREC_NONE},
	[TOKEN_THIS] 			= {nil, nil, PREC_NONE},
	[TOKEN_TRUE] 			= {nil, nil, PREC_NONE},
	[TOKEN_VAR] 			= {nil, nil, PREC_NONE},
	[TOKEN_WHILE] 			= {nil, nil, PREC_NONE},
	[TOKEN_ERROR] 			= {nil, nil, PREC_NONE},
	[TOKEN_EOF] 			= {nil, nil, PREC_NONE},
};

static void
parsePrecedence(Precedence precedence)
{
	ParseFn prefixRule;
	ParseFn infixRule;

	advance();
	prefixRule = getRule(parser.previous.type)->prefix;
	if(prefixRule == nil){
		error("Expect expression.");
		return;
	}

	prefixRule();

	while(precedence <= getRule(parser.current.type)->precedence){
		advance();
		infixRule = getRule(parser.previous.type)->infix;
		if(infixRule != nil)
			infixRule();
	}
}

static ParseRule*
getRule(TokenType type)
{
	return &rules[type];
}

static void
expression(void)
{
	parsePrecedence(PREC_ASSIGNMENT);
}

int
compile(char* source, Chunk* chunk)
{
	initScanner(source);
	compilingChunk = chunk;

	parser.hadError = 0;
	parser.panicMode = 0;

	advance();
	expression();
	consume(TOKEN_EOF, "Expected end of expression.");
	endCompiler();
	return !parser.hadError;
}
