// Copyright 2014 Bobby Powers. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#include <libgen.h>
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <ctype.h>

#include "utf.h"
#include "sd.h"
#include "sd_internal.h"


#define INITIAL_CAP 8


static void file_free(File *f);


static const char *SD_ERROR_MSGS[] = {
	"no error",          // SD_ERR_NO_ERROR
	"no memory",         // SD_ERR_NOMEM
	"bad file",          // SD_ERR_BAD_FILE
	"unspecified error", // SD_ERR_UNSPECIFIED
	"bad XML",           // SD_ERR_BAD_XML
	"bad equation lex",  // SD_ERR_BAD_LEX
	"EOF",               // SD_ERR_EOF
	"circularity error", // SD_ERR_CIRCULAR
};


ptr<SDProject>
sd_project_open(const char *path, int *err)
{
	FILE *f = NULL;
	char *dir, *dir_str;
	ptr<SDProject> p = NULL;
	int parse_err;

	dir_str = strdup(path);
	dir = dirname(dir_str);

	f = fopen(path, "r");
	if (!f) {
		if (err)
			*err = SD_ERR_BAD_FILE;
		goto error;
	}

	p = calloc(1, sizeof(*p));
	if (!p) {
		if (err)
			*err = SD_ERR_NOMEM;
		goto error;
	}
	sd_project_ref(p);

	p->dir_path = strdup(dir);

	slice_make(&p->files, 0, INITIAL_CAP);
	if (!p->files.elems) {
		if (err)
			*err = SD_ERR_NOMEM;
		goto error;
	}

	parse_err = project_parse_file(p, f);
	if (parse_err) {
		if (err)
			*err = SD_ERR_BAD_XML;
		goto error;
	}

	free(dir_str);
	if (f)
		fclose(f);
	return p;

error:
	free(dir_str);
	if (f)
		fclose(f);
	sd_project_unref(p);
	return NULL;
}

void
sd_project_ref(ptr<SDProject> p)
{
	__sync_fetch_and_add(&p->refcount, 1);
}

void
sd_project_unref(ptr<SDProject> p)
{
	if (!p)
		return;
	if (__sync_sub_and_fetch(&p->refcount, 1) == 0) {
		free(p->dir_path);
		for (size_t i = 0; i < p->files.len; ++i)
			file_free(p->files.elems[i]);
		free(p->files.elems);
		free((SDProject *)p);
	}
}

ptr<SDModel>
sd_project_get_model(ptr<SDProject> p, const char *n)
{
	ptr<SDModel> m = NULL;
	if (!p)
		goto out;

	for (size_t i = 0; i < p->files.len; i++) {
		File *f = p->files.elems[i];
		for (size_t j = 0; j < f->models.len; j++) {
			m = f->models.elems[j];
			// match root model
			if (!m->name && !n)
				goto out;
			// match named model
			else if (m->name && n && strcmp(m->name, n) == 0)
				goto out;
			m = NULL;
		}
	}
out:
	sd_model_ref(m);
	return m;
}

int
project_add_file(ptr<SDProject> p, File *f)
{
	return slice_append(&p->files, f);
}

void
file_free(File *f)
{
	free(f->version);
	free(f->header.smile_version);
	free(f->header.smile_namespace);
	free(f->header.name);
	free(f->header.uuid);
	free(f->header.vendor);
	free(f->header.product.name);
	free(f->header.product.version);
	free(f->header.product.lang);
	free(f->sim_specs.method);
	free(f->sim_specs.time_units);
	for (size_t i = 0; i < f->models.len; ++i)
		sd_model_unref(f->models.elems[i]);
	free(f->models.elems);
	free(f);
}

const char *
sd_error_str(int err)
{
	if (err < SD_ERR_NO_ERROR && err > SD_ERR_MIN)
		return SD_ERROR_MSGS[-err];
	return "unknown error";
}


void
sd_model_ref(ptr<SDModel> m)
{
	if (!m)
		return;
	__sync_fetch_and_add(&m->refcount, 1);
}

void
sd_model_unref(ptr<SDModel> m)
{
	if (!m)
		return;
	if (__sync_sub_and_fetch(&m->refcount, 1) == 0) {
		free(m->name);
		for (size_t i = 0; i < m->vars.len; ++i)
			var_free(m->vars.elems[i]);
		free(m->vars.elems);
		free((SDModel *)m);
	}
}

void
var_free(ptr<Var> v)
{
	if (!v)
		return;

	for (size_t i = 0; i < v->inflows.len; ++i)
		free(v->inflows.elems[i]);
	free(v->inflows.elems);
	for (size_t i = 0; i < v->outflows.len; ++i)
		free(v->outflows.elems[i]);
	free(v->outflows.elems);
	for (size_t i = 0; i < v->conns.len; ++i) {
		Var *ref = v->conns.elems[i];
		var_free(ref);
	}
	free(v->conns.elems);
	free(v->name);
	free(v->eqn);
	free(v->src);
	free(v->gf);
	sd_model_unref(v->model);
	free((Var *)v);
}
