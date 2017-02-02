// Copyright 2014 Bobby Powers. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

#define _GNU_SOURCE // non-global hash table
#include <search.h>
#undef _GNU_SOURCE

#define XML_LARGE_SIZE
#include <expat.h>

#include "sd.h"
#include "sd_internal.h"


// FIXME(bp) remove max stack depth limit
#define STACKLEN 32


typedef struct BuilderOps_s BuilderOps;

typedef struct {
	const BuilderOps *ops;
	int refcount;
} Builder;

typedef struct NodeBuilder_s {
	Builder b;
	char *name;
	char *content;
	size_t content_len;
	struct hsearch_data *attr_map;
	Slice attrs;
	Slice children;
} NodeBuilder;

typedef struct {
	Builder b;
	File *file; // XmileBuilder doesn't own this
} XmileBuilder;

struct BuilderOps_s {
	void (*ref)(void *data);
	void (*unref)(void *data);
	void (*characters)(void *data, const char *contents, int len);
	Builder *(*start_child)(void *data, const char *tag_name, const char **attrs);
	void (*end_child)(void *data, const char *tag_name, Builder *child);
	void (*end_element)(void *data);
};

typedef struct {
	Builder **stack;
	int stack_top;
	File *file;
} BuilderStack;

static void builder_stack_start_element(void *data, const char *name, const char **attrs);
static void builder_stack_end_element(void *data, const char *name);
static void builder_stack_characters(void *data, const char *s, int len);

static void builder_ref(Builder *b);
static void builder_unref(Builder *b);
static void builder_characters(Builder *b, const char *contents, int len);
static Builder *builder_start_child(Builder *b, const char *tag_name, const char **attrs);
static void builder_end_child(Builder *b, const char *tag_name, Builder *child);
static void builder_end_element(Builder *b);

static NodeBuilder *node_builder_new(const char *name, const char **attrs);
static void node_builder_ref(void *data);
static void node_builder_unref(void *data);
static void node_builder_characters(void *data, const char *contents, int len);
static Builder *node_builder_start_child(void *data, const char *tag_name, const char **attrs);
static void node_builder_end_child(void *data, const char *tag_name, Builder *child);
static void node_builder_end_element(void *data);
static NodeBuilder *node_builder_get_first_child(NodeBuilder *nb, const char *name);
static const char *node_builder_get_attr(NodeBuilder *nb, const char *name);

static XmileBuilder *xmile_builder_new(void);
static void xmile_builder_ref(void *data);
static void xmile_builder_unref(void *data);
static Builder *xmile_builder_start_child(void *data, const char *tag_name, const char **attrs);
static void xmile_builder_end_child(void *data, const char *tag_name, Builder *child);
static void xmile_builder_end_element(void *data);

static SDModel *model_from_node_builder(NodeBuilder *b);
static Var *var_from_node_builder(NodeBuilder *b);
static Table *table_from_node_builder(NodeBuilder *b);
static Var *ref_from_node_builder(NodeBuilder *b);

static const BuilderOps NODE_BUILDER_OPS = {
	.ref = node_builder_ref,
	.unref = node_builder_unref,
	.characters = node_builder_characters,
	.start_child = node_builder_start_child,
	.end_child = node_builder_end_child,
	.end_element = node_builder_end_element,
};

static const BuilderOps XMILE_BUILDER_OPS = {
	.ref = xmile_builder_ref,
	.unref = xmile_builder_unref,
	.characters = NULL,
	.start_child = xmile_builder_start_child,
	.end_child = xmile_builder_end_child,
	.end_element = xmile_builder_end_element,
};


int
project_parse_file(SDProject *p, FILE *f)
{
	char *buf = calloc(BUFSIZ, sizeof(char));
	File *sdf = calloc(1, sizeof(File));
	BuilderStack bs;
	XML_Parser parser = NULL;
	int err = SD_ERR_NO_ERROR;
	bool done = false;

	memset(&bs, 0, sizeof(bs));

	if (!buf || !sdf) {
		err = SD_ERR_NOMEM;
		goto error;
	}

	sdf->project = p;

	memset(&bs, 0, sizeof(bs));
	bs.file = sdf;
	bs.stack_top = -1;
	bs.stack = calloc(STACKLEN, sizeof(Builder*));
	if (!bs.stack) {
		err = SD_ERR_NOMEM;
		goto error;
	}

	// TODO(bp) ParserCreateNS
	parser = XML_ParserCreate(NULL);
	XML_SetUserData(parser, &bs);
	XML_SetElementHandler(parser, builder_stack_start_element,
			      builder_stack_end_element);
	XML_SetCharacterDataHandler(parser, builder_stack_characters);

	while (!done) {
		size_t len = fread(buf, 1, BUFSIZ, f);
		done = len < BUFSIZ;
		if (XML_Parse(parser, buf, len, done) == XML_STATUS_ERROR) {
			err = SD_ERR_BAD_XML;
			//fprintf(stderr,
			//	"%s at line %llu\n",
			//	XML_ErrorString(XML_GetErrorCode(parser)),
			//	XML_GetCurrentLineNumber(parser));
			goto error;
		}
	}

	project_add_file(p, sdf);

	XML_ParserFree(parser);
	free(bs.stack);
	free(buf);
	return SD_ERR_NO_ERROR;
error:
	XML_ParserFree(parser);
	free(sdf);
	if (bs.stack) {
		for (int i = bs.stack_top; i >= 0; i--) {
			builder_unref(bs.stack[i]);
			bs.stack[i] = NULL;
		}
	}
	free(bs.stack);
	free(buf);
	return err;
}

void
builder_stack_start_element(void *data, const char *name, const char **attrs)
{
	BuilderStack *bs = data;

	if (bs->stack_top < 0) {
		// TODO(bp) check/handle non-xmile top level tag
		XmileBuilder *xb = xmile_builder_new();
		if (!xb) {
			// TODO(bp) handle ENOMEM
			return;
		}
		xb->file = bs->file;

		bs->stack[0] = &xb->b;
		bs->stack_top = 0;
	} else {
		Builder *b = builder_start_child(bs->stack[bs->stack_top], name, attrs);
		if (!b) {
			NodeBuilder *nb = node_builder_new(name, attrs);
			if (!nb) {
				// TODO(bp) handle ENOMEM
				return;
			}
			b = &nb->b;
		}
		bs->stack[++bs->stack_top] = b;
	}
}

void
builder_stack_end_element(void *data, const char *name)
{
	BuilderStack *bs = data;
	Builder *child = NULL;

	// TODO(bp) remove or add logging, this should never happen
	if (bs->stack_top < 0)
		return;

	child = bs->stack[bs->stack_top];
	bs->stack[bs->stack_top--] = NULL;

	builder_end_element(child);

	if (bs->stack_top < 0) {
		// TODO(bp) free xmile_builder
	} else {
		builder_end_child(bs->stack[bs->stack_top], name, child);
	}
	builder_unref(child);
}

void
builder_stack_characters(void *data, const char *s, int len)
{
	BuilderStack *bs = data;
	Builder *b = bs->stack[bs->stack_top];

	len = strtrim(&s, len);
	if (len)
		builder_characters(b, s, len);
}

void
builder_ref(Builder *b)
{
	b->ops->ref(b);
}

void
builder_unref(Builder *b)
{
	b->ops->unref(b);
}

void
builder_characters(Builder *b, const char *c, int len)
{
	if (b->ops->characters)
		b->ops->characters(b, c, len);
}

Builder *
builder_start_child(Builder *b, const char *tag_name, const char **attrs)
{
	return b->ops->start_child(b, tag_name, attrs);
}

void
builder_end_child(Builder *b, const char *tag_name, Builder *child)
{
	b->ops->end_child(b, tag_name, child);
}

void
builder_end_element(Builder *b)
{
	b->ops->end_element(b);
}

NodeBuilder *
node_builder_new(const char *name, const char **attrs)
{
	NodeBuilder *nb = calloc(1, sizeof(NodeBuilder));
	if (!nb)
		return NULL;
	nb->b.ops = &NODE_BUILDER_OPS;
	builder_ref(&nb->b);
	nb->name = strdup(name);
	slice_make(&nb->attrs, 0, 0);
	slice_make(&nb->children, 0, 0);
	for (size_t i = 0; attrs[i]; i += 2) {
		//printf("%s\t%s\n", name, attrs[i]);
		ENTRY *e = calloc(1, sizeof(ENTRY));
		e->key = strdup(attrs[i]);
		e->data = strdup(attrs[i+1]);
		slice_append(&nb->attrs, e);
	}

	return nb;
}

void
node_builder_ref(void *data)
{
	NodeBuilder *nb = data;
	__sync_fetch_and_add(&nb->b.refcount, 1);
}

void
node_builder_unref(void *data)
{
	NodeBuilder *nb = data;
	if (!nb || __sync_sub_and_fetch(&nb->b.refcount, 1) != 0)
		return;

	free(nb->name);
	free(nb->content);
	for (size_t i = 0; i < nb->attrs.len; i++) {
		ENTRY *e = nb->attrs.elems[i];
		nb->attrs.elems[i] = NULL;
		free(e->key);
		free(e->data);
		free(e);
	}
	free(nb->attrs.elems);
	for (size_t i = 0; i < nb->children.len; i++) {
		NodeBuilder *child = nb->children.elems[i];
		builder_unref(&child->b);
		nb->children.elems[i] = NULL;
	}
	free(nb->children.elems);
	free(nb);
}

void
node_builder_characters(void *data, const char *contents, int len)
{
	NodeBuilder *nb = data;
	if (!nb->content) {
		char *s = malloc(len+1);
		s[len] = '\0';
		memcpy(s, contents, len);
		nb->content = s;
		nb->content_len = len;
	} else {
		size_t content_len = nb->content_len;
		// +2 for ' ' and trailing NULL
		nb->content = realloc(nb->content, content_len + len + 1);
		if (!nb->content) {
			// FIXME(bp) handle ENOMEM
			return;
		}
		memcpy(&nb->content[content_len], contents, len);
		nb->content[content_len+len] = '\0';
		nb->content_len += len;
	}
}

Builder *
node_builder_start_child(void *data, const char *tag_name, const char **attrs)
{
	NodeBuilder *child = node_builder_new(tag_name, attrs);
	if (!child) {
		// TODO(bp) handle ENOMEM
		return NULL;
	}
	return &child->b;
}

void
node_builder_end_child(void *data, const char *tag_name, Builder *child)
{
	NodeBuilder *nb = data;
	builder_ref(child);
	slice_append(&nb->children, (NodeBuilder*)child);
}

void
node_builder_end_element(void *data)
{
}

NodeBuilder *
node_builder_get_first_child(NodeBuilder *nb, const char *name)
{
	for (size_t i = 0; i < nb->children.len; i++) {
		NodeBuilder *child = nb->children.elems[i];
		if (strcmp(child->name, name) == 0)
			return child;
	}
	return NULL;
}

const char *
node_builder_get_attr(NodeBuilder *nb, const char *name)
{
	for (size_t i = 0; i < nb->attrs.len; i++) {
		ENTRY *e = nb->attrs.elems[i];
		if (strcmp(e->key, name) == 0)
			return (const char *)e->data;
	}

	return NULL;
}

XmileBuilder *
xmile_builder_new(void)
{
	XmileBuilder *xb = calloc(1, sizeof(XmileBuilder));
	if (!xb)
		return NULL;
	xb->b.ops = &XMILE_BUILDER_OPS;
	builder_ref(&xb->b);
	return xb;
}

void
xmile_builder_ref(void *data)
{
	XmileBuilder *xb = data;
	__sync_fetch_and_add(&xb->b.refcount, 1);
}

void
xmile_builder_unref(void *data)
{
	XmileBuilder *xb = data;
	if (!xb || __sync_sub_and_fetch(&xb->b.refcount, 1) != 0)
		return;
	free(xb);
}

Builder *
xmile_builder_start_child(void *data, const char *tag_name, const char **attrs)
{
	return NULL;
}

void
xmile_builder_end_child(void *data, const char *tag_name, Builder *child)
{
	const char *val;
	NodeBuilder *nbchild;
	XmileBuilder *xb = data;

	if (strcmp(tag_name, "header") == 0) {
		Header *header = &xb->file->header;
		NodeBuilder *nbheader = (NodeBuilder *)child;
		nbchild = node_builder_get_first_child(nbheader, "smile");
		if (nbchild) {
			val = node_builder_get_attr(nbchild, "version");
			if (val)
				header->smile_version = strdup(val);
			val = node_builder_get_attr(nbchild, "namespace");
			if (val)
				header->smile_namespace = strdup(val);
			// TODO(bp) uses_* flags
		}
		nbchild = node_builder_get_first_child(nbheader, "name");
		if (nbchild && nbchild->content)
			header->name = strdup(nbchild->content);
		nbchild = node_builder_get_first_child(nbheader, "uuid");
		if (nbchild && nbchild->content)
			header->uuid = strdup(nbchild->content);
		nbchild = node_builder_get_first_child(nbheader, "vendor");
		if (nbchild && nbchild->content)
			header->vendor = strdup(nbchild->content);
		nbchild = node_builder_get_first_child(nbheader, "product");
		if (nbchild) {
			if (nbchild->content)
				header->product.name = strdup(nbchild->content);
			val = node_builder_get_attr(nbchild, "version");
			if (val)
				header->product.version = strdup(val);
			val = node_builder_get_attr(nbchild, "lang");
			if (val)
				header->product.lang = strdup(val);
		}
	} else if (strcmp(tag_name, "sim_specs") == 0) {
		SimSpec *specs = &xb->file->sim_specs;
		NodeBuilder *nbspecs = (NodeBuilder *)child;
		bool have_savestep = false;
		val = node_builder_get_attr(nbspecs, "method");
		if (val)
			specs->method = strdup(val);
		val = node_builder_get_attr(nbspecs, "time_units");
		if (val)
			specs->time_units = strdup(val);
		// TODO(bp) better error handling
		nbchild = node_builder_get_first_child(nbspecs, "start");
		if (nbchild && nbchild->content)
			specs->start = strtod(nbchild->content, NULL);
		nbchild = node_builder_get_first_child(nbspecs, "stop");
		if (nbchild && nbchild->content)
			specs->stop = strtod(nbchild->content, NULL);
		nbchild = node_builder_get_first_child(nbspecs, "dt");
		if (nbchild && nbchild->content) {
			specs->dt = strtod(nbchild->content, NULL);
			val = node_builder_get_attr(nbchild, "reciprocal");
			if (val && strcmp(val, "true") == 0)
				specs->dt = 1/specs->dt;
		}
		nbchild = node_builder_get_first_child(nbspecs, "savestep");
		if (nbchild && nbchild->content) {
			specs->savestep = strtod(nbchild->content, NULL);
			have_savestep = true;
		}
		nbchild = node_builder_get_first_child(nbspecs, "save_step");
		if (nbchild && nbchild->content) {
			specs->savestep = strtod(nbchild->content, NULL);
			have_savestep = true;
		}
		if (!have_savestep)
			specs->savestep = specs->dt;
	} else if (strcmp(tag_name, "model") == 0) {
		SDModel *m = model_from_node_builder((NodeBuilder *)child);
		if (m) {
			m->file = xb->file;
			slice_append(&xb->file->models, m);
		}
	}
}

void
xmile_builder_end_element(void *data)
{
}

SDModel *
model_from_node_builder(NodeBuilder *nb)
{
	const char *val;
	SDModel *m = calloc(1, sizeof(SDModel));
	if (!m)
		return NULL; // TODO(bp) handle ENOMEM
	sd_model_ref(m);

	val = node_builder_get_attr(nb, "name");
	if (val)
		m->name = strdup(val);

	NodeBuilder *nbvars = node_builder_get_first_child(nb, "variables");;
	if (nbvars) {
		for (size_t i = 0; i < nbvars->children.len; i++) {
			NodeBuilder *nbvar = nbvars->children.elems[i];
			Var *v = var_from_node_builder(nbvar);
			if (v)
				slice_append(&m->vars, v);
		}
	}

	return m;
}

Var *
var_from_node_builder(NodeBuilder *nb)
{
	const char *val;
	Var *v;
	NodeBuilder *nbeqn;
	VarType ty = 0;

	if (strcmp(nb->name, "aux") == 0)
		ty = VAR_AUX;
	else if (strcmp(nb->name, "stock") == 0)
		ty = VAR_STOCK;
	else if (strcmp(nb->name, "flow") == 0)
		ty = VAR_FLOW;
	else if (strcmp(nb->name, "module") == 0)
		ty = VAR_MODULE;
	else
		return NULL; // TODO(bp) record error

	v = calloc(1, sizeof(Var));
	if (!v)
		return NULL; // TODO(bp) handle ENOMEM

	v->type = ty;

	val = node_builder_get_attr(nb, "name");
	if (val) {
		v->name = canonicalize(val);
	}
	nbeqn = node_builder_get_first_child(nb, "eqn");
	if (nbeqn && nbeqn->content)
		v->eqn = strdup(nbeqn->content);

	for (size_t i = 0; i < nb->children.len; i++) {
		NodeBuilder *child = nb->children.elems[i];
		if (strcmp(child->name, "inflow") == 0 && child->content)
			slice_append(&v->inflows, canonicalize(child->content));
		if (strcmp(child->name, "outflow") == 0 && child->content)
			slice_append(&v->outflows, canonicalize(child->content));
		if (strcmp(child->name, "non_negative") == 0)
			v->is_nonneg = true;
		if (strcmp(child->name, "gf") == 0)
			v->gf = table_from_node_builder(child);
		if (strcmp(child->name, "connect") == 0) {
			Var *ref = ref_from_node_builder(child);
			if (ref)
				slice_append(&v->conns, ref);
		}
	}

	return v;
}

Table *
table_from_node_builder(NodeBuilder *nb)
{
	void *mem = NULL;
	Table *t = NULL;
	char *pts = NULL;

	NodeBuilder *ypts = node_builder_get_first_child(nb, "ypts");
	if (!ypts || !ypts->content)
		goto error;

	NodeBuilder *xpts = node_builder_get_first_child(nb, "xpts");

	size_t n = 1;
	size_t yptslen = strlen(ypts->content);
	for (size_t i = 0; i < yptslen; i++) {
		if (ypts->content[i] == ',')
			n++;
	}

	// round_up ensures following arrays aren't under-aligned.
	// obtain all the memory for the table in a single allocation.
	mem = calloc(1, round_up(sizeof(Table), 8) + 2*n*sizeof(double));
	if (!mem)
		return NULL; // TODO(bp) handle ENOMEM

	t = mem;
	t->len = n;
	t->x = (double *)((char *)mem + round_up(sizeof(Table), 8));
	t->y = (double *)((char *)mem + round_up(sizeof(Table), 8) + n*sizeof(double));

	errno = 0;
	pts = ypts->content;
	for (size_t i = 0; i < t->len; i++) {
		t->y[i] = strtod(pts, &pts);
		if (errno == ERANGE || (*pts && *pts != ','))
			goto error;
		pts++;
	}

	if (xpts) {
		errno = 0;
		pts = xpts->content;
		for (size_t i = 0; i < t->len; i++) {
			t->x[i] = strtod(pts, &pts);
			if (errno == ERANGE || (*pts && *pts != ','))
				goto error;
			pts++;
		}
	} else {
		double xmin = 0, xmax = 0;
		const char *attr;

		NodeBuilder *xscale = node_builder_get_first_child(nb, "xscale");
		if (!xscale)
			goto error;

		attr = node_builder_get_attr(xscale, "min");
		if (attr)
			xmin = atof(attr);
		attr = node_builder_get_attr(xscale, "max");
		if (attr)
			xmax = atof(attr);

		for (size_t i = 0; i < t->len; i++)
			t->x[i] = ((double)i/(t->len-1))*(xmax-xmin) + xmin;
	}

	return t;
error:
	free(mem);
	return NULL;
}

Var *
ref_from_node_builder(NodeBuilder *nb)
{
	Var *ref;
	const char *src, *dst;

	ref = calloc(1, sizeof(*ref));
	src = node_builder_get_attr(nb, "from");
	dst = node_builder_get_attr(nb, "to");

	if (!ref || !src || !dst)
		return NULL;

	ref->type = VAR_REF;
	ref->src = canonicalize(src);
	ref->name = canonicalize(dst);

	if (!ref->src || !ref->name) {
		free(ref->src);
		free(ref->name);
		free(ref);
		// ENOMEM
		return NULL;
	}

	return ref;
}
