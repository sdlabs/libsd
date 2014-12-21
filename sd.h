// Copyright 2014 Bobby Powers. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.
#ifndef LIBSD_SD_H
#define LIBSD_SD_H
#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>

typedef enum {
	SD_USES_ARRAYS    = 1<<1,
	SD_USES_QUEUE     = 1<<2,
	SD_USES_CONVEYER  = 1<<3,
	SD_USES_SUBMODELS = 1<<4,
} SDFeatureFlag;

typedef enum {
	SD_ERR_NO_ERROR    = 0, // 0-value is no error
	SD_ERR_NOMEM       = -1,
	SD_ERR_BAD_FILE    = -2,
	SD_ERR_UNSPECIFIED = -3,
	SD_ERR_BAD_XML     = -4,
	SD_ERR_BAD_LEX     = -5,
	SD_ERR_EOF         = -6,
	SD_ERR_MIN         = -7
} SDErrorEnum;

typedef struct SDProject_s SDProject;
typedef struct SDSim_s SDSim;

/// sd_error_str returns a string representation describing one of the
/// errors enumerated above.  The returned string must not be freed or
/// modified.
const char *sd_error_str(int err);

/// sd_project_open_at opens a XMILE model specified by path,
/// resolving any relative file paths relative to the directory the
/// model file resides in, and is passed an optional pointer to an
/// integer to hold the details of any error that occurs.  If an error
/// occurs, the function returns NULL and if the err parameter is not
/// NULL, details of the error are placed in it.
SDProject *sd_project_open(const char *path, int *err);
void sd_project_ref(SDProject *project);
void sd_project_unref(SDProject *project);

/// sd_sim_new creates a new simulation context for the named model.
/// If model_name is NULL, the context is created for the default/root
/// model in the project.
SDSim *sd_sim_new(SDProject *project, const char *model_name);
void sd_sim_ref(SDSim *sim);
void sd_sim_unref(SDSim *sim);

int sd_sim_run_to(SDSim *sim, double time);
int sd_sim_run_to_end(SDSim *sim);
int sd_sim_get_stepcount(SDSim *sim);
int sd_sim_get_varcount(SDSim *sim);
int sd_sim_get_varnames(SDSim *sim, const char **result, size_t max);

int sd_sim_reset(SDSim *sim);

int sd_sim_get_value(SDSim *sim, const char *name, double *result);
int sd_sim_set_value(SDSim *sim, const char *name, double val);
int sd_sim_get_series(SDSim *sim, const char *name, double *results, size_t len);

#ifdef __cplusplus
}
#endif
#endif // LIBSD_SD_H
