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
	AModel *am;
	AVar *av;
	Node *curr;
} AVarWalker;

static int cmp_avar(const void *a_in, const void *b_in);

static double *sim_curr(SDSim *s);
static double *sim_next(SDSim *s);

static void calc(SDSim *s, double *data, Slice *l);
static void calc_stocks(SDSim *s, double *data, Slice *l);

static double svisit(SDSim *s, Node *n);

static AVarWalker *avar_walker_new(AModel *am, AVar *av);
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


AVar *
avar(Var *v)
{
	AVar *av = NULL;
	int err;

	av = calloc(1, sizeof(*av));
	if (!av)
		goto error;

	if (v->eqn)
		av->src = strdup(v->eqn);
	else
		av->src = strdup("");
	err = avar_eqn_parse(av);
	if (err)
		goto error;
	av->v = v;
	av->is_const = av->node && av->node->type == N_FLOATLIT;

	return av;
error:
	printf("eqn parse failed for %s\n", v->name);
	free(av);
	return NULL;
}

AVarWalker *
avar_walker_new(AModel *am, AVar *av)
{
	AVarWalker *w = calloc(1, sizeof(*w));

	if (!w)
		return NULL;

	w->w.ops = &AVAR_WALKER_OPS;
	avar_walker_ref(w);
	w->av = av;
	w->am = am;

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
	AVarWalker *vw = data;
	AVar *dep;

	switch (n->type) {
	case N_IDENT:
		dep = resolve(vw->am, n->sval);
		if (!dep) {
			printf("resolve failed for %s\n", n->sval);
			// TODO: error
			return;
		}
		n->av = dep;
		slice_append(&vw->av->direct_deps, dep);
		break;
	case N_FLOATLIT:
		n->fval = atof(n->sval);
		break;
	default:
		break;
	}
}

Walker *
avar_walker_start_child(void *data, Node *n)
{
	Walker *w = data;
	w->ops->ref(w);

	return w;
}

void
avar_walker_end_child(void *data, Node *n)
{
}

int
avar_init(AVar *av, AModel *am)
{
	AVarWalker *w;
	bool ok;

	// XXX: what to do for modules?

	w = avar_walker_new(am, av);

	ok = node_walk(&w->w, av->node);
	if (!ok)
		goto error;

	for (size_t i = 0; i < av->v->inflows.len; i++) {
		char *in_name = av->v->inflows.elems[i];
		AVar *in = resolve(am, in_name);
		if (!in)
			goto error;
		slice_append(&av->inflows, in);
	}
	for (size_t i = 0; i < av->v->outflows.len; i++) {
		char *out_name = av->v->outflows.elems[i];
		AVar *out = resolve(am, out_name);
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

	free(av->src);
	node_free(av->node);
	free(av->direct_deps.elems);
	free(av->all_deps.elems);
	free(av->inflows.elems);
	free(av->outflows.elems);
	free(av);
}

AModel *
amodel(SDModel *m)
{
	AModel *am = NULL;

	am = calloc(1, sizeof(*am));
	if (!am)
		goto error;
	am->model = m;

	for (size_t i = 0; i < m->vars.len; i++) {
		AVar *av = avar(m->vars.elems[i]);
		if (!av)
			goto error;
		slice_append(&am->avars, av);

		if (av->v->type == VAR_STOCK) {
			slice_append(&am->initials, av);
			slice_append(&am->stocks, av);
		} else {
			slice_append(&am->initials, av);
			if (av->is_const)
				slice_append(&am->stocks, av);
			else
				slice_append(&am->flows, av);
		}
	}

	return am;
error:
	free(am);
	return NULL;
}

AVar *
resolve(AModel *am, const char *name)
{
	for (size_t i = 0; i < am->avars.len; i++) {
		AVar *av = am->avars.elems[i];
		if (strcmp(av->v->name, name) == 0)
			return av;
	}

	return NULL;
}

void
amodel_free(AModel *am)
{
	if (!am)
		return;

	for (size_t i = 0; i < am->avars.len; i++) {
		AVar *av = am->avars.elems[i];
		avar_free(av);
	}
	free(am->avars.elems);
	free(am->initials.elems);
	free(am->flows.elems);
	free(am->stocks.elems);
	// we don't own any modules, just reference them.
	free(am->modules.elems);
	free(am);
}

int
cmp_avar(const void *a_in, const void *b_in)
{
	const AVar *a = *(AVar *const *)a_in;
	const AVar *b = *(AVar *const *)b_in;
	int result = 0;
	for (size_t i = 0; i < b->all_deps.len; i++) {
		if (a == b->all_deps.elems[i]) {
			result = -1;
			break;
		}
	}
	if (result == 0) {
		for (size_t i = 0; i < a->all_deps.len; i++) {
			if (b == a->all_deps.elems[i]) {
				result = 1;
				break;
			}
		}
	}
	// order stocks last in initial list
	if (result == 0) {
		if (a->v->type == VAR_STOCK && b->v->type != VAR_STOCK)
			result = 1;
		else if (a->v->type != VAR_STOCK && b->v->type == VAR_STOCK)
			result = -1;
	}
	return result;
}

int
amodel_compile(AModel *am, int offset)
{
	AVar *av;
	int err;

	for (size_t i = 0; i < am->avars.len; i++) {
		av = am->avars.elems[i];
		err = avar_init(av, am);
		if (err)
			return err;
	}

	// TODO: do init on demand from all_deps, so we don't need two
	// iterations.  Maybe.  this keeps this pretty simple as is.
	for (size_t i = 0; i < am->avars.len; i++) {
		av = am->avars.elems[i];
		av->offset = offset + i;
		err = avar_all_deps(av, NULL);
		if (err)
			return err;
	}

	// at this point each model knows its dependencies.  we can
	// sort the 3 run lists.
	qsort(am->initials.elems, am->initials.len, sizeof(AModel*), cmp_avar);
	qsort(am->flows.elems, am->flows.len, sizeof(AModel*), cmp_avar);
	qsort(am->stocks.elems, am->stocks.len, sizeof(AModel*), cmp_avar);

	// compile each model (_prepare)
	// - isRef?
	// - sort variables into run lists (initials/flows/stocks)(DONE)
	// - give each variable unique ID
	// - sort initials + flows

	return SD_ERR_NO_ERROR;
}

int
avar_all_deps(AVar *av, Slice *all)
{
	if (!av->have_all_deps) {
		av->have_all_deps = true;
		slice_extend(&av->all_deps, &av->direct_deps);
		for (size_t i = 0; i < av->direct_deps.len; i++) {
			AVar *dep = av->direct_deps.elems[i];
			avar_all_deps(dep, &av->all_deps);
		}
	}
	if (all) {
		slice_extend(all, &av->all_deps);
	}
	return SD_ERR_NO_ERROR;
}

int
module_get_referenced_models(Var *v, Slice *result)
{
	SDModel *m;
	AModel *am;
	int err;

	if (!v || v->type != VAR_MODULE)
		return SD_ERR_UNSPECIFIED;

	m = v->model;

	// if this module's model is already in the result set, add
	// our unique module var to the list of modules.
	for (size_t i = 0; i < result->len; i++) {
		am = result->elems[i];
		if (am->model == m) {
			slice_append(&am->modules, v);
			return SD_ERR_NO_ERROR;
		}
	}

	// otherwise create a new avar, add us, and add all
	// submodules.
	am = amodel(m);
	if (!am)
		return SD_ERR_NOMEM;

	err = slice_append(&am->modules, v);
	if (err)
		return err;

	err = slice_append(result, am);
	if (err)
		return err;

	for (size_t i = 0; i < m->modules.len; i++) {
		Var *submodule = m->modules.elems[i];
		if (!submodule->model)
			submodule->model = sd_project_get_model(m->file->project, submodule->name);
		err = module_get_referenced_models(submodule, result);
		if (err)
			return err;
	}

	return SD_ERR_NO_ERROR;
}

SDSim *
sd_sim_new(SDProject *p, const char *model_name)
{
	SDSim *sim = calloc(1, sizeof(*sim));
	Var *module = calloc(1, sizeof(*module));
	int err;
	size_t nvars = 1; // time is offset 0

	if (!sim || !module)
		goto error;

	sd_project_ref(p);
	sim->project = p;
	sd_sim_ref(sim);

	// create a module corresponding to the model we wish to
	// simulate.
	module->type = VAR_MODULE;
	if (model_name)
		module->name = strdup(model_name);
	module->model = sd_project_get_model(p, model_name);
	if (!module->model)
		goto error;

	sim->module = module;
	module = NULL;

	err = module_get_referenced_models(sim->module, &sim->amodels);
	if (err)
		goto error;

	for (size_t i = 0; i < sim->amodels.len; i++) {
		AModel *am = sim->amodels.elems[i];
		err = amodel_compile(am, nvars);
		if (err)
			goto error;
		// FIXME: this isn't right for modules
		nvars += am->avars.len;
	}

	sim->nvars = nvars;
	err = sd_sim_reset(sim);
	if (err)
		goto error;

	return sim;
error:
	var_free(module);
	sd_sim_unref(sim);
	return NULL;
}

int
sd_sim_reset(SDSim *s)
{
	int err = 0;
	size_t save_every;

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
	// XXX: 1 extra step to simplify run_to
	s->slab = calloc(s->nvars*(s->nsaves + 1), sizeof(double));
	if (!s->slab) {
		err = SD_ERR_NOMEM;
		goto error;
	}
	s->curr = s->slab;
	s->next = NULL;

	s->curr[TIME] = s->spec.start;

	AModel *root = s->amodels.elems[0];
	calc(s, s->curr, &root->initials);
error:
	return err;
}

void
calc(SDSim *s, double *data, Slice *l)
{
	//printf("CALC\n");
	for (size_t i = 0; i < l->len; i++) {
		AVar *av = l->elems[i];
		double v = svisit(s, av->node);
		if (av->v->gf)
			v = lookup(av->v->gf, v);
		//printf("\t%s\t%f\n", av->v->name, v);
		data[av->offset] = v;
	}
}

void
calc_stocks(SDSim *s, double *data, Slice *l)
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
				v += svisit(s, in->node);
			}
			for (size_t i = 0; i < av->outflows.len; i++) {
				AVar *out = av->outflows.elems[i];
				v -= svisit(s, out->node);
			}
			data[av->offset] = prev + v*dt;
			//printf("\t%s\t%f\n", av->v->name, prev + v*dt);
			break;
		default:
			v = svisit(s, av->node);
			data[av->offset] = v;
			break;
		}
	}
}

int
sd_sim_run_to(SDSim *s, double end)
{
	AModel *root;
	double dt;

	if (!s)
		return -1;

	root = s->amodels.elems[0];
	dt = s->spec.dt;
	s->curr = sim_curr(s);
	s->next = sim_next(s);

	while (s->step < s->nsteps && s->curr[TIME] <= end) {
		calc(s, s->curr, &root->flows);
		calc_stocks(s, s->next, &root->stocks);

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
sd_sim_run_to_end(SDSim *s)
{
	if (!s)
		return -1;
	return sd_sim_run_to(s, s->spec.stop + 1);
}

double *
sim_curr(SDSim *s)
{
	return &s->slab[s->save_step*s->nvars];
}

double *
sim_next(SDSim *s)
{
	return &s->slab[(s->save_step+1)*s->nvars];
}

void
sd_sim_ref(SDSim *sim)
{
	if (!sim)
		return;
	__sync_fetch_and_add(&sim->refcount, 1);
}

int
sd_sim_get_value(SDSim *s, const char *name, double *result)
{
	AVar *av;

	if (!s || !name || !result)
		return SD_ERR_UNSPECIFIED;

	if (strcmp(name, "time") == 0) {
		*result = s->curr[TIME];
		return 0;
	}

	av = resolve(s->amodels.elems[0], name);
	if (!av)
		return SD_ERR_UNSPECIFIED;

	*result = s->curr[av->offset];
	return 0;
}

void
sd_sim_unref(SDSim *sim)
{
	if (!sim)
		return;
	if (__sync_sub_and_fetch(&sim->refcount, 1) == 0) {
		var_free(sim->module);
		for (size_t i = 0; i < sim->amodels.len; i++) {
			AModel *am = sim->amodels.elems[i];
			amodel_free(am);
		}
		free(sim->amodels.elems);
		sd_project_unref(sim->project);
		free(sim->slab);
		free(sim);
	}
}

double
svisit(SDSim *s, Node *n)
{
	double v = NAN;
	double cond, l, r;
	const char *name = NULL;

	switch (n->type) {
	case N_PAREN:
		v = svisit(s, n->left);
		break;
	case N_FLOATLIT:
		v = n->fval;
		break;
	case N_IDENT:
		v = s->curr[n->av->offset];
		break;
	case N_CALL:
		name = n->left->sval;
		(void)name;
		break;
	case N_IF:
		cond = svisit(s, n->cond);
		if (cond != 0)
			v = svisit(s, n->left);
		else
			v = svisit(s, n->right);
		break;
	case N_BINARY:
		l = svisit(s, n->left);
		r = svisit(s, n->right);
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
		case '^':
			v = pow(l, r);
			break;
		}
		break;
	case N_UNKNOWN:
	default:
		// TODO: error
		break;
	}

	return v;
}

int
sd_sim_get_stepcount(SDSim *sim)
{
	if (!sim)
		return -1;
	return sim->nsaves;
}

int
sd_sim_get_varcount(SDSim *sim)
{
	if (!sim)
		return -1;
	return sim->nvars;
}

int
sd_sim_get_varnames(SDSim *sim, const char **result, size_t max)
{
	size_t i = 0;

	if (!sim || !result)
		return -1;
	else if (max == 0)
		return 0;

	result[i] = "time";

	// FIXME: revise for modules case
	AModel *am = sim->amodels.elems[0];
	for (i = 0; i < am->avars.len && i+1 < max; i++) {
		AVar *av = am->avars.elems[i];
		result[i+1] = av->v->name;
	}
	return (int)i+1;
}

int
sd_sim_get_series(SDSim *s, const char *name, double *results, size_t len)
{
	int off;
	size_t i;

	if (!s || !name || !results)
		return -1;

	if (strcmp(name, "time") == 0) {
		off = 0;
	} else {
		AVar *av = resolve(s->amodels.elems[0], name);
		if (!av)
			return -1;
		off = av->offset;
	}

	for (i = 0; i <= s->nsaves && i < len; i++)
		results[i] = s->slab[i*s->nvars + off];

	return i;
}
