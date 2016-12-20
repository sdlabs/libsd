// Copyright 2016 Bobby Powers. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "hash_table.h"

int siphash(const uint8_t *in, const size_t inlen, const uint8_t *k,
            uint8_t *out, const size_t outlen);

typedef size_t (*SDHashFn) (uint8_t *k, const void *key);
typedef bool (*SDEqualFn) (const void *a, const void *b);

#define KEY_SIZE  16
#define INIT_SIZE 8
#define NLONG     (KEY_SIZE/(sizeof (unsigned int)))

struct Entry_s {
	bool in_use;
	const void *key;
	void *val;
};
typedef struct Entry_s Entry;

struct SDHashTable_s {
	SDHashFn hash_fn;
	SDEqualFn equal_fn;
	SDDerefFn key_removed_fn;
	SDDerefFn value_removed_fn;
	uint8_t k1[KEY_SIZE];
	uint8_t k2[KEY_SIZE];
	size_t size;
	size_t tbl_size;
	Entry *tbl;
};

void __attribute__((noreturn))
sd_die(const char *fmt, ...)
{
	va_list args;

	va_start(args, fmt);
	vfprintf(stderr, fmt, args);
	va_end(args);

	exit(EXIT_FAILURE);
}

ssize_t
ht_index(SDHashTable *ht, const void *key)
{
	assert(ht->size < ht->tbl_size);

	const size_t tbl_size = ht->tbl_size;
	Entry *tbl = ht->tbl;

	uint64_t h1 = ht->hash_fn(ht->k1, key);
	uint64_t h2 = ht->hash_fn(ht->k2, key);

	for (size_t i = 0; i < tbl_size*16; i++) {
		size_t hash = (h1 + i*h2) % tbl_size;
		//printf("hash: %zu (%zu + %zu * %zu) %% %zu)\n", hash, h1, i, h2, tbl_size);
		Entry *entry = &tbl[hash];
		if (!entry->in_use || ht->equal_fn(key, entry->key))
			return hash;
	}

	return -1;
}

static uint64_t
hash_long(uint8_t *k, const void *vkey)
{
	const long key = (long)vkey;

	uint8_t key_buf[sizeof(long)];
	memcpy(key_buf, &key, sizeof(key_buf));

	uint8_t hash_buf[8];
	memset(hash_buf, 0, sizeof(hash_buf));

	siphash(key_buf, sizeof(key_buf), k, hash_buf, sizeof(hash_buf));

	uint64_t result;
	memcpy(&result, hash_buf, sizeof(result));

	assert(sizeof(result) == sizeof(hash_buf));

	return result;
}

static bool
equal_long(const void *va, const void *vb)
{
	const long a = (long)va;
	const long b = (long)vb;

	return a == b;
}


SDHashTable *
sd_hash_table_new(SDHashTableType type,
		  SDDerefFn key_removed_fn,
		  SDDerefFn value_removed_fn)
{
	SDHashTable *ht = calloc(1, sizeof(*ht));

	switch (type) {
	case SD_HASH_LONG_KEY:
		ht->hash_fn = hash_long;
		ht->equal_fn = equal_long;
		break;
	case SD_HASH_STRING_KEY:
		fprintf(stderr, "WARNING: sd_hash_table_new with pointer key unimplemented\n");
		return NULL;
		break;
	case SD_HASH_POINTER_KEY:
		fprintf(stderr, "WARNING: sd_hash_table_new with pointer key unimplemented\n");
		return NULL;
		break;
	default:
		fprintf(stderr, "WARNING: sd_hash_table_new called with invalid type %d\n", type);
		return NULL;
	}

	ht->key_removed_fn = key_removed_fn;
	ht->value_removed_fn = value_removed_fn;

	unsigned long rand_buf[NLONG];

	for (size_t i = 0; i < NLONG; i++) {
		rand_buf[i] = random();
	}
	memcpy(ht->k1, &rand_buf, KEY_SIZE);

	for (size_t i = 0; i < NLONG; i++) {
		rand_buf[i] = random();
	}
	memcpy(ht->k2, &rand_buf, KEY_SIZE);

	ht->tbl_size = INIT_SIZE;
	ht->tbl = calloc(INIT_SIZE, sizeof(Entry));

	return ht;
}

void
sd_hash_table_insert(SDHashTable *ht, const void *key, void *val)
{
	if (!ht)
		return;

	// TODO: grow the table if above load factor
	
	ssize_t i = ht_index(ht, key);
	if (i < 0)
		sd_die("expected ht_index to be gte 0\n");

	if (!ht->tbl[i].in_use) {
		ht->tbl[i].in_use = true;
		ht->size++;
	}
	ht->tbl[i].key = key;
	ht->tbl[i].val = val;
}

void *
sd_hash_table_lookup(SDHashTable *ht, const void *key, bool *ok)
{
	bool found = false;
	void *val = NULL;

	ssize_t i = ht_index(ht, key);
	if (i < 0)
		sd_die("expected ht_index to be gte 0\n");

	if (!ht->tbl[i].in_use)
		goto out;

	found = true;
	val = ht->tbl[i].val;
out:
	if (ok)
		*ok = found;
	return val;
}

void
sd_hash_table_remove(SDHashTable *ht, const void *key)
{
	ssize_t i = ht_index(ht, key);
	if (i < 0)
		sd_die("expected ht_index to be gte 0\n");

	if (ht->tbl[i].in_use) {
		ht->tbl[i].in_use = false;
		const void *key = ht->tbl[i].key;
		void *val = ht->tbl[i].val;

		ht->tbl[i].key = NULL;
		ht->tbl[i].val = NULL;
		ht->size--;

		// TODO: shrink table

		if (ht->key_removed_fn)
			ht->key_removed_fn((SDHashKey)key);
		if (ht->key_removed_fn)
			ht->key_removed_fn(val);
	}
}

bool
sd_hash_table_contains(SDHashTable *ht, const void *key)
{
	ssize_t i = ht_index(ht, key);
	if (i < 0)
		sd_die("expected ht_index to be gte 0\n");

	return ht->tbl[i].in_use;
}

size_t
sd_hash_table_size(SDHashTable *ht) {
	return ht->size;
}
