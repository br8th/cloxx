#include <stdio.h>
#include <string.h>

#include "memory.h"
#include "object.h"
#include "table.h"
#include "vm.h"

#define ALLOCATE_OBJ(type, objectType) \
	(type *)allocateObject(sizeof(type), objectType)

static Obj *allocateObject(size_t size, ObjType type)
{
	Obj *object = (Obj *)reallocate(NULL, 0, size);
	object->type = type;

	// keep track of all objects for "GC"
	object->next = vm.objects;
	vm.objects = object;
	return object;
}

ObjClosure *newClosure(ObjFunction *function)
{
	ObjUpvalue **upvalues = ALLOCATE(ObjUpvalue *,
									 function->upvalueCount);

	for (int i = 0; i < function->upvalueCount; i++)
	{
		upvalues[i] = NULL;
	}

	ObjClosure *closure = ALLOCATE_OBJ(ObjClosure, OBJ_CLOSURE);
	closure->function = function;
	closure->upvalues = upvalues;
	closure->upvalueCount = function->upvalueCount;
	return closure;
}

ObjFunction *newFunction()
{
	ObjFunction *function = ALLOCATE_OBJ(ObjFunction, OBJ_FUNCTION);
	function->arity = 0;
	function->upvalueCount = 0;
	function->name = NULL;
	initChunk(&function->chunk);
	return function;
}

ObjNative *newNative(NativeFn function, int arity)
{
	ObjNative *native = ALLOCATE_OBJ(ObjNative, OBJ_NATIVE);
	native->function = function;
	native->arity = arity;
	return native;
}

// FNV-1a
static uint32_t hashString(const char *key, int length)
{
	uint32_t hash = 2166136261u;
	for (int i = 0; i < length; i++)
	{
		hash ^= (uint8_t)key[i];
		hash *= 16777619;
	}
	return hash;
}

// Create a string obj* from a char*, and interns it.
// Return the interned string if it already exists.
ObjString *allocateString(const char *chars, int length)
{
	uint32_t hash = hashString(chars, length);

	ObjString *interned = tableFindString(&vm.strings, chars, length, hash);
	if (interned != NULL)
		return interned;

	ObjString *string = (ObjString *)
		allocateObject(sizeof(ObjString) + length + 1, OBJ_STRING);

	memcpy(string->chars, chars, length);
	string->chars[length] = '\0';

	string->hash = hash;
	string->length = length;

	tableSet(&vm.strings, string, NIL_VAL);
	return string;
}

ObjUpvalue *newUpvalue(Value *slot)
{
	ObjUpvalue *upvalue = ALLOCATE_OBJ(ObjUpvalue, OBJ_UPVALUE);
	upvalue->location = slot;
	return upvalue;
}

static void printFunction(ObjFunction *function)
{
	if (function->name == NULL)
	{
		printf("<script>");
		return;
	}

	printf("<fn %s>", function->name->chars);
}

void printObject(Value value)
{
	switch (OBJ_TYPE(value))
	{
	case OBJ_STRING:
		printf("%s", AS_CSTRING(value));
		break;
		// Unused.
	case OBJ_UPVALUE:
		printf("upvalue");
		break;
	case OBJ_FUNCTION:
		printFunction(AS_FUNCTION(value));
		break;
	case OBJ_CLOSURE:
		printFunction(AS_CLOSURE(value)->function);
		break;
	case OBJ_NATIVE:
		printf("<native fn>");
		break;
	}
}