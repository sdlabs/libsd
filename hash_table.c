// Copyright 2016 Bobby Powers. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#include <stdlib.h>
#include <string.h>

#include "hash_table.h"

int siphash(const uint8_t *in, const size_t inlen, const uint8_t *k,
            uint8_t *out, const size_t outlen);

#define KEY_SIZE  16
#define INIT_SIZE 8

struct SDHashTable_s {
	SDHashFn hash_fn;
	SDEqualFn equal_fn;
	SDDerefFn key_removed_fn;
	SDDerefFn value_removed_fn;
	uint8_t k[KEY_SIZE];
	size_t size;
	size_t asize;
};

#define NLONG (KEY_SIZE/(sizeof (unsigned int)))

SDHashTable *
sd_hash_table_new(SDHashFn hash_fn,
		  SDEqualFn equal_fn,
		  SDDerefFn key_removed_fn,
		  SDDerefFn value_removed_fn)
{
	SDHashTable *ht = calloc(1, sizeof(*ht));

	ht->hash_fn = hash_fn;
	ht->equal_fn = equal_fn;
	ht->key_removed_fn = key_removed_fn;
	ht->value_removed_fn = value_removed_fn;

	unsigned long rand_buf[NLONG];

	for (size_t i = 0; i < NLONG; i++) {
		rand_buf[i] = random();
	}
	memcpy(ht->k, &rand_buf, KEY_SIZE);

	return NULL;
}

void
sd_hash_table_insert(SDHashTable *ht, const void *key, const void *val)
{
}

void *
sd_hash_table_lookup(SDHashTable *ht, const void *key, bool *ok)
{
	bool found = false;

	if (ok)
		*ok = found;
	return NULL;
}

void
sd_hash_table_remove(SDHashTable *ht, const void *key)
{
}

bool
sd_hash_table_contains(SDHashTable *ht, const void *key)
{
	return false;
}

size_t
sd_hash_table_size(SDHashTable *ht) {
	return ht->size;
}
