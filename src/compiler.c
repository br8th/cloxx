#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "common.h"
#include "compiler.h"
#include "scanner.h"

#ifdef DEBUG_PRINT_CODE
#include "debug.h"
#endif

typedef struct
{
	Token current;
	Token previous;
	bool hadError;
	bool panicMode;
} Parser;

typedef enum
{
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
} Precedence;

typedef void (*ParseFn)(bool canAssign);

typedef struct
{
	ParseFn prefix;
	ParseFn infix;
	Precedence precedence;
} ParseRule;

typedef struct
{
	Token name;
	int depth;
} Local;

typedef struct
{
	uint8_t index;
	bool isLocal;
} Upvalue;

typedef enum
{
	TYPE_FUNCTION,
	TYPE_SCRIPT
} FunctionType;

// forward declaration because of circular dependency.
typedef struct Compiler
{
	struct Compiler *enclosing;
	ObjFunction *function;
	FunctionType type;

	Local locals[UINT8_COUNT];
	int localCount;
	Upvalue upvalues[UINT8_COUNT];
	int scopeDepth;
} Compiler;

Parser parser;
Compiler *current = NULL;
Chunk *compilingChunk;

static Chunk *currentChunk()
{
	return &current->function->chunk;
}

static void errorAt(Token *token, const char *message)
{
	if (parser.panicMode)
		return;

	parser.panicMode = true;

	fprintf(stderr, "[line %d] Error", token->line);

	if (token->type == TOKEN_EOF)
	{
		fprintf(stderr, " at end");
	}
	else if (token->type == TOKEN_ERROR)
	{
		// Nothing.
	}
	else
	{
		fprintf(stderr, " at '%.*s'", token->length, token->start);
	}

	fprintf(stderr, ": %s\n", message);
	parser.hadError = true;
}

static void errorAtCurrent(const char *message)
{
	errorAt(&parser.current, message);
}

static void error(const char *message)
{
	errorAt(&parser.previous, message);
}

static void advance()
{
	parser.previous = parser.current;

	for (;;)
	{
		parser.current = scanToken();
		if (parser.current.type != TOKEN_ERROR)
			break;

		errorAtCurrent(parser.current.start);
	}
}

static void consume(TokenType type, const char *message)
{
	if (parser.current.type == type)
	{
		advance();
		return;
	}

	errorAtCurrent(message);
}

static bool check(TokenType type)
{
	return parser.current.type == type;
}

static bool match(TokenType type)
{
	if (!check(type))
		return false;

	advance();
	return true;
}

// returns the index of the added const in the constants table
static uint8_t makeConstant(Value value)
{
	int constant = addConstant(currentChunk(), value);
	if (constant > UINT8_MAX)
	{
		error("Too many constants in one chunk.");
		return 0;
	}

	return (uint8_t)constant;
}

static void emitByte(uint8_t byte)
{
	writeChunk(currentChunk(), byte, parser.previous.line);
}

static void emitBytes(uint8_t byte1, uint8_t byte2)
{
	emitByte(byte1);
	emitByte(byte2);
}

static void emitLoop(int loopStart)
{
	emitByte(OP_LOOP);

	// +2 accounts for OP_LOOP operands.
	int offset = currentChunk()->count - loopStart + 2;
	if (offset > UINT16_MAX)
		error("Loop body too large.");

	emitByte((offset >> 8) & 0xff);
	emitByte(offset & 0xff);
}

// Write: JMP, jmp_offset (2 bytes)
// Return: the offset of the JMP instruction.
static int emitJump(uint8_t instruction)
{
	emitByte(instruction);
	emitByte(0xff);
	emitByte(0xff);
	return currentChunk()->count - 2;
}

static void emitReturn()
{
	emitByte(OP_NIL);
	emitByte(OP_RETURN);
}

static void emitConstant(Value value)
{
	emitBytes(OP_CONSTANT, makeConstant(value));
}

static void patchJump(int offset)
{
	// -2 to adjust for the bytecode for the jump offset itself.
	int jump = currentChunk()->count - offset - 2;

	if (jump > UINT16_MAX)
	{
		error("Too much code to jump over.");
	}

	currentChunk()->code[offset] = (jump >> 8) & 0xff;
	currentChunk()->code[offset + 1] = jump & 0xff;
}

static void initCompiler(Compiler *compiler, FunctionType type)
{
	compiler->enclosing = current;
	compiler->function = NULL;
	compiler->type = type;
	compiler->localCount = 0;
	compiler->scopeDepth = 0;

	// garbage collection paranoia.
	compiler->function = newFunction();

	current = compiler;

	Local *local = &current->locals[current->localCount++];
	local->depth = 0;
	local->name.start = "";
	local->name.length = 0;

	if (type != TYPE_SCRIPT)
	{
		current->function->name = allocateString(parser.previous.start,
												 parser.previous.length);
	}
}

ObjFunction *endCompiler()
{
	emitReturn();

	ObjFunction *function = current->function;

#ifdef DEBUG_PRINT_CODE
	if (!parser.hadError)
	{
		disassembleChunk(currentChunk(), function->name != NULL
											 ? function->name->chars
											 : "<script>");
	}
#endif

	current = current->enclosing;

	return function;
}

static void beginScope()
{
	current->scopeDepth++;
}

static void endScope()
{
	current->scopeDepth--;

	while (current->localCount > 0 &&
		   current->locals[current->localCount - 1].depth > current->scopeDepth)
	{
		// TODO: When's this added to the stack?
		emitByte(OP_POP);
		current->localCount--;
	}
}

static void expression();
static void statement();
static void declaration();
static ParseRule *getRule(TokenType type);
static void parsePrecedence(Precedence precedence);

// Return the index of the newly created identifier in the constants table.
static uint8_t identifierConstant(Token *name)
{
	return makeConstant(OBJ_VAL(allocateString(name->start,
											   name->length)));
}

static bool identifiersEqual(Token *a, Token *b)
{
	if (a->length != b->length)
		return false;
	return memcmp(a->start, b->start, a->length) == 0;
}

// Walk the local array backward, to find the latest var with the identifier
// @param name
static int resolveLocal(Compiler *compiler, Token *name)
{
	for (int i = compiler->localCount - 1; i >= 0; i--)
	{
		Local *local = &compiler->locals[i];
		if (identifiersEqual(name, &local->name))
		{
			if (local->depth == -1)
			{
				error("Can't read local variable in its own initializer.");
			}
			return i;
		}
	}

	return -1;
}

static int addUpvalue(Compiler *compiler, uint8_t index,
					  bool isLocal)
{
	int upvalueCount = compiler->function->upvalueCount;

	// Check if the upvalue already exists.
	for (int i = 0; i < upvalueCount; i++)
	{
		Upvalue *upvalue = &compiler->upvalues[i];
		if (upvalue->index == index && upvalue->isLocal == isLocal)
		{
			return i;
		}
	}

	if (upvalueCount == UINT8_COUNT)
	{
		error("Too many closure variables in function.");
		return 0;
	}

	compiler->upvalues[upvalueCount].isLocal = isLocal;
	compiler->upvalues[upvalueCount].index = index;
	return compiler->function->upvalueCount++;
}

static int resolveUpvalue(Compiler *compiler, Token *name)
{
	if (compiler->enclosing == NULL)
		return -1;

	int local = resolveLocal(compiler->enclosing, name);
	if (local != -1)
	{
		return addUpvalue(compiler, (uint8_t)local, true);
	}

	int upvalue = resolveUpvalue(compiler->enclosing, name);
	if (upvalue != -1)
	{
		return addUpvalue(compiler, (uint8_t)upvalue, false);
	}

	return -1;
}

static void addLocal(Token name)
{
	// TODO: increase this limit.
	if (current->localCount == UINT8_COUNT)
	{
		error("Too many local variables in function.");
		return;
	}

	Local *local = &current->locals[current->localCount++];
	local->name = name;

	// local->depth = current->scopeDepth;

	// Declaration and initialization happen in two steps.
	// First we assign the depth a sentinel value.
	local->depth = -1;
}

// Declare local variables.
// TODO: Rename
static void declareVariable()
{
	if (current->scopeDepth == 0)
		return;

	Token *name = &parser.previous;

	for (int i = current->localCount - 1; i >= 0; i--)
	{
		Local *local = &current->locals[i];

		if (local->depth != -1 && local->depth < current->scopeDepth)
		{
			break;
		}

		if (identifiersEqual(&local->name, name))
		{
			error("Already a variable with this name in this scope.");
		}
	}

	addLocal(*name);
}

static void grouping(bool canAssign)
{
	expression();
	consume(TOKEN_RIGHT_PAREN, "Expect ')' after expression.");
}

static void binary(bool canAssign)
{
	TokenType operatorType = parser.previous.type;
	ParseRule *rule = getRule(operatorType);
	parsePrecedence((Precedence)(rule->precedence + 1));

	switch (operatorType)
	{
	case TOKEN_PLUS:
		emitByte(OP_ADD);
		break;
	case TOKEN_MINUS:
		emitByte(OP_SUBTRACT);
		break;
	case TOKEN_STAR:
		emitByte(OP_MULTIPLY);
		break;
	case TOKEN_SLASH:
		emitByte(OP_DIVIDE);
		break;
	case TOKEN_BANG_EQUAL:
		emitBytes(OP_EQUAL, OP_NOT);
		break;
	case TOKEN_EQUAL_EQUAL:
		emitByte(OP_EQUAL);
		break;
	case TOKEN_GREATER:
		emitByte(OP_GREATER);
		break;
	case TOKEN_GREATER_EQUAL:
		emitBytes(OP_LESS, OP_NOT);
		break;
	case TOKEN_LESS:
		emitByte(OP_LESS);
		break;
	case TOKEN_LESS_EQUAL:
		emitBytes(OP_GREATER, OP_NOT);
		break;
	default:
		return; // Unreachable.
	}
}

static uint8_t argumentList()
{
	uint8_t argCount = 0;
	if (!check(TOKEN_RIGHT_PAREN))
	{
		do
		{
			expression();

			if (argCount == 255)
			{
				error("Can't have more than 255 arguments.");
			}

			argCount++;
		} while (match(TOKEN_COMMA));
	}
	consume(TOKEN_RIGHT_PAREN, "Expect ')' after arguments.");
	return argCount;
}

static void literal(bool canAssign)
{
	switch (parser.previous.type)
	{
	case TOKEN_FALSE:
		emitByte(OP_FALSE);
		break;
	case TOKEN_NIL:
		emitByte(OP_NIL);
		break;
	case TOKEN_TRUE:
		emitByte(OP_TRUE);
		break;
	default:
		return; // Unreachable.
	}
}

static void call(bool canAssign)
{
	uint8_t argCount = argumentList();
	emitBytes(OP_CALL, argCount);
}

static void number(bool canAssign)
{
	double value = strtod(parser.previous.start, NULL);
	emitConstant(NUMBER_VAL(value));
}

static void string(bool canAssign)
{
	emitConstant(OBJ_VAL(allocateString(parser.previous.start + 1,
										parser.previous.length - 2)));
}

static void namedVariable(Token name, bool canAssign)
{
	int8_t getOp, setOp;
	int arg = resolveLocal(current, &name);

	if (arg != -1)
	{
		getOp = OP_GET_LOCAL;
		setOp = OP_SET_LOCAL;
	}
	else if ((arg = resolveUpvalue(current, &name)) != -1)
	{
		getOp = OP_GET_UPVALUE;
		setOp = OP_SET_UPVALUE;
	}
	else
	{
		// This is a global variable.
		arg = identifierConstant(&name);
		getOp = OP_GET_GLOBAL;
		setOp = OP_SET_GLOBAL;
	}

	if (canAssign && match(TOKEN_EQUAL))
	{
		expression();
		emitBytes(setOp, (uint8_t)arg);
	}
	else
	{
		emitBytes(getOp, (uint8_t)arg);
	}
}

static void variable(bool canAssign)
{
	namedVariable(parser.previous, canAssign);
}

static void unary(bool canAssign)
{
	TokenType operatorType = parser.previous.type;

	// Compile the operand.
	parsePrecedence(PREC_ASSIGNMENT);

	// Emit the operator instruction.
	switch (operatorType)
	{
	case TOKEN_BANG:
		emitByte(OP_NOT);
		break;
	case TOKEN_MINUS:
		emitByte(OP_NEGATE);
		break;
	default:
		return; // Unreachable.
	}
}

static void and_(bool canAssign)
{
	int endJump = emitJump(OP_JUMP_IF_FALSE);

	emitByte(OP_POP);
	parsePrecedence(PREC_AND);

	patchJump(endJump);
}

// This can definitely be improved.
static void or_(bool canAssign)
{
	int elseJump = emitJump(OP_JUMP_IF_FALSE);
	int endJump = emitJump(OP_JUMP);

	patchJump(elseJump);
	emitByte(OP_POP);

	parsePrecedence(PREC_OR);
	patchJump(endJump);
}

ParseRule rules[] = {
	[TOKEN_LEFT_PAREN] = {grouping, call, PREC_CALL},
	[TOKEN_RIGHT_PAREN] = {NULL, NULL, PREC_NONE},
	[TOKEN_LEFT_BRACE] = {NULL, NULL, PREC_NONE},
	[TOKEN_RIGHT_BRACE] = {NULL, NULL, PREC_NONE},
	[TOKEN_COMMA] = {NULL, NULL, PREC_NONE},
	[TOKEN_DOT] = {NULL, NULL, PREC_NONE},
	[TOKEN_MINUS] = {unary, binary, PREC_TERM},
	[TOKEN_PLUS] = {NULL, binary, PREC_TERM},
	[TOKEN_SEMICOLON] = {NULL, NULL, PREC_NONE},
	[TOKEN_SLASH] = {NULL, binary, PREC_FACTOR},
	[TOKEN_STAR] = {NULL, binary, PREC_FACTOR},
	[TOKEN_BANG] = {unary, NULL, PREC_NONE},
	[TOKEN_BANG_EQUAL] = {NULL, binary, PREC_EQUALITY},
	[TOKEN_EQUAL] = {NULL, NULL, PREC_NONE},
	[TOKEN_EQUAL_EQUAL] = {NULL, binary, PREC_EQUALITY},
	[TOKEN_GREATER] = {NULL, binary, PREC_COMPARISON},
	[TOKEN_GREATER_EQUAL] = {NULL, binary, PREC_COMPARISON},
	[TOKEN_LESS] = {NULL, binary, PREC_COMPARISON},
	[TOKEN_LESS_EQUAL] = {NULL, binary, PREC_COMPARISON},
	[TOKEN_IDENTIFIER] = {variable, NULL, PREC_NONE},
	[TOKEN_STRING] = {string, NULL, PREC_NONE},
	[TOKEN_NUMBER] = {number, NULL, PREC_NONE},
	[TOKEN_AND] = {NULL, and_, PREC_AND},
	[TOKEN_CLASS] = {NULL, NULL, PREC_NONE},
	[TOKEN_ELSE] = {NULL, NULL, PREC_NONE},
	[TOKEN_FALSE] = {literal, NULL, PREC_NONE},
	[TOKEN_FOR] = {NULL, NULL, PREC_NONE},
	[TOKEN_FUN] = {NULL, NULL, PREC_NONE},
	[TOKEN_IF] = {NULL, NULL, PREC_NONE},
	[TOKEN_NIL] = {literal, NULL, PREC_NONE},
	[TOKEN_OR] = {NULL, or_, PREC_OR},
	[TOKEN_PRINT] = {NULL, NULL, PREC_NONE},
	[TOKEN_RETURN] = {NULL, NULL, PREC_NONE},
	[TOKEN_SUPER] = {NULL, NULL, PREC_NONE},
	[TOKEN_THIS] = {NULL, NULL, PREC_NONE},
	[TOKEN_TRUE] = {literal, NULL, PREC_NONE},
	[TOKEN_VAR] = {NULL, NULL, PREC_NONE},
	[TOKEN_WHILE] = {NULL, NULL, PREC_NONE},
	[TOKEN_ERROR] = {NULL, NULL, PREC_NONE},
	[TOKEN_EOF] = {NULL, NULL, PREC_NONE},
};

static void parsePrecedence(Precedence precedence)
{
	advance();
	ParseFn prefixRule = getRule(parser.previous.type)->prefix;
	if (prefixRule == NULL)
	{
		error("Expect expression.");
		return;
	}

	bool canAssign = precedence <= PREC_ASSIGNMENT;
	prefixRule(canAssign);

	while (precedence <= getRule(parser.current.type)->precedence)
	{
		advance();
		ParseFn infixRule = getRule(parser.previous.type)->infix;
		infixRule(canAssign);
	}

	if (canAssign && match(TOKEN_EQUAL))
	{
		error("Invalid assignment target.");
	}
}

// Parse the current identifier, and put it in the constants table. Return the index.
static uint8_t parseVariable(char *errorMessage)
{
	consume(TOKEN_IDENTIFIER, errorMessage);

	declareVariable();
	if (current->scopeDepth > 0)
		return 0;

	return identifierConstant(&(parser.previous));
}

static void markInitialized()
{
	// global scope. These variables are late bound.
	if (current->scopeDepth == 0)
		return;

	// set the depth of the last local variable to the current scope depth.
	current->locals[current->localCount - 1].depth =
		current->scopeDepth;
}

// Takes the index of the global variable identifier in the globals table.
static void defineVariable(uint8_t global)
{
	// local variable
	if (current->scopeDepth > 0)
	{
		markInitialized();
		return;
	}

	emitBytes(OP_DEFINE_GLOBAL, global);
}

static ParseRule *getRule(TokenType type)
{
	return &rules[type];
}

static void expression()
{
	parsePrecedence(PREC_ASSIGNMENT);
}

static void block()
{
	while (!check(TOKEN_RIGHT_BRACE) && !check(TOKEN_EOF))
	{
		declaration();
	}

	consume(TOKEN_RIGHT_BRACE, "Expect '}' after block.");
}

static void function(FunctionType type)
{
	Compiler compiler;
	initCompiler(&compiler, type);
	// No end scope
	beginScope();

	consume(TOKEN_LEFT_PAREN, "Expect '(' after function name.");

	if (!check(TOKEN_RIGHT_PAREN))
	{
		do
		{
			current->function->arity++;
			if (current->function->arity > 255)
			{
				errorAtCurrent("Can't have more than 255 parameters.");
			}
			uint8_t constant = parseVariable("Expect parameter name.");
			defineVariable(constant);
		} while (match(TOKEN_COMMA));
	}

	consume(TOKEN_RIGHT_PAREN, "Expect ')' after parameters.");
	consume(TOKEN_LEFT_BRACE, "Expect '{' before function body.");
	block();

	ObjFunction *function = endCompiler();
	emitBytes(OP_CLOSURE, makeConstant(OBJ_VAL(function)));

	for (int i = 0; i < function->upvalueCount; i++)
	{
		emitByte(compiler.upvalues[i].isLocal ? 1 : 0);
		emitByte(compiler.upvalues[i].index);
	}
}

/*
static void method()
{

static void classDeclaration()
{
	consume(TOKEN_IDENTIFIER, "Expect class name.");
}
}*/

static void funDeclaration()
{
	uint8_t global = parseVariable("Expect function name.");

	// Mark the function's name as initialized, before we compile the body.
	// This allows the function to recursively call itself, without error.
	markInitialized();
	function(TYPE_FUNCTION);
	defineVariable(global);
}

static void varDeclaration()
{
	uint8_t global = parseVariable("Expect variable name.");

	if (match(TOKEN_EQUAL))
	{
		expression();
	}
	else
	{
		emitByte(OP_NIL);
	}

	consume(TOKEN_SEMICOLON,
			"Expect ';' after variable declaration.");

	defineVariable(global);
}

static void expressionStatement()
{
	expression();
	consume(TOKEN_SEMICOLON, "Expect ';' after expression.");
	emitByte(OP_POP);
}

static void forStatement()
{
	beginScope();
	consume(TOKEN_LEFT_PAREN, "Expect '(' after 'for'.");

	// [start] Initializer
	if (match(TOKEN_SEMICOLON))
	{
		// No initializer.
	}
	else if (match(TOKEN_VAR))
	{
		varDeclaration();
	}
	else
	{
		expressionStatement();
	}
	// [end] Initializer

	int loopStart = currentChunk()->count;

	// [start] Condition
	int exitJump = -1;
	if (!match(TOKEN_SEMICOLON))
	{
		expression();
		consume(TOKEN_SEMICOLON, "Expect ';' after loop condition.");

		// Jump out of the loop if the condition is false.
		exitJump = emitJump(OP_JUMP_IF_FALSE);
		emitByte(OP_POP); // Condition.
	}
	// [end] Condition

	// [start] Increment
	if (!match(TOKEN_RIGHT_PAREN))
	{
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

	// [end] Increment

	// Exit loop
	if (exitJump != -1)
	{
		patchJump(exitJump);
		emitByte(OP_POP); // Condition.
	}

	endScope();
}

static void ifStatement()
{
	consume(TOKEN_LEFT_PAREN, "Expect '(' after 'if'.");
	expression();
	consume(TOKEN_RIGHT_PAREN, "Expect ')' after condition."); // [paren]

	// Returns Opcode Address
	int thenJump = emitJump(OP_JUMP_IF_FALSE);

	// Remove condition from stack, each stmt has 0 effect on the stack size.
	emitByte(OP_POP);

	statement();

	// After the last statement in the then branch, we must skip the else branch.
	int elseJump = emitJump(OP_JUMP);

	patchJump(thenJump);

	// Remove condition from stack.
	emitByte(OP_POP);

	if (match(TOKEN_ELSE))
		statement();

	patchJump(elseJump);
}

static void printStatement()
{
	expression();
	consume(TOKEN_SEMICOLON, "Expect ';' after value.");
	emitByte(OP_PRINT);
}

static void returnStatement()
{
	if (current->type == TYPE_SCRIPT)
	{
		error("Can't return from top-level code.");
	}

	if (match(TOKEN_SEMICOLON))
	{
		emitReturn();
	}
	else
	{
		expression();
		consume(TOKEN_SEMICOLON, "Expect ';' after return value.");
		emitByte(OP_RETURN);
	}
}

static void whileStatement()
{
	int loopStart = currentChunk()->count;

	consume(TOKEN_LEFT_PAREN, "Expect '(' after 'while'.");
	expression();
	consume(TOKEN_RIGHT_PAREN, "Expect ')' after condition.");

	// exit loop if condition is false.
	int exitJump = emitJump(OP_JUMP_IF_FALSE);

	// condition was truthy, but we re-evaluate on every loop.
	emitByte(OP_POP);
	statement();

	emitLoop(loopStart);

	patchJump(exitJump);

	// remove condtion value from the top of the stack.
	emitByte(OP_POP);
}

// TODO:
static void synchronize()
{
	parser.panicMode = false;

	while (parser.current.type != TOKEN_EOF)
	{
		if (parser.previous.type == TOKEN_SEMICOLON)
			return;
		switch (parser.current.type)
		{
		case TOKEN_CLASS:
		case TOKEN_FUN:
		case TOKEN_VAR:
		case TOKEN_FOR:
		case TOKEN_IF:
		case TOKEN_WHILE:
		case TOKEN_PRINT:
		case TOKEN_RETURN:
			return;

		default:; // Do nothing.
		}

		advance();
	}
}

static void declaration()
{
	if (match(TOKEN_FUN))
	{
		funDeclaration();
	}
	else if (match(TOKEN_VAR))
	{
		varDeclaration();
	}
	else
	{
		statement();
	}

	if (parser.panicMode)
		synchronize();
}

static void statement()
{
	if (match(TOKEN_PRINT))
	{
		printStatement();
	}
	else if (match(TOKEN_FOR))
	{
		forStatement();
	}
	else if (match(TOKEN_IF))
	{
		ifStatement();
	}
	else if (match(TOKEN_RETURN))
	{
		returnStatement();
	}
	else if (match(TOKEN_WHILE))
	{
		whileStatement();
	}
	else if (match(TOKEN_LEFT_BRACE))
	{
		beginScope();
		block();
		endScope();
	}
	else
	{
		expressionStatement();
	}
}

ObjFunction *compile(const char *source)
{
	initScanner(source);

	Compiler compiler;
	initCompiler(&compiler, TYPE_SCRIPT);

	parser.hadError = false;
	parser.panicMode = false;

	advance();

	while (!match(TOKEN_EOF))
	{
		declaration();
	}

	ObjFunction *function = endCompiler();
	return parser.hadError ? NULL : function;
}
