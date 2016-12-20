// Copyright 2016 Bobby Powers. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.
#ifndef LIBSD_HASH_TABLE_H
#define LIBSD_HASH_TABLE_H
#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "utf.h"

void sd_die(const char *, ...);

typedef struct SDHashTable_s SDHashTable;
typedef struct SDHashTableIter_s SDHashTableIter;

struct SDHashTableIter_s {
	SDHashTable *ht;
	size_t i;
	// for consistency checking
	size_t size;
	size_t tbl_size;
};

typedef enum {
	SD_HASH_LONG_KEY    = 1<<1,
	SD_HASH_STRING_KEY  = 1<<2,
	SD_HASH_POINTER_KEY = 1<<3,
} SDHashTableType;

typedef const void* SDHashKey;
typedef void* SDHashVal;
typedef void (*SDConstDerefFn) (const void *data);
typedef void (*SDDerefFn) (void *data);

SDHashTable *sd_hash_table_new(SDHashTableType type,
			       SDConstDerefFn key_removed_fn,
			       SDDerefFn value_removed_fn);
SDHashTable *sd_hash_table_ref(SDHashTable *ht);
void sd_hash_table_unref(SDHashTable *ht);

void sd_hash_table_insert(SDHashTable *ht, const void *key, void *val);
void *sd_hash_table_lookup(SDHashTable *ht, const void *key, bool *ok);
void sd_hash_table_remove(SDHashTable *ht, const void *key);
bool sd_hash_table_contains(SDHashTable *ht, const void *key);
size_t sd_hash_table_size(SDHashTable *ht);

void sd_hash_table_iter_init(SDHashTableIter *it, SDHashTable *ht);
bool sd_hash_table_iter_next(SDHashTableIter *it, SDHashKey *key, SDHashVal *val);

#ifdef __cplusplus
}
#endif
#endif // LIBSD_HASH_TABLE_H
