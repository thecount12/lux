#include "lux.h"
#include "common.h"
#include "value.h"
#include "chunk.h"
#include "vm.h"
#include "memory.h"
#include "object.h"
#include "compiler.h"
#include "scanner.h"
#include "table.h"

#define UINT16_MAX 0xffff

/* Lint state (when opts && opts->lint) */
#define MAX_REACHABLE_DEPTH 64
static CompileOptions* lintOpts = nil;
static Table lintGlobals;
static int lintReachable = 1;
static int lintReachableStack[MAX_REACHABLE_DEPTH];
static int lintReachableDepth = 0;

/* Encode global info: (line << 8) | (type << 1) | used. type: 0=var, 1=fun, 2=class */
#define LINT_LINE(v) ((int)(AS_NUMBER(v)) >> 8)
#define LINT_TYPE(v) (((int)(AS_NUMBER(v)) >> 1) & 3)
#define LINT_USED(v) ((int)(AS_NUMBER(v)) & 1)
#define LINT_VAL(line, type, used) NUMBER_VAL((double)(((line) << 8) | ((type) << 1) | (used)))

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
	bool used;  /* for lint: variable was read or written */
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
	int loopDepth;
	int loopStart;
	int loopScopeDepth;
};

typedef struct ClassCompiler ClassCompiler;
struct ClassCompiler {
	struct ClassCompiler* enclosing;
	bool hasSuperclass;
};

Parser parser;
Compiler* current = nil;
ClassCompiler* currentClass = nil;

/* Break statement support */
#define MAX_BREAK_JUMPS 256
static int breakJumps[MAX_BREAK_JUMPS];
static int breakJumpCount = 0;

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
static void breakStatement(void);
static void continueStatement(void);
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
warnAt(Token* token, char* message)
{
	if(lintOpts == nil || !lintOpts->lint)
		return;
	lintOpts->warningCount++;
	fprint(2, "[line %d] Warning", token->line);
	if(token->type != TOKEN_EOF && token->type != TOKEN_ERROR && token->length > 0)
		fprint(2, " at '%.*s'", token->length, token->start);
	fprint(2, ": %s\n", message);
}

static void
warnAtLine(int line, char* message)
{
	if(lintOpts == nil || !lintOpts->lint)
		return;
	lintOpts->warningCount++;
	fprint(2, "[line %d] Warning: %s\n", line, message);
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

static int
makeConstant(Value value)
{
	int constant = addConstant(currentChunk(), value);
	if(constant > 0xFFFFFF){
		error("Too many constants in one chunk.");
		return 0;
	}

	return constant;
}

static void
emitConstant(Value value)
{
	int constant;
	constant = makeConstant(value);
	
	if(constant < 256){
		emitBytes(OP_CONSTANT, (uchar)constant);
	} else {
		emitByte(OP_CONSTANT_LONG);
		emitByte((constant >> 16) & 0xff);
		emitByte((constant >> 8) & 0xff);
		emitByte(constant & 0xff);
	}
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
	compiler->loopDepth = 0;
	compiler->loopStart = 0;
	compiler->loopScopeDepth = 0;
	compiler->function = newFunction();
	current = compiler;
	if (type != TYPE_SCRIPT) {
		current->function->name = copyString(parser.previous.start,
											 parser.previous.length);
	}


	Local* local = &current->locals[current->localCount++];
	local->depth = 0;
	local->isCaptured = false;
	local->used = true;  /* this or slot 0: always "used" */
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
		Local* local = &current->locals[current->localCount - 1];
		if (lintOpts != nil && lintOpts->lint && !local->used && local->name.length > 0) {
			warnAt(&local->name, "Unused variable.");
		}
		if (local->isCaptured) {
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

static int
identifierConstant(Token* name)
{
	int constant;
	constant = makeConstant(OBJ_VAL(copyString(name->start, name-> length)));
	if(constant > 255){
		error("Too many unique identifiers in one chunk.");
		return 0;
	}
	return constant;
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
	local->used = false;
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

	if (lintOpts != nil && lintOpts->lint) {
		ObjString* key = copyString(name->start, name->length);
		Value dummy;
		if (tableGet(&lintGlobals, key, &dummy)) {
			warnAt(name, "Local variable shadows global.");
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
defineVariableWithType(unsigned long global, int type, int line)
{
	if (current->scopeDepth > 0) {
		markInitialized();
		return;
	}

	if (lintOpts != nil && lintOpts->lint) {
		ObjString* key = AS_STRING(currentChunk()->constants.values[global]);
		Value existing;
		if (tableGet(&lintGlobals, key, &existing)) {
			if (type == 1)
				warnAtLine(line, "Duplicate function name.");
			else
				warnAtLine(line, "Duplicate global name.");
		}
		tableSet(&lintGlobals, key, LINT_VAL(line, type, 0));
	}

	emitBytes(OP_DEFINE_GLOBAL, global);
}

static void
defineVariable(unsigned long global)
{
	defineVariableWithType(global, 0, parser.previous.line);
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
	USED(canAssign);
	int endJump = emitJump(OP_JUMP_IF_FALSE);

	emitByte(OP_POP);
	parsePrecedence(PREC_AND);

	patchJump(endJump);
}

static void
binary(bool canAssign)
{
	USED(canAssign);
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
		case TOKEN_PERCENT:	emitByte(OP_MODULO); break;
	default:
		return;
	}
}

static void
ternary(bool canAssign)
{
	(void)canAssign;
	/* At this point the condition (left expression) is already evaluated
	 * and its value is on the stack. Implement conditional expression:
	 *
	 * condition ? exprTrue : exprFalse
	 *
	 * Compile to:
	 *  - jump if false -> elseJump
	 *  - pop condition
	 *  - compile exprTrue (leaves value)
	 *  - jump -> endJump
	 *  - patch elseJump, pop
	 *  - consume ':' and compile exprFalse (leaves value)
	 *  - patch endJump
	 */
	int elseJump = emitJump(OP_JUMP_IF_FALSE);
	emitByte(OP_POP);

	/* true branch */
	parsePrecedence(PREC_ASSIGNMENT);

	int endJump = emitJump(OP_JUMP);

	/* else branch */
	patchJump(elseJump);
	emitByte(OP_POP);

	consume(TOKEN_COLON, "Expect ':' after expression.");
	parsePrecedence(PREC_ASSIGNMENT);

	patchJump(endJump);
}

static void 
call(bool canAssign) 
{
	USED(canAssign);
	unsigned int argCount = argumentList();
	emitBytes(OP_CALL, argCount);
}

static void 
dot(bool canAssign)
{
	USED(canAssign);
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
	USED(canAssign);
	switch (parser.previous.type) {
		case TOKEN_FALSE: emitByte(OP_FALSE); break;
		case TOKEN_NIL: emitByte(OP_NIL); break;
		case TOKEN_TRUE: emitByte(OP_TRUE); break;
		default: return;
	}
}

static void
array(bool canAssign)
{
	USED(canAssign);
	int itemCount = 0;
	
	if (!check(TOKEN_RIGHT_BRACKET)) {
		do {
			if (check(TOKEN_RIGHT_BRACKET)) {
				/* Trailing comma case */
				break;
			}
			
			expression();
			
			if (itemCount == 255) {
				error("Cannot have more than 255 items in array literal.");
			}
			itemCount++;
		} while (match(TOKEN_COMMA));
	}
	
	consume(TOKEN_RIGHT_BRACKET, "Expect ']' after array elements.");
	emitBytes(OP_ARRAY, (uchar)itemCount);
}

static void
subscript(bool canAssign)
{
	expression();
	consume(TOKEN_RIGHT_BRACKET, "Expect ']' after index.");
	
	if (canAssign && match(TOKEN_EQUAL)) {
		expression();
		emitByte(OP_STORE_SUBSCR);
	} else {
		emitByte(OP_INDEX_SUBSCR);
	}
}

static void
grouping(bool canAssign)
{
	USED(canAssign);
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
importStatement(void)
{
	int constant;
	consume(TOKEN_STRING, "Expect filename string after 'import'.");
	constant = makeConstant(OBJ_VAL(copyString(parser.previous.start + 1, 
	                                            parser.previous.length - 2)));
	if(constant > 255){
		error("Too many constants for import statement.");
		return;
	}
	emitBytes(OP_IMPORT, (uchar)constant);
	emitByte(OP_POP);
	consume(TOKEN_SEMICOLON, "Expect ';' after import statement.");
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
	if (lintOpts != nil && lintOpts->lint) lintReachable = 0;
}

static void
discardLoopLocals(void)
{
	int i;
	for (i = current->localCount - 1;
	     i >= 0 && current->locals[i].depth > current->loopScopeDepth;
	     i--) {
		if (current->locals[i].isCaptured)
			emitByte(OP_CLOSE_UPVALUE);
		else
			emitByte(OP_POP);
	}
}

static void
whileStatement()
{
	int savedBreakCount = breakJumpCount;
	int savedLoopStart = current->loopStart;
	int savedLoopScope = current->loopScopeDepth;
	current->loopDepth++;
	
	int loopStart = currentChunk()->count;
	current->loopStart = loopStart;
	current->loopScopeDepth = current->scopeDepth;
	consume(TOKEN_LEFT_PAREN, "Expect '(' after 'while'.");
	expression();
	consume(TOKEN_RIGHT_PAREN, "Expect ')' after condition.");

	int exitJump = emitJump(OP_JUMP_IF_FALSE);
	emitByte(OP_POP);
	/* If linting enabled, warn when the while body is not a braced block.
	 * Single-statement while bodies are valid but often error-prone. */
	if (lintOpts != nil && lintOpts->lint) {
		if (parser.current.type != TOKEN_LEFT_BRACE) {
			warnAtLine(parser.previous.line, "While body should be wrapped in '{ }'.");
		}
	}
	statement();
	emitLoop(loopStart);

	patchJump(exitJump);
	emitByte(OP_POP);
	
	/* Patch all break statements */
	while (breakJumpCount > savedBreakCount) {
		patchJump(breakJumps[--breakJumpCount]);
	}

	current->loopStart = savedLoopStart;
	current->loopScopeDepth = savedLoopScope;
	current->loopDepth--;
}

static void
breakStatement()
{
	if (current->loopDepth == 0) {
		error("Cannot use 'break' outside of a loop.");
		return;
	}
	
	consume(TOKEN_SEMICOLON, "Expect ';' after 'break'.");
	discardLoopLocals();
	
	if (breakJumpCount >= MAX_BREAK_JUMPS) {
		error("Too many break statements in one loop.");
		return;
	}
	
	breakJumps[breakJumpCount++] = emitJump(OP_JUMP);
	if (lintOpts != nil && lintOpts->lint) lintReachable = 0;
}

static void
continueStatement()
{
	if (current->loopDepth == 0) {
		error("Cannot use 'continue' outside of a loop.");
		return;
	}

	consume(TOKEN_SEMICOLON, "Expect ';' after 'continue'.");
	discardLoopLocals();
	emitLoop(current->loopStart);
	if (lintOpts != nil && lintOpts->lint) lintReachable = 0;
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
				case TOKEN_BREAK:
				case TOKEN_CONTINUE:
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
	USED(canAssign);
	double value = strtod(parser.previous.start, nil);
	emitConstant(NUMBER_VAL(value));
}

static void
or_(bool canAssign)
{
	USED(canAssign);
	int elseJump = emitJump(OP_JUMP_IF_FALSE);
	int endJump = emitJump(OP_JUMP);

	patchJump(elseJump);
	parsePrecedence(PREC_OR);
	patchJump(endJump);
}

static void
string(bool canAssign)
{
	USED(canAssign);
	const char *src;
	int rawLen, outLen, i;
	char *buf;

	src = parser.previous.start + 1;
	rawLen = parser.previous.length - 2;
	buf = (char*)malloc(rawLen + 1);
	if (buf == nil) return;
	outLen = 0;
	for (i = 0; i < rawLen; i++) {
		if (src[i] == '\\' && i + 1 < rawLen) {
			i++;
			switch (src[i]) {
			case '"':  buf[outLen++] = '"'; break;
			case '\\': buf[outLen++] = '\\'; break;
			case 'n':  buf[outLen++] = '\n'; break;
			case 't':  buf[outLen++] = '\t'; break;
			case 'r':  buf[outLen++] = '\r'; break;
			default:   buf[outLen++] = '\\'; buf[outLen++] = src[i]; break;
			}
		} else {
			buf[outLen++] = src[i];
		}
	}
	emitConstant(OBJ_VAL(copyString(buf, outLen)));
	free(buf);
}

static void
namedVariable(Token name, bool canAssign)
{
	unsigned char getOp, setOp;

	int arg = resolveLocal(current, &name);
	if (arg != -1) {
		if (lintOpts != nil && lintOpts->lint) current->locals[arg].used = true;
		getOp = OP_GET_LOCAL;
		setOp = OP_SET_LOCAL;
	} else if ((arg = resolveUpvalue(current, &name)) != -1) {
		if (lintOpts != nil && lintOpts->lint && current->upvalues[arg].isLocal) {
			current->enclosing->locals[current->upvalues[arg].index].used = true;
		}
		getOp = OP_GET_UPVALUE;
		setOp = OP_SET_UPVALUE;
	} else {
		arg = identifierConstant(&name);
		getOp = OP_GET_GLOBAL;
		setOp = OP_SET_GLOBAL;
		if (lintOpts != nil && lintOpts->lint) {
			ObjString* key = copyString(name.start, name.length);
			Value val;
			if (tableGet(&lintGlobals, key, &val)) {
				tableSet(&lintGlobals, key, LINT_VAL(LINT_LINE(val), LINT_TYPE(val), 1));
			}
		}
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

static Token 
syntheticToken(const char* text)
{
	Token token;
	token.start = (char*)text;
	token.length = (int)strlen(text);
	return token;
}

static void 
super_(bool canAssign)
{
	(void)canAssign;
	if (currentClass == nil ) {
		error("Can't use 'super' outside of a class.");
	} else if (!currentClass->hasSuperclass) {
		error("Can't user 'super' in a class with no superclass.");
	}

	consume(TOKEN_DOT, "Expect '.' after 'super'.");
	consume(TOKEN_IDENTIFIER, "Expect superclass method name.");
	unsigned char name = identifierConstant(&parser.previous);

	namedVariable(syntheticToken("this"), false);
	if (match(TOKEN_LEFT_PAREN)) {
		unsigned char argCount = argumentList();
		namedVariable(syntheticToken("super"), false);
		emitBytes(OP_SUPER_INVOKE, name);
		emitByte(argCount);
	} else {
		namedVariable(syntheticToken("super"), false);
		emitBytes(OP_GET_SUPER, name);
	}
}

static void 
this_(bool canAssign)
{
	USED(canAssign);
	if (currentClass == nil) {
		error("Cean't use 'this' outside of a class.");
		return;
	}
	variable(false);
}

static void
unary(bool canAssign)
{
	USED(canAssign);
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
	{array,    subscript, PREC_CALL},    /* TOKEN_LEFT_BRACKET */
	{nil,      nil,    PREC_NONE},       /* TOKEN_RIGHT_BRACKET */
	{nil,      nil,    PREC_NONE},       /* TOKEN_COMMA */
	{nil,      dot,    PREC_CALL},       /* TOKEN_DOT */
	{unary,    binary, PREC_TERM},       /* TOKEN_MINUS */
	{nil,      binary, PREC_TERM},       /* TOKEN_PLUS */
	{nil,      nil,    PREC_NONE},       /* TOKEN_SEMICOLON */
	{nil,      binary, PREC_FACTOR},     /* TOKEN_SLASH */
	{nil,      binary, PREC_FACTOR},     /* TOKEN_STAR */
	{nil,      binary, PREC_FACTOR},     /* TOKEN_PERCENT */
	{nil,      or_,    PREC_OR},         /* TOKEN_PIPE */
	{nil,      and_,   PREC_AND},        /* TOKEN_AMPERSAND */
	{nil,      ternary,PREC_ASSIGNMENT}, /* TOKEN_QUESTION */
	{nil,      nil,    PREC_NONE},       /* TOKEN_COLON */
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
	{nil,      nil,    PREC_NONE},       /* TOKEN_BREAK */
	{nil,      nil,    PREC_NONE},       /* TOKEN_CLASS */
	{nil,      nil,    PREC_NONE},       /* TOKEN_CONTINUE */
	{nil,      nil,    PREC_NONE},       /* TOKEN_ELSE */
	{literal,  nil,    PREC_NONE},       /* TOKEN_FALSE */
	{nil,      nil,    PREC_NONE},       /* TOKEN_FOR */
	{nil,      nil,    PREC_NONE},       /* TOKEN_FUN */
	{nil,      nil,    PREC_NONE},       /* TOKEN_IF */
	{nil,      nil,    PREC_NONE},       /* TOKEN_IMPORT */
	{literal,  nil,    PREC_NONE},       /* TOKEN_NIL */
	{nil,      or_,    PREC_OR},       /* TOKEN_OR */
	{nil,      nil,    PREC_NONE},       /* TOKEN_PRINT */
	{nil,      nil,    PREC_NONE},       /* TOKEN_RETURN */
	{super_,      nil,    PREC_NONE},       /* TOKEN_SUPER */
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
	if (lintOpts != nil && lintOpts->lint && lintReachableDepth < MAX_REACHABLE_DEPTH) {
		lintReachableStack[lintReachableDepth++] = lintReachable;
	}
	while (!check(TOKEN_RIGHT_BRACE) && !check(TOKEN_EOF)) {
		declaration();
	}
	if (lintOpts != nil && lintOpts->lint && lintReachableDepth > 0) {
		lintReachable = lintReachableStack[--lintReachableDepth];
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
	Token className = parser.previous;
	unsigned char nameConstant = identifierConstant(&parser.previous);
	declareVariable();

	emitBytes(OP_CLASS, nameConstant);
	defineVariableWithType(nameConstant, 2, className.line);

	ClassCompiler classCompiler;
	classCompiler.hasSuperclass = false;
	classCompiler.enclosing = currentClass;
	currentClass = &classCompiler;

	if (match(TOKEN_LESS)) {
		consume(TOKEN_IDENTIFIER, "Expect superclass name.");
		variable(false);


		if (identifiersEqual(&className, &parser.previous)) {
			error("A class can't inherit from itself.");
		}

		beginScope();
		addLocal(syntheticToken("super"));
		current->locals[current->localCount - 1].used = true;  /* super: always "used" */
		defineVariable(0);

		namedVariable(className, false);
		emitByte(OP_INHERIT);
		classCompiler.hasSuperclass = true;
	}

	namedVariable(className, false);
	consume(TOKEN_LEFT_BRACE, "Expect '{' before class body.");
	while (!check(TOKEN_RIGHT_BRACE) && !check(TOKEN_EOF)) {
		method();
	}
	consume(TOKEN_RIGHT_BRACE, "Expect '}' after class body.");
	emitByte(OP_POP);

	if (classCompiler.hasSuperclass) {
		endScope();
	}

	currentClass = currentClass->enclosing;
}

static void 
funDeclaration(void) 
{
	unsigned int global = parseVariable("Expect function name.");
	int line = parser.previous.line;
	markInitialized();
	function(TYPE_FUNCTION);
	defineVariableWithType(global, 1, line);
}

static void
varDeclaration(void) {
	unsigned long global = parseVariable("Expect variable name.");
	int line = parser.previous.line;

	if (match(TOKEN_EQUAL)) {
		expression();
	} else {
		emitByte(OP_NIL);
	}
	consume(TOKEN_SEMICOLON, "Expect ';' after variable declaration.");

	defineVariableWithType(global, 0, line);
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
	int savedBreakCount = breakJumpCount;
	int savedLoopStart = current->loopStart;
	int savedLoopScope = current->loopScopeDepth;
	current->loopDepth++;
	
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

	current->loopStart = loopStart;
	current->loopScopeDepth = current->scopeDepth;

	/* If linting enabled, warn when the for body is not a braced block. */
	if (lintOpts != nil && lintOpts->lint) {
		if (parser.current.type != TOKEN_LEFT_BRACE) {
			warnAtLine(parser.previous.line, "For body should be wrapped in '{ }'.");
		}
	}
	statement();
	emitLoop(loopStart);
	
	if (exitJump != -1) {
		patchJump(exitJump);
		emitByte(OP_POP);
	}
	
	/* Patch all break statements */
	while (breakJumpCount > savedBreakCount) {
		patchJump(breakJumps[--breakJumpCount]);
	}

	endScope();
	current->loopStart = savedLoopStart;
	current->loopScopeDepth = savedLoopScope;
	current->loopDepth--;
}

static void
ifStatement(void)
{
	consume(TOKEN_LEFT_PAREN, "Expect '(' after 'if'.");
	expression();
	consume(TOKEN_RIGHT_PAREN, "Expect ')' after condition.");

	int thenJump = emitJump(OP_JUMP_IF_FALSE);
	emitByte(OP_POP);
	/* If linting enabled, warn when the 'then' body is not a braced block. */
	if (lintOpts != nil && lintOpts->lint) {
		if (parser.current.type != TOKEN_LEFT_BRACE) {
			warnAtLine(parser.previous.line, "If 'then' body should be wrapped in '{ }'.");
		}
	}
	statement();

	int elseJump = emitJump(OP_JUMP);

	patchJump(thenJump);
	emitByte(OP_POP);

	if (match(TOKEN_ELSE)) {
		if (lintOpts != nil && lintOpts->lint) lintReachable = 1;
		/* Warn when the else body is not a braced block. */
		if (lintOpts != nil && lintOpts->lint) {
			if (parser.current.type != TOKEN_LEFT_BRACE) {
				warnAtLine(parser.previous.line, "If 'else' body should be wrapped in '{ }'.");
			}
		}
		statement();
	}
	patchJump(elseJump);
}

static void 
declaration(void) 
{
	if (lintOpts != nil && lintOpts->lint && !lintReachable) {
		warnAtLine(parser.current.line, "Unreachable code after return.");
	}
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
	} else if (match(TOKEN_IMPORT)) {
		importStatement();
	} else if (match(TOKEN_FOR)) {
		forStatement();
	} else if (match(TOKEN_IF)) {
		ifStatement();
	} else if (match(TOKEN_RETURN)) {
		returnStatement();
	} else if (match(TOKEN_BREAK)) {
		breakStatement();
	} else if (match(TOKEN_CONTINUE)) {
		continueStatement();
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

static void
reportUnusedGlobal(ObjString* key, Value value, void* arg)
{
	(void)arg;
	if (!LINT_USED(value)) {
		char buf[256];
		snprint(buf, sizeof buf, "Unused variable '%.*s'.", key->length, key->chars);
		warnAtLine(LINT_LINE(value), buf);
	}
}

ObjFunction*
compile(char* source)
{
	return compileWithOptions(source, nil);
}

ObjFunction*
compileWithOptions(char* source, CompileOptions* opts)
{
	lintOpts = opts;
	if (opts != nil && opts->lint) {
		opts->warningCount = 0;
		initTable(&lintGlobals);
		lintReachable = 1;
		lintReachableDepth = 0;
	}

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

	if (opts != nil && opts->lint && !parser.hadError) {
		tableForEach(&lintGlobals, reportUnusedGlobal, nil);
		freeTable(&lintGlobals);
	}
	lintOpts = nil;

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
