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
		ObjString *str = (ObjString *)object;
		FREE_ARRAY(char, str->chars, str->length + 1);
		FREE(ObjString, object); // TODO: Is this equivalent to free str?
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
