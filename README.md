libsd - System Dynamics for developers
======================================

A modern, high-performance, open source system dynamics engine written
in C, designed for embedding in larger projects.  It has been tested
to build & run on Linux, FreeBSD, Mac OS X, and Windows (under mingw).
Patches are welcome for Visual Studio support.

libsd relies heavily on unit testing. A recent code coverage report of
the unit tests is [here](https://bpowers.github.io/coverage/libsd/)
with >93% line coverage.


Getting & Building the code
---------------------------

The build process is easy:

```
$ git clone git@github.com:sdlabs/libsd.git
$ cd libsd
$ make
$ sudo make install
```

That will install the static library `libsd`, the header `sd.h`, and
the command-line simulation tool `mdl`.  Run mdl to simulate a model
and provide the results in a tab-separated output format:

```
[bpowers@freebsd11 libsd]$ mdl models/one_stock.xmile
time	stock	input	initial
0.000000	2.000000	1.000000	2.000000
100000.000000	100002.000000	1.000000	2.000000
200000.000000	200002.000000	1.000000	2.000000
300000.000000	300002.000000	1.000000	2.000000
400000.000000	400002.000000	1.000000	2.000000
500000.000000	500002.000000	1.000000	2.000000
600000.000000	600002.000000	1.000000	2.000000
700000.000000	700002.000000	1.000000	2.000000
800000.000000	800002.000000	1.000000	2.000000
900000.000000	900002.000000	1.000000	2.000000
1000000.000000	1000002.000000	1.000000	2.000000
```

Using libsd
-----------

Simply `#include <sd.h>` in your project, and link with `-lsd
-lm`. (`-lm` is the math library provided by libc, and is explicitly
required on most platforms).

Code for opening a model, simulating it, and retreiving the
time-series results for a variable is straightforward:

```C
SDProject *project;
SDSim *sim;
int err = 0;
int nsteps, n;
double *series;

project = sd_project_open(path, &err);
if (!project)
	die("error opening project: %s\n", sd_error_str(err));

sim = sd_sim_new(project, NULL);
if (!sim)
	die("couldn't create simulation context\n");

err = sd_sim_run_to_end(sim);
if (err)
	die("run_to_end: %s\n", sd_err_str(err));

nsteps = sd_sim_get_stepcount(sim);
series = calloc(nsteps, sizeof(*series));
if (!series)
	die("out of memory\n");

n = sd_sim_get_series(sim, "stock", series, nsteps);
if (n != nsteps)
	die("didn't get all data\n");

for (int i = 0; i < nsteps; i++) {
	printf("%f\n", series[i]);
}

free(series);
sd_sim_unref(sim);
sd_project_unref(project);
```

TODO
----

- tests for reciprocal DT
