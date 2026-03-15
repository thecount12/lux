#include "lux.h"
#include "common.h"
#include "memory.h"
#include "object.h"
#include "table.h"
#include "value.h"

#define TABLE_MAX_LOAD 0.75

void 
initTable(Table* table) 
{
	table-> count = 0;
	table->capacity = 0;
	table->entries = nil;
}

void 
freeTable(Table* table) 
{
	reallocate(table->entries, sizeof(Entry) * table->capacity, 0);
	initTable(table);
}

static Entry* 
findEntry(Entry* entries, int capacity, ObjString* key) 
{
	// performance improvement
	// long index = key->hash % capacity;
	long index = key->hash & (capacity - 1);
	Entry* tombstone = nil;

	for (;;) {
		Entry* entry = &entries[index];
		if (entry->key == nil) {
			if (IS_NIL(entry->value)) {
				// Empty entry
				return tombstone != nil ? tombstone : entry;
			} else {
				// we found a tombstone.
				if (tombstone == nil) tombstone = entry;
			}
		} else if (entry->key == key) {
			// we found the kkey.
			return entry;
		}
	
	// performance improvement	
	// index = (index + 1) % capacity;
	index = (index + 1) & (capacity - 1);
	}
}

bool 
tableGet(Table* table, ObjString* key, Value* value) 
{
	if (table->count == 0) return false;
	Entry* entry = findEntry(table->entries, table->capacity, key);
	if (entry->key == nil) return false;

	*value = entry->value;
	return true;
}

static void 
adjustCapacity(Table* table, int capacity) 
{
	Entry* entries = ALLOCATE(Entry, capacity);
	for (int i = 0; i < capacity; i++) {
		entries[i].key = nil;
		entries[i].value = NIL_VAL;
	}
	
	table->count = 0;
	for (int i = 0; i < table->capacity; i++) {
		Entry* entry = &table->entries[i];
		if (entry->key == nil) continue;
		Entry* dest = findEntry(entries, capacity, entry->key);
		dest->key = entry->key;
		dest->value = entry->value;
		table->count++;
	}

	reallocate(table->entries, sizeof(Entry) * table->capacity, 0);
	table->entries = entries;
	table->capacity = capacity;
}

bool 
tableSet(Table* table, ObjString* key, Value value) 
{
	if (table->count + 1 > table->capacity * TABLE_MAX_LOAD) {
		int capacity = GROW_CAPACITY(table->capacity);
		adjustCapacity(table, capacity);
	}

	Entry* entry = findEntry(table->entries, table->capacity, key);
	bool isNewKey = entry->key == nil;
	if (isNewKey && IS_NIL(entry->value)) table->count++;

	entry->key = key;
	entry->value = value;
	return isNewKey;
}

bool 
tableDelete(Table* table, ObjString* key) 
{
	if (table->count == 0) return false;
	
	// find entry
	Entry* entry = findEntry(table->entries, table->capacity, key);
	if (entry->key == nil) return false;

	// place a tombstone in the entry
	entry->key = nil;
	entry->value = BOOL_VAL(true);
	return true;
}

void 
tableAddAll(Table* from, Table* to) 
{
	for (int i=0; i < from->capacity; i++) {
		Entry* entry = &from-> entries[i];
		if (entry->key != nil) {
			tableSet(to, entry->key, entry->value);
		}
	}
}

ObjString* 
tableFindString(Table* table, const char* chars, int length, unsigned long hash) 
{
	if (table->count == 0) return nil;
	
	// performance improvement
	// unsigned long index = hash % table->capacity;
	unsigned long index = hash & (table->capacity - 1);
	for (;;) {
		Entry* entry = &table->entries[index];
		if (entry->key == nil) {
			// stop if we find an empty non-tombstone entry.
			if (IS_NIL(entry->value)) return nil;
		} else if (entry->key->length == length &&
			entry->key->hash == hash && 
			memcmp(entry->key->chars, chars, length) == 0) {
			// we found it
			return entry->key;
		}
		// performance improvement
		// index = (index + 1) % table->capacity;
		index = (index + 1) & (table->capacity - 1);
	}
}

void 
tableRemoveWhite(Table* table)
{
	for (int i = 0; i < table->capacity; i++) {
		Entry* entry = &table->entries[i];
		if (entry->key != nil && !entry->key->obj.isMarked) {
			tableDelete(table,entry->key);
		}
	}
}

void 
markTable(Table* table)
{
	for (int i = 0; i < table->capacity; i++) {
		Entry* entry = &table->entries[i];
		if (entry->key != nil) {
			markObject((Obj*)entry->key);
			markValue(entry->value);
		}
	}
}

void 
tableForEach(Table* table, void (*fn)(ObjString* key, Value value, void* arg), void* arg)
{
	for (int i = 0; i < table->capacity; i++) {
		Entry* entry = &table->entries[i];
		if (entry->key != nil) {
			fn(entry->key, entry->value, arg);
		}
	}
}
