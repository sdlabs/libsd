// Copyright 2014 Bobby Powers. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "sd.h"


typedef struct {
	double *series;
} Result;


static void die(const char *, ...);
static void usage(void);

static const char *argv0;


void __attribute__((noreturn))
die(const char *fmt, ...)
{
	va_list args;

	va_start(args, fmt);
	vfprintf(stderr, fmt, args);
	va_end(args);

	exit(EXIT_FAILURE);
}

void
usage(void)
{
	die("Usage: %s [OPTION...] PATH\n" \
	    "Simulate system dynamics models.\n\n" \
	    "Options:\n" \
	    "  -help:\tshow this message\n",
	    argv0);
}

int
main(int argc, char *const argv[])
{
	int err = 0;
	ptr<SDProject> p;
	ptr<SDSim> s;
	int nvars, nsteps, n;
	Result *results;
	const char *fmt;
	const char **names = NULL;
	const char *path = NULL;

	for (argv0 = argv[0], argv++, argc--; argc > 0; argv++, argc--) {
		char const* arg = argv[0];
		if (strcmp("-help", arg) == 0) {
			usage();
		} else if (arg[0] == '-') {
			fprintf(stderr, "unknown arg '%s'\n", arg);
			usage();
		} else {
			if (!path) {
				path = arg;
			} else {
				fprintf(stderr, "specify a single path to a model\n");
				usage();
			}
		}
	}

	if (!path) {
		fprintf(stderr, "specify a single path to a model\n");
		usage();
	}

	p = sd_project_open(path, &err);
	if (err)
		die("error opening project: %s\n", sd_error_str(err));

	s = sd_sim_new(p, NULL);
	if (!s)
		die("couldn't create simulation context\n");

	sd_sim_run_to_end(s);

	nsteps = sd_sim_get_stepcount(s);
	nvars = sd_sim_get_varcount(s);
	names = calloc(nvars, sizeof(*names));
	results = calloc(nvars, sizeof(*results));
	if (!names || !results)
		die("out of memory\n");

	if (sd_sim_get_varnames(s, names, nvars) != nvars)
		die("get_varnames unexpected result != %d\n", nvars);

	for (int v = 0; v < nvars; v++) {
		Result *result = results + v;
		result->series = calloc(nsteps, sizeof(double));
		n = sd_sim_get_series(s, names[v], result->series, nsteps);
		if (n != nsteps)
			die("short series read of %d for '%s' (%d/%d)\n", n, names[v], v, nvars);
		fmt = v == nvars-1 ? "%s\n" : "%s\t";
		printf(fmt, names[v]);
	}

	for (int i = 0; i < nsteps; i++) {
		for (int v = 0; v < nvars; v++) {
			Result *result = results + v;
			fmt = v == nvars-1 ? "%f\n" : "%f\t";
			printf(fmt, result->series[i]);
		}
	}

	for (int i = 0; i < nvars; i++) {
		Result *r = results + i;
		free(r->series);
	}
	free(results);
	free(names);

	sd_sim_unref(s);
	sd_project_unref(p);

	fflush(stdout);

	return 0;
}
