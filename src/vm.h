#ifndef clox_vm_h
#define clox_vm_h

#include "object.h"
#include "table.h"

#define FRAMES_MAX 64
#define STACK_MAX (FRAMES_MAX * UINT8_COUNT)

typedef struct
{
	ObjFunction *function;
	uint8_t *ip;
	Value *slots; // Base of the function's stack frame
} CallFrame;

typedef struct
{
	CallFrame frames[FRAMES_MAX];
	int frameCount;

	Value stack[STACK_MAX];
	Value *stackTop;
	Obj *objects;
	Table globals;
	Table strings;
} VM;

typedef enum
{
	INTERPRET_OK,
	INTERPRET_COMPILE_ERROR,
	INTERPRET_RUNTIME_ERROR
} InterpretResult;

extern VM vm;

void initVM();
InterpretResult interpret(const char *source);
void freeVM();

void push(Value value);
Value pop();

#endif