
#include "options.h"
#include "foomaticrip.h"
#include "util.h"
#include <stdlib.h>
#include <assert.h>
#include <regex.h>


option_t *optionlist = NULL;
option_t *optionlist_sorted_by_order = NULL;

int optionset_alloc, optionset_count;
char **optionsets;


void init_optionlist()
{
    assert(!optionlist);

    /* create optionset list */
    optionset_alloc = 8;
    optionset_count = 0;
    optionsets = calloc(optionset_alloc, sizeof(char *));
}

static void free_option(option_t *opt)
{
    setting_t *setting;
    value_t *val;

    if (!opt)
        return;

    while (opt->settinglist) {
        setting = opt->settinglist;
        opt->settinglist = opt->settinglist->next;
        free(setting);
    }

    while (opt->valuelist) {
        val = opt->valuelist;
        opt->valuelist = opt->valuelist->next;
        free(val);
    }

    free(opt->allowedchars);
    free(opt->allowedregexp);

    free_dstr(opt->compositesubst);
    free_dstr(opt->jclsetup);
    free_dstr(opt->prolog);
    free_dstr(opt->setup);
    free_dstr(opt->pagesetup);

    free(opt);
}

void free_optionlist()
{
    option_t *opt;
    int i;

    /* free options */
    while (optionlist) {
        opt = optionlist;
        optionlist = optionlist->next;
        free_option(opt);
    }

    /* free optionsets */
    for (i = 0; i < optionset_count; i++)
        free(optionsets[i]);
    free(optionsets);
    optionsets = NULL;
    optionset_alloc = 0;
    optionset_count = 0;
}

static inline int optionset_exists(int idx)
{
    return idx >= 0 && idx < optionset_count;
}

const char * optionset_name(int idx)
{
    if (idx < 0 || idx >= optionset_count) {
        _log("Optionset with index %d does not exist\n", idx);
        return NULL;
    }
    return optionsets[idx];
}

int optionset(const char * name)
{
    int i;

    for (i = 0; i < optionset_count; i++) {
        if (!strcmp(optionsets[i], name))
            return i;
    }

    if (optionset_count == optionset_alloc) {
        optionset_alloc *= 2;
        optionsets = realloc(optionsets, optionset_alloc * sizeof(char *));
        for (i = optionset_count; i < optionset_alloc; i++)
            optionsets[i] = NULL;
    }

    optionsets[optionset_count] = strdup(name);
    optionset_count++;
    return optionset_count -1;
}

void optionset_copy_values(int src_optset, int dest_optset)
{
    option_t *opt;
    value_t *val;

    assert(optionset_exists(src_optset) && optionset_exists(dest_optset));

    for (opt = optionlist; opt; opt = opt->next) {
        for (val = opt->valuelist; val; val = val->next) {
            if (val->optionset == src_optset) {
                option_set_value(opt, dest_optset, val->value);
                break;
            }
        }
    }
}

void optionset_delete_values(int optionset)
{
    option_t *opt;
    value_t *val, *prev_val;

    assert(optionset_exists(optionset));

    for (opt = optionlist; opt; opt = opt->next) {
        val = opt->valuelist;
        prev_val = NULL;
        while (val) {
            if (val->optionset == optionset) {
                if (prev_val)
                    prev_val->next = val->next;
                else
                    opt->valuelist = val->next;
                free(val);
                val = prev_val ? prev_val->next : opt->valuelist;
                break;
            } else {
				prev_val = val;
                val = val->next;
            }
        }
    }    
}

int optionset_equal(int optset1, int optset2, int exceptPS)
{
    option_t *opt;
    value_t *val1, *val2;

    assert(optionset_exists(optset1) && optionset_exists(optset2));

    for (opt = optionlist; opt; opt = opt->next) {
        if (exceptPS && opt->style == 'G')
            continue;

        val1 = option_get_value(opt, optset1);
        val2 = option_get_value(opt, optset2);

        if (val1 && val2) { /* both entries exist */
            if (strcmp(val1->value, val2->value)) /* but aren't equal */
                return 0;
        }
        else if (val1 || val2) /* one entry exists --> can't be equal */
            return 0;
        /* If no extry exists, the non-existing entries are considered as equal */
    }
    return 1;
}

const char * type_name(int type)
{
    switch (type) {
        case TYPE_NONE:
            return "none";
        case TYPE_ENUM:
            return "enum";
        case TYPE_PICKMANY:
            return "pickmany";
        case TYPE_BOOL:
            return "bool";
        case TYPE_INT:
            return "int";
        case TYPE_FLOAT:
            return "float";
        case TYPE_STRING:
            return "string";
    };
    _log("type '%d' does not exist\n", type);
    return NULL;
}

size_t option_count()
{
    option_t *opt;
    size_t cnt = 0;

    for (opt = optionlist; opt; opt = opt->next)
        cnt++;
    
    return cnt;
}

option_t * find_option(const char *name)
{
    option_t *opt;

    for (opt = optionlist; opt; opt = opt->next) {
        if (!strcmp(opt->name, name))
            return opt;
    }
    return NULL;
}

option_t * assure_option(const char *name)
{
    option_t *opt, *last;

    if ((opt = find_option(name)))
        return opt;

    opt = calloc(1, sizeof(option_t));

    strlcpy(opt->name, name, 128);
    
    /* Default execution style is 'G' (PostScript) since all arguments for
    which we don't find "*Foomatic..." keywords are usual PostScript options */
    opt->style = 'G';
    
    /* Default prototype for code to insert, used by enum options */
    strcpy(opt->proto, "%s");

    opt->type = TYPE_NONE;
    opt->maxlength = -1;

    opt->compositesubst = create_dstr();
    opt->jclsetup = create_dstr();
    opt->prolog = create_dstr();
    opt->setup = create_dstr();
    opt->pagesetup = create_dstr();
    
    /* append opt to optionlist */
    if (optionlist) {
        for (last = optionlist; last->next; last = last->next);
        last->next = opt;
    }
    else
        optionlist = opt;

    /* prepend opt to optionlist_sorted_by_order (0 is always at the beginning) */
    if (optionlist_sorted_by_order) {
        opt->next_by_order = optionlist_sorted_by_order;
        optionlist_sorted_by_order = opt;
    }
    else {
        optionlist_sorted_by_order = opt;
    }

    _log("Added option %s\n", opt->name);
    return opt;
}

size_t option_setting_count(option_t *opt)
{
    setting_t *setting;
    size_t res = 0;

    for (setting = opt->settinglist; setting; setting = setting->next)
        res++;
    
    return res;
}

setting_t *option_find_setting(option_t *opt, const char *value)
{
    setting_t *setting;

    for (setting = opt->settinglist; setting; setting = setting->next) {
        if (!strcasecmp(setting->value, value))
            return setting;
    }
    return NULL;
}

setting_t *option_assure_setting(option_t* opt, const char *value)
{
    setting_t *setting, *last;

    assert(opt && value);

    if ((setting = option_find_setting(opt, value)))
        return setting;

    setting = calloc(1, sizeof(setting_t));
    strlcpy(setting->value, value, 128);

    /* append to opt->settinglist */
    if (opt->settinglist) {
        for (last = opt->settinglist; last->next; last = last->next);
        last->next = setting;
    }
    else
        opt->settinglist = setting;

    /* TODO print the following when debugging is turned on */
    /* _log("Added setting %s to argument %s\n", setting->value, argitem->item->name); */
    return setting;
}

void option_set_order(option_t *opt, int order)
{
    option_t *prev;

    /* remove opt from old position */
    if (opt == optionlist_sorted_by_order)
        optionlist_sorted_by_order = opt->next_by_order;
    else {
        for (prev = optionlist_sorted_by_order;
             prev && prev->next_by_order != opt;
             prev = prev->next_by_order);
        prev->next_by_order = opt->next_by_order;
    }

    opt->order = order;

    /* insert into new position */
    if (!optionlist_sorted_by_order)
        optionlist_sorted_by_order = opt;
    else {
        for (prev = optionlist_sorted_by_order;
            prev->next_by_order && prev->next_by_order->order < opt->order;
            prev = prev->next_by_order);
        opt->next_by_order = prev->next_by_order;
        prev->next_by_order = opt;
    }
}

value_t * option_get_value(option_t *opt, int optionset)
{
    value_t *val;
    
    assert(opt);

    for (val = opt->valuelist; val; val = val->next) {
        if (val->optionset == optionset)
            return val;
    }
    return NULL;
}

const char * option_get_value_string(option_t *opt, int optionset)
{
    value_t *val = option_get_value(opt, optionset);
    return val ? val->value : NULL;
}

void option_set_value(option_t *opt, int optionset, const char *value)
{
    value_t *val, *last;

    assert(opt && optionset_exists(optionset));

    val = option_get_value(opt, optionset);
    if (!val) {
        val = calloc(1, sizeof(value_t));
        val->optionset = optionset;

        /* append to opt->valuelist */
        if (opt->valuelist) {
            for (last = opt->valuelist; last->next; last = last->next);
            last->next = val;
        }
        else
            opt->valuelist = val;
    }
    
    strlcpy(val->value, value ? value : "", 128);
}

/* Checks whether a user-supplied value for a string option is valid
   It must be within the length limit, should only contain allowed
   characters and match the given regexp */
static int stringvalid(option_t *opt, const char *value)
{
    regex_t rx;

    /* Maximum length */
    if (opt->maxlength >= 0 && strlen(value) > opt->maxlength)
        return 0;

    /* Allowed Characters */
	/* TODO opt->allowedchars may contain character ranges */
    /* if (opt->allowedchars) {
        for (p = value; *p; p++) {
            if (!strchr(opt->allowedchars, *p))
                return 0;
        }
    } */

    /* Regular expression */
    if (opt->allowedregexp) {
        if (regcomp(&rx, opt->allowedregexp, 0) == 0) {
            /* TODO quote slashes? (see perl version) */
            if (regexec(&rx, value, 0, NULL, 0) != 0)
                return 0;
        }
        else {
            _log("FoomaticRIPOptionAllowedRegExp for %s could not be compiled.\n", opt->name);
            /* TODO yreturn success or failure??? */
            return 1;
        }
        regfree(&rx);
    }

    return 1;
}


/* Returns true if the value has been set */
int option_set_validated_value(option_t *opt, int optionset, const char *value, int forcevalue)
{
    setting_t *setting;
    int ivalue, imin, imax;
    float fvalue, fmin, fmax;
    char *p;
    char tmp [256];
    
    assert(opt && optionset_exists(optionset));

    switch (opt->type) {
        case TYPE_BOOL:
            if (strcasecmp(value, "false") == 0 || strcasecmp(value, "off") == 0 ||
                    strcasecmp(value, "no") == 0 || strcasecmp(value, "0") == 0) {
                option_set_value(opt, optionset, "0");
                return 1;
            }
            
            else if (strcasecmp(value, "true") == 0 || strcasecmp(value, "on") == 0 ||
                     strcasecmp(value, "yes") == 0 || strcasecmp(value, "1") == 0) {
                option_set_value(opt, optionset, "1");
                return 1;
            }
            
            else if (forcevalue) {
                /* This maps Unknown to mean False. Good? Bad?
                   It was done so since Foomatic 2.0.x */
                _log("The value %s for %s is not a choice!\n --> Using False instead!\n", value, opt->name);
                option_set_value(opt, optionset, "0");
                return 1;
            }
            break;

        case TYPE_ENUM:
            if (strcasecmp(value, "none") == 0) {
                option_set_value(opt, optionset, "None");
                return 1;
            }
            else if ((setting = option_find_setting(opt, value))) {
                option_set_value(opt, optionset, value);
                return 1;
            }
            else if ((!strcmp(opt->name, "PageSize") || !strcmp(opt->name, "PageRegion")) &&
                      (setting = option_find_setting(opt, "Custom")) && prefixcmp(value, "Custom.") == 0) {
                /* Custom paper size */
                option_set_value(opt, optionset, value);
                return 1;
            }

            else if (forcevalue) {
                /* wtf!? that's not a choice! */
                _log("The %s for %s is not a choice!\n Using %s instead!\n",
                     value, opt->name, opt->settinglist ? opt->settinglist->value : "<null>");
                option_set_value(opt, optionset, opt->settinglist->value);
                return 1;
            }
            break;

        case TYPE_INT:
            ivalue = atoi(value);
            imin = atoi(opt->min);
            imax = atoi(opt->max);
            if (ivalue >= imin && ivalue <= imax)
                option_set_value(opt, optionset, value);
            else if (forcevalue) {
                if (ivalue < imin)
                    p = opt->min;
                else if (ivalue > imax)
                    p = opt->max;
                _log("The value %s for %s is out of range!\n --> Using %s instead!\n", value, opt->name, p);
                option_set_value(opt, optionset, p);
                return 1;
            }
            break;

        case TYPE_FLOAT:
            fvalue = atof(value);
            fmin = atof(opt->min);
            fmax = atof(opt->max);
            if (fvalue >= fmin && fvalue <= fmax)
                option_set_value(opt, optionset, value);
            else if (forcevalue) {
                if (fvalue < fmin)
                    p = opt->min;
                else if (fvalue > imax)
                    p = opt->max;
                _log("The value %s for %s is out of range!\n --> Using %s instead!\n", value, opt->name, p);
                option_set_value(opt, optionset, p);
                return 1;
            }
            break;

        case TYPE_STRING:
            if ((setting = option_find_setting(opt, value))) {
                _log("The value %s for %s is a predefined choice\n", value, opt->name);
                option_set_value(opt, optionset, value);
                return 1;
            }
            
            else if (stringvalid(opt, value)) {
                /* Check whether the string is one of the enumerated choices */

                /* replace %s in opt->proto by 'value', don't use snprintf because
                   of possible other % markers */
                p = strstr(opt->proto, "%s");
                if (p) {
                    strlcpy(tmp, opt->proto, p - opt->proto);
                    strlcat(tmp, value, 256);
                    strlcat(tmp, (p +2), 256);
                }
                else
                    strlcpy(tmp, opt->proto, 256);
                
                for (setting = opt->settinglist; setting; setting = setting->next) {
                    if (!strcmp(setting->driverval, tmp) || !strcmp(setting->driverval, value)) {
                        _log("The string %s for %s is the predefined choice for %s\n", value, opt->name, setting->value);
                        option_set_value(opt, optionset, setting->value);
                        return 1;
                    }
                }

                /* "None" is mapped to the empty string */
                if (strcasecmp(value, "None") == 0) {
                    _log("Option %s: 'None' is mapped to the empty string\n", opt->name);
                    option_set_value(opt, optionset, "");
                    return 1;
                }

                /* No matching choice? Keep the original string */
				option_set_value(opt, optionset, value);
				return 1;
            }

            else if (forcevalue) {
                if (opt->maxlength > 0) {
                    p = strndup(value, opt->maxlength -1);
                    if (stringvalid(opt, p)) {
                        _log("The string %s for %s is is longer than %d, string shortened to %s\n",
                             value, opt->name, opt->maxlength, p);
                        option_set_value(opt, optionset, p);
                        free(p);
                        return 1;
                    }
                    else {
                        _log("Option %s incorrectly defined in the PPD file! Exiting.\n", opt->name);
                        exit(EXIT_PRNERR_NORETRY_BAD_SETTINGS);
                    }
                }
                else if (opt->settinglist) {
                    /* First list item */
                    _log("The string %s for %s contains forbidden characters or does not match the reular"
                            "expression defined for this option, using predefined choice %s instead\n",
                            value, opt->name, opt->settinglist ? opt->settinglist->value : "<none>");
                    option_set_value(opt, optionset, opt->settinglist ? opt->settinglist->value : "<none>");
                    return 1;
                }
                else {
                    _log("Option %s incorrectly defined in the PPD file! Exiting.\n", opt->name);
                    exit(EXIT_PRNERR_NORETRY_BAD_SETTINGS);
                }
            }
            break;
    }
    return 0;
}


/* Let the values of a boolean option being 0 or 1 instead of
   "True" or "False", range-check the defaults of all options and
   issue warnings if the values are not valid */
void check_options(int optionset)
{
    option_t *opt, *opt_ps, *opt_pr;
    value_t *val, *val_ps, *val_pr;
    
    assert(optionset_exists(optionset));

    for (opt = optionlist; opt; opt = opt->next) {
        if ((val = option_get_value(opt, optionset)))
            option_set_validated_value(opt, optionset, val->value, 1);
    }

    /* If the settings for "PageSize" and "PageRegion" are different,
       set the one for "PageRegion" to the one for "PageSize" and issue
       a warning.*/
    opt_ps = assure_option("PageSize");
    opt_pr = assure_option("PageRegion");
    val_ps = option_get_value(opt_ps, optionset);
    val_pr = option_get_value(opt_pr, optionset);

    if (!val_ps || !val_pr || strcmp(val_ps->value, val_pr->value)) {
        _log("Settings for \"PageSize\" and \"PageRegion\" are "
                "different:\n"
                "   PageSize: %s\n"
                "   PageRegion: %s\n"
                "Using the \"PageSize\" value \"%s\", for both.\n",
                val_ps ? val_ps->value : "",
                val_pr ? val_pr->value : "",
                val_ps ? val_ps->value : "");
        option_set_value(opt_pr, optionset, val_ps ? val_ps->value : "");
    }
}

void sync_pagesize(option_t *opt, const char *value, int optionset)
{
    option_t *other = NULL;
    setting_t *setting;

    if (!strcmp(opt->name, "PageSize"))
        other = find_option("PageRegion");
    else if (!strcmp(opt->name, "PageRegion"))
        other = find_option("PageSize");

    if (!other)
        return; /* Don't do anything if opt is not "PageSize" or "PageRegion"*/

    /* TODO doesn't this set the same in both cases?? */
    if ((setting = option_find_setting(other, value)))
        /* Standard paper size */
        option_set_value(other, optionset, setting->value);
    else
        /* Custom paper size */
        option_set_value(other, optionset, value);
}
