libsd - System Dynamics for developers
======================================

A modern, high-performance, open source system dynamics engine written
in C, designed for embedding in larger projects.

Clean, Simple API
-----------------

Running a model and retreiving the time-series results for a variable
is straightforward:

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
