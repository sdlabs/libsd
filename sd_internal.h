// Copyright 2014 Bobby Powers. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.
#ifndef LIBSD_SD_INTERNAL_H
#define LIBSD_SD_INTERNAL_H
#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>

#include "utf.h"

#define likely(x)	__builtin_expect(!!(x), 1)
#define unlikely(x)	__builtin_expect(!!(x), 0)

#define charntorune(p,s,n) (fullrune(s,n) ? chartorune(p,s) : 0)

#define TIME 0


typedef enum {
	VAR_UNKNOWN,
	VAR_STOCK,
	VAR_FLOW,
	VAR_AUX,
	VAR_MODULE,
	VAR_REF,
	VAR_MAX
} VarType;

typedef enum {
	N_UNKNOWN,
	N_PAREN,
	N_FLOATLIT,
	N_IDENT,
	N_CALL,
	N_BINARY,
	N_UNARY,
	N_IF,
} NodeType;

typedef enum {
	TOK_TOKEN    = 1<<1,
	TOK_IDENT    = 1<<2,
	TOK_RESERVED = 1<<3,
	TOK_NUMBER   = 1<<4,
} TokenType;


typedef struct File_s File;
typedef struct SDModel_s SDModel;
typedef struct AVar_s AVar;
typedef struct Node_s Node;
typedef struct WalkerOps_s WalkerOps;

typedef double (*Fn)(ptr<SDSim> s, Node *n, double dt, double t, size_t len, double *args);


typedef struct {
	void **elems;
	size_t len;
	size_t cap;
} Slice;

typedef struct {
	char *name;
	char *version;
	char *lang;
} Product;

typedef struct {
	char *smile_version;
	char *smile_namespace;
	int smile_features;
	char **smile_unknown_features;
	char *name;
	char *uuid;
	char *vendor;
	Product product;
} Header;

typedef struct {
	char *time_units;
	double start;
	double stop;
	double dt;
	double savestep;
	char *method;
} SimSpec;

typedef struct {
	char *name;
	char *size;
} Dim;

typedef struct {
	double *x;
	double *y;
	size_t len;
} Table;

typedef struct {
	VarType type;
	char *name;
	char *eqn;
	char *src; // for Ref
	Slice inflows;
	Slice outflows;
	Slice conns; // AVars of type VAR_REF
	Table *gf;
	ptr<SDModel> model;
	bool is_nonneg;
} Var;

struct SDProject_s {
	char *dir_path;
	Slice files;
	int refcount;
};

struct File_s {
	ptr<SDProject> project;
	char *version;
	int level;
	Header header;
	SimSpec sim_specs;
	Dim **dims;
	Slice models;
};

struct SDModel_s {
	File *file;
	char *name;
	Slice vars;
	int refcount;
};

// annotated var
struct AVar_s {
	Var *v;
	Node *node;
	AVar *parent;

	// fully qualified name, for use in get_varnames.  Always
	// empty for variables that reside in the root model, lazily
	// calculated for variables in nested modules.
	char *qual_name;

	// dependencies are defined as other AVars that must be
	// simulated before this one in the current simulation phase.
	// For example, an outflow that references a rate auxiliary
	// and a stock in its equation would only list the rate as its
	// direct dependency, because the stock's value is calculated
	// in a prior simulation phase.
	Slice direct_deps;

	// TODO: using a tagged union would shrink this structure, but
	// it doesn't seem worth it at this point

	// only stocks have inflows + outflows
	Slice inflows;
	Slice outflows;

	// The model refers to this module's model.  initials, flows &
	// stocks are also for modules.
	ptr<SDModel> model;
	Slice initials;
	Slice flows;
	Slice stocks;
	Slice avars;
	Var *time;

	AVar *src; // for ref

	int offset;

	bool is_const;
	bool visited;
	bool visiting;
};

struct SDSim_s {
	ptr<SDProject> project;
	AVar *module;
	SimSpec spec;
	double *slab;
	double *curr;
	double *next;
	size_t nvars;
	size_t nsaves;
	size_t nsteps;
	size_t step;
	size_t save_step;
	size_t save_every;
	int refcount;
};

struct Node_s {
	Node *left;
	Node *right;
	Node *cond;
	NodeType type;
	Rune op;
	char *sval;
	double fval;
	AVar *av;
	Slice args;
	Fn fn;
};

typedef struct {
	short line;
	short pos;
} SourceLoc;

/* sized to produce a SDToken of 32 bytes on 64-bit systems */
#define TOKBUF_LEN 8

typedef struct {
	char *start;
	size_t len;
	SourceLoc loc;
	TokenType type;
	char buf[TOKBUF_LEN];
} Token;

typedef struct {
	const char *orig;
	char *src;
	Rune peek;
	size_t len;
	size_t pos;
	short line;
	short lstart;
	Token tpeek;
	bool havetpeek;
} Lexer;

// someone who iterates over nodes
typedef struct {
	const WalkerOps *ops;
	int refcount;
} Walker;

struct WalkerOps_s {
	void (*ref)(void *data);
	void (*unref)(void *data);
	void (*start)(void *data, Node *n);
	Walker *(*start_child)(void *data, Node *n); // returns w/ refcount of 1
	void (*end_child)(void *data, Node *n);
	void (*end)(void *data);
};

/// given integer i, when divided by integer n, if there is a
/// remainder round up to the next largest n.
size_t round_up(size_t i, size_t n);

int utf8_tolower(char **s);
int strtrim(const char **s, int len);
// new must be shorter or equal to orig, this function will not
// allocate.
int strrepl(char *s, const char *orig, const char *new);

char *canonicalize(const char *n);

int slice_make(Slice *s, size_t len, size_t cap);
int slice_append(Slice *s, void *e);
int slice_extend(Slice *s, Slice *other);

int project_parse_file(ptr<SDProject> p, FILE *f);
int project_add_file(ptr<SDProject> p, File *f);

int module_get_referenced_models(Var *v, Slice *result);

ptr<SDModel> sd_project_get_model(ptr<SDProject> project, const char *model_name);
void sd_model_ref(ptr<SDModel> m);
void sd_model_unref(ptr<SDModel> m);

void var_free(Var *v);

int lexer_init(Lexer *l, const char *src);
void lexer_free(Lexer *l); // frees lexer resources, not struct
int lexer_peek(Lexer *l, Token *t);
int lexer_nexttok(Lexer *l, Token *t);

void token_init(Token *t);
void token_free(Token *t); // frees token resources, not struct

Node *node(NodeType ty);
// nodes are not reference counted.  It is expected that there is a
// single, sole owner of a node (currently always an AVar), and that
// if you want to mutate or free the node you must lock on the owner.
void node_free(Node *n);
bool node_walk(Walker *w, Node *n);

AVar *avar(AVar *module, Var *v);
void avar_free(AVar *av);
int avar_init(AVar *av, AVar *module); // called after all avars have been created
int avar_eqn_parse(AVar *av);
int avar_all_deps(AVar *av, Slice *all);

AVar *resolve(AVar *module, const char *name);

double lookup(Table *t, double index);

#ifdef __cplusplus
}
#endif
#endif // LIBSD_SD_INTERNAL_H
