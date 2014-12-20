// Copyright 2014 Bobby Powers. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <ctype.h>

#include "utf.h"
#include "sd.h"
#include "sd_internal.h"


int slice_make(Slice *s, size_t len, size_t cap)
{
	if (!s)
		return SD_ERR_UNSPECIFIED;

	if (cap) {
		s->elems = calloc(cap, sizeof(*s->elems));
		if (!s->elems)
			return SD_ERR_NOMEM;
	} else {
		s->elems = NULL;
	}
	s->len = len;
	s->cap = cap;
	return SD_ERR_NO_ERROR;
}

int slice_append(Slice *s, void *e)
{
	size_t new_cap;

	if (!s)
		return SD_ERR_UNSPECIFIED;

	if (s->cap == 0) {
		new_cap = 2;
		s->elems = calloc(new_cap, sizeof(*s->elems));
		if (!s->elems)
			return SD_ERR_NOMEM;
		s->cap = new_cap;
	}
	else if (s->len == s->cap) {
		new_cap = s->cap*2;
		s->elems = realloc(s->elems, new_cap*sizeof(*s->elems));
		if (!s->elems) {
			s->len = 0;
			s->cap = 0;
			return SD_ERR_NOMEM;
		}
		for (size_t i = s->len; i < new_cap; ++i)
			s->elems[i] = NULL;
		s->cap = new_cap;
	}
	s->elems[s->len++] = e;
	return SD_ERR_NO_ERROR;
}

int slice_extend(Slice *s, Slice *other)
{
	if (!s || !other)
		return SD_ERR_UNSPECIFIED;

	// XXX: could be more efficient
	for (size_t i = 0; i < other->len; i++)
		slice_append(s, other->elems[i]);

	return SD_ERR_NO_ERROR;
}

int
strrepl(char *s, const char *orig, const char *new)
{
	char *found;
	int replacements = 0;
	size_t olen = strlen(orig);
	size_t nlen = strlen(new);
	int diff = strlen(orig) - strlen(new);

	// we will not allocate - exit early if we're being asked to
	// do so.
	if (diff < 0)
		return 0;

	while ((found = strstr(s, orig))) {
		replacements++;
		memcpy(found, new, strlen(new));
		// +1 is to copy trailing null
		if (diff)
			memmove(found + nlen, found + olen, strlen(found + olen)+1);
		s = found + nlen;
	}
	return replacements;
}

int
strtrim(const char **s, int len)
{
	int n;
	Rune r;
	for (; (n = charntorune(&r, *s, len)); *s += n, len -= n) {
		if (!r || !isspace(r))
			break;
	}
	for (int i = 1; len > 0 && (n = charntorune(&r, &(*s)[len-i], i)); i++) {
		if (r == Runeerror)
			continue;
		if (!isspace(r))
			break;
		len -= i;
		i = 0;
	}
	return len;
}

int
utf8_tolower(char **s)
{
	int n;
	Rune u;
	const size_t slen = strlen(*s);
	size_t dlen = 0;
	char *src, **ss;
	bool needs_realloc = false;

	src = *s;
	ss = &src;

	for (size_t len = slen; (n = charntorune(&u, *ss, len)); *ss += n, len -= n) {
		const Rune l = tolowerrune(u);
		needs_realloc |= runelen(l) > n;
		dlen += runelen(l);
	}
	char *d = needs_realloc ? realloc(*s, dlen) : *s;
	if (!d)
		return SD_ERR_NOMEM;


	src = *s;
	size_t doff = 0;
	for (size_t len = slen; (n = charntorune(&u, *ss, len)); *ss += n, len -= n) {
		Rune l = tolowerrune(u);
		const int ln = runetochar(&d[doff], &l);
		doff += ln;
	}

	*s = d;

	return SD_ERR_NO_ERROR;
}

size_t
round_up(size_t i, size_t n)
{
	return n*((i - 1)/n + 1);
}

double
lookup(Table *t, double index)
{
	size_t len = t->len;
	if (unlikely(t->len == 0))
		return 0;

	double *x = t->x;
	double *y = t->y;

	// if the request is outside the min or max, then we return
	// the nearest element of the array
	if (unlikely(index < x[0]))
		return y[0];
	else if (unlikely(index > x[len-1]))
		return y[len-1];

	// binary search makes more sense here
	size_t low = 0;
	size_t high = len;
	size_t mid;
	while (low < high) {
		mid = low + ((high-low)/2);
		if (x[mid] < index)
			low = mid + 1;
		else
			high = mid;
	}

	// at this point low == high, so using 'i' seems more readable.
	size_t i = low;
	if (unlikely(x[i] == index)) {
		return y[i];
	} else {
		// slope = deltaY/deltaX
		double slope = (y[i] - y[i-1]) / (x[i] - x[i-1]);
		return (index-x[i-1])*slope + y[i-1];
	}

	return 0;
}
