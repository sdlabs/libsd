// Copyright 2014 Bobby Powers. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#undef NDEBUG
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
// intptr_t
#include <unistd.h>

#include "sd.h"
#include "sd_internal.h"

static void die(const char *, ...);
static bool same(double a, double b);

static void test_slice(void);
static void test_hares_and_lynxes(void);
static void test_predator_prey(void);
static void test_one_stock(void);
static void test_failure_cases(void);
static void test_strrepl(void);
static void test_tolower(void);
static void test_round_up(void);
static void test_lex(void);
static void test_table(void);
static void test_parse2(void);

typedef void (*test_f)(void);

static test_f TESTS[] = {
	test_slice,
	test_failure_cases,
	test_hares_and_lynxes,
	test_predator_prey,
	test_one_stock,
	test_strrepl,
	test_tolower,
	test_round_up,
	test_lex,
	test_table,
	test_parse2,
};

int
main(int argc, const char *argv[])
{
	for (size_t i = 0; i < sizeof(TESTS)/sizeof(*TESTS); i++)
		TESTS[i]();
	return 0;
}

void __attribute__((noreturn))
die(const char *fmt, ...)
{
	va_list args;

	va_start(args, fmt);
	vfprintf(stderr, fmt, args);
	va_end(args);

	exit(EXIT_FAILURE);
}

bool
same(double a, double b)
{
	// FIXME: be smarter
	return fabs(a - b) < .0000001;
}

void
test_slice(void)
{
	Slice a, b;

	memset(&a, 0, sizeof(a));
	memset(&b, 0, sizeof(b));

	// shouldn't segfault
	if (!slice_make(NULL, 1, 2))
		die("make w/ null should fail\n");
	if (!slice_append(NULL, &b))
		die("append w/ null should fail\n");
	if (!slice_extend(NULL, NULL))
		die("extend w/ null should fail\n");

	if (slice_append(&a, (void *)1))
		die("append a failed\n");
	if (slice_append(&b, (void *)2))
		die("append b failed\n");
	if (slice_append(&b, (void *)3))
		die("append b2 failed\n");

	if (slice_extend(&a, &b))
		die("extend failed\n");

	if (a.len != 3)
		die("len %zu not 3\n", a.len);

	if ((long)a.elems[0] != 1)
		die("bad contents 1\n");
	if ((long)a.elems[1] != 2)
		die("bad contents 1\n");
	if ((long)a.elems[2] != 3)
		die("bad contents 1\n");

	free(a.elems);
	memset(&a, 0, sizeof(a));

	if (slice_extend(&a, &b))
		die("extend failed\n");
	if (a.len != 2)
		die("bad len %zu\n", a.len);
	if ((long)a.elems[0] != 2)
		die("bad contents 1\n");
	if ((long)a.elems[1] != 3)
		die("bad contents 1\n");

	free(a.elems);
	free(b.elems);
}

static double hares_series[] = {
	50000,
	50000,
	47376.32810349842,
	42195.61223233826,
	35196.33309688633,
	27599.44640998561,
	20801.153479356053,
	15748.807647105632,
	13437.402391878048,
	12602.545018608995,
	13008.069426878246,
	14561.850628709137,
	17349.333733028627,
	21601.894390733047,
	27640.04884405842,
	35818.09826232532,
	46458.8138330597,
	59666.85619835115,
	75114.80613383661,
	91824.22122627276,
	108075.99705611054,
	121401.36218180231,
	128961.45083410625,
};

void
test_hares_and_lynxes(void)
{
	int err;
	SDProject *p;
	SDModel *m;
	SDSim *s;

	err = 0;
	p = sd_project_open("models/hares_and_lynxes.xmile", &err);
	if (p == NULL)
		die("couldn't open 'models/hares_and_lynxes.xmile': %s\n",
		    sd_error_str(err));

	if (p->files.len != 1)
		die("expected 1 file in project, not %d\n", p->files.len);

	File *f = p->files.elems[0];
	if (strcmp(f->header.uuid, "5c1276e0-9bab-4489-b31c-a1e5dfc3a410") != 0)
		die("bad UUID '%s'\n", f->header.uuid);
	if (strcmp(f->header.vendor, "SDLabs") != 0)
		die("bad vendor '%s'\n", f->header.vendor);
	if (strcmp(f->header.product.name, "libsd") != 0)
		die("bad product name '%s'\n", f->header.product.name);
	if (strcmp(f->header.product.version, "0.1.0") != 0)
		die("bad product version '%s'\n", f->header.product.version);
	if (strcmp(f->header.product.lang, "en") != 0)
		die("bad product lang '%s'\n", f->header.product.lang);

	if (strcmp(f->sim_specs.time_units, "time") != 0)
		die("bad time units %s\n", f->sim_specs.time_units);
	if (strcmp(f->sim_specs.method, "Euler") != 0)
		die("bad method %s\n", f->sim_specs.method);

	if (f->sim_specs.start != 1)
		die("bad start %f\n", f->sim_specs.start);
	if (f->sim_specs.stop != 12)
		die("bad stop %f\n", f->sim_specs.stop);
	if (f->sim_specs.dt != .5)
		die("bad dt %f\n", f->sim_specs.dt);

	if (f->models.len != 3)
		die("not 3 models: %d\n", f->models.len);

	m = sd_project_get_model(NULL, NULL);
	if (m)
		die("get_model didn't fail\n");

	m = sd_project_get_model(p, "doesn't exist");
	if (m)
		die("get_model didn't fail\n");

	m = sd_project_get_model(p, NULL);
	if (!m)
		die("get_model failed\n");
	if (!m->name) {
		if (m->vars.len != 4)
			die("bad model len %d\n", m->vars.len);
	}
	sd_model_unref(m);

	m = sd_project_get_model(p, "hares");
	if (!m)
		die("get_model failed\n");
	sd_model_unref(m);

	m = sd_project_get_model(p, NULL);
	if (!m)
		die("get_model failed\n");
	int conditions_checked = 0;
	for (size_t i = 0; i < m->vars.len; i++) {
		Var *v = m->vars.elems[i];
		if (strcmp(v->name, "hares") == 0) {
			if (v->conns.len != 2)
				die("hares should have 2 conns");
			conditions_checked++;
		}
		if (strcmp(v->name, "lynxes") == 0) {
			if (v->conns.len != 2)
				die("hares should have 2 conns");
			conditions_checked++;
		}
	}
	if (conditions_checked != 2)
		die("didn't see both hares and lynxes\n");
	sd_model_unref(m);

	s = sd_sim_new(p, NULL);
	//if (!s)
	//	die("new failed\n");

	(void)hares_series;

	sd_sim_unref(s);

	sd_project_unref(p);
	p = NULL;
}

void
test_predator_prey(void)
{
	SDProject *p;
	int err;
	double v;

	err = 0;
	p = sd_project_open("models/predator_prey.xmile", &err);
	if (p == NULL)
		die("couldn't open 'models/predator_prey.xmile': %s\n",
		    sd_error_str(err));

	if (p->files.len != 1)
		die("expected 1 file in project, not %d\n", p->files.len);

	File *f = p->files.elems[0];
	if (f->sim_specs.start != 0)
		die("bad start %f\n", f->sim_specs.start);
	if (f->sim_specs.stop != 60)
		die("bad stop %f\n", f->sim_specs.stop);
	if (f->sim_specs.dt != .125)
		die("bad dt %f\n", f->sim_specs.dt);

	if (f->models.len != 1)
		die("not 1 model: %d\n", f->models.len);

	int conditions_checked = 0;
	SDModel *m = f->models.elems[0];
	for (size_t i = 0; i < m->vars.len; i++) {
		Var *v = m->vars.elems[i];
		if (strcmp(v->name, "one_time_lynx_harvest") == 0) {
			if (!v->is_nonneg)
				die("one_time_lynx_harvest should be nonneg\n");
			conditions_checked++;
		}
		if (strcmp(v->name, "lynx") == 0) {
			if (v->is_nonneg)
				die("lynx shouldn't be nonneg\n");
			conditions_checked++;
		}
	}
	if (conditions_checked != 2)
		die("only %d conditions checked\n", conditions_checked);

	SDSim *s = sd_sim_new(p, NULL);
	if (!s)
		die("pred_prey sim_new failed\n");

	if ((err = sd_sim_run_to_end(s)))
		die("run_to_end failed: %d\n", err);

	err = sd_sim_get_value(s, "hares", &v);
	//if (err || v != 50000)
	//	die("hares value %f not 50000\n", v);

	sd_sim_unref(s);
	sd_project_unref(p);
	p = NULL;
}

void
test_one_stock(void)
{
	int err;
	SDProject *p;
	SDSim *s;
	SDModel *m;
	double *series;
	size_t len;
	double v;

	err = 0;
	p = sd_project_open("models/one_stock.xmile", &err);
	if (p == NULL)
		die("couldn't open 'models/one_stock.xmile': %s\n",
		    sd_error_str(err));

	if (p->files.len != 1)
		die("expected 1 file in project, not %d\n", p->files.len);

	File *f = p->files.elems[0];
	if (strcmp(f->header.uuid, "3152f4c6-db2c-43a4-844d-b4c3b4afa057") != 0)
		die("bad UUID '%s'\n", f->header.uuid);
	if (strcmp(f->header.name, "speed") != 0)
		die("bad name '%s'\n", f->header.name);
	if (strcmp(f->header.vendor, "SDLabs") != 0)
		die("bad vendor '%s'\n", f->header.vendor);
	if (strcmp(f->header.product.name, "libsd") != 0)
		die("bad product name '%s'\n", f->header.product.name);
	if (strcmp(f->header.product.version, "0.1.0") != 0)
		die("bad product version '%s'\n", f->header.product.version);
	if (strcmp(f->header.product.lang, "en") != 0)
		die("bad product lang '%s'\n", f->header.product.lang);

	if (strcmp(f->sim_specs.time_units, "Time") != 0)
		die("bad time units %s\n", f->sim_specs.time_units);
	if (strcmp(f->sim_specs.method, "Euler") != 0)
		die("bad method %s\n", f->sim_specs.method);

	if (f->sim_specs.start != 0)
		die("bad start %f\n", f->sim_specs.start);
	if (f->sim_specs.stop != 1000000)
		die("bad stop %f\n", f->sim_specs.stop);
	if (f->sim_specs.dt != 1)
		die("bad dt %f\n", f->sim_specs.dt);
	if (f->sim_specs.savestep != 100000)
		die("bad savestep %f\n", f->sim_specs.savestep);

	if (f->models.len != 1)
		die("not 1 models: %d\n", f->models.len);

	m = f->models.elems[0];
	if (m->vars.len != 3)
		die("bad model len %d\n", m->vars.len);

	int ok_vars = 0;
	for (size_t i = 0; i < m->vars.len; i++) {
		Var *v = m->vars.elems[i];
		if (strcmp(v->name, "stock") == 0) {
			if (strcmp(v->eqn, "(initial)+1-1") != 0)
				die("stock equation not '(initial)' (%s)\n", v->eqn);
			ok_vars++;
		} else if (strcmp(v->name, "input") == 0) {
			if (strcmp(v->eqn, "1") != 0)
				die("input equation not '1' (%s)\n", v->eqn);
			ok_vars++;
		} else if (strcmp(v->name, "initial") == 0) {
			if (strcmp(v->eqn, "2") != 0)
				die("initial equation not '2' (%s)\n", v->eqn);
			ok_vars++;
		}
	}
	if (ok_vars != 3)
		die("not 3 correct vars (%d)\n", ok_vars);

	s = sd_sim_new(p, "doesn't exist");
	if (s)
		die("created simulation context for bad model\n");

	s = sd_sim_new(p, NULL);
	if (!s)
		die("couldn't create simulation context\n");

	sd_sim_reset(s);
	sd_sim_run_to_end(s);

	err = sd_sim_get_value(s, "time", &v);
	if (err || v != 1000000)
		die("time value %f not 100 (%d)\n", v, err);
	err = sd_sim_get_value(s, "stock", &v);
	if (err || v != 1000002)
		die("stock value %.1f not 1000002\n", v);
	err = sd_sim_get_value(s, "initial", &v);
	if (err || v != 2)
		die("initial value %f not 2\n", v);
	err = sd_sim_get_value(s, "input", &v);
	if (err || v != 1)
		die("input value %f not 1\n", v);
	err = sd_sim_get_value(s, "non-existant", &v);
	if (err >= 0)
		die("non-existant get_value should have failed\n");

	if (sd_sim_get_varcount(s) != 4)
		die("varcount %d not 4\n", sd_sim_get_varcount(s));
	if (sd_sim_get_stepcount(s) != 11)
		die("stepcount %d not 1000001\n", sd_sim_get_stepcount(s));

	const char **names = calloc(6, sizeof(*names));
	int n = sd_sim_get_varnames(s, names, 6);
	if (n != 4)
		die("get_varnames %d not 4\n", n);
	if (strcmp(names[0], "time") != 0)
		die("time not first name");

	len = sd_sim_get_stepcount(s);
	series = calloc(len, sizeof(double));

	for (int i = 0; i < 4; i++) {
		n = sd_sim_get_series(s, names[i], series, len);
		if (n != 11)
			die("series bad return: %d\n", n);
		if (strcmp(names[i], "time") == 0) {
			for (int j = 0; j < n; j++) {
				if (series[j] != j*100000)
					die("time not identitiy: t:%f vs s:%f\n",
					    (double)j, series[j]);
			}
		} else if (strcmp(names[i], "stock") == 0) {
			for (int j = 0; j < n; j++) {
				if (series[j] != j*100000 + 2)
					die("stock bad val %d:%f\n",
					    j, series[j]);
			}
		}
	}

	free(series);

	if (sd_sim_get_series(s, "non-existant", series, len) >= 0)
		die("get non-existant series should have failed\n");
	sd_sim_get_varnames(s, names, 0);

	free(names);
	sd_sim_unref(s);

	// shouldn't segfault
	sd_sim_ref(NULL);
	sd_sim_get_series(NULL, NULL, NULL, 0);
	sd_sim_get_varnames(NULL, NULL, 0);
	sd_sim_get_value(NULL, NULL, NULL);
	sd_sim_get_stepcount(NULL);
	sd_sim_get_varcount(NULL);
	sd_sim_run_to_end(NULL);
	sd_sim_run_to(NULL, 0);

	sd_project_unref(p);
	p = NULL;
}

void
test_failure_cases(void)
{
	int err;
	SDProject *p;

	p = sd_project_open("non/existant/dir", &err);
	if (p != NULL)
		die("opened bad dir?\n");

	p = sd_project_open("non_existant_file", &err);
	if (p != NULL)
		die("opened bad file?\n");

	err = 0;
	p = sd_project_open("models/bad_xml.xmile", &err);
	if (p != NULL || err != SD_ERR_BAD_XML)
		die("opened bad XML?\n");

	(void)sd_error_str(err);
	if (strcmp(sd_error_str(1), "unknown error") != 0)
		die("unknown error str is wrong\n");
}

void
test_strrepl(void)
{
	int n;
	char *s;

	s = strdup("\\n");
	n = strrepl(s, "\\n", "_");
	if (n != 1)
		die("bad number of replacements %d\n", n);
	if (strcmp(s, "_") != 0)
		die("bad replacement '%s'\n", s);

	if (strrepl(s, "\\n", "___"))
		die("should exit early if given longer new\n");
	free(s);

	s = strdup("s\\ns");
	n = strrepl(s, "\\n", "_");
	if (n != 1)
		die("bad number of replacements %d\n", n);
	if (strcmp(s, "s_s") != 0)
		die("bad replacement '%s'\n", s);
	free(s);

	s = strdup("\\ns\\ns\\n");
	n = strrepl(s, "\\n", "_");
	if (n != 3)
		die("bad number of replacements %d\n", n);
	if (strcmp(s, "_s_s_") != 0)
		die("bad replacement '%s'\n", s);
	free(s);
}

void
test_tolower(void)
{
	char *s;
	s = strdup("ABC");
	utf8_tolower(&s);
	if (strcmp(s, "abc") != 0)
		die("tolower abc failed: '%s'\n", s);
	free(s);
	s = strdup("ÅBC");
	utf8_tolower(&s);
	if (strcmp(s, "åbc") != 0)
		die("tolower abc failed: '%s'\n", s);
	free(s);
}

void
test_round_up(void)
{
	if (round_up(16, 16) != 16)
		die("round_up %d failed", 16);
	if (round_up(15, 16) != 16)
		die("round_up %d failed", 15);
	if (round_up(17, 16) != 32)
		die("round_up %d failed", 17);
}

typedef struct {
	const char *in;
	struct {
		const char *tok;
		TokenType type;
	} tokens[16];
} LexTestData;

static const LexTestData LEX_TESTS[] = {
	{"a", {
		{"a", TOK_IDENT},
	}},
	{"å", {
		{"å", TOK_IDENT},
	}},
	{"a1_åbc________", {
		{"a1_åbc________", TOK_IDENT},
	}},
	{"IF value THEN MAX(flow, 1) ELSE flow", {
		{"if", TOK_RESERVED},
		{"value", TOK_IDENT},
		{"then", TOK_RESERVED},
		{"max", TOK_IDENT},
		{"(", TOK_TOKEN},
		{"flow", TOK_IDENT},
		{",", TOK_TOKEN},
		{"1", TOK_NUMBER},
		{")", TOK_TOKEN},
		{"else", TOK_RESERVED},
		{"flow", TOK_IDENT},
	}},
	// exponent 'e' is case insensitive
	{"5E4", {
		{"5e4", TOK_NUMBER},
	}},
	{"5e4", {
		{"5e4", TOK_NUMBER},
	}},
	{"5.0000000000000e4.00000000000000", {
		{"5.0000000000000e4.00000000000000", TOK_NUMBER},
	}},
	{"3", {
		{"3", TOK_NUMBER},
	}},
	{"3.1.1e.1.1e1e1", {
		{"3.1", TOK_NUMBER},
		{".1e.1", TOK_NUMBER},
		{".1e1", TOK_NUMBER},
		{"e1", TOK_IDENT},
	}},
	{"-3.222\n", {
		{"-", TOK_TOKEN},
		{"3.222", TOK_NUMBER},
	}},
	{"-30000.222", {
		{"-", TOK_TOKEN},
		{"30000.222", TOK_NUMBER},
	}},
	{"5.3e4.", {
		{"5.3e4.", TOK_NUMBER},
	}},
	{"3 == 4 \n\n= 1", {
		{"3", TOK_NUMBER},
		{"==", TOK_TOKEN},
		{"4", TOK_NUMBER},
		{"=", TOK_TOKEN},
		{"1", TOK_NUMBER},
	}},
	{"3 <> 4", {
		{"3", TOK_NUMBER},
		{"<>", TOK_TOKEN},
		{"4", TOK_NUMBER},
	}},
	{"3 >< 4", {
		{"3", TOK_NUMBER},
		{">", TOK_TOKEN},
		{"<", TOK_TOKEN},
		{"4", TOK_NUMBER},
	}},
	{"3 <= 4", {
		{"3", TOK_NUMBER},
		{"<=", TOK_TOKEN},
		{"4", TOK_NUMBER},
	}},
	{"3 >= 4", {
		{"3", TOK_NUMBER},
		{">=", TOK_TOKEN},
		{"4", TOK_NUMBER},
	}},
	{"hares * birth_fraction", {
		{"hares", TOK_IDENT},
		{"*", TOK_TOKEN},
		{"birth_fraction", TOK_IDENT},
	}},
	{"", {{NULL, 0}}},
	{"\n", {{NULL, 0}}},
	{"{comment}", {{NULL, 0}}},
	{"{unclosed comment", {{NULL, 0}}},
	{"{comment before num}3", {
		{"3", TOK_NUMBER},
	}},
	{"{}", {{NULL, 0}}}, // empty comment
	{"pulse(size_of_1_time_lynx_harvest, 4, 1e3)\n", {
		{"pulse", TOK_IDENT},
		{"(", TOK_TOKEN},
		{"size_of_1_time_lynx_harvest", TOK_IDENT},
		{",", TOK_TOKEN},
		{"4", TOK_NUMBER},
		{",", TOK_TOKEN},
		{"1e3", TOK_NUMBER},
		{")", TOK_TOKEN},
	}},
};

void
test_lex(void)
{
	Lexer l;
	Token tok;

	token_init(&tok);

	for (size_t i = 0; i < sizeof(LEX_TESTS)/sizeof(*LEX_TESTS); i++) {
		const LexTestData *test = &LEX_TESTS[i];
		int err;

		//printf("testing '%s'\n", test->in);
		lexer_init(&l, test->in);
		token_init(&tok);

		for (size_t j = 0; test->tokens[j].tok; j++) {
			int err = lexer_nexttok(&l, &tok);
			if (err)
				die("failed to get token '%s'\n", test->tokens[j].tok);
			if (!tok.len)
				die("empty len, expected token '%s'\n", test->tokens[j].tok);
			if (tok.len != strlen(test->tokens[j].tok))
				die("tok len mismatch: '%s'/'%s' %zu/%zu\n",
				    tok.start, test->tokens[j].tok, tok.len,
				    strlen(test->tokens[j].tok));
			if (strncmp(test->tokens[j].tok, tok.start, tok.len) != 0)
				die("expected token '%s'\n", test->tokens[j].tok);
			if (test->tokens[j].type != tok.type)
				die("expected type(%s) %d\n", tok.start, test->tokens[j].type);
		}
		if ((err = lexer_nexttok(&l, NULL)) != SD_ERR_EOF) {
			Token extra;
			token_init(&extra);
			lexer_nexttok(&l, &extra);
			die("extra tokens for '%s' - err:'%s' ty:%d '%s'\n",
			    test->in, sd_error_str(err), tok.type, tok.start);
		}
		token_free(&tok);
		lexer_free(&l);
	}
	if (!lexer_init(NULL, ""))
		die("expected error 1\n");
	if (!lexer_init(NULL, NULL))
		die("expected error 2\n");
	if (!lexer_init(&l, NULL))
		die("expected error 3\n");

	if (lexer_init(&l, "å"))
		die("unexpected error\n");
	if (!lexer_nexttok(NULL, NULL))
		die("exepcted nexttok error\n");
	if (!lexer_nexttok(&l, NULL))
		die("exepcted nexttok error\n");

	if (!lexer_peek(NULL, NULL))
		die("expected peek error\n");

	if (lexer_peek(&l, &tok))
		die("unexpected peek error\n");
	if (strcmp("å", tok.start) != 0)
		die("peek incorrect char\n");
	if (lexer_peek(&l, &tok))
		die("unexpected peek error2\n");
	if (strcmp("å", tok.start) != 0)
		die("peek incorrect char2\n");
	if (lexer_nexttok(&l, &tok))
		die("unexpected nexttok err\n");
	if (strcmp("å", tok.start) != 0)
		die("peek incorrect char2\n");
	if (!lexer_peek(&l, &tok))
		die("expected peek error3\n");
	token_free(&tok);
	lexer_free(&l);
}

void
test_table(void)
{
	int err;
	SDProject *p;

	err = 0;
	p = sd_project_open("models/predator_prey.xmile", &err);
	if (p == NULL)
		die("couldn't open 'models/predator_prey.xmile': %s\n",
		    sd_error_str(err));

	int conditions_checked = 0;
	SDModel *m = sd_project_get_model(p, NULL);
	for (size_t i = 0; i < m->vars.len; i++) {
		Var *v = m->vars.elems[i];
		if (strcmp(v->name, "hares_killed__per_lynx") == 0 || strcmp(v->name, "hares_killed__per_lynx_2") == 0) {
			if (!v->gf)
				die("no gf for hares_killed\n");
			conditions_checked++;

			if (v->gf->len != 11)
				die("wrong len %zu\n", v->gf->len);
			double ys[] = {3.8899999999999998e-305,50,100,150,200,250,300,350,400,450,500};
			double xs[] = {0,50,100,150,200,250,300,350,400,450,500};
			for (size_t j = 0; j < v->gf->len; j++) {
				if (!same(ys[j], v->gf->y[j]))
					die("j%zu: y mismatch %f != %f\n", j, ys[j], v->gf->y[j]);
				if (!same(xs[j], v->gf->x[j]))
					die("j%zu: x mismatch %f != %f\n", j, xs[j], v->gf->x[j]);
			}
			if (!same(lookup(v->gf, 0), 0))
				die("lookup failed\n");
			if (!same(lookup(v->gf, -1), 0))
				die("lookup failed\n");
			if (!same(lookup(v->gf, 600), 500))
				die("lookup failed\n");
			if (!same(lookup(v->gf, 425), 425))
				die("lookup failed\n");
		}
	}
	if (conditions_checked != 2)
		die("t only %d conditions checked\n", conditions_checked);

	sd_model_unref(m);
	m = NULL;
	sd_project_unref(p);
	p = NULL;

	Table *t = calloc(1, sizeof(*t));
	if (lookup(t, 5) != 0)
		die("lookup on empty table failed\n");
	free(t);
}

typedef struct {
	NodeType type;
	Rune op;
	const char *sval;
} NodeInfo;

typedef struct {
	const char *in;
	NodeInfo nodes[16];
} ParseTestData2;

static const ParseTestData2 PARSE_TESTS2[] = {
	{"a", {
		{N_IDENT, 0, "a"},
	}},
	{"3.2 + åbc", {
		{N_BINARY, '+', NULL},
		{N_FLOATLIT, 0, "3.2"},
		{N_IDENT, 0, "åbc"},
	}},
	{"hares * birth_fraction", {
		{N_BINARY, '*', NULL},
		{N_IDENT, 0, "hares"},
		{N_IDENT, 0, "birth_fraction"},
	}},
	{"5. * åbc", {
		{N_BINARY, '*', NULL},
		{N_FLOATLIT, 0, "5."},
		{N_IDENT, 0, "åbc"},
	}},
	{"(5. * åbc4)", {
		{N_PAREN, 0, NULL},
		{N_BINARY, '*', NULL},
		{N_FLOATLIT, 0, "5."},
		{N_IDENT, 0, "åbc4"},
	}},
	{"smooth()", {
		{N_CALL, 0, NULL},
		{N_IDENT, 0, "smooth"},
	}},
	{"smooth(1, 2 + 3, d)", {
		{N_CALL, 0, NULL},
		{N_IDENT, 0, "smooth"},
		{N_FLOATLIT, 0, "1"},
		{N_BINARY, '+', NULL},
		{N_FLOATLIT, 0, "2"},
		{N_FLOATLIT, 0, "3"},
		{N_IDENT, 0, "d"},
	}},
	{"IF a THEN b ELSE c", {
		{N_IF, 0, NULL},
		{N_IDENT, 0, "a"},
		{N_IDENT, 0, "b"},
		{N_IDENT, 0, "c"},
	}},
};

static const char *PARSE_TEST_FAILS[] = {
	"("
	"(3",
	"3 +",
	"(3 +)",
	"call(a,",
	"call(a,1+",
	"if if",
	"if 1 then",
	"if then",
	"if 1 then 2 else",
};

typedef struct {
	Walker w;
	Slice s;
} VerifyWalker;

static VerifyWalker *verify_walker_new(void);
static void verify_walker_ref(void *data);
static void verify_walker_unref(void *data);
static void verify_walker_start(void *data, Node *n);
static Walker *verify_walker_start_child(void *data, Node *n);
static void verify_walker_end_child(void *data, Node *n);

static const WalkerOps VERIFY_WALKER_OPS = {
	.ref = verify_walker_ref,
	.unref = verify_walker_unref,
	.start = verify_walker_start,
	.start_child = verify_walker_start_child,
	.end_child = verify_walker_end_child,
	.end = NULL,
};

VerifyWalker *
verify_walker_new(void)
{
	VerifyWalker *w = calloc(1, sizeof(*w));

	if (!w)
		return NULL;

	w->w.ops = &VERIFY_WALKER_OPS;
	verify_walker_ref(w);

	return w;
}

void
verify_walker_ref(void *data)
{
	VerifyWalker *w = data;
	__sync_fetch_and_add(&w->w.refcount, 1);
}

void
verify_walker_unref(void *data)
{
	VerifyWalker *w = data;

	if (!w || __sync_sub_and_fetch(&w->w.refcount, 1) != 0)
		return;

	for (size_t i = 0; i < w->s.len; i++) {
		NodeInfo *ni = w->s.elems[i];
		free((void *)(intptr_t)ni->sval);
		free(ni);
	}
	free(w->s.elems);
	free(w);
}

void
verify_walker_start(void *data, Node *n)
{
	VerifyWalker *vw = data;
	NodeInfo *ni = calloc(1, sizeof(*ni));

	ni->type = n->type;
	ni->op = n->op;
	if (n->sval)
		ni->sval = strdup(n->sval);
	slice_append(&vw->s, ni);
}

Walker *
verify_walker_start_child(void *data, Node *n)
{
	Walker *w = data;
	w->ops->ref(w);

	return w;
}

void
verify_walker_end_child(void *data, Node *n)
{
}

void
test_parse2(void)
{
	AVar expr;
	int err;

	memset(&expr, 0, sizeof(expr));

	if (!avar_eqn_parse(NULL))
		die("expected parser error 1\n");
	if (!avar_eqn_parse(&expr))
		die("expected parser error 2\n");

	expr.src = NULL;
	if (!avar_eqn_parse(&expr))
		die("expected parser error 3\n");

	expr.src = (char *)(intptr_t)"";
	err = avar_eqn_parse(&expr);
	if (err)
		die("unexpected error\n");

	node_free(expr.node);
	expr.node = 0;

	for (size_t i = 0; i < sizeof(PARSE_TEST_FAILS)/sizeof(*PARSE_TEST_FAILS); i++) {
		const char *expected_failure = PARSE_TEST_FAILS[i];

		expr.src = (char *)(intptr_t)expected_failure;
		//printf("testing '%s'\n", expected_failure);
		err = avar_eqn_parse(&expr);
		if (!err)
			die("expected error for '%s'\n", expected_failure);

		node_free(expr.node);
		expr.src = NULL;
		expr.node = NULL;
	}
	(void)verify_walker_new;

	for (size_t i = 0; i < sizeof(PARSE_TESTS2)/sizeof(*PARSE_TESTS2); i++) {
		const ParseTestData2 *test = &PARSE_TESTS2[i];

		expr.src = (char *)(intptr_t)test->in;
		err = avar_eqn_parse(&expr);
		if (err)
			die("failed to parse '%s' (%d)\n", test->in, err);
		if (!expr.node)
			die("no parse tree returned for '%s'\n", test->in);

		VerifyWalker *w = verify_walker_new();

		node_walk(&w->w, expr.node);

		for (size_t j = 0; j < w->s.len; j++) {
			const NodeInfo *ni1 = &test->nodes[j];
			NodeInfo *ni2 = w->s.elems[j];

			if (ni1->type != ni2->type)
				die("%s j%d type mismatch %d != %d\n", test->in, j, ni1->type, ni2->type);
			if (ni1->op != ni2->op)
				die("j%d op mismatch %d != %d\n", j, ni1->op, ni2->op);
			if (ni1->sval && ni2->sval) {
				if (strcmp(ni1->sval, ni2->sval) != 0)
					die("j%d sval mismatch %s != %s\n", j, ni1->sval, ni2->sval);
			} else if (ni1->sval || ni2->sval) {
				die("j%d sval NULL mismatch %s != %s\n", j, ni1->sval, ni2->sval);
			}
		}

		w->w.ops->unref(w);

		node_free(expr.node);
		expr.src = NULL;
		expr.node = NULL;
	}

	// dont segfault
	node_walk(NULL, NULL);
}
