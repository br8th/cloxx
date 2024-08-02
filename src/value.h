#ifndef clox_value_h
#define clox_value_h

#include "common.h"

// types supported by the Lox VM
typedef enum
{
	VAL_BOOL,
	VAL_NIL,
	VAL_NUMBER,
	VAL_OBJ,
} ValueType;

typedef struct Obj Obj;
typedef struct ObjString ObjString;

typedef struct
{
	ValueType type;
	union
	{
		double number;
		bool boolean;
		Obj *obj;
	} as;
} Value;


#define BOOL_VAL(value)   ((Value){VAL_BOOL, {.boolean = value}})
#define NIL_VAL           ((Value){VAL_NIL, {.number = 0}})
#define NUMBER_VAL(value) ((Value){VAL_NUMBER, {.number = value}})
#define OBJ_VAL(object) ((Value){VAL_OBJ, {.obj = (Obj*) object}})

#define AS_BOOL(val) (val.as.boolean)
#define AS_NUMBER(val) (val.as.number)
#define AS_OBJ(val) (val.as.obj)

#define IS_BOOL(value)    ((value).type == VAL_BOOL)
#define IS_NIL(value)     ((value).type == VAL_NIL)
#define IS_NUMBER(value)  ((value).type == VAL_NUMBER)
#define IS_OBJ(value)  ((value).type == VAL_OBJ)

typedef struct
{
	int capacity;
	int count;
	Value *values;
} ValueArray;

bool valuesEqual(Value a, Value b);
void initValueArray(ValueArray *array);
void writeValueArray(ValueArray *array, Value value);
void freeValueArray(ValueArray *array);
void printValue(Value value);

#endif