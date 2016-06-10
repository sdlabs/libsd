// Copyright 2014 Bobby Powers. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#include <stdarg.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include "utf.h"
#include "sd.h"
#include "sd_internal.h"

// FIXME: cleanup
#define MAX_ERR_LEN 64

typedef struct {
	Lexer l;
	Slice errs;
} Parser;

static Rune nextrune(Lexer *l);
static void skip_whitespace(Lexer *l);
static bool numstart(Rune r);
static bool identstart(Rune r);
static int lex_number(Lexer *l, Token *t);
static int lex_ident(Lexer *l, Token *t);

static const char *const RESERVED[] = {
	"if",
	"then",
	"else",
};

static const char *const OP_WORDS[] = {
	"not",
	"and",
	"or",
	"mod",
};

static const char *const OP_SHORT[] = {
	"!",
	"&",
	"|",
	"%",
};

static const char *const UNARY = "+-!";

static const char *const BINARY[] = {
	"^",
	"!", // FIXME(bp) right-associativity
	"*/%",
	"+-",
	"><≥≤",
	"=≠",
	"&",
	"|",
};
static const int MAX_BINARY = sizeof(BINARY)/sizeof(BINARY[0]);

static void parser_errorf(Parser *p, const char *s, ...);
static bool consume_tok(Parser *p, Rune r);
static bool consume_any(Parser *p, const char *ops, Rune *op);
static bool consume_reserved(Parser *p, const char *s);
static bool expr(Parser *p, Node **n, int level);
static bool fact(Parser *p, Node **n);
static bool call(Parser *p, Node **n, Node *fn);
static bool ident(Parser *p, Node **n);
static bool num(Parser *p, Node **n);

static bool visit(Walker *w, Node *n);

int
avar_eqn_parse(AVar *v)
{
	Parser p;
	Node *n = NULL;
	int err = SD_ERR_NO_ERROR;
	bool ok;

	if (!v || !v->v || !v->v->eqn)
		return SD_ERR_UNSPECIFIED;

	memset(&p, 0, sizeof(p));

	lexer_init(&p.l, v->v->eqn);

	ok = expr(&p, &n, 0);
	if (!ok) {
		//printf("expr '%s' bad (%zu)\n", v->v->eqn, p.errs.len);
		//if (p.errs.len)
		//	printf("err: %s\n", (char *)p.errs.elems[0]);
		err = SD_ERR_UNSPECIFIED;
		node_free(n);
		goto out;
	}

	v->node = n;

	// TODO(bp) check that entire token stream was consumed
out:
	for (size_t i = 0; i < p.errs.len; i++)
		free(p.errs.elems[i]);
	free(p.errs.elems);
	lexer_free(&p.l);
	return err;
}



int
lexer_init(Lexer *l, const char *src)
{
	if (!l || !src)
		return SD_ERR_UNSPECIFIED;

	memset(l, 0, sizeof(*l));
	l->orig = src;
	l->src = strdup(src);
	int err = utf8_tolower(&l->src);
	if (err)
		return err;
	l->len = strlen(l->src);
	if (l->len)
		charntorune(&l->peek, src, l->len);

	return 0;
}

void
lexer_free(Lexer *l)
{
	if (!l)
		return;

	if (l->havetpeek)
		token_free(&l->tpeek);
	free(l->src);
	l->src = NULL;
}

void
token_init(Token *t)
{
	memset(t, 0, sizeof(*t));
}

void
token_free(Token *t)
{
	if (!t)
		return;

	if (t->start != t->buf) {
		free(t->start);
		t->start = NULL;
	}
}

int
lexer_peek(Lexer *l, Token *t)
{
	int err = SD_ERR_NO_ERROR;

	if (!l || !t)
		return SD_ERR_UNSPECIFIED;

	if (!l->havetpeek) {
		err = lexer_nexttok(l, &l->tpeek);
		l->havetpeek = err == SD_ERR_NO_ERROR;
	}

	if (l->havetpeek) {
		memcpy(t, &l->tpeek, sizeof(*t));
		// fixup pointer
		if (l->tpeek.start == l->tpeek.buf)
			t->start = t->buf;
		else
			t->start = strdup(l->tpeek.start);
	}
	return err;
}

int
lexer_nexttok(Lexer *l, Token *t)
{
	int len;
	int pos;

	if (!l)
		return SD_ERR_UNSPECIFIED;

	if (l->havetpeek) {
		if (!t)
			return SD_ERR_UNSPECIFIED;
		token_free(t);

		memcpy(t, &l->tpeek, sizeof(*t));
		// fixup pointer
		if (l->tpeek.start == l->tpeek.buf)
			t->start = t->buf;
		l->havetpeek = false;
		// if we're consuming the peeked token, we are
		// transferring ownership of the malloc'd start
		// string, so set start to NULL here to make sure we
		// don't try to double free it at some point in the
		// future.
		l->tpeek.start = NULL;
		return SD_ERR_NO_ERROR;
	}

	skip_whitespace(l);
	if (!l->peek)
		return SD_ERR_EOF;

	if (!t)
		return SD_ERR_UNSPECIFIED;
	token_free(t);

	if (numstart(l->peek))
		return lex_number(l, t);
	if (identstart(l->peek))
		return lex_ident(l, t);

	pos = l->pos;

	// we either have a 1 or two rune token - handle all the two
	// rune cases first.
	len = runelen(l->peek);
	if (l->peek == '=') {
		nextrune(l);
		if (l->peek == '=') {
			// eat the second '=' since we matched
			nextrune(l);
			len++;
		}
	} else if (l->peek == '<') {
		nextrune(l);
		if (l->peek == '=' || l->peek == '>') {
			// eat the second '=' since we matched
			nextrune(l);
			len++;
		}
	} else if (l->peek == '>') {
		nextrune(l);
		if (l->peek == '=') {
			// eat the second '=' since we matched
			nextrune(l);
			len++;
		}
	} else {
		nextrune(l);
	}

	strncpy(t->buf, &l->src[pos], len);
	t->buf[len] = '\0';

	// replace common multi-rune ops with single-rune equivalents.
	if (strcmp(t->buf, ">=") == 0) {
		strcpy(t->buf, "≥");
	} else if (strcmp(t->buf, "<=") == 0) {
		strcpy(t->buf, "≤");
	} else if (strcmp(t->buf, "<>") == 0) {
		strcpy(t->buf, "≠");
	}

	t->start = t->buf;
	t->len = strlen(t->buf);
	t->loc.line = l->line;
	t->loc.pos = pos - l->lstart;
	t->type = TOK_TOKEN;
	return 0;
}

Rune
nextrune(Lexer *l)
{
	if (l->pos < l->len) {
		l->pos += runelen(l->peek);
		int n = charntorune(&l->peek, &l->src[l->pos], l->len - l->pos);
		if (!n)
			l->peek = '\0';
	} else {
		l->peek = '\0';
	}

	return l->peek;
}

void
skip_whitespace(Lexer *l)
{
	bool in_comment = false;
	do {
		if (l->peek == '\n') {
			l->line++;
			l->lstart = l->pos + 1;
		}
		if (in_comment) {
			if (l->peek == '}')
				in_comment = false;
			continue;
		}
		if (l->peek == '{') {
			in_comment = true;
			continue;
		}
		if (!isspacerune(l->peek))
			break;
	} while (nextrune(l));
}

bool
numstart(Rune r)
{
	return (r >= '0' && r <= '9') || r == '.';
}

bool
identstart(Rune r)
{
	return !numstart(r) && (isalpharune(r) || r == '_' || r == '"');
}

int
lex_number(Lexer *l, Token *t)
{
	Rune r;
	int pos = l->pos;
	bool have_e = false;
	bool have_dot1 = false;
	bool have_dot2 = false;

	// approximately match /\d*(\.\d*)?(e(\d+(\.\d*)?)?)?/
	while ((r = nextrune(l))) {
		if (r >= '0' && r <= '9')
			continue;
		if (r == '.') {
			if (!have_e && !have_dot1) {
				have_dot1 = true;
				continue;
			} else if (have_e && !have_dot2) {
				have_dot2 = true;
				continue;
			}
			break;
		}
		if (r == 'e') {
			if (!have_e) {
				have_e = true;
				continue;
			}
			break;
		}
		// non-error end of number
		break;
	}

	t->len = l->pos - pos;
	if (t->len < TOKBUF_LEN) {
		t->start = t->buf;
	} else {
		t->start = malloc(t->len + 1);
		if (!t->start)
			return SD_ERR_NOMEM;
	}
	memcpy(t->start, &l->src[pos], t->len);
	t->start[t->len] = '\0';

	t->loc.line = l->line;
	t->loc.pos = pos - l->lstart;
	t->type = TOK_NUMBER;
	return 0;
}

int
lex_ident(Lexer *l, Token *t)
{
	Rune r;
	bool quoted = l->peek == '"';
	int pos = l->pos;

	// Eat opening "
	if (quoted)
		nextrune(l);

	while ((r = nextrune(l))) {
		if (isalpharune(r))
			continue;
		if (r == '_')
			continue;
		if (r >= '0' && r <= '9')
			continue;
		if (quoted) {
			if (r == '"') {
				// Eat closing "
				nextrune(l);
				break;
			}
			if (isspacerune(r))
				continue;
			// TODO: check for escape sequences
		}
		break;
	}

	t->len = l->pos - pos;
	if (t->len < TOKBUF_LEN) {
		t->start = t->buf;
	} else {
		t->start = malloc(t->len + 1);
		if (!t->start)
			return SD_ERR_NOMEM;
	}
	memcpy(t->start, &l->src[pos], t->len);
	t->start[t->len] = '\0';

	t->loc.line = l->line;
	t->loc.pos = pos - l->lstart;
	t->type = TOK_IDENT;

	for (size_t i = 0; i < sizeof(RESERVED)/sizeof(*RESERVED); i++) {
		if (strcmp(t->start, RESERVED[i]) == 0) {
			t->type = TOK_RESERVED;
			break;
		}
	}

	for (size_t i = 0; i < sizeof(OP_WORDS)/sizeof(*OP_WORDS); i++) {
		if (strcmp(t->start, OP_WORDS[i]) == 0) {
			strcpy(t->buf, OP_SHORT[i]);
			t->start = t->buf;
			t->len = strlen(t->buf);
			t->type = TOK_TOKEN;
			break;
		}
	}

	return 0;
}

bool
expr(Parser *p, Node **n, int level)
{
	Token t;
	int err;
	Node *x = NULL;
	Node *lhs = NULL;
	Node *rhs = NULL;
	bool ok = false;
	const char *ops;
	Rune op;

	token_init(&t);

	err = lexer_peek(&p->l, &t);
	if (err == SD_ERR_EOF)
		return true;

	if (level + 1 == MAX_BINARY)
		ok = fact(p, &lhs);
	else
		ok = expr(p, &lhs, level + 1);

	if (!ok)
		goto out;

	ops = BINARY[level];
	while (true) {
		ok = consume_any(p, ops, &op);
		// its fine if we didn't have a binary operator
		if (!ok) {
			ok = true;
			break;
		}

		// expr must be passed level + 1, not 0, to preserve
		// left-associativity
		if (level + 1 == MAX_BINARY)
			ok = fact(p, &rhs);
		else
			ok = expr(p, &rhs, level + 1);
		if (!ok || !rhs) {
			ok = false;
			goto out;
		}

		x = node(N_BINARY);
		// FIXME(bp) no mem
		if (!x) {
			ok = false;
			goto out;
		}
		x->left = lhs;
		x->right = rhs;
		x->op = op;
		lhs = x;
		x = NULL;
		rhs = NULL;
	}
	*n = lhs;
	lhs = NULL;
out:
	node_free(lhs);
	node_free(rhs);
	token_free(&t);
	return ok;
}

bool
fact(Parser *p, Node **n)
{
	Node *x, *l, *r, *cond;
	Rune op;
	bool ok = false;

	x = l = r = cond = NULL;

	if (consume_tok(p, '(')) {
		ok = expr(p, &l, 0);
		if (!ok)
			goto out;
		if (!consume_tok(p, ')')) {
			parser_errorf(p, "expected ')'");
			ok = false;
			goto out;
		}
		x = node(N_PAREN);
		// FIXME(bp) no mem
		if (!x) {
			ok = false;
			goto out;
		}
		x->left = l;
		*n = x;
		x = NULL;
		l = NULL;
		ok = true;
		goto out;
	}

	if (consume_any(p, UNARY, &op)) {
		ok = expr(p, &l, 0);
		if (!ok)
			goto out;

		x = node(N_UNARY);
		if (!x) {
			ok = false;
			goto out;
		}

		x->op = op;
		x->left = l;
		*n = x;
		x = NULL;
		l = NULL;
		ok = true;
		goto out;
	}

	if ((ok = num(p, n)))
		goto out;
	if (consume_reserved(p, "if")) {
		ok = expr(p, &cond, 0);
		if (!ok)
			goto out;
		if (!consume_reserved(p, "then")) {
			parser_errorf(p, "expected 'then'");
			ok = false;
			goto out;
		}
		ok = expr(p, &l, 0);
		if (!ok || !l) {
			ok = false;
			goto out;
		}
		if (consume_reserved(p, "else")) {
			ok = expr(p, &r, 0);
			if (!ok || !r) {
				ok = false;
				goto out;
			}
		}

		x = node(N_IF);
		if (!x) {
			ok = false;
			goto out;
		}
		x->cond = cond;
		x->left = l;
		x->right = r;
		*n = x;
		cond = l = r = x = NULL;
		goto out;
	}
	if ((ok = ident(p, &x))) {
		if (consume_tok(p, '(')) {
			ok = call(p, n, x);
			x = NULL;
		} else {
			*n = x;
			x = NULL;
		}
		goto out;
	}
	// TODO(bp) []
out:
	node_free(x);
	node_free(l);
	node_free(r);
	node_free(cond);
	return ok;
}

bool
call(Parser *p, Node **n, Node *fn)
{
	Node *arg = NULL;
	Node *x = node(N_CALL);

	// FIXME: no mem
	if (!x)
		goto error;
	x->left = fn;
	fn = NULL;

	// no-arg call - simplifies logic to special case this.
	if (consume_tok(p, ')'))
		goto out;

	while (true) {
		bool ok = expr(p, &arg, 0);
		if (!ok) {
			parser_errorf(p, "call: expected expr arg");
			goto error;
		}
		slice_append(&x->args, arg);
		arg = NULL;
		if (consume_tok(p, ','))
			continue;
		if (consume_tok(p, ')'))
			break;
		parser_errorf(p, "call: expected ',' or ')'");
		goto error;
	}
out:
	*n = x;
	return true;
error:
	node_free(arg);
	node_free(x);
	node_free(fn);
	return false;
}

bool
ident(Parser *p, Node **n)
{
	Token t;
	Node *x;
	int err;
	bool ok = false;

	token_init(&t);

	err = lexer_peek(&p->l, &t);
	// FIXME(bp) log
	if (err || t.type != TOK_IDENT)
		goto error;

	lexer_nexttok(&p->l, &t);

	x = node(N_IDENT);
	// FIXME(bp) no mem
	if (!x)
		goto error;
	x->sval = canonicalize(t.start);
	*n = x;
	ok = true;
error:
	token_free(&t);
	return ok;
}

bool
num(Parser *p, Node **n)
{
	Token t;
	Node *x;
	int err;
	bool ok = false;

	token_init(&t);
	err = lexer_peek(&p->l, &t);
	if (err || t.type != TOK_NUMBER)
		goto error;

	lexer_nexttok(&p->l, &t);
	x = node(N_FLOATLIT);
	// FIXME(bp) no mem
	if (!x)
		goto error;

	x->sval = strdup(t.start);
	*n = x;
	ok = true;
error:
	token_free(&t);
	return ok;
}

bool
consume_reserved(Parser *p, const char *s)
{
	Token t;
	int err;
	bool ok = false;

	token_init(&t);

	err = lexer_peek(&p->l, &t);
	if (err)
		goto out;
	// FIXME(bp) better error handling
	ok = t.type == TOK_RESERVED && strcmp(t.start, s) == 0;
	if (ok)
		lexer_nexttok(&p->l, &t);
out:
	token_free(&t);
	return ok;
}

bool
consume_tok(Parser *p, Rune r)
{
	Token t;
	Rune tr = 0;
	int err;
	bool ok = false;

	token_init(&t);

	err = lexer_peek(&p->l, &t);
	if (err)
		goto out;
	charntorune(&tr, t.start, t.len);
	// FIXME(bp) better error handling
	ok = t.type == TOK_TOKEN && tr == r;
	if (ok)
		lexer_nexttok(&p->l, &t);
out:
	token_free(&t);
	return ok;
}

bool
consume_any(Parser *p, const char *ops, Rune *op)
{
	size_t len, pos, n;
	Rune r;
	bool ok;

	len = strlen(ops);

	for (pos = 0; (n = charntorune(&r, &ops[pos], len-pos)); pos += n) {
		ok = consume_tok(p, r);
		if (ok) {
			*op = r;
			return true;
		}
	}
	return false;
}

void
parser_errorf(Parser *p, const char *fmt, ...)
{
	char *err = calloc(MAX_ERR_LEN, 1);
	// FIXME: no mem
	if (!err)
		return;

	// FIXME: peek current token - it is where the error is located.
	va_list args;

	va_start(args, fmt);
	vsnprintf(err, MAX_ERR_LEN, fmt, args);
	va_end(args);

	slice_append(&p->errs, err);
}

Node *
node(NodeType ty)
{
	Node *n = calloc(1, sizeof(*n));
	if (!n)
		return NULL;
	n->type = ty;
	return n;
}

void
node_free(Node *n)
{
	if (!n)
		return;

	node_free(n->left);
	node_free(n->right);
	node_free(n->cond);
	free(n->sval);

	for (size_t i = 0; i < n->args.len; i++)
		node_free((Node *)n->args.elems[i]);
	free(n->args.elems);

	// XXX(bp) remove?
	memset(n, 0, sizeof(*n));

	free(n);
}

bool
visit(Walker *w, Node *n)
{
	Walker *wc = NULL;
	bool ok = true;

	w->ops->start(w, n);

	switch (n->type) {
	case N_PAREN:
		wc = w->ops->start_child(w, n->left);
		ok = visit(wc, n->left);
		wc->ops->unref(wc);
		w->ops->end_child(w, n->left);
		break;
	case N_FLOATLIT:
	case N_IDENT:
		break;
	case N_CALL:
		wc = w->ops->start_child(w, n->left);
		if (wc) {
			ok = visit(wc, n->left);
			wc->ops->unref(wc);
			w->ops->end_child(w, n->left);
		}
		if (!ok)
			break;
		for (size_t i = 0; i < n->args.len; i++) {
			Node *nc = n->args.elems[i];
			wc = w->ops->start_child(w, nc);
			ok = visit(wc, nc);
			wc->ops->unref(wc);
			w->ops->end_child(w, nc);
			if (!ok)
				break;
		}
		break;
	case N_UNARY:
		wc = w->ops->start_child(w, n->left);
		ok = visit(wc, n->left);
		wc->ops->unref(wc);
		w->ops->end_child(w, n->left);
		if (!ok)
			break;
		break;
	case N_IF:
		wc = w->ops->start_child(w, n->cond);
		ok = visit(wc, n->cond);
		wc->ops->unref(wc);
		w->ops->end_child(w, n->cond);
		if (!ok)
			break;
		// fallthrough
	case N_BINARY:
		wc = w->ops->start_child(w, n->left);
		ok = visit(wc, n->left);
		wc->ops->unref(wc);
		w->ops->end_child(w, n->left);
		if (!ok)
			break;
		wc = w->ops->start_child(w, n->right);
		ok = visit(wc, n->right);
		wc->ops->unref(wc);
		w->ops->end_child(w, n->right);
		if (!ok)
			break;
		break;
	case N_UNKNOWN:
	default:
		// TODO: error
		ok = false;
		break;
	}

	if (w->ops->end)
		w->ops->end(w);

	return ok;
}

bool
node_walk(Walker *w, Node *n)
{
	bool ok;

	if (!w || !n)
		return false;

	// XXX: not necessary prob
	w->ops->ref(w);

	ok = visit(w, n);

	w->ops->unref(w);

	return ok;
}
