#ifndef clox_value_h
#define clox_value_h

#include "common.h"

// types supported by the Lox VM
typedef enum
{
	VAL_BOOL,
	VAL_NIL,
	VAL_NUMBER,
} ValueType;

typedef struct
{
	ValueType type;
	union
	{
		double number;
		bool boolean;
	} as;
} Value;

typedef struct
{
	int capacity;
	int count;
	Value *values;
} ValueArray;

void initValueArray(ValueArray *array);
void writeValueArray(ValueArray *array, Value value);
void freeValueArray(ValueArray *array);
void printValue(Value value);

#endif