#include <stdlib.h>
#include "memory.h"
#include "object.h"
#include "vm.h"

void *reallocate(void *pointer, size_t oldSize, size_t newSize)
{
	if (newSize == 0)
	{
		free(pointer);
		return NULL;
	}

	void *result = realloc(pointer, newSize);

	if (result == NULL)
		exit(1);

	return result;
}

static void freeObject(Obj *object)
{
	switch (object->type)
	{
	case OBJ_STRING:
		// ObjString *str = (ObjString *)object;
		// FREE_ARRAY(char, str->chars, str->length + 1); // include '\0'
		FREE(ObjString, object);
		break;
	case OBJ_CLOSURE:
	{
		ObjClosure *closure = (ObjClosure *)object;

		FREE(ObjClosure, object);
		FREE_ARRAY(ObjUpvalue *, closure->upvalues,
				   closure->upvalueCount);
		break;
	}
	case OBJ_UPVALUE:
		FREE(ObjUpvalue, object);
		break;
	case OBJ_FUNCTION:
		ObjFunction *func = (ObjFunction *)object;
		freeChunk(&func->chunk);
		FREE(ObjFunction, object);
		break;
	case OBJ_NATIVE:
		FREE(ObjNative, object);
		break;

	default:
		break;
	}
}

void freeObjects()
{
	Obj *obj = vm.objects;

	while (obj != NULL)
	{
		Obj *next = obj->next;
		freeObject(obj);
		obj = next;
	}
}
