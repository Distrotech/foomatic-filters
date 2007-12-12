
#ifndef options_h
#define options_h

#include <stddef.h>
#include "util.h"

#define TYPE_NONE      0
#define TYPE_ENUM      1
#define TYPE_PICKMANY  2
#define TYPE_BOOL      3
#define TYPE_INT       4
#define TYPE_FLOAT     5
#define TYPE_STRING    6


typedef struct setting_s {
    char value [128];
    char comment [128];
    char driverval [256];
    struct setting_s *next;
} setting_t;

typedef struct value_s {
    int optionset;
    char value [128];
    struct value_s *next;
} value_t;

typedef struct option_s {
    char name [128];
    char comment [128];
    char style;
    char proto [128];
    char protof [128];
    int type;        /* one of the ARG_TYPE_xxx defines */
    char spot;
    int order;       /* only set with option_set_order() to preserve correct
                        ordering of optionlist_sorted_by_value*/
    char section [128];

    /* for numeric options */
    char min[32], max[32];

    /* for string options */
    int maxlength;
    char *allowedchars;
    char *allowedregexp;

    struct option_s *controlledby;
    char fromcomposite[128];
    dstr_t *compositesubst;

    /* used by build_commandline */
    dstr_t *jclsetup;
    dstr_t *prolog;
    dstr_t *setup;
    dstr_t *pagesetup;
    
    char varname [128];

    int notfirst;

    setting_t *settinglist; /* possible settings for this option */
    value_t *valuelist;
    
    struct option_s *next;
    struct option_s *next_by_order; /* in optionlist_sorted_by_order */
    
} option_t;


/* global optionlist */
extern option_t *optionlist;

extern option_t *optionlist_sorted_by_order; /* is in correct order as long
                                                as option->order is always set
                                                by option_set_order*/

void init_optionlist();
void free_optionlist();

const char * optionset_name(int idx);
int optionset(const char *name); /* return optionset id, creates one if it doesn't exist yet */
void optionset_copy_values(int src_optset, int dest_optset);
void optionset_delete_values(int optionset);
int optionset_equal(int optset1, int optset2, int exceptPS);

const char * type_name(int type);

size_t option_count();
option_t * find_option(const char *name);
option_t * assure_option(const char *name);

size_t option_setting_count(option_t *opt);
setting_t * option_find_setting(option_t *opt, const char *value);
setting_t * option_assure_setting(option_t* opt, const char *value);
void option_set_order(option_t *opt, int order);

value_t * option_get_value(option_t *opt, int optionset);
const char * option_get_value_string(option_t *opt, int optionset);
void option_set_value(option_t *opt, int optionset, const char *value); /* value may be NULL */
int option_set_validated_value(option_t *opt, int optionset, const char *value, int forcevalue);
void check_options(int optionset);

/* If the "PageSize" or "PageRegion" was changed, also change the other */
void sync_pagesize(option_t *opt, const char *value, int optionset);


#endif
