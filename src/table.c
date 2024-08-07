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
		if (entry->key == NULL)
		{
			// The entry is either empty or a tombstone.
			if (IS_NIL(entry->value))
			{
				// This is truly an empty entry.
				// If we've found a tombstone before, reuse it instead.
				return tombstone != NULL ? tombstone : entry;
			}
			else
			{
				// We found a tombstone. Note that we don't return yet.
				if (tombstone == NULL)
					tombstone = entry;
			}
		}
		else if (entry->key == key)
		{
			// We found the key.
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

ObjString *tableFindString(Table *table, const char *chars, int length,
						   uint32_t hash)
{
	if (table->count == 0)
		return NULL;

	uint32_t index = hash % table->capacity;
	for (;;)
	{
		Entry *entry = &table->entries[index];
		if (entry->key == NULL)
		{
			// Stop if we find an empty non-tombstone entry.
			if (IS_NIL(entry->value))
				return NULL;
		}
		else if (entry->key->length == length &&
				 entry->key->hash == hash &&
				 memcmp(entry->key->chars, chars, length) == 0)
		{
			// We found it.
			return entry->key;
		}

		index = (index + 1) % table->capacity;
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

	table->count = 0;

	// Walk old list, inserting all it's non-null values into new list
	for (int i = 0; i < table->capacity; i++)
	{
		Entry *oldEntry = &table->entries[i];
		if (oldEntry->key == NULL)
			continue;

		Entry *dest = findEntry(newEntries, newCapacity, oldEntry->key);
		dest->key = oldEntry->key;
		dest->value = oldEntry->value;

		table->count++;
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

	// Only increase count if we are not inserting into a tombstone.
	if (isNewKey && IS_NIL(entry->value))
		table->count++;

	entry->key = key;
	entry->value = value;
	return isNewKey;
}

bool tableDelete(Table *table, ObjString *key)
{
	if (table->count == 0)
		return false;

	// Find the entry.
	Entry *entry = findEntry(table->entries, table->capacity, key);
	if (entry->key == NULL)
		return false;

	// Place a tombstone in the entry.
	entry->key = NULL;
	entry->value = BOOL_VAL(true);
	return true;
}