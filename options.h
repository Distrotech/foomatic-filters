
#ifndef options_h
#define options_h


#include <stddef.h>
#include <regex.h>
#include "util.h"

/* Option types */
#define TYPE_NONE       0
#define TYPE_ENUM       1
#define TYPE_PICKMANY   2
#define TYPE_BOOL       3
#define TYPE_INT        4
#define TYPE_FLOAT      5
#define TYPE_STRING     6
#define TYPE_PASSWORD   7
#define TYPE_CURVE      8
#define TYPE_INVCURVE   9
#define TYPE_PASSCODE   10
#define TYPE_POINTS     11

/* Sections */
#define SECTION_ANYSETUP        1
#define SECTION_PAGESETUP       2
#define SECTION_PROLOG          3
#define SECTION_DOCUMENTSETUP   4
#define SECTION_JCLSETUP        5



typedef struct choice_s {
    char value [128];
    char text [128];
    char command [256];
    struct choice_s *next;
} choice_t;

/* Custom option parameter */
typedef struct param_s {
    char name [128];
    char text [128];       /* formerly comment, changed to 'text' to
                              be consistent with cups */
    int order;
    
    int type;
    char min[20], max[20]; /* contents depend on 'type' */
    
    regex_t *allowedchars;
    regex_t *allowedregexp;
    
    struct param_s *next;
} param_t;

/* Option */
typedef struct option_s {
    char name [128];
    char text [128];
    char varname [128];         /* clean version of 'name' (no spaces etc.) */
    int type;
    int style;
    char spot;
    int order;
    int section;
    
    int notfirst;               /* TODO remove */
    
    choice_t *choicelist;
    
    char *custom_command;       /* *CustomFoo */
    char *proto;                /* *FoomaticRIPOptionPrototype: if this is set
                                   it will be used with only the first option
                                   in paramlist (there should be only one) */
                                   
    param_t *paramlist;         /* for custom values, sorted by stack order */
    size_t param_count;
    
    struct value_s *valuelist;
  
    struct option_s *next;
    struct option_s *next_by_order;    
} option_t;


/* A value for an option */
typedef struct value_s {
    int optionset;
    char *value;
    option_t *fromoption; /* This is set when this value is set by a composite */
    struct value_s *next;
} value_t;


extern option_t *optionlist;
extern option_t *optionlist_sorted_by_order;

extern char jclbegin[256];
extern char jcltointerpreter[256];
extern char jclend[256];
extern char jclprefix[256];

extern char cmd[1024];

extern int ps_accounting;


int option_is_composite(option_t *opt);
int option_is_ps_command(option_t *opt);
int option_is_jcl_arg(option_t *opt);
int option_is_commandline_arg(option_t *opt);


int option_get_section(option_t *opt); /* TODO deprecated */

/* handles ANYSETUP (for (PAGE|DOCUMENT)SETUP) */
int option_is_in_section(option_t *opt, int section); 

void options_init();
void options_free();

option_t *find_option(const char *name);

void read_ppd_file(const char *filename);


int option_set_value(option_t *opt, int optset, const char *value);
const char * option_get_value(option_t *opt, int optset);

/* section == -1 for all sections */
int option_get_command(dstr_t *cmd, option_t *opt, int optset, int section);

int option_accepts_value(option_t *opt, const char *value);
int option_has_choice(option_t *opt, const char *choice);


const char * optionset_name(int idx);
int optionset(const char * name);


#endif
