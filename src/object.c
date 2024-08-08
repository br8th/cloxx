#include <stdio.h>
#include <string.h>

#include "memory.h"
#include "object.h"
#include "table.h"
#include "vm.h"

#define ALLOCATE_OBJ(type, objectType) \
	(type *)allocateObject(sizeof(type), objectType)

void printObject(Value value)
{
	switch (OBJ_TYPE(value))
	{
	case OBJ_STRING:
		printf("%s", AS_CSTRING(value));
		break;
	}
}

static Obj *allocateObject(size_t size, ObjType type)
{
	Obj *object = (Obj *)reallocate(NULL, 0, size);
	object->type = type;

	// keep track of all objects for "GC"
	object->next = vm.objects;
	vm.objects = object;
	return object;
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