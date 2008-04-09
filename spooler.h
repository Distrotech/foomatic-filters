
#ifndef SPOOLER_H
#define SPOOLER_H

#include "foomaticrip.h"
#include "util.h"

const char *spooler_name(int spooler);
void init_ppr(list_t *arglist, jobparams_t *job);
void init_cups(list_t *arglist, dstr_t *filelist, jobparams_t *job);
void init_solaris(list_t *arglist, dstr_t *filelist, jobparams_t *job);
void init_direct_cps_pdq(list_t *arglist, dstr_t *filelist, jobparams_t *job);

#endif

