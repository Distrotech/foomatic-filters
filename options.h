
#ifndef options_h
#define options_h

#include <stddef.h>

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
    int type;        /* one of the ARG_TYPE_xxx defines */
    char spot;
    int order;
    char section [128];

    /* for numeric options */
    char min[32], max[32];

    /* for string options */
    int maxlength;
    char *allowedchars;
    char *allowedregexp;
    
    char varname [128];

    setting_t *settinglist; /* possible settings for this option */
    value_t *valuelist;
    
    struct option_s *next;
    
} option_t;


/* global optionlist */
extern option_t *optionlist;

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

value_t * option_get_value(option_t *opt, int optionset);
void option_set_value(option_t *opt, int optionset, const char *value); /* value may be NULL */
int option_set_validated_value(option_t *opt, int optionset, const char *value, int forcevalue);
void check_options(int optionset);

/* If the "PageSize" or "PageRegion" was changed, also change the other */
void sync_pagesize(option_t *opt, const char *value, int optionset);

#endif
