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

#define LOAD_FACTOR 0.6
#define KEY_SIZE    16
#define INIT_SIZE   8
#define NLONG       (KEY_SIZE/(sizeof (unsigned int)))

struct Entry_s {
	bool in_use;
	const void *key;
	void *val;
};
typedef struct Entry_s Entry;

struct SDHashTable_s {
	SDHashFn hash_fn;
	SDEqualFn equal_fn;
	SDConstDerefFn key_removed_fn;
	SDDerefFn value_removed_fn;
	uint8_t k1[KEY_SIZE];
	uint8_t k2[KEY_SIZE];
	size_t size;
	size_t tbl_size;
	Entry *tbl;
	int32_t refcount;
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

	for (size_t i = 0; i < tbl_size*16; i++) {
		size_t hash = (h1 + i) % tbl_size;
		printf("hash: %zu (%zu + %zu) %% %zu)\n", hash, h1, i, tbl_size);
		Entry *entry = &tbl[hash];
		if (!entry->in_use || ht->equal_fn(key, entry->key)) {
			printf("\tOK\n");
			return hash;
		}
	}

	return -1;
}

static uint64_t
hash_long(uint8_t *k, const void *vkey)
{
	const long key = (long)vkey;

	uint8_t key_buf[sizeof(long)];
	memcpy(key_buf, &key, sizeof(key_buf));

	// FNV 1-a hash function
#define FNV_offset_basis 0xcbf29ce484222325
#define FNV_prime        0x100000001b3

	uint64_t hash = FNV_offset_basis;
	for (size_t i = 0; i < sizeof(long); i++) {
		hash ^= key_buf[i];
		hash *= FNV_prime;
	}
	for (size_t i = 0; i < 16; i++) {
		hash ^= k[i];
		hash *= FNV_prime;
	}

	return hash;
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
		  SDConstDerefFn key_removed_fn,
		  SDDerefFn value_removed_fn)
{
	SDHashTable *ht = calloc(1, sizeof(*ht));
	if (!ht)
		return NULL;

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
		rand_buf[i] = arc4random();
	}
	memcpy(ht->k1, &rand_buf, KEY_SIZE);

	for (size_t i = 0; i < NLONG; i++) {
		rand_buf[i] = arc4random();
	}
	memcpy(ht->k2, &rand_buf, KEY_SIZE);

	ht->tbl_size = INIT_SIZE;
	ht->tbl = calloc(INIT_SIZE, sizeof(Entry));
	if (!ht->tbl) {
		free(ht);
		return NULL;
	}

	ht->refcount = 1;

	return ht;
}

SDHashTable *
sd_hash_table_ref(SDHashTable *ht)
{
	__sync_fetch_and_add(&ht->refcount, 1);
	return ht;
}

void
sd_hash_table_unref(SDHashTable *ht)
{
	if (!ht)
		return;
	if (__sync_sub_and_fetch(&ht->refcount, 1) == 0) {
		free(ht->tbl);
		free(ht);
	}
}

static void
ht_double_table(SDHashTable *ht)
{
	size_t old_size = ht->size;
	Entry *old_tbl = ht->tbl;
	size_t old_tbl_size = ht->tbl_size;

	fprintf(stderr, "table_double to %zu!\n", ht->tbl_size*2);

	ht->size = 0;
	ht->tbl_size *= 2;
	ht->tbl = calloc(ht->tbl_size, sizeof(Entry));
	if (!ht->tbl)
		sd_die("failed to alloc for table doubling\n");

	for (size_t i = 0; i < old_tbl_size; i++) {
		if (!old_tbl[i].in_use)
			continue;

		const void *key = old_tbl[i].key;
		void *val = old_tbl[i].val;

		sd_hash_table_insert(ht, key, val);
	}

	free(old_tbl);

	if (ht->size != old_size)
		sd_die("expected size to be invariant after doubling\n");
}

void
sd_hash_table_insert(SDHashTable *ht, const void *key, void *val)
{
	if (!ht)
		return;

	ssize_t i = ht_index(ht, key);
	if (i < 0)
		sd_die("insert: expected ht_index to be gte 0\n");

	ht->tbl[i].key = key;
	ht->tbl[i].val = val;

	if (!ht->tbl[i].in_use) {
		ht->tbl[i].in_use = true;
		ht->size++;

		if (((float)(ht->size + 1))/ht->tbl_size > LOAD_FACTOR)
			ht_double_table(ht);
	}
}

void *
sd_hash_table_lookup(SDHashTable *ht, const void *key, bool *ok)
{
	bool found = false;
	void *val = NULL;

	ssize_t i = ht_index(ht, key);
	if (i < 0)
		sd_die("lookup: expected ht_index to be gte 0\n");

	if (ht->tbl[i].in_use) {
		found = true;
		val = ht->tbl[i].val;
	}

	if (ok)
		*ok = found;
	return val;
}

void
sd_hash_table_remove(SDHashTable *ht, const void *key)
{
	ssize_t i = ht_index(ht, key);
	if (i < 0)
		sd_die("remove: expected ht_index to be gte 0\n");

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
		sd_die("contians: expected ht_index to be gte 0\n");

	return ht->tbl[i].in_use;
}

size_t
sd_hash_table_size(SDHashTable *ht) {
	return ht->size;
}

void
sd_hash_table_iter_init(SDHashTableIter *it, SDHashTable *ht)
{
	if (!it || !ht)
		return;

	memset(it, 0, sizeof(*it));

	it->ht = ht;
	it->i = 0;
	it->size = ht->size;
	it->tbl_size = ht->tbl_size;
}

bool
sd_hash_table_iter_next(SDHashTableIter *it, SDHashKey *key, SDHashVal *val)
{
	if (!it || !it->ht)
		return false;

	SDHashTable *ht = it->ht;
	if (it->size != ht->size || it->tbl_size != ht->tbl_size) {
		fprintf(stderr, "WARNING: hash table modified while iterating, this may cause crashes.\n");
		return false;
	}

	if (it->i >= it->tbl_size)
		return false;

	size_t tbl_size = it->tbl_size;
	for (; it->i < tbl_size; it->i++) {
		Entry *entry = &ht->tbl[it->i];
		if (!entry->in_use)
			continue;
		if (key)
			*key = entry->key;
		if (val)
			*val = entry->val;

		it->i++;
		return true;
	}
	return false;
}
