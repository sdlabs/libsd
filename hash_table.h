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

typedef struct SDHashTable_s SDHashTable;
	
SDHashTable *sd_hash_table_new();

#ifdef __cplusplus
}
#endif
#endif // LIBSD_HASH_TABLE_H
