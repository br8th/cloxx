#include <stdlib.h>
#include <string.h>

#include "memory.h"
#include "object.h"
#include "table.h"
#include "value.h"

#define TABLE_MAX_LOAD 0.75

void initTable(Table *table)
{
	table->count = 0;
	table->capacity = 0;
	table->entries = NULL;
}

void freeTable(Table *table)
{
	FREE_ARRAY(Entry, table->entries, table->capacity);
	initTable(table);
}

static Entry *findEntry(Entry *entries, int capacity, ObjString *key)
{
	uint32_t index = key->hash % capacity;
	Entry *tombstone = NULL;

	for (;;)
	{
		Entry *entry = &entries[index];
		if (entry->key == key || entry->key == NULL)
		{
			return entry;
		}

		index = (index + 1) % capacity;
	}
}

bool tableGet(Table *table, ObjString *key, Value *value)
{
	if (table->count == 0)
		return false;

	Entry *entry = findEntry(table->entries, table->capacity, key);
	if (entry->key == NULL)
		return false;

	*value = entry->value;
	return true;
}

void tableAddAll(Table *from, Table *to)
{
	for (int i = 0; i < from->capacity; i++)
	{
		Entry *entry = &from->entries[i];
		if (entry->key != NULL)
		{
			tableSet(to, entry->key, entry->value);
		}
	}
}

static void adjustCapacity(Table *table, int newCapacity)
{
	Entry *newEntries = ALLOCATE(Entry, newCapacity);
	for (int i = 0; i < newCapacity; i++)
	{
		newEntries[i].key = NULL;
		newEntries[i].value = NIL_VAL;
	}

	// Walk old list, inserting all it's non-null values into new list
	for (int i = 0; i < table->capacity; i++)
	{
		Entry *oldEntry = &table->entries[i];
		if (oldEntry->key == NULL)
			continue;

		Entry *dest = findEntry(newEntries, newCapacity, oldEntry->key);
		dest->key = oldEntry->key;
		dest->value = oldEntry->value;
	}

	FREE_ARRAY(Entry, table->entries, table->capacity);

	table->entries = newEntries;
	table->capacity = newCapacity;
}

bool tableSet(Table *table, ObjString *key, Value value)
{
	if (table->count + 1 > table->capacity * TABLE_MAX_LOAD)
	{
		int capacity = GROW_CAPACITY(table->capacity);
		adjustCapacity(table, capacity);
	}

	Entry *entry = findEntry(table->entries, table->capacity, key);

	bool isNewKey = entry->key == NULL;
	if (isNewKey)
		table->count++;

	entry->key = key;
	entry->value = value;
	return isNewKey;
}

bool tableDelete(Table *table, ObjString *key)
{
}