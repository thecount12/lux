#include "lux.h"
#include "types.h"
#include "common.h"
#include "value.h"
#include "chunk.h"
#include "vm.h"
#include "memory.h"
#include "object.h"
#include "compiler.h"
#include "scanner.h"

#define UINT16_MAX 0xffff

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

/* Using explicit enum tags for Plan 9 compatibility */
enum PrecedenceTag {
	PREC_NONE,
	PREC_ASSIGNMENT,
	PREC_OR,
	PREC_AND,
	PREC_EQUALITY,
	PREC_COMPARISON,
	PREC_TERM,
	PREC_FACTOR,
	PREC_UNARY,
	PREC_CALL,
	PREC_PRIMARY
};

typedef enum PrecedenceTag Precedence;

typedef void (*ParseFn)(bool canAssign);

typedef struct ParseRule ParseRule;
struct ParseRule {
	ParseFn		prefix;
	ParseFn		infix;
	Precedence	precedence;
};

typedef struct {
	Token name;
	int depth;
	bool isCaptured;
} Local;

typedef struct {
	unsigned index;
	bool isLocal;
} Upvalue;

typedef enum {
	TYPE_FUNCTION,
	TYPE_INITIALIZER,
	TYPE_METHOD,
	TYPE_SCRIPT
} FunctionType;

struct Compiler {
	struct Compiler* enclosing;
	ObjFunction* function;
	FunctionType type;
	Local locals[UINT8_COUNT];
	int localCount;
	Upvalue upvalues[UINT8_COUNT];
	int scopeDepth;
};

typedef struct ClassCompiler ClassCompiler;
struct ClassCompiler {
	struct ClassCompiler* enclosing;
};

Parser parser;
Compiler* current = nil;
ClassCompiler* currentClass = nil;

static Chunk* 
currentChunk(void)
{
	return &current->function->chunk;
}

static void declareVariable(void);
static void markInitialized(void);
static void beginScope(void);
static void block(void);
static void endScope(void);
static int resolveLocal(Compiler* compiler, Token* name);
static void forStatement(void);
static void whileStatement(void);
static void ifStatement(void);
static void expression(void);
static void statement(void);
static void varDeclaration(void);
static void expressionStatement(void);
static void parsePrecedence(Precedence precedence);
static unsigned int argumentList(void);
static int resolveUpvalue(Compiler* compiler, Token* name);
static void classDeclaration(void);
static void method(void);

static void
errorAt(Token* token, char* message)
{
	if(parser.panicMode)
		return;
	parser.panicMode = 1;

	/* Plan 9 print matches the libc.h definition */
	print("[line %d] Error", token->line);

	if(token->type == TOKEN_EOF){
		print(" at end");
	}else if(token->type == TOKEN_ERROR){
		/* No additional output */
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

static bool 
check(TokenType type) 
{
	return parser.current.type == type;
}

static bool 
match(TokenType type) 
{
	if (!check(type)) return false;
	advance();
	return true;
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
emitLoop(int loopStart)
{
	emitByte(OP_LOOP);

	int offset = currentChunk()->count - loopStart + 2;
	if (offset > UINT16_MAX) error("Loop body too large.");

	emitByte((offset >> 8) & 0xff);
	emitByte(offset & 0xff);
}

static int
emitJump(unsigned int instruction) // uint8_tunsigned char maybe
{
	emitByte(instruction);
	emitByte(0xff);
	emitByte(0xff);
	return currentChunk()->count - 2;
}

static void
emitReturn(void)
{
	if (current->type == TYPE_INITIALIZER) {
		emitBytes(OP_GET_LOCAL, 0);
	} else {
		emitByte(OP_NIL);
	}

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
patchJump(int offset)
{
	int jump = currentChunk()->count - offset - 2;
	
	if (jump > UINT16_MAX) {
		error("Too much code to jump over.");
	}

	currentChunk()->code[offset] = (jump >> 8) & 0xff;
	currentChunk()->code[offset + 1] = jump & 0xff;
}

static void
initCompiler(Compiler* compiler, FunctionType type)
{
	compiler->enclosing = current;
	compiler->function = nil;
	compiler->type = type;
	compiler->localCount = 0;
	compiler->scopeDepth = 0;
	compiler->function = newFunction();
	current = compiler;
	if (type != TYPE_SCRIPT) {
		current->function->name = copyString(parser.previous.start,
											 parser.previous.length);
	}


	Local* local = &current->locals[current->localCount++];
	local->depth = 0;
	local->isCaptured = false;
	if (type != TYPE_FUNCTION) {
		local->name.start = "this";
		local->name.length = 4;
	} else {
		local->name.start = "";
		local->name.length = 0;
	}
}

static ObjFunction*
endCompiler(void)
{
	emitReturn();
	ObjFunction* function = current->function;

#ifdef DEBUG_PRINT_CODE
	if(!parser.hadError){
		disassembleChunk(currentChunk(), function->name != nil
			? function->name->chars : "<script>");
	}
#endif
	current = current->enclosing;
	return function;
}

static void
beginScope()
{
	current->scopeDepth++;
}

static void
endScope()
{
	current->scopeDepth--;

	while (current->localCount > 0 &&
		current->locals[current->localCount -1].depth >
			current->scopeDepth) {
		if (current->locals[current->localCount - 1].isCaptured) {
			emitByte(OP_CLOSE_UPVALUE);
		} else {
			emitByte(OP_POP);
		}
		current->localCount--;
	}
}

static void expression(void);
static void statement(void);
static void declaration(void);
static ParseRule* getRule(TokenType type);
static void parsePrecedence(Precedence precedence);

static unsigned long 
identifierConstant(Token* name)
{
	return makeConstant(OBJ_VAL(copyString(name->start, name-> length)));
}

static bool
identifiersEqual(Token* a, Token* b)
{
	if (a->length != b->length) return false;
	return memcmp(a->start, b->start, a->length) == 0;
}

static int
resolveLocal(Compiler* compiler, Token* name)
{
	for (int i = compiler->localCount -1; i >= 0; i--) {
		Local* local = &compiler->locals[i];
		if (identifiersEqual(name, &local->name)) {
			if (local->depth == -1) {
				error("Can't read local variable in its own initializer.");
			}
			return i;
		}
	}
	return -1;
}

static int 
addUpvalue(Compiler* compiler, unsigned index, bool isLocal) 
{
	int upvalueCount = compiler->function->upvalueCount;

	for (int i = 0; i < upvalueCount; i++) {
		Upvalue* upvalue = &compiler->upvalues[i];
		if (upvalue->index == index && upvalue->isLocal) {
			return i;
		}
	}

	if (upvalueCount == UINT8_COUNT) {
		error("Too many closure variables in function.");
		return 0;
	}

	compiler->upvalues[upvalueCount].isLocal = isLocal;
	compiler->upvalues[upvalueCount].index = index;
	return compiler->function->upvalueCount++;
}

static int 
resolveUpvalue(Compiler* compiler, Token* name) 
{
	if (compiler->enclosing == nil) return -1;

	int local = resolveLocal(compiler->enclosing, name);
	if (local != -1) {
		compiler->enclosing->locals[local].isCaptured = true;
		return addUpvalue(compiler, (unsigned)local, true);
	}

	int upvalue = resolveUpvalue(compiler->enclosing, name);
	if (upvalue != -1) {
		return addUpvalue(compiler, (unsigned)upvalue, false);
	}

	return -1;
}

static void
addLocal(Token name)
{
	if (current->localCount == UINT8_COUNT) {
		error("Too many local variables in function.");
		return;
	}
	
	Local* local = &current->locals[current->localCount++];
	local->name = name;
	local->depth = -1;
	local->isCaptured = false;
}

static void
declareVariable()
{
	if (current->scopeDepth == 0) return;

	Token* name = &parser.previous;
	for (int i = current->localCount - 1; i >= 0; i--) {
		Local* local = &current->locals[i];
		if (local->depth != -1 && local->depth < current->scopeDepth) {
			break;
		}

		if (identifiersEqual(name, &local->name)) {
			error("Already a variable with this name in this scope.");
		}
	}

	addLocal(*name);
}

static unsigned long 
parseVariable(char* errorMessage) 
{
	consume(TOKEN_IDENTIFIER, errorMessage);

	declareVariable();
	if (current->scopeDepth > 0) return 0;

	return identifierConstant(&parser.previous);
}

static void
markInitialized()
{
	if (current->scopeDepth == 0) return;
	current->locals[current->localCount -1].depth = current->scopeDepth;
}

static void
defineVariable(unsigned long global)
{
	if (current->scopeDepth > 0) {
		markInitialized();
		return;
	}

	emitBytes(OP_DEFINE_GLOBAL, global);
}

static unsigned int 
argumentList() 
{
	unsigned int argCount = 0;
	if (!check(TOKEN_RIGHT_PAREN)) {
		do {
			expression();
			if (argCount == 255) {
				error("Can't have more than 255 arguments.");
			}
			argCount++;
		} while (match(TOKEN_COMMA));
	}
	consume(TOKEN_RIGHT_PAREN, "Expect ')' after arguments.");
	return argCount;
}

static void
and_(bool canAssign)
{
	int endJump = emitJump(OP_JUMP_IF_FALSE);

	emitByte(OP_POP);
	parsePrecedence(PREC_AND);

	patchJump(endJump);
}

static void
binary(bool canAssign)
{
	TokenType operatorType = parser.previous.type;
	ParseRule* rule = getRule(operatorType);
	parsePrecedence((Precedence)(rule->precedence + 1));
	
	switch(operatorType){
		case TOKEN_BANG_EQUAL: emitBytes(OP_EQUAL, OP_NOT); break;
		case TOKEN_EQUAL_EQUAL: emitByte(OP_EQUAL); break;
		case TOKEN_GREATER: emitByte(OP_GREATER); break;
		case TOKEN_GREATER_EQUAL: emitBytes(OP_LESS, OP_NOT); break;
		case TOKEN_LESS: emitByte(OP_LESS); break;
		case TOKEN_LESS_EQUAL: emitBytes(OP_GREATER, OP_NOT); break;
		case TOKEN_PLUS:	emitByte(OP_ADD); break;
		case TOKEN_MINUS:	emitByte(OP_SUBTRACT); break;
		case TOKEN_STAR:	emitByte(OP_MULTIPLY); break;
		case TOKEN_SLASH:	emitByte(OP_DIVIDE); break;
	default:
		return;
	}
}

static void 
call(bool canAssign) 
{
	unsigned int argCount = argumentList();
	emitBytes(OP_CALL, argCount);
}

static void 
dot(bool canAssign)
{
	consume(TOKEN_IDENTIFIER, "Expect property name after '.'.");
	unsigned char name = identifierConstant(&parser.previous);

	if (canAssign && match(TOKEN_EQUAL)) {
		expression();
		emitBytes(OP_SET_PROPERTY, name);
	} else if (match(TOKEN_LEFT_PAREN)) {
		unsigned char argCount = argumentList();
		emitBytes(OP_INVOKE, name);
		emitByte(argCount);
	} else {
		emitBytes(OP_GET_PROPERTY, name);
	}
}

static void
literal(bool canAssign)
{
	switch (parser.previous.type) {
		case TOKEN_FALSE: emitByte(OP_FALSE); break;
		case TOKEN_NIL: emitByte(OP_NIL); break;
		case TOKEN_TRUE: emitByte(OP_TRUE); break;
		default: return;
	}
}

static void
grouping(bool canAssign)
{
	expression();
	consume(TOKEN_RIGHT_PAREN, "expect ')' after expression.");
}

static void
printStatement(void)
{
	expression();
	consume(TOKEN_SEMICOLON, "Expect ';' after value.");
	emitByte(OP_PRINT);
}

static void 
returnStatement(void)
{
	if (current->type == TYPE_SCRIPT) {
		error("Can't return from top-level code.");
	}
	if (match(TOKEN_SEMICOLON)) {
		emitReturn();
	} else {
		if (current->type == TYPE_INITIALIZER) {
			error("Can't return a value from an initialzer.");
		}
		expression();
		consume(TOKEN_SEMICOLON, "Expect ';' after return value.");
		emitByte(OP_RETURN);
	}
}

static void
whileStatement()
{
	int loopStart = currentChunk()->count;
	consume(TOKEN_LEFT_PAREN, "Expect '(' after 'while'.");
	expression();
	consume(TOKEN_RIGHT_PAREN, "Expect ')' after condition.");

	int exitJump = emitJump(OP_JUMP_IF_FALSE);
	emitByte(OP_POP);
	statement();
	emitLoop(loopStart);

	patchJump(exitJump);
	emitByte(OP_POP);

}

static void 
synchronize(void)
{
	parser.panicMode = false;

	while (parser.current.type != TOKEN_EOF) {
		if (parser.previous.type == TOKEN_SEMICOLON)
			switch (parser.current.type) {
				case TOKEN_CLASS:
				case TOKEN_FUN:
				case TOKEN_VAR:
				case TOKEN_FOR:
				case TOKEN_IF:
				case TOKEN_WHILE:
				case TOKEN_PRINT:
				case TOKEN_RETURN:
					return;
				default:
					; // Do nothing				
			}
			advance();
	}
}

static void
number(bool canAssign)
{
	double value = strtod(parser.previous.start, nil);
	emitConstant(NUMBER_VAL(value));
}

static void
or_(bool canAssign)
{
	int elseJump = emitJump(OP_JUMP_IF_FALSE);
	int endJump = emitJump(OP_JUMP);

	patchJump(elseJump);
	parsePrecedence(PREC_OR);
	patchJump(endJump);
}

static void
string(bool canAssign)
{
	emitConstant(OBJ_VAL(copyString(parser.previous.start + 1,
									parser.previous.length - 2)));
}

static void
namedVariable(Token name, bool canAssign)
{
	unsigned char getOp, setOp;

	int arg = resolveLocal(current, &name);
	if (arg != -1) {
		getOp = OP_GET_LOCAL;
		setOp = OP_SET_LOCAL;
	} else if ((arg = resolveUpvalue(current, &name)) != -1) {
		getOp = OP_GET_UPVALUE;
		setOp = OP_SET_UPVALUE;
	} else {
		arg = identifierConstant(&name);
		getOp = OP_GET_GLOBAL;
		setOp = OP_SET_GLOBAL;
	}

	if (canAssign && match(TOKEN_EQUAL)) {
		expression();
		emitBytes(setOp, (uchar)arg);
	} else {
		emitBytes(getOp, (uchar)arg);
	}
}

static void
variable(bool canAssign)
{
	namedVariable(parser.previous, canAssign);
}

static void 
this_(bool canAssign)
{
	if (currentClass == nil) {
		error("Cean't use 'this' outside of a class.");
		return;
	}
	variable(false);
}

static void
unary(bool canAssign)
{
	TokenType operatorType = parser.previous.type;
	parsePrecedence(PREC_UNARY);

	switch(operatorType){
		case TOKEN_BANG: emitByte(OP_NOT); break;
		case TOKEN_MINUS: emitByte(OP_NEGATE); break;
	default:		return;
	}
}

/* 
 * Plan 9 Fix: 8c does not support [INDEX] = { ... } initialization.
 * The rules must be defined in the exact order of the TokenType enum.
 */
ParseRule rules[] = {
	{grouping, call,    PREC_CALL},       /* TOKEN_LEFT_PAREN */
	{nil,      nil,    PREC_NONE},       /* TOKEN_RIGHT_PAREN */
	{nil,      nil,    PREC_NONE},       /* TOKEN_LEFT_BRACE */
	{nil,      nil,    PREC_NONE},       /* TOKEN_RIGHT_BRACE */
	{nil,      nil,    PREC_NONE},       /* TOKEN_COMMA */
	{nil,      dot,    PREC_CALL},       /* TOKEN_DOT */
	{unary,    binary, PREC_TERM},       /* TOKEN_MINUS */
	{nil,      binary, PREC_TERM},       /* TOKEN_PLUS */
	{nil,      nil,    PREC_NONE},       /* TOKEN_SEMICOLON */
	{nil,      binary, PREC_FACTOR},     /* TOKEN_SLASH */
	{nil,      binary, PREC_FACTOR},     /* TOKEN_STAR */
	{unary,    nil,    PREC_NONE},       /* TOKEN_BANG */
	{nil,      binary, PREC_EQUALITY},   /* TOKEN_BANG_EQUAL */
	{nil,      binary, PREC_COMPARISON}, /* TOKEN_EQUAL */
	{nil,      binary, PREC_COMPARISON}, /* TOKEN_EQUAL_EQUAL */
	{nil,      binary, PREC_COMPARISON}, /* TOKEN_GREATER */
	{nil,      binary, PREC_COMPARISON}, /* TOKEN_GREATER_EQUAL */
	{nil,      binary, PREC_COMPARISON}, /* TOKEN_LESS */
	{nil,      binary, PREC_COMPARISON}, /* TOKEN_LESS_EQUAL */
	{variable, nil,	   PREC_NONE}, 		 /* TOEKN_IDENTIFIER */
	{string,   nil,    PREC_NONE},       /* TOKEN_STRING */
	{number,   nil,    PREC_NONE},       /* TOKEN_NUMBER */
	{nil,      and_,    PREC_AND},       /* TOKEN_AND */
	{nil,      nil,    PREC_NONE},       /* TOKEN_CLASS */
	{nil,      nil,    PREC_NONE},       /* TOKEN_ELSE */
	{literal,  nil,    PREC_NONE},       /* TOKEN_FALSE */
	{nil,      nil,    PREC_NONE},       /* TOKEN_FOR */
	{nil,      nil,    PREC_NONE},       /* TOKEN_FUN */
	{nil,      nil,    PREC_NONE},       /* TOKEN_IF */
	{literal,  nil,    PREC_NONE},       /* TOKEN_NIL */
	{nil,      or_,    PREC_OR},       /* TOKEN_OR */
	{nil,      nil,    PREC_NONE},       /* TOKEN_PRINT */
	{nil,      nil,    PREC_NONE},       /* TOKEN_RETURN */
	{nil,      nil,    PREC_NONE},       /* TOKEN_SUPER */
	{this_,      nil,    PREC_NONE},       /* TOKEN_THIS */
	{literal,  nil,    PREC_NONE},       /* TOKEN_TRUE */
	{nil,      nil,    PREC_NONE},       /* TOKEN_VAR */
	{nil,      nil,    PREC_NONE},       /* TOKEN_WHILE */
	{nil,      nil,    PREC_NONE},       /* TOKEN_ERROR */
	{nil,      nil,    PREC_NONE},       /* TOKEN_EOF */
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
	// adjustment
	bool canAssign = precedence <= PREC_ASSIGNMENT;
	prefixRule(canAssign);

	while(precedence <= getRule(parser.current.type)->precedence){
		advance();
		infixRule = getRule(parser.previous.type)->infix;
		infixRule(canAssign);
	}

	if (canAssign && match(TOKEN_EQUAL)) {
		error("Invalid assignment target.");
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

static void
block(void)
{
	while (!check(TOKEN_RIGHT_BRACE) && !check(TOKEN_EOF)) {
		declaration();
	}

	consume(TOKEN_RIGHT_BRACE, "Expect '}' after block.");
}

static void 
function(FunctionType type)
{
	Compiler compiler;
	initCompiler(&compiler, type);
	beginScope();

	consume(TOKEN_LEFT_PAREN, "Expect '(' after functions names.");
	if (!check(TOKEN_RIGHT_PAREN)) {
		do {
			uchar constant;
			current->function->arity++;
			if (current->function->arity > 255) {
				errorAtCurrent("Can't have more than 255 parameters.");
			}
		constant = parseVariable("Expect parameter name.");
		defineVariable(constant);
		} while (match(TOKEN_COMMA));
	}
	consume(TOKEN_RIGHT_PAREN, "Expect ')' after parameters.");
	consume(TOKEN_LEFT_BRACE, "Expect '{' before function body.");
	block();

	ObjFunction* function = endCompiler();
	emitBytes(OP_CLOSURE, makeConstant(OBJ_VAL(function)));

	for (int i = 0; i < function->upvalueCount; i++) {
		emitByte(compiler.upvalues[i].isLocal ? 1 : 0);
		emitByte(compiler.upvalues[i].index);
	}
}

static void 
method()
{
	consume(TOKEN_IDENTIFIER, "Expect method name.");
	unsigned char constant = identifierConstant(&parser.previous);

	FunctionType type = TYPE_METHOD;
	if (parser.previous.length == 4 && memcmp(parser.previous.start, "init", 4) == 0) {
		type = TYPE_INITIALIZER;
	}
	function(type);
	emitBytes(OP_METHOD, constant);
}

static void 
classDeclaration()
{
	consume(TOKEN_IDENTIFIER, "Expect class name.");
	unsigned char nameConstant = identifierConstant(&parser.previous);
	declareVariable();

	emitBytes(OP_CLASS, nameConstant);
	defineVariable(nameConstant);

	ClassCompiler classCompiler;
	classCompiler.enclosing = currentClass;
	currentClass = &classCompiler;

	namedVariable(parser.previous, false);
	consume(TOKEN_LEFT_BRACE, "Expect '{' before class body.");
	while (!check(TOKEN_RIGHT_BRACE) && !check(TOKEN_EOF)) {
		method();
	}
	consume(TOKEN_RIGHT_BRACE, "Expect '}' after class body.");
	emitByte(OP_POP);

	currentClass = currentClass->enclosing;
}

static void 
funDeclaration(void) 
{
	unsigned int global = parseVariable("Expect function name.");
	markInitialized();
	function(TYPE_FUNCTION);
	defineVariable(global);
}

static void
varDeclaration(void) {
	unsigned long global = parseVariable("Expect variable name.");

	if (match(TOKEN_EQUAL)) {
		expression();
	} else {
		emitByte(OP_NIL);
	}
	consume(TOKEN_SEMICOLON, "Expect ';' after variable declaration.");

	defineVariable(global);
}

static void
expressionStatement(void) {
	expression();
	consume(TOKEN_SEMICOLON, "Expect ';' after expression.");
	emitByte(OP_POP);
}

static void
forStatement()
{
	beginScope();
	consume(TOKEN_LEFT_PAREN, "Expect '(' after 'for'.");
	if (match(TOKEN_SEMICOLON)) {
		// no initializer.
	} else if (match(TOKEN_VAR)) {
		varDeclaration();
	} else {
		expressionStatement();
	}

	int loopStart = currentChunk()->count;
	int exitJump = -1;
	if(!match(TOKEN_SEMICOLON)) {
		expression();
		consume(TOKEN_SEMICOLON, "Expect ';' after loop condition.");

		// Jump out of the loop if the condition is false.
		exitJump = emitJump(OP_JUMP_IF_FALSE);
		emitByte(OP_POP);
	}
	
	if (!match(TOKEN_RIGHT_PAREN)) {
		int bodyJump = emitJump(OP_JUMP);
		int incrementStart = currentChunk()->count;
		expression();
		emitByte(OP_POP);
		consume(TOKEN_RIGHT_PAREN, "Expect ')' after for clauses.");

		emitLoop(loopStart);
		loopStart = incrementStart;
		patchJump(bodyJump);
	}

	statement();
	emitLoop(loopStart);
	
	if (exitJump != -1) {
		patchJump(exitJump);
		emitByte(OP_POP);
	}

	endScope();
}

static void
ifStatement(void)
{
	consume(TOKEN_LEFT_PAREN, "Expect '(' after 'if'.");
	expression();
	consume(TOKEN_RIGHT_PAREN, "Expect ')' after condition.");

	int thenJump = emitJump(OP_JUMP_IF_FALSE);
	emitByte(OP_POP);
	statement();

	int elseJump = emitJump(OP_JUMP);

	patchJump(thenJump);
	emitByte(OP_POP);

	if (match(TOKEN_ELSE)) statement();
	patchJump(elseJump);
}

static void 
declaration(void) 
{
	if (match(TOKEN_CLASS)) {
		classDeclaration();
	} else if (match(TOKEN_FUN)) {
		funDeclaration();
	} else if (match(TOKEN_VAR)) {
		varDeclaration();
	} else {
		statement();
	}

	if (parser.panicMode) synchronize();
}

static void 
statement()
{
	if (match(TOKEN_PRINT)) {
		printStatement();
	} else if (match(TOKEN_FOR)) {
		forStatement();
	} else if (match(TOKEN_IF)) {
		ifStatement();
	} else if (match(TOKEN_RETURN)) {
		returnStatement();
	} else if (match(TOKEN_WHILE)) {
		whileStatement();
	} else if (match(TOKEN_LEFT_BRACE)) {
		beginScope();
		block();
		endScope();
	} else {
		expressionStatement();
	}
}

ObjFunction*
compile(char* source)
{
	initScanner(source);
	Compiler compiler;
	initCompiler(&compiler, TYPE_SCRIPT);

	parser.hadError = 0;
	parser.panicMode = 0;

	advance();
	while (!match(TOKEN_EOF)) {
		declaration();
	}
	ObjFunction* function = endCompiler();
	return parser.hadError ? nil : function;
}

void 
markCompilerRoots(void)
{
	Compiler* compiler = current;
	while (compiler != nil) {
		markObject((Obj*)compiler->function);
		compiler = compiler->enclosing;
	}
}
