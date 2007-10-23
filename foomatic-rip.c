
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <errno.h>
#include <memory.h>
#include <ctype.h>
#include <stdarg.h>
#include <assert.h>
#include <time.h>


/* TODO will this be replaced? By who? */
#define RIP_VERSION "$Revision$"

/* Location of the configuration file "filter.conf" this file can be
 * used to change the settings of foomatic-rip without editing
 * foomatic-rip. itself. This variable must contain the full pathname
 * of the directory which contains the configuration file, usually
 * "/etc/foomatic".
 */
#ifndef CONFIG_PATH
#define CONFIG_PATH "/usr/local/etc/foomatic"
#endif

/* This is the location of the debug logfile (and also the copy of the
 * processed PostScript data) in case you have enabled debugging above.
 * The logfile will get the extension ".log", the PostScript data ".ps".
 */
#ifndef LOG_FILE
#define LOG_FILE "/tmp/foomatic-rip"
#endif

/* The loghandle */
FILE* logh = NULL;

void _log(const char* msg, ...)
{
    va_list ap;
    if (!logh)
        return;
    va_start(ap, msg);
    vfprintf(logh, msg, ap);
    fflush(logh);
    va_end(ap);
}


/* Constants used by this filter
 *
 * Error codes, as some spooles behave different depending on the reason why
 * the RIP failed, we return an error code. As I have only found a table of
 * error codes for the PPR spooler. If our spooler is really PPR, these
 * definitions get overwritten by the ones of the PPR version currently in
 * use.
 */
#define EXIT_PRINTED 0                          /* file was printed normally */
#define EXIT_PRNERR 1                           /* printer error occured */
#define EXIT_PRNERR_NORETRY 2                   /* printer error with no hope of retry */
#define EXIT_JOBERR 3                           /* job is defective */
#define EXIT_SIGNAL 4                           /* terminated after catching signal */
#define EXIT_ENGAGED 5                          /* printer is otherwise engaged (connection refused) */
#define EXIT_STARVED = 6;                       /* starved for system resources */
#define EXIT_PRNERR_NORETRY_ACCESS_DENIED 7     /* bad password? bad port permissions? */
#define EXIT_PRNERR_NOT_RESPONDING 8            /* just doesn't answer at all (turned off?) */
#define EXIT_PRNERR_NORETRY_BAD_SETTINGS 9      /* interface settings are invalid */
#define EXIT_PRNERR_NO_SUCH_ADDRESS 10          /* address lookup failed, may be transient */
#define EXIT_PRNERR_NORETRY_NO_SUCH_ADDRESS 11  /* address lookup failed, not transient */
#define EXIT_INCAPABLE 50                       /* printer wants (lacks) features or resources */


/* We don't know yet, which spooler will be used. If we don't detect
 * one.  we assume that we do spooler-less printing. Supported spoolers
 * are currently:
 *
 *   cups    - CUPS - Common Unix Printing System
 *   solaris - Solaris LP (possibly some other SysV LP services as well)
 *   lpd     - LPD - Line Printer Daemon
 *   lprng   - LPRng - LPR - New Generation
 *   gnulpr  - GNUlpr, an enhanced LPD (development stopped)
 *   ppr     - PPR (foomatic-rip runs as a PPR RIP)
 *   ppr_int - PPR (foomatic-rip runs as an interface)
 *   cps     - CPS - Coherent Printing System
 *   pdq     - PDQ - Print, Don't Queue (development stopped)
 *   direct  - Direct, spooler-less printing
 */
#define SPOOLER_CUPS      1
#define SPOOLER_SOLARIS   2
#define SPOOLER_LPD       3
#define SPOOLER_LPRNG     4
#define SPOOLER_GNULPR    5
#define SPOOLER_PPR       6
#define SPOOLER_PPR_INT   7
#define SPOOLER_CPS       8
#define SPOOLER_PDQ       9
#define SPOOLER_DIRECT    10


/* Filters to convert non-PostScript files */
#define NUM_FILE_CONVERTERS 3
char* fileconverters[] = {
    /* a2ps (converts also other files than text) */
    "a2ps -1 @@--medium=@@PAGESIZE@@ @@--center-title=@@JOBTITLE@@ -o -",
    /* enscript */
    "enscript -G @@-M @@PAGESIZE@@ @@-b \"Page $%|@@JOBTITLE@@ --margins=36:36:36:36 --mark-wrapped-lines=arrow --word-wrap -p-",
    /* mpage */
    "mpage -o -1 @@-b @@PAGESIZE@@ @@-H -h @@JOBTITLE@@ -m36l36b36t36r -f -P- -"
};


const char* shellescapes = "|<>&!$\'\"#*?()[]{}";

/* TODO move JCL options into a struct */

/* JCL prefix to put before the JCL options
 (Can be modified by a "*JCLBegin:" keyword in the ppd file): */
char jclbegin[256] = "\033%-12345X@PJL\n";

/* JCL command to switch the printer to the PostScript interpreter
 (Can be modified by a "*JCLToPSInterpreter:" keyword in the PPD file): */
char jcltointerpreter[256] = "";

/* JCL command to close a print job
 (Can be modified by a "*JCLEnd:" keyword in the PPD file): */
char jclend[256] = "\033%-12345X@PJL RESET\n";

/* Prefix for starting every JCL command
 (Can be modified by "*FoomaticJCLPrefix:" keyword in the PPD file): */
char jclprefix[256] = "@PJL ";
int jclprefixset = 0;



char printer_model[128] = "";
char jobid[128] = "";
char jobuser[128] = "";
char jobhost[128] = "";
char jobtitle[128] = "";
char copies[128] = "";
char optstr[1024] = ""; 
char postpipe[1024] = "";  /* command into which the output of this filter should be piped */
/* Set to 1 to insert postscript code for page accounting (CUPS only). */
int ps_accounting = 1;
char *accounting_prolog = ""; 


static inline int prefixcmp(const char* str, const char* prefix)
{
    return strncmp(str, prefix, strlen(prefix));
}

/*
 * Like strncpy, but omits characters for which omit_func returns true
 * It also assures that dest is zero terminated.
 * Returns a pointer to the position in 'src' right after the last byte that has been copied.
 */
const char * strncpy_omit(char* dest, const char* src, size_t n, int (*omit_func)(int))
{
    const char* psrc = src;
    char* pdest = dest;
    int cnt = n -1;
    if (!pdest)
        return;
    if (psrc) {
        while (*psrc != 0 && cnt > 0) {
            if (!omit_func(*psrc)) {
                *pdest = *psrc;
                pdest++;
                cnt--;
            }
            psrc++;
        }
    }
    *pdest = '\0';
    return psrc;
}
int omit_unprintables(int c) { return c>= '\x00' && c <= '\x1f'; }
int omit_shellescapes(int c) { return strchr(shellescapes, c) != NULL; }
int omit_specialchars(int c) { return omit_unprintables(c) || omit_shellescapes(c); }
int omit_whitespace(int c) { return c == ' ' || c == '\t'; }


/* copies characters from 'src' to 'dest', until 'src' contains a character from 'stopchars'
   will not copy more than 'max' chars
   dest will be zero terminated in either case
   returns a pointer to the position right after the last byte that has been copied
*/
const char * strncpy_tochar(char *dest, const char *src, size_t max, const char *stopchars)
{
    const char *psrc = src;
    char *pdest = dest;
    while (*psrc && --max > 0 && !strchr(stopchars, *psrc)) {
        *pdest = *psrc;
        pdest++;
        psrc++;
    }
    *pdest = '\0';
    return psrc;
}

void unhtmlify(char *dest, size_t size, const char *src)
{
    char *pdest = dest;
    const char *psrc = src;
    const char *repl;
    time_t t = time(NULL);
    struct tm *time = localtime(&t);
    char yearstr[5], monstr[3], mdaystr[3], hourstr[3], minstr[3], secstr[3];
    
    /* TODO this should be global and only set once */
    sprintf(yearstr, "%04d", time->tm_year + 1900);
    sprintf(monstr, "%02d", time->tm_mon + 1);
    sprintf(mdaystr, "%02d", time->tm_mday);
    sprintf(hourstr, "%02d", time->tm_hour);
    sprintf(minstr, "%02d", time->tm_min);
    sprintf(secstr, "%02d", time->tm_sec);
    

    while (*psrc && pdest - dest < size) {
    
        if (*psrc == '&') {
            psrc++;
            repl = NULL;
    
            /* Replace HTML/XML entities by the original characters */
            if (prefixcmp(psrc, "apos;"))
                repl = "\'";
            else if (prefixcmp(psrc, "quot;"))
                repl = "\"";
            else if (prefixcmp(psrc, "gt;"))
                repl = ">";
            else if (prefixcmp(psrc, "lt;"))
                repl = "<";
            else if (prefixcmp(psrc, "amp;"))
                repl = "&";
    
            /* Replace special entities by job data */
            else if (prefixcmp(psrc, "job;"))
                repl = jobid;
            else if (prefixcmp(psrc, "user;"))
                repl = jobuser;
            else if (prefixcmp(psrc, "host;"))
                repl = jobhost;
            else if (prefixcmp(psrc, "title;"))
                repl = jobtitle;
            else if (prefixcmp(psrc, "copies;"))
                repl = copies;
            /* TODO convert options list to a string and insert here */
            /* else if (prefixcmp(psrc, "options;"))
                repl = optstr; */
            else if (prefixcmp(psrc, "year;"))
                repl = yearstr;
            else if (prefixcmp(psrc, "month;"))
                repl = monstr;
            else if (prefixcmp(psrc, "day;"))
                repl = mdaystr;
            else if (prefixcmp(psrc, "hour;"))
                repl = hourstr;
            else if (prefixcmp(psrc, "min;"))
                repl = minstr;
            else if (prefixcmp(psrc, "sec;"))
                repl = secstr;
    
            if (repl) {
                strncpy(pdest, repl, size - (pdest - dest));
                pdest += strlen(repl);
                psrc = strchr(psrc, ';') +1;
            }
            else {
                psrc++;
            }
        }
        else {
            *pdest = *psrc;
            pdest++;
            psrc++;
        }
    }
    dest[size -1] = '\0';
}

/* Replace hex notation for unprintable characters in PPD files
   by the actual characters ex: "<0A>" --> chr(hex("0A")) */
void unhexify(char *dest, size_t size, const char *src)
{
    char *pdest = dest;
    char *psrc = (char*)src;
    long int n;

    while (*psrc && pdest - dest < size) {
        if (*psrc == '<') {
            n = strtol(psrc, &psrc, 16);
            *pdest = (char)n;
        }
        else {
            *pdest = *psrc;
            psrc++;
        }
        pdest++;
    }
    dest[size -1] = '\0';
}

/* Command line options */
struct option {
    char pagerange [64];
    char key [64];
    char value [128];
    struct option *next;
};

struct option_list {
    struct option *first, *last;
};

int option_has_pagerange(const struct option * op)
{
    return op->pagerange[0];
}

struct option_list * create_option_list()
{
    struct option_list *list = malloc(sizeof(struct option_list));
    list->first = NULL;
    list->last = NULL;
    return list;
}

void free_option_list(struct option_list *list)
{
    struct option *node = list->first, *tmp;
    while (node) {
        tmp = node;
        node = node->next;
        free(tmp);
    }
    free(list);
}

void option_list_append(struct option_list *list, struct option *op)
{
    if (list->last) {
        list->last->next = op;
        list->last = op;
    }
    else {
        list->first = op;
        list->last = list->first;
    }
}

/*
 * Adds options from 'optstr' to 'list'.
   The options are "foo='bar nut'", "foo", "nofoo", "'bar nut'", or
   "foo:'bar nut'" (when GPR was used) all with spaces between...
   In addition they can be preceeded by page ranges, separated with a colon.
 */
void option_list_append_from_string(struct option_list *list, const char *optstr)
{
    int cnt;
    const char *pstr = optstr;
    char *p;
    char stopchar;
    size_t pos;
    struct option *op;
    char characterstring [2] = " ";

    if (!optstr)
        return;

    while (1)
    {
        while (isspace(*pstr)) pstr++;
        if (*pstr == '\0')
            break;

        op = malloc(sizeof(struct option));
        option_list_append(list, op);
        op->next = NULL;

        /* read the pagerange if we have one */
        if (prefixcmp(pstr, "even:") == 0 || prefixcmp(pstr, "odd:") == 0 || isdigit(*pstr))
            pstr = strncpy_tochar(op->pagerange, pstr, 64, ":") +1;
        else
            op->pagerange[0] = '\0';

        /* read the key */
        if (*pstr == '\'' || *pstr == '\"') {
            characterstring[0] = *pstr;
            pstr = strncpy_tochar(op->key, pstr +1, 64, characterstring) +1;
        }
        else {
            pstr = strncpy_tochar(op->key, pstr, 64, ":= ");
        }

        if (*pstr != ':' && *pstr != '=') { /* no value for this option */
            op->value[0] = '\0';
            continue;
        }

        pstr++; /* skip the separator sign */

        if (*pstr == '\"' || *pstr == '\'') {
            characterstring[0] = *pstr;
            pstr = strncpy_tochar(op->value, pstr +1, 128, characterstring) +1;
        }
        else {
            pstr = strncpy_tochar(op->value, pstr, 128, " \t,");
        }

        /* skip whitespace and commas */
        while (isspace(*pstr) || *pstr == ',') pstr++;
    }
}

void print_option_list(struct option_list *list, FILE *stream)
{
    struct option *op = list->first;
    while (op) {
        if (op->value[0] == '\0')
            fprintf(stream, "%s\n", op->key);
        else
            fprintf(stream, "%s: %s\n", op->key, op->value);
        op = op->next;
    }
}

struct config {
    int debug;                   /* Set debug to 1 to enable the debug logfile for this filter; it will
                                    appear as defined by LOG_FILE. It will contain status from this
                                    filter, plus the renderer's stderr output. You can also add a line
                                    "debug: 1" to your /etc/foomatic/filter.conf to get all your
                                    Foomatic filters into debug mode.
                                    WARNING: This logfile is a security hole; do not use in production. */

    char execpath [128];         /* What path to use for filter programs and such. Your printer driver
                                    must be in the path, as must be the renderer, $enscriptcommand, and
                                    possibly other stuff. The default path is often fine on Linux, but
                                    may not be on other systems. */

    char cupsfilterpath[128];    /* CUPS raster drivers are searched here */

    char fileconverter[256];     /* Command for converting non-postscript files (especially text)
                                    to PostScript. */

};


void config_set_default_options(struct config *conf)
{
    conf->debug = 0;
    strcpy(conf->execpath, "/usr/local/bin:/usr/bin:/bin");
    strcpy(conf->cupsfilterpath, "/usr/local/lib/cups/filter:/usr/local/lib/cups/filter:/usr/local/libexec/cups/filter:/opt/cups/filter:/usr/lib/cups/filter");
    strcpy(conf->fileconverter, "");
}

void config_set_option(struct config *conf, const char *key, const char *value)
{
    if (strcmp(key, "debug") == 0) {
        conf->debug = atoi(value);
    }
    else if (strcmp(key, "execpath") == 0) {
        strncpy(conf->execpath, value, 127);
        conf->execpath[127] = '\0';
    }
    else if (strcmp(key, "cupsfilterpath") == 0) {
        strncpy(conf->cupsfilterpath, value, 127);
        conf->execpath[127] = '\0';
    }
    else if (strcmp(key, "textfilter") == 0) {
        if (strcmp(value, "a2ps") == 0)
            strcpy(conf->fileconverter, fileconverters[0]);
        else if (strcmp(value, "enscript") == 0)
            strcpy(conf->fileconverter, fileconverters[1]);
        else if (strcmp(value, "mpage") == 0)
            strcpy(conf->fileconverter, fileconverters[2]);
        else {
            strncpy(conf->fileconverter, value, 255);
            conf->fileconverter[255] = '\0';
        }
    }
    /* printf("setting %s to %s.\n", key, value); */
}

void config_read_from_file(struct config *conf, const char *filename)
{
    FILE *fh;
    char line[256];
    char *key, *value;

    fh = fopen(filename, "r");
    if (fh == NULL)
        return; /* no error here, only read config file if it is present */

    while (fgets(line, 256, fh) != NULL) {
        key = strtok(line, " :\t\r\n");
        if (key == NULL || key[0] == '#')
            continue;
        value = strtok(NULL, " :\t\r\n#");
        config_set_option(conf, key, value);
    }
    fclose(fh);
}

void extract_filename(char *dest, const char *path, size_t dest_size)
{
    const char *p = strrchr(path, '/');
    char *pdest = dest;
    if (!pdest)
        return;
    if (p)
        p += 1;
    else
        p = path;
    while (*p != 0 &&  *p != '.' && --dest_size > 0) {
        *pdest++ = *p++;
    }
    *pdest = '\0';
}

/* returns the index of the argument 'name', if it exists 
   returns -1 if not */
int argindex(const char* name, int argc, char** argv)
{
    int i;

    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], name) == 0)
            return i;
    }
    return -1;
}



struct ppd_argument_setting {
    char value [128];
    char comment [128];
    char driverval [256];
};

struct ppd_argument_setting_list {
    struct ppd_argument_setting *item;
    struct ppd_argument_setting_list *next;
};


#define ARG_TYPE_NONE      0
#define ARG_TYPE_ENUM      1
#define ARG_TYPE_PICKMANY  2
#define ARG_TYPE_BOOL      3

/* TODO maybe this should be renamed to 'ppd_option' ? */
struct ppd_argument {
    char name [128];
    char comment [128];
    char style;
    char proto [128];
    int type;        /* one of the ARG_TYPE_xxx defines */
    char spot;
    int min, max;
    int maxlength;
    char *allowedchars;
    char *allowedregexp;
    int order;
    char section [128];
    char defaultvalue [128];
    int fdefault;  /* Foomatic default */
    struct ppd_argument_setting_list *settings; /* boolean values have 2 settings: "true" and "false" */
    struct ppd_argument_value_list *values;
};

struct ppd_argument_list {
    struct ppd_argument *item;
    struct ppd_argument_list *next;
};

struct ppd_options {
    char id [128];
    char driver [128];
    char cmd [1024];
    char cupsfilter [256];      /* cups filter for mime type "application/vnd.cups-raster" */
    char colorprofile [128];    /* TODO substitute for %X and %W in 'cmd' (see perl version line 1850) */
    struct ppd_argument_list *arguments;
};

struct ppd_argument_list * ppd_options_find_argument_list(struct ppd_options *opts, const char *argname)
{
    struct ppd_argument_list *p;
    for (p = opts->arguments; p; p = p->next) {
        if (strcmp(p->item->name, argname) == 0)
            return p;
    }
    return NULL;
}

/* finds an argument in a case insensetive way */
struct ppd_argument * ppd_options_find_argument(struct ppd_options *opts, const char *argname)
{
    struct ppd_argument_list *p;
    for (p = opts->arguments; p; p = p->next) {
        if (strcasecmp(p->item->name, argname) == 0)
            return p->item;
    }
    return NULL;
}

/* case insensitive */
struct ppd_argument_setting * ppd_argument_find_setting(struct ppd_argument *arg, const char *settingname)
{
    struct ppd_argument_setting_list *p;
    for (p = arg->settings; p; p = p->next) {
        if (strcasecmp(p->item->value, settingname) == 0)
            return p->item;
    }
    return NULL;
}

void ppd_options_free_argument(struct ppd_argument * arg)
{
    struct ppd_argument_setting_list *p = arg->settings, *tmp;
    while (p) {
        tmp = p;
        p = p->next;
        free(tmp);
    }
    free(arg->allowedchars);
    free(arg->allowedregexp);
}

/* check if there already is an argument record 'arg' in 'opts', if not, create one */
static struct ppd_argument * ppd_options_checkarg(struct ppd_options *opts, const char *argname)
{
    struct ppd_argument_list *p, *last = NULL;
    struct ppd_argument *arg;

    for (p = opts->arguments; p; p = p->next) {
        if (strcmp(p->item->name, argname) == 0)
            return p->item;
        last = p;
    }

    arg = malloc(sizeof(struct ppd_argument));
    strncpy(arg->name, argname, 127);
    arg->name[127] = '\0';

    /* Default execution style is 'G' (PostScript) since all arguments for
       which we don't find "*Foomatic..." keywords are usual PostScript options */
    arg->style = 'G';

    /* Default prototype for code to insert, used by enum options */
    strcpy(arg->proto, "%s");

    arg->comment[0] = '\0';

    arg->spot = 0;
    arg->type = ARG_TYPE_NONE;

    arg->min = arg->max = -1;
    arg->maxlength = -1;
    arg->allowedchars = NULL;
    arg->allowedregexp = NULL;
    arg->order = 0;
    arg->section[0] = '\0';
    arg->defaultvalue[0] = '\0';

    /* insert arg into opts->arguments list */
    p = malloc(sizeof(struct ppd_argument_list));
    p->item = arg;
    p->next = NULL;
    if (!last)
        opts->arguments = p;
    else
        last->next = p;

    _log("Added option %s\n", arg->name);
    return arg;
}

/* Check if there already is a choice record 'setting' in the 'argname' argument in 'opts',
   if not, create one */
static struct ppd_argument_setting* ppd_options_checksetting(struct ppd_options *opts, const char *argname, const char *setting_name, const char *comment)
{
    struct ppd_argument_list *argitem = ppd_options_find_argument_list(opts, argname);
    struct ppd_argument_setting_list *p, *last = NULL;
    struct ppd_argument_setting *setting;

    if (!argitem) /* given argument does not exist */
        return NULL;

    /* is the setting already present */
    for (p = argitem->item->settings; p; p = p->next) {
        if (strcmp(p->item->value, setting_name) == 0)
            return p->item;
        last = p;
    }

    setting = malloc(sizeof(struct ppd_argument_setting));
    strncpy(setting->value, setting_name, 127);
    setting->value[127] = '\0';
    if (comment) {
        strncpy(setting->comment, comment, 127);
        setting->comment[127] = '\0';
    }
    else
        setting->comment[0] = '\0';
    setting->driverval[0] = '\0';

    p = malloc(sizeof(struct ppd_argument_setting_list));
    p->item = setting;
    p->next = NULL;
    if (!last)
        argitem->item->settings = p;
    else
        last->next = p;

    /* _log("Added setting %s to argument %s\n", setting->value, argitem->item->name); */
    return setting;
}

void free_ppd_options(struct ppd_options *opts)
{
    struct ppd_argument_list *ap = opts->arguments, *atmp;

    while (ap) {
        atmp = ap;
        ap = ap->next;
        ppd_options_free_argument(atmp->item);
        free(atmp);
    }
    free(opts);
}

struct ppd_options* parse_ppd_file(const char* filename)
{
    struct ppd_options* opts;
    FILE *fh;
    char *line;
    int buf_size = 256;
    size_t read, line_len;
    char *p;
    char *key, *argname;
    char *value;
    char tmp[1024];
    struct ppd_argument *arg, *current_arg;
    struct ppd_argument_setting *setting, *setting2;
    int len;

    fh = fopen(filename, "r");
    if (!fh) {
        _log("error opening %s\n", filename);
        /* TODO quit gracefully */
        exit(EXIT_PRNERR_NORETRY_BAD_SETTINGS);
    }
    _log("Parsing PPD file ...\n");

    opts = malloc(sizeof(struct ppd_options));
    opts->id[0] = '\0';
    opts->driver[0] = '\0';
    opts->cmd[0] = '\0';
    opts->cupsfilter[0] = '\0';

    line = (char*)malloc(buf_size);
    while (!feof(fh)) {

        /* Read entire current line (including contination on the next line) */
        p = line;
        line_len = 0;
        while (1) {
            fgets(p, buf_size - line_len, fh);
            read = strlen(p);
            assert(read > 0); /* should always read at least \n or EOF */

            if (p[read -1] == EOF) {
                p[read -1] = '\0';
                read -= 1;
                break;
            }

            /* remove dos style newlines */
            if (read > 2 && p[read -2] == '\r' && p[read -1] == '\n') {
                p[read -2] = '\n';
                p[read -1] = '\0';
                read -= 1;
            }

            /* the line continues after the \n if it ends on '&&', or if we didn't read a newline yet */
            if ((read > 3 && p[read -2] == '&' && p[read -3] == '&') || (p[read -1] != '\n')) {
                p = &p[read -3];
                if (buf_size - read < buf_size / 2) {
                    buf_size *= 2;
                    line = (char*)realloc(line, buf_size);
                }
            }
            else
                break;
        }

        /* extract key/value from ppd line */
        key = strchr(line, '*');
        if (!key)
            continue;
        else
            key += 1; /* move to the next char behind the '*' */
        if (key[0] == '%')
            continue;

        value = strchr(key, ':');
        if (!value)
            continue;
        *value = '\0'; /* 0-terminate the key string */
        value += 1;
        while (isspace(*value) && *value != '\0')
            value += 1;

        if (*value == '\"') { /* remove enclosing quotation marks from value (if present) */
            value += 1;
            p = strrchr(value, '\"');
            if (p)
                *p = '\0';
        }
        /* remove newline (if it is still present ) */
        p = strchr(value, '\n');
        if (p)
            *p = '\0';


        /* process key/value pairs */
         if (strcmp(key, "NickName") == 0) {
            unhtmlify(printer_model, 128, value);
        }
        else if (strcmp(key, "FoomaticIDs") == 0) { /* *FoomaticIDs: <printer ID> <driver ID> */
            p = strtok(value, " \t");
            strncpy(opts->id, p, 127);
            opts->id[127] = '\0';
            p = strtok(NULL, " \t");
            strncpy(opts->driver, p, 127);
            opts->driver[127] = '\0';
        }
        else if (strcmp(key, "FoomaticRIPPostPipe") == 0) {
            unhtmlify(postpipe, 1024, value);
        }
        else if (strcmp(key, "FoomaticRIPCommandLine") == 0) {
            unhtmlify(opts->cmd, 1024, value);
        }
        else if (strcmp(key, "FoomaticNoPageAccounting") == 0) { /* Boolean value */
            if (strcasecmp(value, "true")) { /* TODO strcasecmp is POSIX.1-2001, is that ok? */
                /* Driver is not compatible with page accounting according to the
                   Foomatic database, so turn it off for this driver */
                ps_accounting = 0;
                accounting_prolog = "";
                _log("CUPS page accounting disabled by driver.\n");
            }
        }
        else if (strcmp(key, "cupsFilter") == 0) { /* cupsFilter: <code> */
            /* only save the filter for "application/vnd.cups-raster" */
            if (prefixcmp(value, "application/vnd.cups-raster") == 0) {
                p = strrchr(value, ' ');
                unhtmlify(opts->cupsfilter, 256, p);
            }
        }
        else if (strcmp(key, "CustomPageSize True") == 0) {
            ppd_options_checkarg(opts, "PageSize");
            ppd_options_checkarg(opts, "PageRegion");
            setting = ppd_options_checksetting(opts, "PageSize", "Custom", "Custom Size");
            setting2 = ppd_options_checksetting(opts, "PageRegion", "Custom", "Custom Size");
            if (setting && setting2 && prefixcmp(value, "%% FoomaticRIPOptionSetting") != 0) {
                strncpy(setting->driverval, value, 255);
                setting->driverval[255] = '\0';
                strncpy(setting2->driverval, value, 255);
                setting2->driverval[255] = '\0';
            }
        }
        else if (prefixcmp(key, "OpenUI") == 0 || prefixcmp(key, "JCLOpenUI") == 0) {
            /* "*[JCL]OpenUI *<option>[/<translation>]: <type>" */
            if (!(argname = strchr(key, '*')))
                continue;
            argname += 1;
            p = strchr(argname, '/');
            if (p) {
                *p = '\0';
                p += 1;
            }
            current_arg = ppd_options_checkarg(opts, argname);
            if (p) {
                strncpy(current_arg->comment, p, 127);
                current_arg->comment[127] = '\0';
            }

            /* Set the argument type only if not defined yet,
            a definition in "*FoomaticRIPOption" has priority */
            if (current_arg->type == ARG_TYPE_NONE) {
                if (strcmp(value, "PickOne") == 0)
                    current_arg->type = ARG_TYPE_ENUM;
                else if (strcmp(value, "PickMany") == 0)
                    current_arg->type = ARG_TYPE_PICKMANY;
                else if (strcmp(value, "Boolean") == 0)
                    current_arg->type = ARG_TYPE_BOOL;
            }
        }
        else if (strcmp(key, "CloseUI") == 0 || strcmp(key, "JCLCloseUI") == 0) {
            /* *[JCL]CloseUI: *<option> */
            value = strchr(value, '*');
            if (!value || !current_arg || strcmp(current_arg->name, value +1) != 0)
                _log("CloseUI found without corresponding OpenUI (%s).\n", value +1);
            current_arg = NULL;
        }
        else if (prefixcmp(key, "FoomaticRIPOption ") == 0) {
            /* "*FoomaticRIPOption <option>: <type> <style> <spot> [<order>]"
               <order> only used for 1-choice enum options */
            if (!(argname = strchr(key, ' ')))
                continue;
            argname += 1;
            arg = ppd_options_checkarg(opts, argname);

            p = strtok(value, " \t"); /* type */
            if (strcmp(p, "enum") == 0)
                arg->type = ARG_TYPE_ENUM;
            else if (strcmp(p, "pickmany") == 0)
                arg->type = ARG_TYPE_PICKMANY;
            else if (strcmp(p, "bool") == 0)
                arg->type = ARG_TYPE_BOOL;

            p = strtok(NULL, " \t"); /* style */
            if (strcmp(p, "PS") == 0)
                arg->style = 'G';
            else if (strcmp(p, "CmdLine") == 0)
                arg->style = 'C';
            else if (strcmp(p, "JCL") == 0)
                arg->style = 'J';
            else if (strcmp(p, "Composite") == 0)
                arg->style = 'X';

            p = strtok(NULL, " \t");
            arg->spot = *p;

            /* TODO order - which format? */
        }
        else if (prefixcmp(key, "FoomaticRIPOptionPrototype") == 0) {
            /* "*FoomaticRIPOptionPrototype <option>: <code>"
               Used for numerical and string options only */
            if (!(argname = strchr(key, ' ')))
                continue;
            argname += 1;
            arg = ppd_options_checkarg(opts, argname);

            unhtmlify(arg->proto, 128, value);
        }
        else if (prefixcmp(key, "FoomaticRIPOptionRange") == 0) {
            /* *FoomaticRIPOptionRange <option>: <min> <max>
               Used for numerical options only */
            if (!(argname = strchr(key, ' ')))
                continue;
            argname += 1;
            arg = ppd_options_checkarg(opts, argname);

            p = strtok(value, " \t"); /* min */
            arg->min = atoi(p);
            p = strtok(NULL, " \t"); /* max */
            arg->max = atoi(p);
        }
        else if (prefixcmp(key, "FoomaticRIPOptionMaxLength") == 0) {
            /*     "*FoomaticRIPOptionMaxLength <option>: <length>"
                Used for string options only */
            if (!(argname = strchr(key, ' ')))
                continue;
            argname += 1;
            arg = ppd_options_checkarg(opts, argname);

            arg->maxlength = atoi(value);
        }
        else if (prefixcmp(key, "FoomaticRIPOptionAllowedChars") == 0) {
            /* *FoomaticRIPOptionAllowedChars <option>: <code> */
            if (!(argname = strchr(key, ' ')))
                continue;
            argname += 1;
            arg = ppd_options_checkarg(opts, argname);

            len = strlen(value) +1;
            arg->allowedchars = malloc(len);
            unhtmlify(arg->allowedchars, len, value);
        }
        else if (prefixcmp(key, "FoomaticRIPOptionAllowedRegExp") == 0) {
            /* "*FoomaticRIPOptionAllowedRegExp <option>: <code>"
               Used for string options only */
            if (!(argname = strchr(key, ' ')))
                continue;
            argname += 1;
            arg = ppd_options_checkarg(opts, argname);

            len = strlen(value) +1;
            arg->allowedregexp = malloc(len);
            unhtmlify(arg->allowedregexp, len, value);
        }
        else if (strcmp(key, "OrderDependency") == 0) {
            /* OrderDependency: <order> <section> *<option> */
            if (!(argname = strchr(value, '*')))
                continue;
            argname += 1;
            arg = ppd_options_checkarg(opts, argname);

            p = strtok(value, " \t");
            arg->order = atoi(p);
            p = strtok(NULL, " \t");
            strncpy(arg->section, p, 127);
            arg->section[127] = '\0';
        }
        else if (prefixcmp(key, "Default") == 0) {
            /* Default<option>: <value> */
            argname = &key[7];
            arg = ppd_options_checkarg(opts, argname);
            strncpy(arg->defaultvalue, value, 127);
            arg->defaultvalue[127] = '\0';
        }
        else if (prefixcmp(key, "FoomaticRIPDefault") == 0) {
            /* FoomaticRIPDefault<option>: <value>
               Used for numerical options only */
            argname = &key[18];
            arg = ppd_options_checkarg(opts, argname);
            arg->fdefault = atoi(value);
        }
        else if (current_arg && prefixcmp(key, current_arg->name) == 0) { /* current argument */
            /* *<option> <choice>[/translation]: <code> */
            arg = current_arg;

            while (*key && isalnum(*key)) key++;
            while (*key && isspace(*key)) key++; /* 'key' now points to the choice string */
            if (!key)
                continue;

            p = strrchr(key, '/'); /* translation present? */
            if (p) {
                *p = '\0';
                p += 1; /* points to translation string */
            }

            if (arg->type == ARG_TYPE_BOOL) {
                /* make sure that boolean arguments always have exactly two settings: 'true' and 'false' */
                if (strcasecmp(key, "true") == 0)
                    setting = ppd_options_checksetting(opts, arg->name, "true", p);
                else
                    setting = ppd_options_checksetting(opts, arg->name, "false", p);
            }
            else {
                setting = ppd_options_checksetting(opts, arg->name, key, p);
                /* Make sure that this argument has a default setting, even if
                   none is defined in the PPD file */
                if (!*arg->defaultvalue) {
                    strncpy(arg->defaultvalue, key, 127);
                    arg->defaultvalue[127] = '\0';
                }
            }

            if (prefixcmp(value, "%% FoomaticRIPOptionSetting") != 0) {
                strncpy(setting->driverval, value, 255);
                setting->driverval[255] = '\0';
            }
        }
        else if (prefixcmp(key, "FoomaticRIPOptionSetting") == 0) {
            /* "*FoomaticRIPOptionSetting <option>[=<choice>]: <code>
               For boolean options <choice> is not given */
            argname = key;
            while (*argname && isalnum(*argname)) argname++;
            while (*argname && isspace(*argname)) argname++;

            p = strchr(argname, '=');
            if (p) {
                *p = 0;
                p += 1; /* points to <choice> */
            }
            arg = ppd_options_checkarg(opts, argname);

            if (p) {
                setting = ppd_options_checksetting(opts, argname, p, NULL);
                /* Make sure this argument has a default setting, even if
                   none is defined in this PPD file */
                if (!*arg->defaultvalue) {
                    strncpy(arg->defaultvalue, p, 127);
                    arg->defaultvalue[127] = '\0';
                }
                strncpy(setting->driverval, value, 255);
                setting->driverval[255] = '\0';
            }
            else {
                unhtmlify(arg->proto, 128, value);
            }
        }
        else if (prefixcmp(key, "JCL") == 0 || prefixcmp(key, "FoomaticJCL") == 0) {
            /*     "*(Foomatic|)JCL(Begin|ToPSInterpreter|End|Prefix): <code>"
                The printer supports PJL/JCL when there is such a line */

            key = strstr(key, "JCL") + 3;
            if (strcmp(key, "Begin") == 0) {
                unhexify(jclbegin, 256, value);
                if (!jclprefixset && strstr(jclbegin, "PJL") == NULL)
                    jclprefix[0] = '\0';
            }
            else if (strcmp(key, "ToPSInterpreter") == 0) {
                unhexify(jcltointerpreter, 256, value);
            }
            else if (strcmp(key, "End") == 0) {
                unhexify(jclend, 256, value);
            }
            else if (strcmp(key, "Prefix") == 0) {
                unhexify(jclprefix, 256, value);
                jclprefixset = 1;
            }
        }
        else if (prefixcmp(key, "% COMDATA #") == 0) {
            /* old foomtic 2.0.x PPD file */
            _log("You are using an old Foomatic 2.0 PPD file, which is no longer supported by Foomatic >4.0.\n");
            exit(1); /* TODO exit more gracefully */
        }
    }

    free(line);
    fclose(fh);
    return opts;
}

/* TODO clean up global vars */
int spooler = SPOOLER_DIRECT;
int do_docs = 0;
struct ppd_options * ppdopts;

/* Variable for PPR's backend interface name (parallel, tcpip, atalk, ...) */
char backend [64];


/* processes the command line options */
void process_options(struct option_list *cmdl_options)
{
    struct option *op = cmdl_options->first;
    struct ppd_argument *arg;
    struct ppd_argument_setting *setting;
    char *p;

    while (op) {
        /* "docs" option to print help page */
        if (strcmp(op->key, "docs") == 0 && (!op->value[0] || strcmp(op->value, "true") == 0)) {
            do_docs = 1;
            continue;
        }
        /* "profile" option to supply a color correction profile to a CUPS raster driver */
        if (strcmp(op->key, "profile") == 0) {
            strncpy(ppdopts->colorprofile, op->value, 127);
            ppdopts->colorprofile[127] = '\0';
            continue;
        }
        if (option_has_pagerange(op) && (arg = ppd_options_find_argument(ppdopts, op->key)) &&
            (!arg->section[0] || prefixcmp(arg->section, "AnySetup") != 0 || prefixcmp(arg->section, "PageSetup") != 0)) {
            _log("This option (%s) is not a \"PageSetup\" or \"AnySetup\" option, so it cannot be restricted to a page range.\n", op->key);
            continue;
        }

        /* At first look for the "backend" option to determine the PPR backend to use */
        if (spooler == SPOOLER_PPR_INT && strcasecmp(op->key, "backend") == 0) {
            /* backend interface name */
            strncpy(backend, op->value, 63);
            backend[63] = '\0';
        }
        else if (strcasecmp(op->key, "media") == 0) {
            /*  Standard arguments?
                media=x,y,z
                sides=one|two-sided-long|short-edge

                Rummage around in the media= option for known media, source,
                etc types.
                We ought to do something sensible to make the common manual
                boolean option work when specified as a media= tray thing.

                Note that this fails miserably when the option value is in
                fact a number; they all look alike.  It's unclear how many
                drivers do that.  We may have to standardize the verbose
                names to make them work as selections, too. */

            /* TODO !!! */
            /* p = strtok(op->value, ",");
            do {
                if (arg = ppd_options_find_argument(ppdopts, "PageSize") {
                    if (setting = ppd_argument_find_setting(arg, p)) {
                    }
                    else if (
                }

            } while (p = strtok(NULL, ","));
            */
        }
        /* TODO */
    }
}

int main(int argc, char** argv)
{
    struct config conf;
    char ppdfile[256]; /* 256 sensible? */
    char printer[256];
    struct option_list* options = create_option_list();
    struct option* op = NULL;
    int i;
    int verbose = 0, quiet = 0, showdocs = 0;
    const char* str;
    char* p;
    const char *jobhost, *jobuser, *jobtitle;  /* TODO fill jobuser with `whoami` */

     /* stringlist_t* rargs = createStringList(); */

    config_set_default_options(&conf);
    config_read_from_file(&conf, CONFIG_PATH "/filter.conf");
    setenv("PATH", conf.execpath, 1);

    ppdfile[0] = '\0';
    printer[0] = '\0';


    /* General command line options */
    for (i = 1; i < argc; i++) {
        if (strcmp("-v", argv[i]) == 0)
            verbose = 1;
        else if (strcmp("-q", argv[i]) == 0)
            quiet = 1;
        else if (strcmp("-d", argv[i]) == 0)
            showdocs = 1;
        else if (strcmp("--debug", argv[i]) == 0)
            conf.debug = 1;
    }

    if (conf.debug)
        logh = fopen(LOG_FILE ".log", "r"); /* insecure, use for debugging only */
    else if (quiet && !verbose)
        logh = NULL; /* Quiet mode, do not log*/
    else
        logh = stderr; /* Default: log to stderr */

    /* Start debug logging */
    if (conf.debug) {
        /* If we are not in debug mode, we do this later, as we must find out at
        first which spooler is used. When printing without spooler we
        suppress logging because foomatic-rip is called directly on the
        command line and so we avoid logging onto the console. */
        _log("foomatic-rip version "RIP_VERSION" running...\n");

        /* Print the command line only in debug mode, Mac OS X adds very many
        options so that CUPS cannot handle the output of the command line
        in its log files. If CUPS encounters a line with more than 1024
        characters sent into its log files, it aborts the job with an error. */
        if (spooler != SPOOLER_CUPS) {
            _log("called with arguments: ");
            for (i = 0; i < argc -1; i++)
                _log("\'%s\', ", argv[i]);
            _log("\'%s\'\n");
        }
    }


    /* finding out the spooler which called us is taken directly from the perl
       program to make sure the logic stays the same. TODO optimize */

    if (getenv("PPD")) {
        strncpy_omit(ppdfile, getenv("PPD"), 256, omit_specialchars);
        spooler = SPOOLER_CUPS;
    }

    if (getenv("SPOOLER_KEY")) {
        spooler = SPOOLER_SOLARIS;
        /* set the printer name from the ppd file name */
        strncpy_omit(ppdfile, getenv("PPD"), 256, omit_specialchars);
        extract_filename(printer, ppdfile, 256);
        /* TODO read attribute file*/
    }

    if (getenv("PPR_VERSION"))
        spooler = SPOOLER_PPR;

    if (getenv("PPR_RIPOPTS")) {
        /* PPR 1.5 allows the user to specify options for the PPR RIP with the
           "--ripopts" option on the "ppr" command line. They are provided to
           the RIP via the "PPR_RIPOPTS" environment variable. */
        option_list_append_from_string(options, getenv("PPR_RIPOPTS"));
        spooler = SPOOLER_PPR;
    }

    if (getenv("LPOPTS")) { /* "LPOPTS": Option settings for some LPD implementations (ex: GNUlpr) */
        spooler = SPOOLER_GNULPR;
        option_list_append_from_string(options, getenv("LPOPTS"));
    }

    /* Check for LPRng first so we do not pick up bogus ppd files by the -ppd option */
    if (argindex("--lprng", argc, argv) > 0)
        spooler = SPOOLER_LPRNG;

    /* 'PRINTCAP_ENTRY' environment variable is : LPRng
       the :ppd=/path/to/ppdfile printcap entry should be used */
    if (getenv("PRINTCAP_ENTRY")) {
        spooler = SPOOLER_LPRNG;
        if (str = strstr(getenv("PRINTCAP_ENTRY"), "ppd="))
            str += 4;
        else if (str = strstr(getenv("PRINTCAP_ENTRY"), "ppdfile="));
        str += 8;
        if (str) {
            while (isspace(*str)) str++;
            p = ppdfile;
            while (*str != '\0' && !isspace(*str) && *str != '\n') {
                if (isprint(*str) && strchr(shellescapes, *str) == NULL)
                    *p++ = *str;
                str++;
            }
        }
    }

    /* PPD file name given via the command line
       allow duplicates, and use the last specified one */
    if (spooler != SPOOLER_LPRNG) {
        for (i = 1; i < argc; i++) {
            if (strcmp(argv[i], "-p") == 0 || prefixcmp(argv[i], "--ppd") /* could be --ppd= */) {
                if (i +1 < argc)
                    strncpy_omit(ppdfile, argv[i+1], 256, omit_shellescapes);
                i += 1;
            }
        }
    }

    /* Check for LPD/GNUlpr by typical options which the spooler puts onto
       the filter's command line (options "-w": text width, "-l": text
       length, "-i": indent, "-x", "-y": graphics size, "-c": raw printing,
       "-n": user name, "-h": host name) */
    for (i = 1; i < argc; i++) {
        if (strlen(argv[i]) == 2 && argv[i][0] == '-' && i +1 < argc) {
            switch (argv[i][1]) {
                case 'h':
                    if (spooler != SPOOLER_GNULPR && spooler != SPOOLER_LPRNG) {
                        spooler = SPOOLER_LPD;
                        jobhost = argv[i +1];
                    }
                    break;
                case 'n':
                    if (spooler != SPOOLER_GNULPR && spooler != SPOOLER_LPRNG) {
                        spooler = SPOOLER_LPD;
                        jobuser = argv[i +1];
                    }
                    break;
                case 'w':
                case 'l':
                case 'x':
                case 'y':
                case 'i':
                case 'c':
                    if (spooler != SPOOLER_GNULPR && spooler != SPOOLER_LPRNG)
                        spooler = SPOOLER_LPD;
                    break;
                case 'Z': /* LPRng dilvers the option settings via the "-Z" argument */
                    spooler = SPOOLER_LPRNG;
                    if (i +1 < argc)
                        option_list_append_from_string(options, argv[i +1]);
                    break;
                case 'j': /* Job title and options for stock LPD */
                case 'J':
                    if (i +1 < argc)
                        jobtitle = argv[i +1];
                    if (spooler == SPOOLER_LPD) { /* classic LPD hack */
                        op = malloc(sizeof(struct option));
                        strncpy(op->key, "jobtitle", 127);
                        op->key[127] = '\0';
                        option_list_append(options, op);
                    }
                    break;
            }
        }
    }

    /* Check for CPS */
    if (argindex("--cps", argc, argv) > 0)
        spooler = SPOOLER_CPS;
    
    /* Options for spooler-less printing, CPS, or PDQ */
    while (i = argindex("-o", argc, argv) > 0) {
        if (i +1 < argc)
            option_list_append_from_string(options, argv[i +1]);
        /* If we don't print as PPR RIP or as CPS filter, we print
           without spooler (we check for PDQ later) */
        if (spooler != SPOOLER_PPR && spooler != SPOOLER_CPS)
            spooler = SPOOLER_DIRECT;
    }
    
    /* Printer for spooler-less printing or PDQ */
    if (i = argindex("-d", argc, argv) > 0 && i +1 < argc)
        strncpy_omit(printer, argv[i +1], 256, omit_shellescapes);
    
    /* Printer for spooler-less printing, PDQ, or LPRng */
    if (i = argindex("-P", argc, argv) > 0 && i +1 < argc)
        strncpy_omit(printer, argv[i +1], 256, omit_shellescapes);
    
    /* Were we called from a PDQ wrapper? */
    if (argindex("--pdq", argc, argv))
        spooler = SPOOLER_PDQ;
    
    /* Were we called to build the PDQ driver declaration file?
       "--appendpdq=<file>" appends the data to the <file>,
       "--genpdq=<file>" creates/overwrites <file> for the data, and
       "--genpdq" writes to standard output */
     /* TODO */


    ppdopts = parse_ppd_file("/etc/cups/ppd/hp1012.ppd");

    process_options(options);

    free_option_list(options);
    free_ppd_options(ppdopts);
    return 0;
}
