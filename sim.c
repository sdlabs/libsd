// Copyright 2014 Bobby Powers. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "utf.h"
#include "sd.h"
#include "sd_internal.h"


typedef struct {
	Walker w;
	AVar *module;
	AVar *av;
	Node *curr;
} AVarWalker;

typedef struct {
	const char *const name;
	Fn fn;
} FnDef;

static double rt_min(ptr<SDSim> s, Node *n, double dt, double t, size_t len, double *args);
static double rt_max(ptr<SDSim> s, Node *n, double dt, double t, size_t len, double *args);
static double rt_pulse(ptr<SDSim> s, Node *n, double dt, double t, size_t len, double *args);

static double *sim_curr(ptr<SDSim> s);
static double *sim_next(ptr<SDSim> s);

static void calc(ptr<SDSim> s, double *data, Slice *l, bool initial);
static void calc_stocks(ptr<SDSim> s, double *data, Slice *l);

static double svisit(ptr<SDSim> s, Node *n, double dt, double time);

static AVar *module(ptr<SDProject> p, AVar *parent, ptr<SDModel> model, Var *module);
static int module_compile(AVar *module);
static int module_assign_offsets(AVar *module, int *offset);
static int module_get_varnames(AVar *module, const char **result, size_t max);
static void module_clear_visited(AVar *module);
static int module_sort_runlists(AVar *module);
static int module_add_to_runlists(AVar *module, AVar *av);

static const char *avar_qual_name(AVar *av);

static AVarWalker *avar_walker_new(AVar *module, AVar *av);
static void avar_walker_ref(void *data);
static void avar_walker_unref(void *data);
static void avar_walker_start(void *data, Node *n);
static Walker *avar_walker_start_child(void *data, Node *n);
static void avar_walker_end_child(void *data, Node *);

static const WalkerOps AVAR_WALKER_OPS = {
	.ref = avar_walker_ref,
	.unref = avar_walker_unref,
	.start = avar_walker_start,
	.start_child = avar_walker_start_child,
	.end_child = avar_walker_end_child,
	.end = NULL,
};

static const FnDef RT_FNS[] = {
	{"pulse", rt_pulse},
	{"min", rt_min},
	{"max", rt_max},
};
static const size_t RT_FNS_LEN = sizeof(RT_FNS)/sizeof(RT_FNS[0]);

AVar *
avar(AVar *parent, Var *v)
{
	ptr<SDModel> model;
	AVar *av;
	int err;

	av = NULL;
	err = 0;

	if (v->type == VAR_MODULE) {
		model = sd_project_get_model(parent->model->file->project, v->name);
		if (!model)
			goto error;
		av = module(parent->model->file->project, parent, model, v);
		if (av)
			av->v = v;
		return av;
	}

	av = calloc(1, sizeof(*av));
	if (!av)
		goto error;
	av->v = v;
	av->parent = parent;

	if (v->eqn)
		err = avar_eqn_parse(av);

	if (err)
		goto error;
	av->is_const = av->node && av->node->type == N_FLOATLIT;

	return av;
error:
	printf("eqn parse failed for %s\n", v->name);
	free(av);
	return NULL;
}

const char *
avar_qual_name(AVar *av)
{
	const char *parent;
	size_t parent_len, name_len;

	if (av->qual_name)
		return av->qual_name;

	// if we _are_ the root model, return <main>
	if (!av->parent)
		return "<main>";

	// if we're in the root model, return the var's name
	if (av->parent->parent == NULL)
		return av->v->name;

	parent = avar_qual_name(av->parent);
	if (!parent)
		// TODO(bp) handle ENOMEM
		return NULL;
	parent_len = strlen(parent);

	name_len = strlen(av->v->name);

	av->qual_name = malloc(parent_len + name_len + 2);
	if (!av->qual_name)
		// TODO(bp) handle ENOMEM
		return NULL;

	memcpy(av->qual_name, parent, parent_len);
	av->qual_name[parent_len] = '.';
	memcpy(&av->qual_name[parent_len+1], av->v->name, name_len);
	av->qual_name[parent_len+name_len+1] = '\0';

	return av->qual_name;
}

AVarWalker *
avar_walker_new(AVar *module, AVar *av)
{
	AVarWalker *w = calloc(1, sizeof(*w));

	if (!w)
		return NULL;

	w->w.ops = &AVAR_WALKER_OPS;
	avar_walker_ref(w);
	w->av = av;
	w->module = module;

	return w;
}

void
avar_walker_ref(void *data)
{
	AVarWalker *w = data;
	__sync_fetch_and_add(&w->w.refcount, 1);
}

void
avar_walker_unref(void *data)
{
	AVarWalker *w = data;

	if (!w || __sync_sub_and_fetch(&w->w.refcount, 1) != 0)
		return;

	free(w);
}

void
avar_walker_start(void *data, Node *n)
{
	AVarWalker *avw = data;
	AVar *dep;

	avw->curr = n;

	switch (n->type) {
	case N_IDENT:
		dep = resolve(avw->module, n->sval);
		if (!dep) {
			printf("resolve failed for %s\n", n->sval);
			// TODO: error
			return;
		}
		n->av = dep;
		slice_append(&avw->av->direct_deps, dep);
		break;
	case N_FLOATLIT:
		n->fval = atof(n->sval);
		break;
	case N_CALL:
		for (size_t i = 0; i < RT_FNS_LEN; i++) {
			if (strcmp(n->left->sval, RT_FNS[i].name) == 0) {
				n->fn = RT_FNS[i].fn;
				break;
			}
		}
		if (!n->fn)
			printf("unknown fn '%s' for call\n", n->left->sval);
		break;
	default:
		break;
	}
}

Walker *
avar_walker_start_child(void *data, Node *n)
{
	AVarWalker *avw = data;
	Node *curr = avw->curr;

	// skip trying to resolve function name identifiers for calls
	// a second time - already handled in avar_walker_start above
	if (curr->type == N_CALL && n == curr->left)
		return NULL;

	avw->w.ops->ref(&avw->w);
	return &avw->w;
}

void
avar_walker_end_child(void *data, Node *n)
{
}

int
avar_init(AVar *av, AVar *module)
{
	AVarWalker *w;
	bool ok;

	w = NULL;

	// is amodule if we have a model pointer
	if (av->model) {
		return module_compile(av);
	} else if (av->v->type == VAR_REF && module) {
		AVar *src = resolve(module->parent, av->v->src);
		if (src) {
			av->src = src;
			return 0;
		}
		goto error;
	} else if (!av->v->eqn && av->v->name && strcmp(av->v->name, "time") == 0) {
		return 0;
	}

	w = avar_walker_new(module, av);

	ok = node_walk(&w->w, av->node);
	if (!ok)
		goto error;

	for (size_t i = 0; i < av->v->inflows.len; i++) {
		char *in_name = av->v->inflows.elems[i];
		AVar *in = resolve(module, in_name);
		if (!in)
			goto error;
		slice_append(&av->inflows, in);
	}
	for (size_t i = 0; i < av->v->outflows.len; i++) {
		char *out_name = av->v->outflows.elems[i];
		AVar *out = resolve(module, out_name);
		if (!out)
			goto error;
		slice_append(&av->outflows, out);
	}

	avar_walker_unref(w);

	return SD_ERR_NO_ERROR;
error:
	avar_walker_unref(w);
	return SD_ERR_UNSPECIFIED;
}

void
avar_free(AVar *av)
{
	if (!av)
		return;

	sd_model_unref(av->model);
	for (size_t i = 0; i < av->avars.len; i++) {
		AVar *child = av->avars.elems[i];
		avar_free(child);
	}
	free(av->avars.elems);
	free(av->qual_name);
	node_free(av->node);
	free(av->direct_deps.elems);
	free(av->inflows.elems);
	free(av->outflows.elems);
	free(av->initials.elems);
	free(av->flows.elems);
	free(av->stocks.elems);
	var_free(av->time);
	free(av);
}

AVar *
module(ptr<SDProject> p, AVar *parent, ptr<SDModel> model, Var *vmodule)
{
	AVar *module;
	Slice conns;

	module = calloc(1, sizeof(*module));
	if (!module)
		goto error;
	module->v = vmodule;
	module->model = model;
	module->parent = parent;

	if (!parent) {
		Var *time = calloc(1, sizeof(*time));
		if (!time)
			goto error;
		time->type = VAR_AUX;
		time->name = strdup("time");
		module->time = time;

		AVar *atime = calloc(1, sizeof(*atime));
		if (!atime)
			goto error;
		atime->v = time;
		atime->parent = module;
		slice_append(&module->avars, atime);
	}

	memset(&conns, 0, sizeof(conns));
	if (vmodule)
		conns = vmodule->conns;

	for (size_t i = 0; i < model->vars.len; i++) {
		AVar *av;
		Var *v;

		av = NULL;
		v = model->vars.elems[i];
		// FIXME: this is unelegant and seems inefficient.
		for (size_t j = 0; j < conns.len; j++) {
			Var *r = conns.elems[j];
			if (strcmp(v->name, r->name) == 0) {
				av = avar(module, r);
				break;
			}
		}
		if (!av)
			av = avar(module, v);
		if (!av)
			goto error;
		slice_append(&module->avars, av);
	}

	return module;
error:
	avar_free(module);
	return NULL;
}

AVar *
resolve(AVar *module, const char *name)
{
	size_t len;
	const char *subvar;

	len = 0;

	if (!name)
		return NULL;

	// a name like .area gets resolved to area
	if (name[0] == '.')
		name++;

	subvar = strchr(name, '.');
	if (subvar) {
		len = subvar - name;
		subvar++;
	}

	for (size_t i = 0; i < module->avars.len; i++) {
		AVar *av = module->avars.elems[i];
		if (subvar && av->v->type == VAR_MODULE && strncmp(av->v->name, name, len) == 0) {
			return resolve(av, subvar);
		}
		else if (strcmp(av->v->name, name) == 0)
			return av;
	}

	return NULL;
}

int
module_compile(AVar *module)
{
	AVar *av;
	int err, failed;

	failed = 0;
	for (size_t i = 0; i < module->avars.len; i++) {
		av = module->avars.elems[i];
		err = avar_init(av, module);
		if (err)
			failed++;
	}
	if (failed)
		return SD_ERR_UNSPECIFIED;

	for (size_t i = 0; i < module->avars.len; i++) {
		av = module->avars.elems[i];
		// TODO: fix for multiple levels of indirection.
		if (av->v->type == VAR_REF)
			av->offset = av->src->offset;
	}

	// sorting of runlists is done in a separate step after
	// offsets are assigned.

	return SD_ERR_NO_ERROR;
}

ptr<SDSim>
sd_sim_new(ptr<SDProject> p, const char *model_name)
{
	ptr<SDSim> sim;
	ptr<SDModel> model;
	int err, offset;

	offset = 0;
	model = NULL;
	sim = calloc(1, sizeof(*sim));
	if (!sim)
		goto error;
	sd_sim_ref(sim);

	// FIXME: check refcounting
	model = sd_project_get_model(p, model_name);
	if (!model)
		goto error;

	sim->module = module(p, NULL, model, NULL);
	if (!sim->module)
		goto error;

	sd_project_ref(p);
	sim->project = p;

	err = avar_init(sim->module, NULL);
	if (err)
		goto error;

	err = module_assign_offsets(sim->module, &offset);
	if (err)
		goto error;

	err = module_sort_runlists(sim->module);
	if (err)
		goto error;

	sim->nvars = offset;
	err = sd_sim_reset(sim);
	if (err)
		goto error;

	return sim;
error:
	sd_sim_unref(sim);
	return NULL;
}

int
module_assign_offsets(AVar *module, int *offset)
{
	int err;
	for (size_t i = 0; i < module->avars.len; i++) {
		AVar *av;
		av = module->avars.elems[i];
		if (av->model) {
			err = module_assign_offsets(av, offset);
			if (err)
				return err;
		} else if (!av->src) {
			// assign offsets for everything thats not a
			// module or ref
			av->offset = (*offset)++;
		}
	}

	return 0;
}

int module_add_to_runlists(AVar *module, AVar *av)
{
	if (av->visited)
		return 0;

	// TODO: better circularity error reporting
	if (av->visiting)
		return SD_ERR_CIRCULAR;

	av->visiting = true;

	// make sure any of our dependencies are on runlists before
	// us.
	for (size_t i = 0; i < av->direct_deps.len; i++) {
		AVar *dep = av->direct_deps.elems[i];
		int err;

		if (dep->visited)
			continue;

		err = module_add_to_runlists(module, dep);
		if (err)
			return err;
	}

	if (av->v->type == VAR_MODULE) {
		slice_append(&module->initials, av);
		slice_append(&module->flows, av);
		slice_append(&module->stocks, av);
	} else if (av->v->type == VAR_STOCK) {
		slice_append(&module->initials, av);
		slice_append(&module->stocks, av);
		// refs are not simulated
	} else if (av->v->type != VAR_REF) {
		slice_append(&module->initials, av);
		if (av->is_const)
			slice_append(&module->stocks, av);
		else
			slice_append(&module->flows, av);
	}

	av->visited = true;
	av->visiting = false;

	return 0;
}

int
module_sort_runlists(AVar *module)
{
	int off;

	module_clear_visited(module);

	module->visiting = true;

	if (!module->parent)
		off = 1;
	else
		off = 0;

	for (size_t i = off; i < module->avars.len; i++) {
		AVar *sub = module->avars.elems[i];
		int err;

		if (sub->visited)
			continue;

		if (sub->v->type == VAR_MODULE) {
			err = module_sort_runlists(sub);
			if (err)
				return err;
		}

		err = module_add_to_runlists(module, sub);
		if (err)
			return err;
	}

	module->visiting = false;

	return 0;
}

int
sd_sim_reset(ptr<SDSim> s)
{
	int err = 0;
	size_t save_every, nvars;

	s->spec = s->module->model->file->sim_specs;
	s->step = 0;
	s->save_step = 0;
	s->nsteps = (s->spec.stop - s->spec.start)/s->spec.dt + 1;

	save_every = s->spec.savestep/s->spec.dt+.5;
	s->save_every = save_every > 1 ? save_every : 1;
	s->nsaves = s->nsteps/s->save_every;
	if (s->nsteps % s->save_every)
		s->nsaves++;

	free(s->slab);
	nvars = s->nvars;
	// ensure we don't ask calloc to allocate 0 elements
	if (!nvars)
		nvars = 1;
	// XXX: 1 extra step to simplify run_to
	s->slab = calloc(nvars*(s->nsaves + 1), sizeof(double));
	if (!s->slab) {
		err = SD_ERR_NOMEM;
		goto error;
	}
	s->curr = s->slab;
	s->next = NULL;

	s->curr[TIME] = s->spec.start;

	calc(s, s->curr, &s->module->initials, true);
error:
	return err;
}

void
calc(ptr<SDSim> s, double *data, Slice *l, bool initial)
{
	double dt;

	dt = s->spec.dt;

	//printf("CALC\n");
	for (size_t i = 0; i < l->len; i++) {
		AVar *av = l->elems[i];
		if (!av->node) {
			if (initial)
				calc(s, data, &av->initials, true);
			else
				calc(s, data, &av->flows, false);
			continue;
		}
		double v = svisit(s, av->node, dt, data[0]);
		if (av->v->gf)
			v = lookup(av->v->gf, v);
		data[av->offset] = v;
	}
}

void
calc_stocks(ptr<SDSim> s, double *data, Slice *l)
{
	double prev, v, dt;

	dt = s->spec.dt;

	//printf("CALC STOCKS\n");
	for (size_t i = 0; i < l->len; i++) {
		AVar *av = l->elems[i];
		// XXX: this could also be implemented by building the
		// addition and subtraction of flows in the stock's
		// AST.  Maybe that would be cleaner?
		switch (av->v->type) {
		case VAR_STOCK:
			prev = s->curr[av->offset];
			v = 0;
			for (size_t i = 0; i < av->inflows.len; i++) {
				AVar *in = av->inflows.elems[i];
				v += s->curr[in->offset];
			}
			for (size_t i = 0; i < av->outflows.len; i++) {
				AVar *out = av->outflows.elems[i];
				v -= s->curr[out->offset];
			}
			data[av->offset] = prev + v*dt;
			break;
		case VAR_MODULE:
			calc_stocks(s, data, &av->stocks);
			break;
		default:
			v = svisit(s, av->node, dt, s->curr[0]);
			data[av->offset] = v;
			break;
		}
	}
}

int
sd_sim_run_to(ptr<SDSim> s, double end)
{
	double dt;

	if (!s)
		return -1;

	dt = s->spec.dt;
	s->curr = sim_curr(s);
	s->next = sim_next(s);

	while (s->step < s->nsteps && s->curr[TIME] <= end) {
		calc(s, s->curr, &s->module->flows, false);
		calc_stocks(s, s->next, &s->module->stocks);

		if (s->step + 1 == s->nsteps)
			break;

		// calculate this way instead of += dt to minimize
		// cumulative floating point errors.
		s->next[TIME] = s->spec.start + (s->step+1)*dt;

		if (s->step++ % s->save_every != 0) {
			memcpy(s->curr, s->next, s->nvars*sizeof(double));
		} else {
			s->save_step++;
			s->curr = sim_curr(s);
			s->next = sim_next(s);
		}
	}

	return 0;
}

int
sd_sim_run_to_end(ptr<SDSim> s)
{
	if (!s)
		return -1;
	return sd_sim_run_to(s, s->spec.stop + 1);
}

double *
sim_curr(ptr<SDSim> s)
{
	return &s->slab[s->save_step*s->nvars];
}

double *
sim_next(ptr<SDSim> s)
{
	return &s->slab[(s->save_step+1)*s->nvars];
}

void
sd_sim_ref(ptr<SDSim> sim)
{
	if (!sim)
		return;
	__sync_fetch_and_add(&sim->refcount, 1);
}

int
sd_sim_get_value(ptr<SDSim> s, const char *name, double *result)
{
	AVar *av;

	if (!s || !name || !result)
		return SD_ERR_UNSPECIFIED;

	if (strcmp(name, "time") == 0) {
		*result = s->curr[TIME];
		return 0;
	}

	av = resolve(s->module, name);
	if (!av)
		return SD_ERR_UNSPECIFIED;

	*result = s->curr[av->offset];
	return 0;
}

void
sd_sim_unref(ptr<SDSim> sim)
{
	if (!sim)
		return;
	if (__sync_sub_and_fetch(&sim->refcount, 1) == 0) {
		avar_free(sim->module);
		sd_project_unref(sim->project);
		free(sim->slab);
		free((SDSim *)sim);
	}
}

double
svisit(ptr<SDSim> s, Node *n, double dt, double time)
{
	double v = NAN;
	double cond, l, r;
	double args[6];
	int off;

	switch (n->type) {
	case N_PAREN:
		v = svisit(s, n->left, dt, time);
		break;
	case N_FLOATLIT:
		v = n->fval;
		break;
	case N_IDENT:
		if (n->av->src)
			off = n->av->src->offset;
		else
			off = n->av->offset;
		v = s->curr[off];
		break;
	case N_CALL:
		memset(args, 0, 6*sizeof(*args));
		(void)n->left->sval;
		for (size_t i = 0; i < n->args.len; i++) {
			Node *arg = n->args.elems[i];
			args[i] = svisit(s, arg, dt, time);
		}
		v = n->fn(s, n, dt, time, n->args.len, args);
		break;
	case N_IF:
		cond = svisit(s, n->cond, dt, time);
		if (cond != 0)
			v = svisit(s, n->left, dt, time);
		else
			v = svisit(s, n->right, dt, time);
		break;
	case N_UNARY:
		l = svisit(s, n->left, dt, time);
		switch (n->op) {
		case '+':
			v = l;
			break;
		case '-':
			v = -l;
			break;
		case '!':
			v = l == 0 ? 1 : 0;
			break;
		}
		break;
	case N_BINARY:
		l = svisit(s, n->left, dt, time);
		r = svisit(s, n->right, dt, time);
		switch (n->op) {
		case '+':
			v = l + r;
			break;
		case '-':
			v = l - r;
			break;
		case '*':
			v = l * r;
			break;
		case '/':
			v = l / r;
			break;
		case '<':
			v = l < r ? 1 : 0;
			break;
		case '>':
			v = l > r ? 1 : 0;
			break;
		case '&':
			v = l == 1 && r == 1 ? 1 : 0;
			break;
		case '|':
			v = l == 1 || r == 1 ? 1 : 0;
			break;
		case '=':
			v = l == r;
			break;
		case u'≠':
			v = l != r;
			break;
		case u'≤':
			v = l <= r ? 1 : 0;
			break;
		case u'≥':
			v = l >= r ? 1 : 0;
			break;
		case '^':
			v = pow(l, r);
			break;
		default:
			printf("unknown binary op (%c) encountered\n", n->op);
		}
		break;
	case N_UNKNOWN:
	default:
		printf("unknown node encountered\n");
		// TODO: error
		break;
	}

	return v;
}

int
sd_sim_get_stepcount(ptr<SDSim> sim)
{
	if (!sim)
		return -1;
	return sim->nsaves;
}

int
sd_sim_get_varcount(ptr<SDSim> sim)
{
	if (!sim)
		return -1;
	return sim->nvars;
}

int
sd_sim_get_varnames(ptr<SDSim> sim, const char **result, size_t max)
{
	if (!sim || !result)
		return -1;
	else if (max == 0)
		return 0;

	return module_get_varnames(sim->module, result, max);
}

int
module_get_varnames(AVar *module, const char **result, size_t max)
{
	const char **start = result;

	for (size_t i = 0; i < module->avars.len && max > 0; i++) {
		AVar *av = module->avars.elems[i];
		if (av->model) {
			size_t n = module_get_varnames(av, result, max);
			result += n;
			max -= n;
		} else if (!av->src) {
			// only include non-ghosts in output
			*result = avar_qual_name(av);
			result++;
			max--;
		}
	}
	return result-start;
}

void
module_clear_visited(AVar *module)
{
	for (size_t i = 0; i < module->avars.len; i++) {
		AVar *av = module->avars.elems[i];
		if (av->model) {
			module_clear_visited(av);
		} else {
			av->visited = false;
			av->visiting = false;
		}
	}
	module->visited = false;
	module->visiting = false;
}

int
sd_sim_get_series(ptr<SDSim> s, const char *name, double *results, size_t len)
{
	int off;
	size_t i;

	if (!s || !name || !results)
		return -1;

	if (strcmp(name, "time") == 0) {
		off = 0;
	} else {
		AVar *av = resolve(s->module, name);
		if (!av)
			return -1;
		off = av->offset;
	}

	for (i = 0; i <= s->nsaves && i < len; i++)
		results[i] = s->slab[i*s->nvars + off];

	return i;
}

double
rt_pulse(ptr<SDSim> s, Node *n, double dt, double time, size_t len, double *args)
{
	double magnitude, first_pulse, next_pulse, interval;

	magnitude = args[0];
	first_pulse = args[1];
	if (len > 2)
		interval = args[2];
	else
		interval = 0;

	if (time < first_pulse)
		return 0;

	next_pulse = first_pulse;

	while (time >= next_pulse) {
		if (time < next_pulse + dt)
			return magnitude/dt;
		else if (interval <= 0)
			break;
		else
			next_pulse += interval;
	}
	return 0;
}

double
rt_min(ptr<SDSim> s, Node *n, double dt, double time, size_t len, double *args)
{
	double a, b;

	if (len != 2)
		return NAN;

	a = args[0];
	b = args[1];

	return a < b ? a : b;
}

double
rt_max(ptr<SDSim> s, Node *n, double dt, double time, size_t len, double *args)
{
	double a, b;

	if (len != 2)
		return NAN;

	a = args[0];
	b = args[1];

	return a > b ? a : b;
}
