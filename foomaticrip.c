
#include "foomaticrip.h"
#include "util.h"
#include "options.h"
#include "pdf.h"

#include <stdio.h>
#include <stdlib.h>
#include <strings.h>
#include <errno.h>
#include <memory.h>
#include <ctype.h>
#include <stdarg.h>
#include <assert.h>
#include <time.h>
#include <unistd.h>
#include <sys/wait.h>
#include <math.h>
#include <signal.h>
#include <fcntl.h>
#include <pwd.h>


/* Returns a static string */
const char *spooler_name(int spooler)
{
    switch (spooler) {
        case SPOOLER_CUPS: return "cups";
        case SPOOLER_SOLARIS: return "solaris";
        case SPOOLER_LPD: return "lpd";
        case SPOOLER_LPRNG: return "lprng";
        case SPOOLER_GNULPR: return "gnu lpr";
        case SPOOLER_PPR: return "ppr";
        case SPOOLER_PPR_INT: return "ppr interface";
        case SPOOLER_CPS: return "cps";
        case SPOOLER_PDQ: return "pdq";
        case SPOOLER_DIRECT: return "direct";
    };
    return "<unknown>";
}

/*  This piece of PostScript code (initial idea 2001 by Michael
    Allerhand (michael.allerhand at ed dot ac dot uk, vastly
    improved by Till Kamppeter in 2002) lets GhostScript output
    the page accounting information which CUPS needs on standard
    error.
    Redesign by Helge Blischke (2004-11-17):
    - As the PostScript job itself may define BeginPage and/or EndPage
    procedures, or the alternate pstops filter may have inserted
    such procedures, we make sure that the accounting routine
    will safely coexist with those. To achieve this, we force
    - the accountint stuff to be inserted at the very end of the
        PostScript job's setup section,
    - the accounting stuff just using the return value of the
        existing EndPage procedure, if any (and providing a default one
        if not).
    - As PostScript jobs may contain calls to setpagedevice "between"
    pages, e.g. to change media type, do in-job stapling, etc.,
    we cannot rely on the "showpage count since last pagedevice
    activation" but instead count the physical pages by ourselves
    (in a global dictionary).
*/
const char *accounting_prolog_code =
    "[{\n"
    "%% Code for writing CUPS accounting tags on standard error\n"
    "\n"
    "/cupsPSLevel2 % Determine whether we can do PostScript level 2 or newer\n"
    "    systemdict/languagelevel 2 copy\n"
    "    known{get exec}{pop pop 1}ifelse 2 ge\n"
    "def\n"
    "\n"
    "cupsPSLevel2\n"
    "{                    % in case of level 2 or higher\n"
    "    currentglobal true setglobal    % define a dictioary foomaticDict\n"
    "    globaldict begin        % in global VM and establish a\n"
    "    /foomaticDict            % pages count key there\n"
    "    <<\n"
    "        /PhysPages 0\n"
    "    >>def\n"
    "    end\n"
    "    setglobal\n"
    "}if\n"
    "\n"
    "/cupsGetNumCopies { % Read the number of Copies requested for the current\n"
    "            % page\n"
    "    cupsPSLevel2\n"
    "    {\n"
    "    % PS Level 2+: Get number of copies from Page Device dictionary\n"
    "    currentpagedevice /NumCopies get\n"
    "    }\n"
    "    {\n"
    "    % PS Level 1: Number of copies not in Page Device dictionary\n"
    "    null\n"
    "    }\n"
    "    ifelse\n"
    "    % Check whether the number is defined, if it is \"null\" use #copies \n"
    "    % instead\n"
    "    dup null eq {\n"
    "    pop #copies\n"
    "    }\n"
    "    if\n"
    "    % Check whether the number is defined now, if it is still \"null\" use 1\n"
    "    % instead\n"
    "    dup null eq {\n"
    "    pop 1\n"
    "    } if\n"
    "} bind def\n"
    "\n"
    "/cupsWrite { % write a string onto standard error\n"
    "    (%stderr) (w) file\n"
    "    exch writestring\n"
    "} bind def\n"
    "\n"
    "/cupsFlush    % flush standard error to make it sort of unbuffered\n"
    "{\n"
    "    (%stderr)(w)file flushfile\n"
    "}bind def\n"
    "\n"
    "cupsPSLevel2\n"
    "{                % In language level 2, we try to do something reasonable\n"
    "  <<\n"
    "    /EndPage\n"
    "    [                    % start the array that becomes the procedure\n"
    "      currentpagedevice/EndPage 2 copy known\n"
    "      {get}                    % get the existing EndPage procedure\n"
    "      {pop pop {exch pop 2 ne}bind}ifelse    % there is none, define the default\n"
    "      /exec load                % make sure it will be executed, whatever it is\n"
    "      /dup load                    % duplicate the result value\n"
    "      {                    % true: a sheet gets printed, do accounting\n"
    "        currentglobal true setglobal        % switch to global VM ...\n"
    "        foomaticDict begin            % ... and access our special dictionary\n"
    "        PhysPages 1 add            % count the sheets printed (including this one)\n"
    "        dup /PhysPages exch def        % and save the value\n"
    "        end                    % leave our dict\n"
    "        exch setglobal                % return to previous VM\n"
    "        (PAGE: )cupsWrite             % assemble and print the accounting string ...\n"
    "        16 string cvs cupsWrite            % ... the sheet count ...\n"
    "        ( )cupsWrite                % ... a space ...\n"
    "        cupsGetNumCopies             % ... the number of copies ...\n"
    "        16 string cvs cupsWrite            % ...\n"
    "        (\\n)cupsWrite                % ... a newline\n"
    "        cupsFlush\n"
    "      }/if load\n"
    "                    % false: current page gets discarded; do nothing    \n"
    "    ]cvx bind                % make the array executable and apply bind\n"
    "  >>setpagedevice\n"
    "}\n"
    "{\n"
    "    % In language level 1, we do no accounting currently, as there is no global VM\n"
    "    % the contents of which are undesturbed by save and restore. \n"
    "    % If we may be sure that showpage never gets called inside a page related save / restore pair\n"
    "    % we might implement an hack with showpage similar to the one above.\n"
    "}ifelse\n"
    "\n"
    "} stopped cleartomark\n";


/* Logging */
FILE* logh = NULL;

void _logv(const char *msg, va_list ap)
{
    if (!logh)
        return;
    vfprintf(logh, msg, ap);
    fflush(logh);
}

void _log(const char* msg, ...)
{
    va_list ap;
    va_start(ap, msg);
    _logv(msg, ap);
    va_end(ap);
}

void rip_die(int status, const char *msg, ...)
{
    va_list ap;

    _log("Process is dying with \"");
    va_start(ap, msg);
    _logv(msg, ap);
    va_end(ap);
    _log("\", exit stat %d\n", status);

    _log("Cleaning up...\n");
    kill_all_processes();

    exit(status);
}


char cups_fileconverter [512];
char ppdfile[256] = "";
char printer[256] = "";
char printer_model[128] = "";
char jobid[128] = "";
char jobuser[128] = "";
char jobhost[128] = "";
char jobtitle[128] = "";
char copies[128] = "1";
dstr_t *postpipe;  /* command into which the output of this filter should be piped */
const char *accounting_prolog = NULL;
char attrpath[256] = "";

char modern_shell[64] = "/bin/bash";

pid_t renderer_pid = 0;

int spooler = SPOOLER_DIRECT;
int do_docs = 0;
int dontparse = 0;
int jobhasjcl;

/* Variable for PPR's backend interface name (parallel, tcpip, atalk, ...) */
char backend [64];

/* Array to collect unknown options so that they can get passed to the
backend interface of PPR. For other spoolers we ignore them. */
dstr_t *backendoptions = NULL;

/* These variables were in 'dat' before */
char colorprofile [128];
char id[128];
char driver[128];
dstr_t *currentcmd;
char cupsfilter[256];
dstr_t *jclprepend;
dstr_t *jclappend;


dstr_t *optstr;

char *cwd;

typedef struct {
    char year[5];
    char mon[3];
    char day[3];
    char hour[3];
    char min[3];
    char sec[3];
} timestrings_t;

time_t curtime;
timestrings_t curtime_strings;


void fill_timestrings(timestrings_t *ts, time_t time)
{
    struct tm *t = localtime(&time);

    sprintf(ts->year, "%04d", t->tm_year + 1900);
    sprintf(ts->mon, "%02d", t->tm_mon + 1);
    sprintf(ts->day, "%02d", t->tm_mday);
    sprintf(ts->hour, "%02d", t->tm_hour);
    sprintf(ts->min, "%02d", t->tm_min);
    sprintf(ts->sec, "%02d", t->tm_sec);
}

void unhtmlify(char *dest, size_t size, const char *src)
{
    char *pdest = dest;
    const char *psrc = src;
    const char *repl;

    while (*psrc && pdest - dest < size) {

        if (*psrc == '&') {
            psrc++;
            repl = NULL;

            /* Replace HTML/XML entities by the original characters */
            if (!prefixcmp(psrc, "apos;"))
                repl = "\'";
            else if (!prefixcmp(psrc, "quot;"))
                repl = "\"";
            else if (!prefixcmp(psrc, "gt;"))
                repl = ">";
            else if (!prefixcmp(psrc, "lt;"))
                repl = "<";
            else if (!prefixcmp(psrc, "amp;"))
                repl = "&";

            /* Replace special entities by job data */
            else if (!prefixcmp(psrc, "job;"))
                repl = jobid;
            else if (!prefixcmp(psrc, "user;"))
                repl = jobuser;
            else if (!prefixcmp(psrc, "host;"))
                repl = jobhost;
            else if (!prefixcmp(psrc, "title;"))
                repl = jobtitle;
            else if (!prefixcmp(psrc, "copies;"))
                repl = copies;
            else if (!prefixcmp(psrc, "options;"))
                repl = optstr->data;
            else if (!prefixcmp(psrc, "year;"))
                repl = curtime_strings.year;
            else if (!prefixcmp(psrc, "month;"))
                repl = curtime_strings.mon;
            else if (!prefixcmp(psrc, "date;"))
                repl = curtime_strings.day;
            else if (!prefixcmp(psrc, "hour;"))
                repl = curtime_strings.hour;
            else if (!prefixcmp(psrc, "min;"))
                repl = curtime_strings.min;
            else if (!prefixcmp(psrc, "sec;"))
                repl = curtime_strings.sec;

            if (repl) {
                strncpy(pdest, repl, size - (pdest - dest));
                pdest += strlen(repl);
                psrc = strchr(psrc, ';') +1;
            }
            else {
                psrc = strchr(psrc, ';') +1;
            }
        }
        else {
            *pdest = *psrc;
            pdest++;
            psrc++;
        }
    }
    *pdest = '\0';
}


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

char cupsfilterpath[256];    /* CUPS raster drivers are searched here */


#define FILECONVERTER_NONE      "echo \"Cannot convert file to PostScript!\" 1>&2"
#define FILECONVERTER_A2PS      "a2ps -1 @@--medium=@@PAGESIZE@@ @@--center-title=@@JOBTITLE@@ -o -"
#define FILECONVERTER_ENSCRIPT  "enscript -G @@-M @@PAGESIZE@@ @@-b \"Page $%|@@JOBTITLE@@ --margins=36:36:36:36 --mark-wrapped-lines=arrow --word-wrap -p-"
#define FILECONVERTER_MPAGE     "mpage -o -1 @@-b @@PAGESIZE@@ @@-H -h @@JOBTITLE@@ -m36l36b36t36r -f -P- -"

char fileconverter[PATH_MAX] = FILECONVERTER_NONE;


void set_config_option(const char *key, const char *value)
{
    if (strcmp(key, "debug") == 0) {
        debug = atoi(value);
    }
    else if (strcmp(key, "execpath") == 0) {
        strncpy(execpath, value, 127);
        execpath[127] = '\0';
    }
    else if (strcmp(key, "cupsfilterpath") == 0) {
        strncpy(cupsfilterpath, value, 255);
        execpath[255] = '\0';
    }
    else if (strcmp(key, "preferred_shell") == 0) {
        strlcpy(modern_shell, value, 32);
    }
    else if (strcmp(key, "textfilter") == 0) {
        if (strcmp(value, "a2ps") == 0)
            strlcpy(fileconverter, FILECONVERTER_A2PS, PATH_MAX);
        else if (strcmp(value, "enscript") == 0)
            strlcpy(fileconverter, FILECONVERTER_ENSCRIPT, PATH_MAX);
        else if (strcmp(value, "mpage") == 0)
            strlcpy(fileconverter, FILECONVERTER_MPAGE, PATH_MAX);
        else
            strlcpy(fileconverter, value, PATH_MAX);
    }
}

void read_config_file(const char *filename)
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
        set_config_option(key, value);
    }
    fclose(fh);
}

const char * get_modern_shell()
{
    return modern_shell;
}

/* returns position in 'str' after the option */
char * extract_next_option(char *str, char **pagerange, char **key, char **value)
{
    char *p = str;
    char quotechar;

    *pagerange = NULL;
    *key = NULL;
    *value = NULL;

    if (!str)
        return NULL;

    /* skip whitespace and commas */
    while (*p && (isspace(*p) || *p == ',')) p++;

    if (!*p)
        return NULL;

    /* read the pagerange if we have one */
    if (prefixcmp(p, "even:") == 0 || prefixcmp(p, "odd:") == 0 || isdigit(*p)) {
        *pagerange = p;
        p = strchr(p, ':');
        if (!p)
            return NULL;
        *p = '\0';
        p++;
    }

    /* read the key */
    if (*p == '\'' || *p == '\"') {
        quotechar = *p;
        *key = p +1;
        p = strchr(*key, quotechar);
        if (!p)
            return NULL;
    }
    else {
        *key = p;
        while (*p && *p != ':' && *p != '=' && *p != ' ') p++;
    }

    if (*p != ':' && *p != '=') /* no value for this option */
        return NULL;

    *p++ = '\0'; /* remove the separator sign */

    if (*p == '\"' || *p == '\'') {
        quotechar = *p;
        *value = p +1;
        p = strchr(*value, quotechar);
        if (!p)
            return NULL;
        *p = '\0';
        p++;
    }
    else {
        *value = p;
        while (*p && *p != ' ' && *p != ',') p++;
        if (*p == '\0')
            return NULL;
        *p = '\0';
        p++;
    }

    return *p ? p : NULL;
}

/* processes optstr */
void process_cmdline_options()
{
    char *p, *nextopt, *pagerange, *key, *value;
    option_t *opt, *opt2;
    int optset;
    char tmp [256];
    float width, height;
    char unit[2];

    nextopt = extract_next_option(optstr->data, &pagerange, &key, &value);
    while (key) {
        if (value)
            _log("Pondering option '%s=%s'\n", key, value);
        else
            _log("Pondering option '%s'\n", key);

        /* "docs" option to print help page */
        if (!strcasecmp(key, "docs")) {
            do_docs = 1;
            continue;
        }
        /* "profile" option to supply a color correction profile to a CUPS raster driver */
        if (!strcmp(key, "profile")) {
            strlcpy(colorprofile, value, 128);
            continue;
        }
        /* Solaris options that have no reason to be */
        if (!strcmp(key, "nobanner") || !strcmp(key, "dest") || !strcmp(key, "protocol"))
            continue;

        if (pagerange) {
            snprintf(tmp, 256, "pages:%s", pagerange);
            optset = optionset(tmp);

            opt = find_option(key);
            if (opt && (option_get_section(opt) != SECTION_ANYSETUP ||
                        option_get_section(opt) != SECTION_PAGESETUP)) {
                _log("This option (%s) is not a \"PageSetup\" or \"AnySetup\" option, so it cannot be restricted to a page range.\n", key);
                continue;
            }
        }
        else
            optset = optionset("userval");


        if (value) {
            /* At first look for the "backend" option to determine the PPR backend to use */
            if (spooler == SPOOLER_PPR_INT && !strcasecmp(key, "backend")) {
                /* backend interface name */
                strlcpy(backend, value, 64);
            }
            else if (strcasecmp(key, "media") == 0) {
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

                p = strtok(value, ",");
                do {
                    if ((opt = find_option("PageSize")) && option_accepts_value(opt, p))
                        option_set_value(opt, optset, p);
                    else if ((opt = find_option("MediaType")) && option_has_choice(opt, p))
                        option_set_value(opt, optset, p);
                    else if ((opt = find_option("InputSlot")) && option_has_choice(opt, p))
                        option_set_value(opt, optset, p);
                    else if (!strcasecmp(p, "manualfeed")) {
                        /* Special case for our typical boolean manual
                           feeder option if we didn't match an InputSlot above */
                        if ((opt = find_option("ManualFeed")))
                            option_set_value(opt, optset, "1");
                    }
                    else
                        _log("Unknown \"media\" component: \"%s\".\n", p);

                } while ((p = strtok(NULL, ",")));
            }
            else if (!strcasecmp(key, "sides")) {
                /* Handle the standard duplex option, mostly */
                if (!prefixcasecmp(value, "two-sided")) {
                    if ((opt = find_option("Duplex"))) {
                        /* Default to long-edge binding here, for the case that
                           there is no binding setting */
                        option_set_value(opt, optset, "DuplexNoTumble");

                        /* Check the binding: "long edge" or "short edge" */
                        if (strcasestr(value, "long-edge")) {
                            if ((opt2 = find_option("Binding")))
                                option_set_value(opt2, optset, "LongEdge");
                            else
                                option_set_value(opt, optset, "DuplexNoTumble");
                        }
                        else if (strcasestr(value, "short-edge")) {
                            if ((opt2 = find_option("Binding")))
                                option_set_value(opt2, optset, "ShortEdge");
                            else
                                option_set_value(opt, optset, "DuplexNoTumble");
                        }
                    }
                }
                else if (!prefixcasecmp(value, "one-sided")) {
                    if ((opt = find_option("Duplex")))
                        option_set_value(opt, optset, "0");
                }

                /*  TODO
                    We should handle the other half of this option - the
                    BindEdge bit.  Also, are there well-known ipp/cups options
                    for Collate and StapleLocation?  These may be here...
                */
            }
            else {
                /* Various non-standard printer-specific options */
                if ((opt = find_option(key))) {
                    if (!option_set_value(opt, optset, value)) {
                        _log("  invalid choice \"%s\", using \"%s\" instead\n", 
                                value, option_get_value(opt, optset));
                    }
                }
                else if (spooler == SPOOLER_PPR_INT) {
                    /* Unknown option, pass it to PPR's backend interface */
                    if (!backendoptions)
                        backendoptions = create_dstr();
                    dstrcatf(backendoptions, "%s=%s ", key, value);
                }
                else
                    _log("Unknown option %s=%s.\n", key, value);
            }
        }
        /* Custom paper size */
        else if ((opt = find_option("PageSize")) && option_set_value(opt, optset, key)) {
            /* do nothing, if the value could be set, it has been set */
        }
        /* Standard bool args:
           landscape; what to do here?
           duplex; we should just handle this one OK now? */
        else if (!startswith(key, "no") && (opt = find_option(&key[3])))
            option_set_value(opt, optset, "0");
        else if ((opt = find_option(key)))
            option_set_value(opt, optset, "1");
        else
            _log("Unknown boolean option \"%s\".\n", key);

        /* get next option */
        nextopt = extract_next_option(nextopt, &pagerange, &key, &value);
    }
}

/* checks whether a pdq driver declaration file should be build
   and returns an opened file handle if so */
FILE * check_pdq_file(list_t *arglist)
{
    /* "--appendpdq=<file>" appends the data to the <file>,
       "--genpdq=<file>" creates/overwrites <file> for the data, and
       "--genpdq" writes to standard output */

    listitem_t *i;
    char filename[256];
    FILE *handle;
    char *p;
    int raw, append;

    if ((i = arglist_find_prefix(arglist, "--genpdq"))) {
        raw = 0;
        append = 0;
    }
    else if ((i = arglist_find_prefix(arglist, "--genrawpdq"))) {
        raw = 1;
        append = 0;
    }
    else if ((i = arglist_find_prefix(arglist, "--appendpdq"))) {
        raw = 0;
        append = 1;
    }
    else if ((i = arglist_find_prefix(arglist, "--appendrawpdq"))) {
        raw = 1;
        append = 1;
    }

    if (!i)
        return NULL;

    p = strchr((char*)i->data, '=');
    if (p) {
        strncpy_omit(filename, p +1, 256, omit_shellescapes);
        handle = fopen(filename, append ? "a" : "w");
        if (!handle) {
            _log("Cannot write PDQ driver declaration file.\n");
            exit(EXIT_PRNERR_NORETRY_BAD_SETTINGS);
        }
    }
    else if (!append)
        handle = stdout;
    else
        return NULL;

    /* remove option from args */
    list_remove(arglist, i);

    /* Do we have a pdq driver declaration for a raw printer */
    if (raw) {
        fprintf(handle,
                "driver \"Raw-Printer-%u\" {\n"
                "  # This PDQ driver declaration file was generated automatically by\n"
                "  # foomatic-rip to allow raw (filter-less) printing.\n"
                "  language_driver all {\n"
                "    # We accept all file types and pass them through without any changes\n"
                "    filetype_regx \"\"\n"
                "    convert_exec {\n"
                "      ln -s $INPUT $OUTPUT\n"
                "    }\n"
                "  }\n"
                "  filter_exec {\n"
                "    ln -s $INPUT $OUTPUT\n"
                "  }\n"
                "}", (unsigned int)curtime);
        if (handle != stdout) {
            fclose(handle);
            handle = NULL;
        }
        exit(EXIT_PRINTED);
    }

    return handle;
}

void init_ppr(list_t *arglist)
{
    size_t arg_count = list_item_count(arglist);
    char ppr_printer [256];
    char ppr_address [128];
    char ppr_options [1024];
    char ppr_jobbreak [128];
    char ppr_feedback [128];
    char ppr_codes [128];
    char ppr_jobname [128];
    char ppr_routing [128];
    char ppr_for [128] = "";
    char ppr_filetype [128] = "";
    char ppr_filetoprint [128] = "";
    FILE *ph;
    char tmp[256];
    char *p;


    /* TODO read interface.sh and signal.sh for exit and signal codes respectively */

    /* Check whether we run as a PPR interface (if not, we run as a PPR RIP)
       PPR calls interfaces with many command line parameters,
       where the forth and the sixth is a small integer
       number. In addition, we have 8 (PPR <= 1.31), 10
       (PPR>=1.32), 11 (PPR >= 1.50) command line parameters.
       We also check whether the current working directory is a
       PPR directory. */
    if ((arg_count == 11 || arg_count == 10 || arg_count == 8) &&
        atoi(arglist_get(arglist, 3)) < 100 && atoi(arglist_get(arglist, 5)) < 100)
    {
        /* get all command line parameters */
        strncpy_omit(ppr_printer, arglist_get(arglist, 0), 256, omit_shellescapes);
        strlcpy(ppr_address, arglist_get(arglist, 1), 128);
        strncpy_omit(ppr_options, arglist_get(arglist, 2), 1024, omit_shellescapes);
        strlcpy(ppr_jobbreak, arglist_get(arglist, 3), 128);
        strlcpy(ppr_feedback, arglist_get(arglist, 4), 128);
        strlcpy(ppr_codes, arglist_get(arglist, 5), 128);
        strncpy_omit(ppr_jobname, arglist_get(arglist, 6), 128, omit_shellescapes);
        strncpy_omit(ppr_routing, arglist_get(arglist, 7), 128, omit_shellescapes);
        if (arg_count >= 8) {
            strlcpy(ppr_for, arglist_get(arglist, 8), 128);
            strlcpy(ppr_filetype, arglist_get(arglist, 9), 128);
            if (arg_count >= 10)
                strncpy_omit(ppr_filetoprint, arglist_get(arglist, 10), 128, omit_shellescapes);
        }

        /* Common job parameters */
        strcpy(printer, ppr_printer);
        strcpy(jobtitle, ppr_jobname);
        if (isempty(jobtitle) && !isempty(ppr_filetoprint))
            strcpy(jobtitle, ppr_filetoprint);
        dstrcatf(optstr, " %s %s", ppr_options, ppr_routing);

        /* Get the path of the PPD file from the queue configuration */
        snprintf(tmp, 255, "LANG=en_US; ppad show %s | grep PPDFile", ppr_printer);
        tmp[255] = '\0';
        ph = popen(tmp, "r");
        if (ph) {
            fgets(tmp, 255, ph);
            tmp[255] = '\0';
            pclose(ph);

            strncpy_omit(ppdfile, tmp, 255, omit_shellescapes);
            if (ppdfile[0] == '/') {
                strcpy(tmp, ppdfile);
                strcpy(ppdfile, "../../share/ppr/PPDFiles/");
                strncat(ppdfile, tmp, 200);
            }
            if ((p = strrchr(ppdfile, '\n')))
                *p = '\0';
        }
        else {
            ppdfile[0] = '\0';
        }

        /* We have PPR and run as an interface */
        spooler = SPOOLER_PPR_INT;
    }
}

void init_cups(list_t *arglist, dstr_t *filelist)
{
    char path [1024] = "";
    char cups_jobid [128];
    char cups_user [128];
    char cups_jobtitle [128];
    char cups_copies [128];
    char cups_options [512];
    char cups_filename [256];

    if (getenv("CUPS_FONTPATH"))
        strcpy(path, getenv("CUPS_FONTPATH"));
    else if (getenv("CUPS_DATADIR")) {
        strcpy(path, getenv("CUPS_DATADIR"));
        strcat(path, "/fonts");
    }
    if (getenv("GS_LIB")) {
        strcat(path, ":");
        strcat(path, getenv("GS_LIB"));
    }
    setenv("GS_LIB", path, 1);

    /* Get all command line parameters */
    strncpy_omit(cups_jobid, arglist_get(arglist, 0), 128, omit_shellescapes);
    strncpy_omit(cups_user, arglist_get(arglist, 1), 128, omit_shellescapes);
    strncpy_omit(cups_jobtitle, arglist_get(arglist, 2), 128, omit_shellescapes);
    strncpy_omit(cups_copies, arglist_get(arglist, 3), 128, omit_shellescapes);
    strncpy_omit(cups_options, arglist_get(arglist, 4), 512, omit_shellescapes);

    /* Common job parameters */
    /* TODO why is this copied into the cups_* vars in the first place? */
    strcpy(jobid, cups_jobid);
    strcpy(jobtitle, cups_jobtitle);
    strcpy(jobuser, cups_user);
    strcpy(copies, cups_copies);
    dstrcatf(optstr, " %s", cups_options);

    /* Check for and handle inputfile vs stdin */
    if (list_item_count(arglist) > 4) {
        strncpy_omit(cups_filename, arglist_get(arglist, 5), 256, omit_shellescapes);
        if (cups_filename[0] != '-') {
            /* We get input from a file */
            dstrcatf(filelist, "%s ", cups_filename);
            _log("Getting input from file %s\n", cups_filename);
        }
    }

    accounting_prolog = ps_accounting ? accounting_prolog_code : NULL;

    /* On which queue are we printing?
       CUPS gives the PPD file the same name as the printer queue,
       so we can get the queue name from the name of the PPD file. */
    file_basename(printer, ppdfile, 256);
}

void init_solaris(list_t *arglist, dstr_t *filelist)
{
    char *str;
    int len;
    listitem_t *i;

    /* Get all command line parameters */
    strncpy_omit(jobtitle, arglist_get(arglist, 2), 128, omit_shellescapes);

    len = strlen(arglist_get(arglist, 4));
    str = malloc(len +1);
    strncpy_omit(str, arglist_get(arglist, 4), len, omit_shellescapes);
    dstrcatf(optstr, " %s", str);
    free(str);

    for (i = arglist->first; i; i = i->next)
        dstrcatf(filelist, "%s ", (char*)i->data);
}

/* search 'configfile' for 'key', copy value into dest, return success */
int configfile_find_option(const char *configfile, const char *key, char *dest, size_t destsize)
{
    FILE *fh;
    char line [1024];
    char *p;

    dest[0] = '\0';

    if (!(fh = fopen(configfile, "r")))
        return 0;

    while (fgets(line, 1024, fh)) {
        if (!prefixcmp(line, "default")) {
            p = strchr(line, ':');
            if (p) {
                strncpy_omit(dest, p, destsize, omit_whitespace);
                if (dest[0])
                    break;
            }
        }
    }
    fclose(fh);
    return dest[0] != '\0';
}

/* tries to find a default printer name in various config files and copies the
 * result into the global var 'printer'. Returns success */
int find_default_printer(const char *user_default_path)
{
    char configfile [1024];
    char *key = "default";

    if (configfile_find_option("./.directconfig", key, printer, 256))
        return 1;
    if (configfile_find_option("./directconfig", key, printer, 256))
        return 1;
    if (configfile_find_option("./.config", key, printer, 256))
        return 1;
    strlcpy(configfile, user_default_path, 1024);
    strlcat(configfile, "/direct/.config", 1024);
    if (configfile_find_option(configfile, key, printer, 256))
        return 1;
    strlcpy(configfile, user_default_path, 1024);
    strlcat(configfile, "/direct.conf", 1024);
    if (configfile_find_option(configfile, key, printer, 256))
        return 1;
    if (configfile_find_option(CONFIG_PATH "/direct/.config", key, printer, 256))
        return 1;
    if (configfile_find_option(CONFIG_PATH "/direct.conf", key, printer, 256))
        return 1;

    return 0;
}

/* used by init_direct_cps_pdq to find a ppd file */
int find_ppdfile(const char *user_default_path)
{
    /* Search also common spooler-specific locations, this way a printer
       configured under a certain spooler can also be used without spooler */

    strcpy(ppdfile, printer);
    if (access(ppdfile, R_OK) == 0)
        return 1;

    /* CPS can have the PPD in the spool directory */
    if (spooler == SPOOLER_CPS) {
        snprintf(ppdfile, 256, "/var/spool/lpd/%s/%s.ppd", printer, printer);
        if (access(ppdfile, R_OK) == 0)
            return 1;
        snprintf(ppdfile, 256, "/var/local/spool/lpd/%s/%s.ppd", printer, printer);
        if (access(ppdfile, R_OK) == 0)
            return 1;
        snprintf(ppdfile, 256, "/var/local/lpd/%s/%s.ppd", printer, printer);
        if (access(ppdfile, R_OK) == 0)
            return 1;
        snprintf(ppdfile, 256, "/var/spool/lpd/%s.ppd", printer);
        if (access(ppdfile, R_OK) == 0)
            return 1;
        snprintf(ppdfile, 256, "/var/local/spool/lpd/%s.ppd", printer);
        if (access(ppdfile, R_OK) == 0)
            return 1;
        snprintf(ppdfile, 256, "/var/local/lpd/%s.ppd", printer);
        if (access(ppdfile, R_OK) == 0)
            return 1;
    }
    snprintf(ppdfile, 256, "%s.ppd", printer); /* current dir */
    if (access(ppdfile, R_OK) == 0)
        return 1;
    snprintf(ppdfile, 256, "%s/%s.ppd", user_default_path, printer); /* user dir */
    if (access(ppdfile, R_OK) == 0)
        return 1;
    snprintf(ppdfile, 256, "%s/direct/%s.ppd", CONFIG_PATH, printer); /* system dir */
    if (access(ppdfile, R_OK) == 0)
        return 1;
    snprintf(ppdfile, 256, "%s/%s.ppd", CONFIG_PATH, printer); /* system dir */
    if (access(ppdfile, R_OK) == 0)
        return 1;
    snprintf(ppdfile, 256, "/etc/cups/ppd/%s.ppd", printer); /* CUPS config dir */
    if (access(ppdfile, R_OK) == 0)
        return 1;
    snprintf(ppdfile, 256, "/usr/local/etc/cups/ppd/%s.ppd", printer); /* CUPS config dir */
    if (access(ppdfile, R_OK) == 0)
        return 1;
    snprintf(ppdfile, 256, "/usr/share/ppr/PPDFiles/%s.ppd", printer); /* PPR PPDs */
    if (access(ppdfile, R_OK) == 0)
        return 1;
    snprintf(ppdfile, 256, "/usr/local/share/ppr/PPDFiles/%s.ppd", printer); /* PPR PPDs */
    if (access(ppdfile, R_OK) == 0)
        return 1;

    /* nothing found */
    ppdfile[0] = '\0';
    return 0;
}

void init_direct_cps_pdq(list_t *arglist, dstr_t *filelist, const char *user_default_path)
{
    char tmp [1024];
    listitem_t *i;

    /* Which files do we want to print? */
    for (i = arglist->first; i; i = i->next) {
        strncpy_omit(tmp, (char*)i->data, 1024, omit_shellescapes);
        dstrcatf(filelist, "%s ", tmp);
    }

    if (ppdfile[0] == '\0') {
        if (printer[0] == '\0') {
            /* No printer definition file selected, check whether we have a
               default printer defined */
            find_default_printer(user_default_path);
        }

        /* Neither in a config file nor on the command line a printer was selected */
        if (!printer[0]) {
            _log("No printer definition (option \"-P <name>\") specified!\n");
            exit(EXIT_PRNERR_NORETRY_BAD_SETTINGS);
        }

        /* Search for the PPD file */
        if (!find_ppdfile(user_default_path)) {
            _log("There is no readable PPD file for the printer %s, is it configured?\n");
            exit(EXIT_PRNERR_NORETRY_BAD_SETTINGS);
        }
    }
}


/* Build a PDQ driver description file to use the given PPD file
   together with foomatic-rip with the PDQ printing system
   and output it into 'pdqfile' */
void print_pdq_driver(FILE *pdqfile, int optset)
{
}
#if 0
    option_t *opt;
    value_t *val;
    setting_t *setting, *setting_true, *setting_false;

    /* Construct option list */
    dstr_t *driveropts = create_dstr();

    /* Do we have a "Custom" setting for the page size?
       Then we have to insert the following into the filter_exec script. */
    dstr_t *setcustompagesize = create_dstr();

    dstr_t *tmp = create_dstr();
    dstr_t *cmdline = create_dstr();
    dstr_t *psfilter = create_dstr();


    /* 1, if setting "PageSize=Custom" was found
       Then we must add options for page width and height */
    int custompagesize = 0;

    /* Data for a custom page size, to allow a custom size as default */
    int pagewidth = 612;
    int pageheight = 792;
    char pageunit[2] = "pt";

    char def [128];

    def[0] = '\0';

    for (opt = optionlist; opt; opt = opt->next) {
        if (opt->type == TYPE_ENUM) {
            /* Option with only one choice, omit it, foomatic-rip will set
               this choice anyway */
            if (option_setting_count(opt) <= 1)
                continue;

            /* Omit "PageRegion" option, it does the same as "PageSize" */
            if (!strcmp(opt->name, "PageRegion"))
                continue;

            /* 1, if setting "PageSize=Custom" was found
               Then we must add options for page width and height */
            custompagesize = 0;

            if ((val = option_get_value(opt, optset)))
                strlcpy(def, val->value, 128);

#if 0 TODO not used ?!
            /* If the default is a custom size we have to set also
               defaults for the width, height, and units of the page */
            if (!strcmp(opt->name, "PageSize") && val && value_has_custom_setting(val))
                strcpy(def, "Custom");
#endif

            dstrcatf(driveropts,
                    "  option {\n"
                    "    var = \"%s\"\n"
                    "    desc = \"%s\"\n", opt->varname, option_text(opt));

            /* get enumeration values for each enum arg */
            dstrclear(tmp);
            for (setting = opt->settinglist; setting; setting = setting->next)  {
                dstrcatf(tmp,
                    "    choice \"%s_%s\" {\n"
                    "      desc = \"%s\"\n"
                    "      value = \" -o %s=%s\"\n"
                    "    }\n",
                     opt->name, setting->value,
                      isempty(setting->comment) ? setting->value : setting->comment,
                    opt->name, setting->value);

                if (!strcmp(opt->name, "PageSize") && !strcmp(setting->value, "Custom")) {
                    custompagesize = 1;
                    if (isempty(setcustompagesize->data)) {
                        dstrcatf(setcustompagesize,
                            "      # Custom page size settings\n"
                            "      # We aren't really checking for legal vals.\n"
                            "      if [ \"x${%s}\" = 'x -o %s=%s' ]; then\n"
                            "        %s=\"${%s}.${PageWidth}x${PageHeight}${PageSizeUnit}\"\n"
                            "      fi\n\n",
                            opt->varname, opt->varname, setting->value, opt->varname, opt->varname);
                    }
                }
            }

            dstrcatf(driveropts, "    default_choice \"%s_%s\"\n", opt->name, def);
            dstrcatf(driveropts, tmp->data);
            dstrcatf(driveropts, "  }\n\n");

            if (custompagesize) {
                /* Add options to set the custom page size */
                dstrcatf(driveropts,
                    "  argument {\n"
                    "    var = \"PageWidth\"\n"
                    "    desc = \"Page Width (for \\\"Custom\\\" page size)\"\n"
                    "    def_value \"%d\"\n"                      /* pagewidth */
                    "    help = \"Minimum value: 0, Maximum value: 100000\"\n"
                    "  }\n\n"
                    "  argument {\n"
                    "    var = \"PageHeight\"\n"
                    "    desc = \"Page Height (for \\\"Custom\\\" page size)\"\n"
                    "    def_value \"%d\"\n"                      /* pageheight */
                    "    help = \"Minimum value: 0, Maximum value: 100000\"\n"
                    "  }\n\n"
                    "  option {\n"
                    "    var = \"PageSizeUnit\"\n"
                    "    desc = \"Unit (for \\\"Custom\\\" page size)\"\n"
                    "    default_choice \"PageSizeUnit_%.2s\"\n"  /* pageunit */
                    "    choice \"PageSizeUnit_pt\" {\n"
                    "      desc = \"Points (1/72 inch)\"\n"
                    "      value = \"pt\"\n"
                    "    }\n"
                    "    choice \"PageSizeUnit_in\" {\n"
                    "      desc = \"Inches\"\n"
                    "      value = \"in\"\n"
                    "    }\n"
                    "    choice \"PageSizeUnit_cm\" {\n"
                    "      desc = \"cm\"\n"
                    "      value = \"cm\"\n"
                    "    }\n"
                    "    choice \"PageSizeUnit_mm\" {\n"
                    "      desc = \"mm\"\n"
                    "      value = \"mm\"\n"
                    "    }\n"
                    "  }\n\n",
                    pagewidth, pageheight, pageunit);
            }
        }
        else if (opt->type == TYPE_INT || opt->type == TYPE_FLOAT) {
            /* Assure that the comment is not emtpy */
            if (isempty(opt->comment))
                strcpy(opt->comment, opt->name);

            if ((val = option_get_value(opt, optset)))
                strlcpy(def, val->value, 128);

            strcpy(opt->varname, opt->name);
            strrepl(opt->varname, "-/.", '_');


            dstrcatf(driveropts,
                "  argument {\n"
                "    var = \"%s\"\n"
                "    desc = \"%s\"\n"
                "    def_value \"%s\"\n"
                "    help = \"Minimum value: %s, Maximum value: %s\"\n"
                "  }\n\n",
                opt->varname, opt->comment, def, opt->min, opt->max);
        }
        else if (opt->type == TYPE_BOOL) {
            /* Assure that the comment is not emtpy */
            if (isempty(opt->comment))
                strcpy(opt->comment, opt->name);

            if ((val = option_get_value(opt, optset)))
                strlcpy(def, val->value, 128);
            strcpy(opt->varname, opt->name);
            strrepl(opt->varname, "-/.", '_');
            setting_true = option_find_setting(opt, "true");
            setting_false = option_find_setting(opt, "false");

            dstrcatf(driveropts,
                "  option {\n"
                "    var = \"%s\"\n"
                "    desc = \"%s\"\n", opt->varname, opt->comment);

            if (!isempty(def) && !strcasecmp(def, "true"))
                dstrcatf(driveropts, "    default_choice \"%s\"\n", def);
            else
                dstrcatf(driveropts, "    default_choice \"no%s\"\n", def);

            dstrcatf(driveropts,
                "    choice \"%s\" {\n"
                "      desc = \"%s\"\n"
                "      value = \" -o %s=True\"\n"
                "    }\n"
                "    choice \"no%s\" {\n"
                "      desc = \"%s\"\n"
                "      value = \" -o %s=False\"\n"
                "    }\n"
                "  }\n\n",
                opt->name, setting_true->comment, opt->name,
                opt->name, setting_false->comment, opt->name);
        }
        else if (opt->type == TYPE_STRING) {
            /* Assure that the comment is not emtpy */
            if (isempty(opt->comment))
                strcpy(opt->comment, opt->name);

            if ((val = option_get_value(opt, optset)))
                strlcpy(def, val->value, 128);
            strcpy(opt->varname, opt->name);
            strrepl_nodups(opt->varname, "-/.", '_');

            dstrclear(tmp);
            if (opt->maxlength)
                dstrcatf(tmp, "Maximum Length: %s characters, ", opt->maxlength);

            dstrcatf(tmp, "Examples/special settings: ");
            for (setting = opt->settinglist; setting; setting = setting->next)  {
                /* Retrieve the original string from the prototype and the driverval */
                /* TODO perl code for this part doesn't make sense to me */
            }
        }
    }

    /* Define the "docs" option to print the driver documentation page */
    dstrcatf(driveropts,
        "  option {\n"
        "    var = \"DRIVERDOCS\"\n"
        "    desc = \"Print driver usage information\"\n"
        "    default_choice \"nodocs\"\n"
        "    choice \"docs\" {\n"
        "      desc = \"Yes\"\n"
        "      value = \" -o docs\"\n"
        "    }\n"
        "    choice \"nodocs\" {\n"
        "      desc = \"No\"\n"
        "      value = \"\"\n"
        "    }\n"
        "  }\n\n");

    /* Build the foomatic-rip command line */
    dstrcatf(cmdline, "foomatic-rip --pdq");
    if (!isempty(printer)) {
        dstrcatf(cmdline, " -P %s", printer);
    }
    else {
        /* Make sure that the PPD file is entered with an absolute path */
        make_absolute_path(ppdfile, 256);
        dstrcatf(cmdline, " --ppd=%s", ppdfile);
    }

    for (opt = optionlist; opt; opt = opt->next) {
        if (!isempty(opt->varname))
            dstrcatf(cmdline, "${%s}", opt->varname);
    }
    dstrcatf(cmdline, "${DRIVERDOCS} $INPUT > $OUTPUT");


    /* Now we generate code to build the command line snippets for the numerical options */
    for (opt = optionlist; opt; opt = opt->next) {
        /* Only numerical and string options need to be treated here */
        if (opt->type != TYPE_INT &&
            opt->type != TYPE_FLOAT &&
            opt->type != TYPE_STRING)
            continue;

        /* If the option's variable is non-null, put in the
           argument.  Otherwise this option is the empty
           string.  Error checking? */
        dstrcatf(psfilter, "      # %s\n", opt->comment);
        if (opt->type == TYPE_INT || opt->type == TYPE_FLOAT) {
            dstrcatf(psfilter,
                "      # We aren't really checking for max/min,\n"
                "      # this is done by foomatic-rip\n"
                "      if [ \"x${%s}\" != 'x' ]; then\n  ", opt->varname);
        }

        dstrcatf(psfilter, "      %s=\" -o %s='${%s}'\"\n", opt->varname, opt->name, opt->varname);

        if (opt->type == TYPE_INT || opt->type == TYPE_FLOAT)
            dstrcatf(psfilter, "      fi\n");
        dstrcatf(psfilter, "\n");
    }

    /* Command execution */
    dstrcatf(psfilter,
        "      if ! test -e $INPUT.ok; then\n"
        "        sh -c \"%s\"\n"
        "        if ! test -e $OUTPUT; then \n"
        "          echo 'Error running foomatic-rip; no output!'\n"
        "          exit 1\n"
        "        fi\n"
        "      else\n"
        "        ln -s $INPUT $OUTPUT\n"
        "      fi\n\n", cmdline->data);


    dstrclear(tmp);
    dstrcatf(tmp, "%s", printer_model);
    strrepl_nodups(tmp->data, " \t\n.,;/()[]{}+*", '-');
    tmp->len = strlen(tmp->data); /* length could have changed */
    if (tmp->data[tmp->len -1] == '-') {
        tmp->data[--tmp->len] = '\0';
    }


    fprintf(pdqfile,
        "driver \"%s-%u\" {\n\n"
        "  # This PDQ driver declaration file was generated automatically by\n"
        "  # foomatic-rip from information in the file %s.\n" /* ppdfile */
        "  # It allows printing with PDQ on the %s.\n"        /* model */
        "\n"
        "  requires \"foomatic-rip\"\n\n"
        "%s" /* driveropts */
        "  language_driver all {\n"
        "    # We accept all file types and pass them to foomatic-rip\n"
        "    # (invoked in \"filter_exec {}\" section) without\n"
        "    # pre-filtering\n"
        "    filetype_regx \"\"\n"
        "    convert_exec {\n"
        "      ln -s $INPUT $OUTPUT\n"
        "    }\n"
        "  }\n\n"
        "  filter_exec {\n"
        "%s" /* setcustompagesize */
        "%s" /* psfilter */
        "  }\n"
        "}\n",
        tmp->data, /* cleaned printer_model */ (unsigned int)curtime, ppdfile, printer_model,
        driveropts->data, setcustompagesize->data, psfilter->data);


    free_dstr(setcustompagesize);
    free_dstr(driveropts);
    free_dstr(tmp);
    free_dstr(cmdline);
    free_dstr(psfilter);
}
#endif

/* returns status - see wait(2) */
int modern_system(const char *cmd)
{
    pid_t pid;
    int status;

    pid = fork();
    if (pid < 0) {
        _log("Failed to fork()\n");
        exit(errno);
    }

    if (pid == 0) { /* child, execute commands under a modern shell */
        execl(get_modern_shell(), get_modern_shell(), "-c", cmd, (char*)NULL);
        _log("exec(%s, \"-c\", %s) failed", get_modern_shell(), cmd);
        exit(errno);
    }
    else { /* parent, wait for the child */
        waitpid(pid, &status, 0);
        return status;
    }
}

int retval = EXIT_PRINTED;

/*  Functions to let foomatic-rip fork to do several tasks in parallel.

To do the filtering without loading the whole file into memory we work
on a data stream, we read the data line by line analyse it to decide what
filters to use and start the filters if we have found out which we need.
We buffer the data only as long as we didn't determing which filters to
use for this piece of data and with which options. There are no temporary
files used.

foomatic-rip splits into up to 6 parallel processes to do the whole
filtering (listed in the order of the data flow):

   KID0: Generate documentation pages (only jobs with "docs" option)
   KID2: Put together already read data and current input stream for
         feeding into the file conversion filter (only non-PostScript
         and "docs" jobs)
   KID1: Run the file conversion filter to convert non-PostScript
         input into PostScript (only non-PostScript and "docs" jobs)
   MAIN: Prepare the job auto-detecting the spooler, reading the PPD,
         extracting the options from the command line, and parsing
         the job data itself. It analyses the job data to check
         whether it is PostScript and starts KID1/KID2 if not, it
         also stuffs PostScript code from option settings into the
         PostScript data stream. It starts the renderer (KID3/KID4)
         as soon as it knows its command line and restarts it when
         page-specific option settings need another command line
         or different JCL commands.
   KID3: The rendering process. In most cases GhostScript, "cat"
         for native PostScript printers with their manufacturer's
         PPD files.
   KID4: Put together the JCL commands and the renderer's output
         and send all that either to STDOUT or pipe it into the
         command line defined with $postpipe. */

/* Signal handling routines */
void set_exit_prnerr()
{
    retval = EXIT_PRNERR;
}

void set_exit_prnerr_noretry()
{
    retval = EXIT_PRNERR_NORETRY;
}

void set_exit_engaged()
{
    retval = EXIT_ENGAGED;
}

/* Check whether we have a Ghostscript version with redirection of the standard
 * output of the PostScript programs via '-sstdout=%stderr' */
int test_gs_output_redirection()
{
    char * gstestcommand = GS_PATH " -dQUIET -dPARANOIDSAFER -dNOPAUSE -dBATCH -dNOMEDIAATTRS "
        "-sDEVICE=pswrite -sstdout=%stderr -sOutputFile=/dev/null -c '(hello\n) print flush' 2>&1";
    char output[10] = "";

    FILE *pd = popen(gstestcommand, "r");
    if (!pd) {
        _log("Failed to execute ghostscript!\n");
        return 0;
    }

    fread(output, 1, 1024, pd);
    pclose(pd);

    if (startswith(output, "hello"))
        return 1;

    return 0;
}

/* Massage arguments to make ghostscript execute properly as a filter, with
   output on stdout and errors on stderr etc.
   (This function does what foomatic-gswrapper used to do) */
void massage_gs_commandline(dstr_t *cmd)
{
    int gswithoutputredirection = test_gs_output_redirection();

    /* TODO Handle commandlines in which the 'gs' command is part of a pipe */
    if (!startswith(cmd->data, "gs"))
        return;

    /* If Ghostscript does not support redirecting the standard output
       of the PostScript program to standard error with
       '-sstdout=%stderr', sen the job output data to fd 3; errors
       will be on 2(stderr) and job ps program interpreter output on
       1(stdout). */
    if (gswithoutputredirection)
        dstrreplace(cmd, "-sOutputFile=- ", "-sOutputFile=%stdout");
    else
        dstrreplace(cmd, "-sOutputFile=- ", "-sOutputFile=/dev/fd/3");

    /* Use always buffered input. This works around a Ghostscript
       bug which prevents printing encrypted PDF files with Adobe
       Reader 8.1.1 and Ghostscript built as shared library
       (Ghostscript bug #689577, Ubuntu bug #172264) */
    dstrreplace(cmd, " - ", " -_ ");
    dstrreplace(cmd, " /dev/fd/0 ", " -_ ");

    /* Turn *off* -q (quiet!); now that stderr is useful! :) */
    dstrreplace(cmd, " -q ", "");

    /* Escape any quotes, and then quote everything just to be sure...
       Escaping a single quote inside single quotes is a bit complex as the shell
       takes everything literal there. So we have to assemble it by concatinating
       different quoted strings.
       Finally we get e.g.: 'x'"'"'y' or ''"'"'xy' or 'xy'"'"'' or ... */
    /* dstrreplace(cmd, "'", "'\"'\"'"); TODO tbd */


    dstrremove(cmd, 0, 2);     /* Remove 'gs' */
    if (gswithoutputredirection)
        dstrprepend(cmd, " -sstdout=%stderr ");
    else
        dstrcat(cmd, " 3>&1 1>&2");
    dstrprepend(cmd, GS_PATH);

    /* If the renderer command line contains the "echo" command, replace the
     * "echo" by the user-chosen $myecho (important for non-GNU systems where
     * GNU echo is in a special path */
    dstrreplace(cmd, "echo", ECHO); /* TODO search for \wecho\w */
}

void exec_kid4()
{
    int fileh;
    char jclstr[64];
    dstr_t *jclheader = create_dstr(); /* JCL header read from renderer output */
    int driverjcl;
    dstr_t *jclprepend_copy = create_dstr();
    char *p, *line;
    char tmp[1024];
    int insert, commandfound;
    dstr_t *dtmp = create_dstr();
    size_t n;

    dstrcpy(jclprepend_copy, jclprepend->data);


    /* trailing task on the pipe; we write jcl stuff */

    /* Do we have a $postpipe, if yes, launch the command(s) and
       point our output into it/them */
    /* TODO */
#if 0
    if (postpipe->len) {
        if ((fileh = open(postpipe->data, O_WRONLY)) == -1) {
            close(pfd_kid4[0]);
            snprintf(tmp, 256, "4 %d\n", EXIT_PRNERR_NORETRY_BAD_SETTINGS);
            write(pfd_kid_message[1], tmp, strlen(tmp));
            close(pfd_kid_message[0]);
            close(pfd_kid_message[1]);
            _log("cannot execute postpipe %s\n", postpipe->data);
            exit(EXIT_PRNERR_NORETRY_BAD_SETTINGS);
        }
    }
    else
#endif
        fileh = STDOUT_FILENO;

    /* Debug output */
    _log("JCL: %s <job data> %s\n\n", jclprepend->data, jclappend->data);

    /* wrap the JCL around the job data, if there are any options specified...
     * Should the driver already have inserted JCL commands we merge our JCL
     * header with the one from the driver */
    if (line_count(jclprepend->data) > 1) {
        /* Determine magic string of JCL in use (usually "@PJL") For that we
         * take the first part of the second JCL line up to the first space */
        if (jclprepend->len && !isspace(jclprepend->data[0])) {
            strncpy_tochar(jclstr, jclprepend->data, 64, " \t\r\n");
            /* read from the renderer output until the first non-JCL line
             * appears */
            /* TODO tbd */

            /* If we had read at least two lines, at least one is a JCL header,
             * so do the merging */
            if (line_count(jclheader->data) > 1) {
                driverjcl = 1;
                /* Discard the first and the last entry of the @jclprepend
                 * array, we only need the option settings to merge them in */
                dstrremove(jclprepend_copy, 0, line_start(jclprepend_copy->data, 1));
                jclprepend_copy->data[
                    line_start(jclprepend_copy->data, line_count(jclprepend_copy->data) -1)] = '\0';

                /* Line after which we insert new JCL commands in the JCL
                 * header of the job */
                insert = 1;

                /* Go through every JCL command in jclprepend */
                for (line = strtok(jclprepend_copy->data, "\r\n"); line; line = strtok(NULL, "\r\n")) {
                    /* Search the command in the JCL header from the driver. As
                     * search term use only the string from the beginning of
                     * the line to the "=", so the command will also be found
                     * when it has another value */
                    strncpy_tochar(tmp, line, 256, "="); /* command */
                    commandfound = 0;
                    dstrclear(dtmp);
                    p = jclheader->data;
                    while (p) {
                        if (startswith(p, tmp)) {
                            dstrcatf(dtmp, "%s\n", line);
                            commandfound = 1;
                        }
                        else
                            dstrcatline(dtmp, p);
                        if ((p = strchr(p, '\n')))
                            p++;
                    }
                    dstrcpy(jclheader, dtmp->data);
                    if (!commandfound) {
                        /* If the command is not found. insert it */
                        if (line_count(jclheader->data) > 2) {
                            /* jclheader has more than one line, insert the new
                             * command beginning right after the first line and
                             * continuing after the previous inserted command */
                            dstrinsert(jclheader, line_start(jclheader->data, insert), line);
                            insert++;
                        }
                        else {
                            /* If we have only one line of JCL it is probably
                             * something like the "@PJL ENTER LANGUAGE=..."
                             * line which has to be in the end, but it also
                             * contains the "<esc>%-12345X" which has to be in
                             * the beginning of the job. So we split the line
                             * right before the $jclstr and append our command
                             * to the end of the first part and let the second
                             * part be a second JCL line. */
                            p = strstr(jclheader->data, jclstr);
                            if (p) {
                                dstrncpy(dtmp, jclheader->data, p - jclheader->data);
                                dstrcatf(dtmp, "%s%s", line, p);
                                dstrcpy(jclheader, dtmp->data);
                            }
                        }
                    }
                }

                /* Now pass on the merged JCL header */
                write(fileh, jclheader->data, jclheader->len);
            }
            else {
                /* The driver didn't create a JCL header, simply
                   prepend ours and then pass on the line which we
                   already have read */
                write(fileh, jclprepend->data, jclprepend->len);
                write(fileh, jclheader->data, jclheader->len);
            }
        }
        else {
            /* No merging of JCL header possible, simply prepend it */
            write(fileh, jclprepend->data, jclprepend->len);
        }
    }

    /* The rest of the job data */
    while ((n = read(STDIN_FILENO, tmp, 1024)) > 0)
        write(fileh, tmp, n);

    /* A JCL trailer */
    if (line_count(jclprepend->data) > 1 && !driverjcl)
        write(fileh, jclappend->data, jclappend->len);

    close(STDIN_FILENO);
    if (close(fileh) != 0) {
        _log("error closing postpipe\n");
        exit(EXIT_PRNERR_NORETRY_BAD_SETTINGS);
    }

    /* Handle signals of the backend interface */
    if (retval != EXIT_PRINTED) {
        exit(retval);
    }

    /* Successful exit, inform main process */
    _log("kid4 finished\n");
    exit(EXIT_PRINTED);

    free_dstr(jclheader);
    free_dstr(dtmp);
}

void exec_kid3()
{
    dstr_t *commandline = create_dstr();
    int kid4, kid4in;
    int status, ret;

    kid4 = start_process("kid4", exec_kid4, &kid4in, NULL);
    if (kid4 < 0)
        exit(EXIT_PRNERR_NORETRY_BAD_SETTINGS);

    dstrcpy(commandline, currentcmd->data);
    massage_gs_commandline(commandline);
    _log("renderer command: %s\n", commandline->data);

    if (dup2(kid4in, STDOUT_FILENO) < 0) {
        _log("kid3: Could not dup stdout to kid4\n");
        close(kid4in);
        exit(EXIT_PRNERR_NORETRY_BAD_SETTINGS);
    }
    if (debug && dup2(fileno(logh), STDERR_FILENO) < 0) {
        _log("Couldn't dup logh to STDERR\n");
        close(kid4in);
        exit(EXIT_PRNERR_NORETRY_BAD_SETTINGS);
    }

    /* In debug mode save the data supposed to be fed into the
    renderer also into a file */
    if (debug) {
        dstrprepend(commandline, "tee -a " LOG_FILE ".ps | ( ");
        dstrcat(commandline, ")");
    }

    /* Actually run the thing */
    status = run_system_process(commandline->data);
    close(kid4in);
    close(STDOUT_FILENO);
    free_dstr(commandline);

    if (WIFEXITED(status)) {
        _log("renderer return value: %d\n", WEXITSTATUS(status));
        switch (WEXITSTATUS(ret)) {
            case 0:  /* Success! */
                /* wait for postpipe/output child */
                wait_for_process(kid4);
                _log("kid3 finished\n");
                exit(EXIT_PRINTED);
            case 1:
                _log("Possible error on renderer command line or PostScript error. Check options.");
                exit(EXIT_JOBERR);
            case 139:
                _log("The renderer may have dumped core.");
                exit(EXIT_JOBERR);
            case 141:
                _log("A filter used in addition to the renderer itself may have failed.");
                exit(EXIT_PRNERR);
            case 243:
            case 255:  /* PostScript error? */
                exit(EXIT_JOBERR);
            default:  /* Unknown error */
                exit(EXIT_PRNERR);
        }
    }
    else if (WIFSIGNALED(status)) {
        _log("renderer received signal: %d\n", WTERMSIG(status));
        switch (WTERMSIG(status)) {
            case SIGUSR1:
                exit(EXIT_PRNERR);
            case SIGUSR2:
                exit(EXIT_PRNERR_NORETRY);
            case SIGTTIN:
                exit(EXIT_ENGAGED);
        }
    }
}

/* This function runs the renderer command line (and if defined also
the postpipe) and returns a file handle for stuffing in the PostScript data. */
void get_renderer_handle(const dstr_t *prepend, int *fd, pid_t *pid)
{
    pid_t kid3;
    int kid3in;

    /* Catch signals */
    retval = EXIT_PRINTED;
    signal(SIGUSR1, set_exit_prnerr);
    signal(SIGUSR2, set_exit_prnerr_noretry);
    signal(SIGTTIN, set_exit_engaged);

    /* Build the command line and get the JCL commands */
    build_commandline(optionset("currentpage"));

    _log("\nStarting renderer\n");
    kid3 = start_process("kid3", exec_kid3, &kid3in, NULL);
    if (kid3 < 0)
        exit(EXIT_PRNERR_NORETRY_BAD_SETTINGS);

    /* Feed the PostScript header and the FIFO contents */
    if (prepend)
        write(kid3in, prepend->data, prepend->len);

    /* We are the parent, return glob to the file handle */
    *fd = kid3in;
    *pid = kid3;
}

/* Close the renderer process and wait until all kid processes finish */
int close_renderer_handle(int rendererhandle, pid_t rendererpid)
{
    int status, retval;

    _log("\nClosing renderer\n");
    close(rendererhandle);

    waitpid(rendererpid, NULL, 0);
    if (WIFEXITED(status)) {
        retval = WEXITSTATUS(status);
        _log("Renderer exited with status %d\n", retval);
    }
    else if (WIFSIGNALED(status)) {
        _log("Renderer caught signal %d\n", WTERMSIG(status));
        retval = EXIT_PRNERR_NORETRY_BAD_SETTINGS;
    }
    return retval;
}


int convkidfailed;
int kid1finished;
int kid2finished;
int pfd_kid_message_conv[2];


/*
 * Replace @@...@@PAGESIZE@@ and @@...@@JOBTITLE@@ with 'pagesize' and
 * 'jobtitle' (if they are are not NULL). Returns a newly malloced string.
 */
char * fileconverter_from_template(const char *fileconverter,
        const char *pagesize, const char *jobtitle)
{
    char *templstart, *templname;
    const char *last = fileconverter;
    char *res;

    res = malloc(strlen(fileconverter) + 
            pagesize ? strlen(pagesize) : 0 + 
            jobtitle ? strlen(jobtitle) : 0 +1);
    res[0] = '\0';

    while (templstart = strstr(last, "@@")) {
        strncat(res, last, templstart - last);
        templstart += 2;
        templname = strstr(templstart, "@@");
        if (!templname)
            break;
        if (startswith(templname, "@@PAGESIZE@@") && pagesize) {
            strncat(res, templstart, templname - templstart);
            strcat(res, pagesize);
            last = templname + 12;
        }
        else if (startswith(templname, "@@JOBTITLE@@") && jobtitle) {
            strncat(res, templstart, templname - templstart);
            strcat(res, jobtitle);
            while (templstart != templname) {
                if (*templstart == '\"') {
                    strcat(res, "\"");
                    break;
                }
                templstart++;
            }
            last = templname + 12;
        }
        else
            last += strlen(res);
    }
    strncat(res, last, fileconverter + strlen(fileconverter) - last);

    return res;
}

/*  This function is only used when the input data is not
    PostScript. Then it runs a filter which converts non-PostScript files into
    PostScript. The user can choose which filter he wants to use. The filter
    command line is provided by 'fileconverter'.
*/
void get_fileconverter_handle(const char *already_read, int *fd, pid_t *pid)
{
    int i, status;
    char tmp[1024];
    const char *p, *p2, *lastp;
    option_t *opt;
    const char *val;
    ssize_t count;
    pid_t kid1, kid2;
    int pfd_kid1[2];
    int pfd_kid2[2];
    const char *pagesize;
    char *fileconv;

    _log("\nStarting converter for non-PostScript files\n");


    pagesize = option_get_value(find_option("PageSize"), optionset("header"));
    /* Use wider margins so that the pages come out completely on
       every printer model (especially HP inkjets) */
    if (pagesize && startswith(fileconverter, "a2ps")) {
        if (!strcasecmp(pagesize, "letter"))
            pagesize = "Letterdj";
        else if (!strcasecmp(pagesize, "a4"))
            pagesize = "A4dj";
    }

    if (do_docs)
        snprintf(jobtitle, 128, "Documentation for the %s", printer_model);

    fileconv = fileconverter_from_template(fileconverter, pagesize, jobtitle);


    /*  Apply "pstops" when having used a file converter under CUPS, so
        CUPS can stuff the default settings into the PostScript output
        of the file converter (so all CUPS settings get also applied when
        one prints the documentation pages (all other files we get
        already converted to PostScript by CUPS). */
    if (spooler == SPOOLER_CUPS) {
        /* TODO */
        /* $fileconverter .=
                " | ${programdir}pstops '$rargs[0]' '$rargs[1]' '$rargs[2]' " .
                "'$rargs[3]' '$rargs[4]'"; */
    }

    /* Set up a pipe for the kids to pass their exit stat to the main process */
    pipe(pfd_kid_message_conv);

    convkidfailed = 0;
    kid1finished = 0;
    kid2finished = 0;

    pipe(pfd_kid1);
    kid1 = fork();
    if (kid1 < 0) {
        _log("cannot fork for kid1!\n");
        exit(EXIT_PRNERR_NORETRY_BAD_SETTINGS);
    }

    if (kid1) { /* parent */
        close(pfd_kid1[1]);
        *fd = pfd_kid1[0];
        *pid = kid1;
        return;
    }
    else { /* child */
        /* We go on reading the job data and stuff into the file converter */
        close(pfd_kid1[0]);

        pipe(pfd_kid2);
        kid2 = fork();
        if (kid2 < 0) {
            _log("cannot fork for kid2!\n");
            close(pfd_kid1[1]);
            close(pfd_kid2[0]);
            close(pfd_kid2[1]);
            close(pfd_kid_message_conv[0]);
            snprintf(tmp, 256, "1 %d\n", EXIT_PRNERR_NORETRY_BAD_SETTINGS);
            write(pfd_kid_message_conv[1], (const void*)tmp, strlen(tmp));
            exit(EXIT_PRNERR_NORETRY_BAD_SETTINGS);
        }

        if (kid2) { /* parent, child of primary task; we are |fileconverter| */
            close(pfd_kid2[1]);
            _log("file converter PID kid2=%d\n", kid2);
            if (debug || spooler != SPOOLER_CUPS)
                _log("file converter command: %s\n", fileconv);

            if (close(STDIN_FILENO) == -1 && errno != ESPIPE) {
                close(pfd_kid1[1]);
                close(pfd_kid2[0]);
                close(pfd_kid_message_conv[0]);
                snprintf(tmp, 256, "1 %d\n", EXIT_PRNERR_NORETRY_BAD_SETTINGS);
                write(pfd_kid_message_conv[1], (const void*)tmp, strlen(tmp));
                _log("Couldn't close STDIN in kid2\n");
                exit(EXIT_PRNERR_NORETRY_BAD_SETTINGS);
            }

            if (dup2(pfd_kid2[0], STDIN_FILENO) == -1) {
                close(pfd_kid1[1]);
                close(pfd_kid2[0]);
                close(pfd_kid_message_conv[0]);
                snprintf(tmp, 256, "1 %d\n", EXIT_PRNERR_NORETRY_BAD_SETTINGS);
                write(pfd_kid_message_conv[1], (const void*)tmp, strlen(tmp));
                _log("Couldn't dup KID2_IN\n");
                exit(EXIT_PRNERR_NORETRY_BAD_SETTINGS);
            }

            if (close(STDOUT_FILENO) == -1) {
                close(pfd_kid1[1]);
                close(pfd_kid2[0]);
                close(pfd_kid_message_conv[0]);
                snprintf(tmp, 256, "1 %d\n", EXIT_PRNERR_NORETRY_BAD_SETTINGS);
                write(pfd_kid_message_conv[1], (const void*)tmp, strlen(tmp));
                _log("Couldn't close STDOUT in kid2\n");
                exit(EXIT_PRNERR_NORETRY_BAD_SETTINGS);
            }

            if (dup2(pfd_kid1[1], STDOUT_FILENO) == -1) {
                close(pfd_kid1[1]);
                close(pfd_kid2[0]);
                close(pfd_kid_message_conv[0]);
                snprintf(tmp, 256, "1 %d\n", EXIT_PRNERR_NORETRY_BAD_SETTINGS);
                write(pfd_kid_message_conv[1], (const void*)tmp, strlen(tmp));
                _log("Couldn't dup KID1\n");
                exit(EXIT_PRNERR_NORETRY_BAD_SETTINGS);
            }

            if (debug) {
                if (dup2(fileno(logh), STDERR_FILENO) == -1) {
                    close(pfd_kid1[1]);
                    close(pfd_kid2[0]);
                    close(pfd_kid_message_conv[0]);
                    snprintf(tmp, 256, "1 %d\n", EXIT_PRNERR_NORETRY_BAD_SETTINGS);
                    write(pfd_kid_message_conv[1], (const void*)tmp, strlen(tmp));
                    _log("Couldn't dup logh to stderr\n");
                    exit(EXIT_PRNERR_NORETRY_BAD_SETTINGS);
                }
            }


            /* Actually run the thing... */
            status = modern_system(fileconv);
            if (!WIFEXITED(status) || (WIFEXITED(status) && WEXITSTATUS(status) != 0)) {
                _log("file converter return value: %d\n", WEXITSTATUS(status));
                if (WIFSIGNALED(status))
                    _log("file converter received signal: %d\n", WTERMSIG(status));

                close(STDIN_FILENO);
                close(STDOUT_FILENO);
                close(pfd_kid1[1]);
                close(pfd_kid2[0]);

                /* Handle signals*/
                if (WIFSIGNALED(status)) {
                    if (WTERMSIG(status) == SIGUSR1)
                        retval = EXIT_PRNERR;
                    else if (WTERMSIG(status) == SIGUSR2)
                        retval = EXIT_PRNERR_NORETRY;
                    else if (WTERMSIG(status) == SIGTTIN)
                        retval = EXIT_ENGAGED;
                }

                if (retval != EXIT_PRINTED) {
                    snprintf(tmp, 256, "1 %d\n", retval);
                    write(pfd_kid_message_conv[1], (const void *)tmp, strlen(tmp));
                    close(pfd_kid_message_conv[0]);
                    close(pfd_kid_message_conv[1]);
                    exit(retval);
                }

                /* Evaluate fileconverter result */
                if (WEXITSTATUS(status) == 0) {
                    /* Success, exit with 0 and inform main process */
                    snprintf(tmp, 256, "1 %d\n", EXIT_PRINTED);
                    write(pfd_kid_message_conv[1], (const void *)tmp, strlen(tmp));
                    close(pfd_kid_message_conv[0]);
                    close(pfd_kid_message_conv[1]);
                    exit(EXIT_PRINTED);
                }
                else {
                    /* Unknown error */
                    snprintf(tmp, 256, "1 %d\n", EXIT_PRNERR);
                    write(pfd_kid_message_conv[1], (const void *)tmp, strlen(tmp));
                    close(pfd_kid_message_conv[0]);
                    close(pfd_kid_message_conv[1]);
                    _log("The file converter command line returned an "
                            "unrecognized error code %d.\n", WEXITSTATUS(status));
                    exit(EXIT_PRNERR);
                }
            }

            close(STDOUT_FILENO);
            close(pfd_kid1[1]);
            close(STDIN_FILENO);
            close(pfd_kid2[0]);

            /* When arrived here the fileconverter command line was successful
               So exit with zero exit value here and inform the main process */
            snprintf(tmp, 256, "1 %d\n", EXIT_PRINTED);
            write(pfd_kid_message_conv[1], (const void *)tmp, strlen(tmp));
            close(pfd_kid_message_conv[0]);
            close(pfd_kid_message_conv[1]);
            /* Wait for input child */
            waitpid(kid1, NULL, 0);
            _log("KID1 finished\n");
            exit(EXIT_PRINTED);
        }
        else {
            /*  child, first part of the pipe, reading in the data from
                standard input and stuffing it into the file converter
                after putting in the already read data (in alreadyread) */
            close(pfd_kid1[1]);
            close(pfd_kid2[0]);

            /* At first pass the data which we already read to the filter */
            write(pfd_kid2[1], (const void *)already_read, strlen(already_read));
            /* Then read the rest from standard input */
            while ((count = read(STDIN_FILENO, (void *)tmp, 1024)))
                write(STDOUT_FILENO, (void *)tmp, count);

            if (close(STDIN_FILENO) == -1 && errno != ESPIPE) {
                close(pfd_kid2[1]);
                snprintf(tmp, 256, "2 %d\n", EXIT_PRNERR_NORETRY_BAD_SETTINGS);
                write(pfd_kid_message_conv[1], (const void *)tmp, strlen(tmp));
                close(pfd_kid_message_conv[0]);
                close(pfd_kid_message_conv[1]);
                _log("error closing STDIN\n");
                exit(EXIT_PRNERR_NORETRY_BAD_SETTINGS);
            }
            close(pfd_kid2[1]);
            _log("tail process done reading data from STDIN\n");

            /* Successful exit, inform main process */
            snprintf(tmp, 256, "2 %d\n", EXIT_PRINTED);
            write(pfd_kid_message_conv[1], (const void *)tmp, strlen(tmp));
            close(pfd_kid_message_conv[0]);
            close(pfd_kid_message_conv[1]);

            _log("KID2 finished\n");
            exit(EXIT_PRINTED);
        }
    }

    free(fileconv);
}

int close_fileconverter_handle(int fileconverter_handle, pid_t fileconverter_pid)
{
    char message [1024];
    ssize_t n;
    int kid_id, exitstat;

    _log("\nClosing file converter\n");
    close(fileconverter_handle);

    /* Wait for all kid processes to finish or one kid process to fail */
    close(pfd_kid_message_conv[1]);

    while (!convkidfailed && !(kid1finished && kid2finished)) {
        n = read(pfd_kid_message_conv[0], message, 1024);
        message[n] = '\0';
        if (sscanf(message, "%d %d", &kid_id, &exitstat) == 2) {
            _log("KID%d exited with status %d\n", kid_id, exitstat);
            if (exitstat > 0)
                convkidfailed = exitstat;
            else if (kid_id == 1)
                kid1finished = 1;
            else if (kid_id == 2)
                kid2finished = 1;
        }
    }

    close(pfd_kid_message_conv[0]);

    /* If a kid failed, return the exit stat of this kid */
    if (convkidfailed != 0)
        retval = convkidfailed;

    _log("File converter exit stat: %s\n", retval);

    /* Wait for fileconverter child */
    waitpid(fileconverter_pid, NULL, 0);

    _log("File converter proecess finished\n");

    return retval;
}

/* Parse a string containing page ranges and either check whether a
   given page is in the ranges or, if the given page number is zero,
   determine the score how specific this page range string is.*/
int parse_page_ranges(const char *ranges, int page)
{
    int rangestart = 0;
    int rangeend = 0;
    int tmp;
    int totalscore = 0;
    int pageinside = 0;
    char *rangestr;
    const char *p;
    const char *rangeend_pos;

    p = ranges;
    rangeend_pos = ranges;
    while (*rangeend_pos && (rangeend_pos = strchrnul(rangeend_pos, ','))) {

        if (!prefixcasecmp(p, "even")) {
            totalscore += 50000;
            if (page % 2 == 0)
                pageinside = 1;
        }
        else if (!prefixcasecmp(p, "odd")) {
            totalscore += 50000;
            if (page % 2 == 1)
                pageinside = 1;
        }
        else if (isdigit(*p)) {
            rangestart = strtol(p, (char**)&p, 10);
            if (*p == '-') {
                p++;
                if (isdigit(*p)) {  /* Page range is a sequence of pages */
                    rangeend = strtol(p, NULL, 10);
                    totalscore += abs(rangeend - rangestart) +1;
                    if (rangeend < rangestart) {
                        tmp = rangestart;
                        rangestart = rangeend;
                        rangeend = tmp;
                    }
                    if (page >= rangestart && page <= rangeend)
                        pageinside = 1;
                }
                else {              /* Page range goes to the end of the document */
                    totalscore += 100000;
                    if (page >= rangestart)
                        pageinside = 1;
                }
            }
            else {                  /* Page range is a single page */
                totalscore += 1;
                if (page == rangestart)
                    pageinside = 1;
            }
        }
        else {  /* Invalid range */
            rangestr = malloc(rangeend_pos - p +1);
            strlcpy(rangestr, p, rangeend_pos - p +1);
            _log("   Invalid range: %s", rangestr);
            free(rangestr);
        }
        rangestart = 0;
        rangeend = 0;
    }

    if (page == 0 || pageinside)
        return totalscore;

    return 0;
}

/* Set the options for a given page */
void set_options_for_page(int optset, int page)
{
    int score, bestscore;
    option_t *opt;
    value_t *val, *bestvalue;
    const char *ranges;
    const char *optsetname;

    for (opt = optionlist; opt; opt = opt->next) {

        bestscore = 10000000;
        bestvalue = NULL;
        for (val = opt->valuelist; val; val = val->next) {

            optsetname = optionset_name(val->optionset);
            if (!startswith(optsetname, "pages:"))
                continue;

            ranges = &optsetname[6]; /* after "pages:" */
            score = parse_page_ranges(ranges, page);
            if (score && score < bestscore) {
                bestscore = score;
                bestvalue = val;
            }
        }

        if (bestvalue)
            option_set_value(opt, optset, bestvalue->value);
    }
}


/* little helper function for print_file */
#define LT_BEGIN_FEATURE 1
#define LT_FOOMATIC_RIP_OPTION_SETTING 2
int line_type(const char *line)
{
    const char *p;
    if (startswith(line, "%%BeginFeature:"))
        return LT_BEGIN_FEATURE;
    p = line;
    while (*p && isspace(*p)) p++;
    if (!startswith(p, "%%"))
        return 0;
    p += 2;
    while (*p && isspace(*p)) p++;
    if (startswith(p, "FoomaticRIPOptionSetting:"))
        return LT_FOOMATIC_RIP_OPTION_SETTING;
    return 0;
}


/*  Next, examine the PostScript job for traces of command-line and
    JCL options. PPD-aware applications and spoolers stuff option
    settings directly into the file, they do not necessarily send
    PPD options by the command line. Also stuff in PostScript code
    to apply option settings given by the command line and to set
    the defaults given in the PPD file.

    Examination strategy: read lines from STDIN until the first
    %%Page: comment appears and save them as @psheader. This is the
    page-independent header part of the PostScript file. The
    PostScript interpreter (renderer) must execute this part once
    before rendering any assortment of pages. Then pages can be
    printed in any arbitrary selection or order. All option
    settings we find here will be collected in the default option
    set for the RIP command line.

    Now the pages will be read and sent to the renderer, one after
    the other. Every page is read into memory until the
    %%EndPageSetup comment appears (or a certain amount of lines was
    read). So we can get option settings only valid for this
    page. If we have such settings we set them in the modified
    command set for this page.

    If the renderer is not running yet (first page) we start it with
    the command line built from the current modified command set and
    send the first page to it, in the end we leave the renderer
    running and keep input and output pipes open, so that it can
    accept further pages. If the renderer is still running from
    the previous page and the current modified command set is the
    same as the one for the previous page, we send the page. If
    the command set is different, we close the renderer, re-start
    it with the command line built from the new modified command
    set, send the header again, and then the page.

    After the last page the trailer (%%Trailer) is sent.

    The output pipe of this program stays open all the time so that
    the spooler does not assume that the job has finished when the
    renderer is re-started.

    Non DSC-conforming documents will be read until a certain line
    number is reached. Command line or JCL options inserted later
    will be ignored.

    If options are implemented by PostScript code supposed to be
    stuffed into the job's PostScript data we stuff the code for all
    these options into our job data, So all default settings made in
    the PPD file (the user can have edited the PPD file to change
    them) are taken care of and command line options get also
    applied. To give priority to settings made by applications we
    insert the options's code in the beginnings of their respective
    sections, so that sommething, which is already inserted, gets
    executed after our code. Missing sections are automatically
    created. In non-DSC-conforming files we insert the option code
    in the beginning of the file. This is the same policy as used by
    the "pstops" filter of CUPS.

    If CUPS is the spooler, the option settings were already
    inserted by the "pstops" filter, so we don't insert them
    again. The only thing we do is correcting settings of numerical
    options when they were set to a value not available as choice in
    the PPD file, As "pstops" does not support "real" numerical
    options, it sees these settings as an invalid choice and stays
    with the default setting. In this case we correct the setting in
    the first occurence of the option's code, as this one is the one
    added by CUPS, later occurences come from applications and
    should not be touched.

    If the input is not PostScript (if there is no "%!" after
    $maxlinestopsstart lines) a file conversion filter will
    automatically be applied to the incoming data, so that we will
    process the resulting PostScript here. This way we have always
    PostScript data here and so we can apply the printer/driver
    features described in the PPD file.

    Supported file conversion filters are "a2ps", "enscript",
    "mpage", and spooler-specific filters. All filters convert
    plain text to PostScript, "a2ps" also other formats. The
    conversion filter is always used when one prints the
    documentation pages, as they are created as plain text,
    when CUPS is the spooler "pstops" is executed after the
    filter so that the default option settings from the PPD file
    and CUPS-specific options as N-up get applied. On regular
    printouts one gets always PostScript when CUPS or PPR is
    the spooler, so the filter is only used for regular
    printouts under LPD, LPRng, GNUlpr or without spooler.
*/

/* PostScript sections */
#define PS_SECTION_JCLSETUP 1
#define PS_SECTION_PROLOG 2
#define PS_SECTION_SETUP 3
#define PS_SECTION_PAGESETUP 4

#define MAX_NON_DSC_LINES_IN_HEADER 1000
#define MAX_LINES_FOR_PAGE_OPTIONS 200


void print_ps_file()
{
    char *p;

    int maxlines = 1000;    /* Maximum number of lines to be read  when the
                               documenent is not  DSC-conforming.
                               "$maxlines = 0"  means that all will be read and
                               examined. If it is  discovered that the input
                               file  is DSC-conforming, this will  be set to 0. */

    int maxlinestopsstart = 200;    /* That many lines are allowed until the
                                      "%!" indicating PS comes. These
                                      additional lines in the
                                      beginning are usually JCL
                                      commands. The lines will be
                                      ignored by our parsing but
                                      passed through. */

    int printprevpage = 0;  /* We set this when encountering "%%Page:" and the
                               previous page is not printed yet. Then it will
                               be printed and the new page will be prepared in
                               the next run of the loop (we don't read a new
                               line and don't increase the $linect then). */

    int linect = 0;         /* how many lines have we examined */
    int nonpslines = 0;     /* lines before "%!" found yet. */
    int more_stuff = 1;     /* there is more stuff in stdin */
    int saved = 0;          /* DSC line not precessed yet */
    int isdscjob = 0;       /* is the job dsc conforming */
    int inheader = 1;       /* Are we still in the header, before first
                               "%%Page:" comment= */

    int optionsalsointoheader = 0; /* 1: We are in a "%%BeginSetup...
                                    %%EndSetup" section after the first
                                    "%%Page:..." line (OpenOffice.org
                                    does this and intends the options here
                                    apply to the whole document and not
                                    only to the current page). We have to
                                    add all lines also to the end of the
                                    @psheader now and we have to set
                                    non-PostScript options also in the
                                    "header" optionset. 0: otherwise. */

    int insertoptions = 1;  /* If we find out that a file with a DSC magic
                               string ("%!PS-Adobe-") is not really DSC-
                               conforming, we insert the options directly
                               after the line with the magic string. We use
                               this variable to store the number of the line
                               with the magic string */

    int prologfound = 0;    /* Did we find the
                               "%%BeginProlog...%%EndProlog" section? */
    int setupfound = 0;     /* Did we find the
                               %%BeginSetup...%%EndSetup" section? */
    int pagesetupfound = 0; /* special page setup handling needed */

    int inprolog = 0;       /* We are between "%%BeginProlog" and "%%EndProlog" */
    int insetup = 0;        /* We are between "%%BeginSetup" and "%%EndSetup" */
    int infeature = 0;      /* We are between "%%BeginFeature" and "%%EndFeature" */

    int optionreplaced = 0; /* Will be set to 1 when we are in an
                               option ("%%BeginFeature...
                               %%EndFeature") which we have replaced. */

    int postscriptsection = PS_SECTION_JCLSETUP; /* In which section of the PostScript file
                                                   are we currently ? */

    int nondsclines = 0;    /* Number of subsequent lines found which are at a
                               non-DSC-conforming place, between the sections
                               of the header.*/

    int nestinglevel = 0;   /* Are we in the main document (0) or in an
                               embedded document bracketed by "%%BeginDocument"
                               and "%%EndDocument" (>0) We do not parse the
                               PostScript in an embedded document. */

    int inpageheader = 0;   /* Are we in the header of a page,
                               between "%%BeginPageSetup" and
                               "%%EndPageSetup" (1) or not (0). */

    int passthru = 0;       /* 0: write data into psfifo,
                               1: pass data directly to the renderer */

    int lastpassthru = 0;   /* State of 'passthru' in previous line
                               (to allow debug output when $passthru
                               switches. */

    int ignorepageheader = 0; /* Will be set to 1 as soon as active
                                 code (not between "%%BeginPageSetup"
                                 and "%%EndPageSetup") appears after a
                                 "%%Page:" comment. In this case
                                 "%%BeginPageSetup" and
                                 "%%EndPageSetup" is not allowed any
                                 more on this page and will be ignored.
                                 Will be set to 0 when a new "%%Page:"
                                 comment appears. */

    int optset = optionset("header"); /* Where do the option settings which
                                         we have found go? */

    /* current line */
    dstr_t *line = create_dstr();

    dstr_t *onelinebefore = create_dstr();
    dstr_t *twolinesbefore = create_dstr();

    /* The header of the PostScript file, to be send after each start of the renderer */
    dstr_t *psheader = create_dstr();

    /* The input FIFO, data which we have pulled from stdin for examination,
       but not send to the renderer yet */
    dstr_t *psfifo = create_dstr();

    int fileconverter_handle = 0; /* File handle to converter process */
    pid_t fileconverter_pid = 0;  /* PID of the fileconverter process */

    int ignoreline;

    jobhasjcl = 0;

    int ooo110 = 0;         /* Flag to work around an application bug */

    dstr_t *tmp = create_dstr();

    int currentpage = 0;   /* The page which we are currently printing */

    option_t *o;
    const char *val;

    int linetype;

    dstr_t *linesafterlastbeginfeature = create_dstr(); /* All codelines after the last "%%BeginFeature" */

    char optionname [128];
    char value [128];
    int fromcomposite = 0;

    dstr_t *pdest;

    double width, height;

    pid_t rendererpid = 0;
    int rendererhandle = -1;


    /* We do not parse the PostScript to find Foomatic options, we check
        only whether we have PostScript. */
    if (dontparse)
        maxlines = 1;

    _log("Reading PostScript input ...\n");

    do {
        ignoreline = 0;

        if (printprevpage || saved || fgetdstr(line, stdin)) {
            saved = 0;
            if (linect == nonpslines) {
                /* In the beginning should be the postscript leader,
                   sometimes after some JCL commands */
                if ( !(line->data[0] == '%' && line->data[1] == '!') &&
                     !(line->data[1] == '%' && line->data[2] == '!')) /* There can be a Windows control character before "%!" */
                {
                    nonpslines++;
                    if (maxlines == nonpslines)
                        maxlines ++;
                    jobhasjcl = 1;

                    if (nonpslines > maxlinestopsstart) {
                        /* This is not a PostScript job, we must convert it */
                        _log("Job does not start with \"%%!\", is it Postscript?\n"
                             "Starting file converter\n");

                        /* Reset all variables but conserve the data which we have already read */
                        jobhasjcl = 0;
                        linect = 0;
                        nonpslines = 1; /* Take into account that the line of this run of the loop
                                        will be put into @psheader, so the first line read by
                                        the file converter is already the second line */
                        maxlines = 1001;

                        dstrclear(onelinebefore);
                        dstrclear(twolinesbefore);

                        dstrcpyf(tmp, "%s%s%s", psheader, psfifo, line);
                        dstrclear(psheader);
                        dstrclear(psfifo);
                        dstrclear(line);

                        /* Start the file conversion filter */
                        if (!fileconverter_pid) {
                            get_fileconverter_handle(tmp->data, &fileconverter_handle, &fileconverter_pid);
                            if (retval != EXIT_PRINTED) {
                                _log("Error opening file converter\n");
                                exit(retval);
                            }
                        }
                        else {
                            _log("File conversion filter probably crashed\n");
                            exit(EXIT_JOBERR);
                        }

                        /* Read the further data from the file converter and not from STDIN */
                        if (close(STDIN_FILENO) == -1 && errno != ESPIPE) {
                            _log("Couldn't close STDIN\n");
                            exit(EXIT_PRNERR_NORETRY_BAD_SETTINGS);
                        }
                        if (dup2(STDIN_FILENO, fileconverter_handle) == -1) {
                            _log("Couldn't dup fileconverter_handle\n");
                            exit(EXIT_PRNERR_NORETRY_BAD_SETTINGS);
                        }
                    }
                }
                else {
                    /* Do we have a DSC-conforming document? */
                    if ((line->data[0] == '%' && startswith(line->data, "%!PS-Adobe-")) ||
                        (line->data[1] == '%' && startswith(line->data, "%!PS-Adobe-")))
                    {
                        /* Do not stop parsing the document */
                        if (!dontparse) {
                            maxlines = 0;
                            isdscjob = 1;
                            insertoptions = linect + 1;
                            /* We have written into psfifo before, now we continue in
                               psheader and move over the data which is already in psfifo */
                            dstrcat(psheader, psfifo->data);
                            dstrclear(psfifo);
                        }
                        _log("--> This document is DSC-conforming!\n");
                    }
                    else {
                        /* Job is not DSC-conforming, stick in all PostScript
                           option settings in the beginning */
                        append_prolog_section(line, optset, 1);
                        append_setup_section(line, optset, 1);
                        append_page_setup_section(line, optset, 1);
                        prologfound = 1;
                        setupfound = 1;
                        pagesetupfound = 1;
                    }
                }
            }
            else {
                if (startswith(line->data, "%%")) {
                    if (startswith(line->data, "%%BeginDocument")) {
                        /* Beginning of an embedded document
                        Note that Adobe Acrobat has a bug and so uses
                        "%%BeginDocument " instead of "%%BeginDocument:" */
                        nestinglevel++;
                        _log("Embedded document, nesting level now: %d\n", nestinglevel);
                    }
                    else if (nestinglevel > 0 && startswith(line->data, "%%EndDocument")) {
                        /* End of an embedded document */
                        nestinglevel--;
                        _log("End of embedded document, nesting level now: %d\n", nestinglevel);
                    }
                    else if (nestinglevel == 0 && startswith(line->data, "%%Creator")) {
                        /* Here we set flags to treat particular bugs of the
                        PostScript produced by certain applications */
                        p = strstr(line->data, "%%Creator") + 9;
                        while (*p && (isspace(*p) || *p == ':')) p++;
                        if (!strcmp(p, "OpenOffice.org")) {
                            p += 14;
                            while (*p && isspace(*p)) p++;
                            if (sscanf(p, "1.1.%d", &ooo110) == 1) {
                                _log("Document created with OpenOffice.org 1.1.x\n");
                                ooo110 = 1;
                            }
                        }
                    }
                    else if (nestinglevel == 0 && startswith(line->data, "%%BeginProlog")) {
                        /* Note: Below is another place where a "Prolog" section
                        start will be considered. There we assume start of the
                        "Prolog" if the job is DSC-Conformimg, but an arbitrary
                        comment starting with "%%Begin", but not a comment
                        explicitly treated here, is found. This is done because
                        many "dvips" (TeX/LaTeX) files miss the "%%BeginProlog"
                        comment.
                        Beginning of Prolog */
                        _log("\n-----------\nFound: %%%%BeginProlog\n");
                        inprolog = 1;
                        if (inheader)
                            postscriptsection = PS_SECTION_PROLOG;
                        nondsclines = 0;
                        /* Insert options for "Prolog" */
                        if (!prologfound) {
                            append_prolog_section(line, optset, 0);
                            prologfound = 1;
                        }
                    }
                    else if (nestinglevel == 0 && startswith(line->data, "%%EndProlog")) {
                        /* End of Prolog */
                        _log("Found: %%%%EndProlog\n");
                        inprolog = 0;
                        insertoptions = linect +1;
                    }
                    else if (nestinglevel == 0 && startswith(line->data, "%%BeginSetup")) {
                        /* Beginning of Setup */
                        _log("\n-----------\nFound: %%%%BeginSetup\n");
                        insetup = 1;
                        nondsclines = 0;
                        /* We need to distinguish with the $inheader variable
                        here whether we are in the header or on a page, as
                        OpenOffice.org inserts a "%%BeginSetup...%%EndSetup"
                        section after the first "%%Page:..." line and assumes
                        this section to be valid for all pages. */
                        if (inheader) {
                            postscriptsection = PS_SECTION_SETUP;
                            /* If there was no "Prolog" but there are
                            options for the "Prolog", push a "Prolog"
                            with these options onto the psfifo here */
                            if (!prologfound) {
                                dstrclear(tmp);
                                append_prolog_section(tmp, optset, 1);
                                dstrprepend(line, tmp->data);
                                prologfound = 1;
                            }
                            /* Insert options for "DocumentSetup" or "AnySetup" */
                            if (spooler != SPOOLER_CUPS && !setupfound) {
                                /* For non-CUPS spoolers or no spooler at all,
                                we leave everythnig as it is */
                                append_setup_section(line, optset, 0);
                                setupfound = 1;
                            }
                        }
                        else {
                            /* Found option settings must be stuffed into both
                            the header and the currrent page now. They will
                            be written into both the "header" and the
                            "currentpage" optionsets and the PostScript code
                            lines of this section will not only go into the
                            output stream, but also added to the end of the
                            @psheader, so that they get repeated (to preserve
                            the embedded PostScript option settings) on a
                            restart of the renderer due to command line
                            option changes */
                            optionsalsointoheader = 1;
                            _log("\"%%%%BeginSetup\" in page header\n");
                        }
                    }
                    else if (nestinglevel == 0 && startswith(line->data, "%%EndSetup")) {
                        /* End of Setup */
                        _log("Found: %%%%EndSetup\n");
                        insetup = 0;
                        if (inheader) {
                            if (spooler == SPOOLER_CUPS) {
                                /* In case of CUPS, we must insert the
                                accounting stuff just before the
                                %%EndSetup comment in order to leave any
                                EndPage procedures that have been
                                defined by either the pstops filter or
                                the PostScript job itself fully
                                functional. */
                                if (!setupfound) {
                                    dstrclear(tmp);
                                    append_setup_section(tmp, optset, 0);
                                    dstrprepend(line, tmp->data);
                                    setupfound = 1;
                                }
                            }
                            insertoptions = linect +1;
                        }
                        else {
                            /* The "%%BeginSetup...%%EndSetup" which
                            OpenOffice.org has inserted after the first
                            "%%Page:..." line ends here, so the following
                            options go only onto the current page again */
                            optionsalsointoheader = 0;
                        }
                    }
                    else if (nestinglevel == 0 && startswith(line->data, "%%Page:")) {
                        if (!lastpassthru && !inheader) {
                            /* In the last line we were not in passthru mode,
                            so the last page is not printed. Prepare to do
                            it now. */
                            printprevpage = 1;
                            passthru = 1;
                            _log("New page found but previous not printed, print it now.\n");
                        }
                        else {
                            /* the previous page is printed, so we can prepare
                            the current one */
                            _log("\n-----------\nNew page: %s", line->data);
                            printprevpage = 0;
                            currentpage++;
                            /* We consider the beginning of the page already as
                            page setup section, as some apps do not use
                            "%%PageSetup" tags. */
                            postscriptsection = PS_SECTION_PAGESETUP;

                            /* TODO can this be removed?
                            Save PostScript state before beginning the page
                            $line .= "/foomatic-saved-state save def\n"; */

                            /* Here begins a new page */
                            if (inheader) {
                                /* Here we add some stuff which still
                                belongs into the header */
                                dstrclear(tmp);

                                /* If there was no "Prolog" but there are
                                options for the "Prolog", push a "Prolog"
                                with these options onto the @psfifo here */
                                if (!prologfound) {
                                    append_prolog_section(tmp, optset, 1);
                                    prologfound = 1;
                                }
                                /* If there was no "Setup" but there are
                                options for the "Setup", push a "Setup"
                                with these options onto the @psfifo here */
                                if (!setupfound) {
                                    append_setup_section(tmp, optset, 1);
                                    setupfound = 1;
                                }
                                /* Now we push this into the header */
                                dstrcat(psheader, tmp->data);

                                /* The first page starts, so header ends */
                                inheader = 0;
                                nondsclines = 0;
                                /* Option setting should go into the page
                                specific option set now */
                                optset = optionset("currentpage");
                            }
                            else {
                                /*  Restore PostScript state after completing the
                                    previous page:

                                        foomatic-saved-state restore
                                        %%Page: ...
                                        /foomatic-saved-state save def

                                    Print this directly, so that if we need to
                                    restart the renderer for this page due to
                                    a command line change this is done under the
                                    old instance of the renderer
                                    rint $rendererhandle
                                    "foomatic-saved-state restore\n"; */

                                /* Save the option settings of the previous page */
                                optionset_copy_values(optionset("currentpage"), optionset("previouspage"));
                                optionset_delete_values(optionset("currentpage"));
                            }
                            /* Initialize the option set */
                            optionset_copy_values(optionset("header"), optionset("currentpage"));

                            /* Set the command line options which apply only
                                to given pages */
                            set_options_for_page(optionset("currentpage"), currentpage);
                            pagesetupfound = 0;
                            if (spooler == SPOOLER_CUPS) {
                                /* Remove the "notfirst" flag from all options
                                    forseen for the "PageSetup" section, because
                                    when these are numerical options for CUPS.
                                    they have to be set to the correct value
                                    for every page */
                                for (o = optionlist; o; o = o->next) {
                                    if (option_get_section(o ) == SECTION_PAGESETUP)
                                        o->notfirst = 0;
                                }
                            }
                            /* Insert PostScript option settings
                                (options for section "PageSetup") */
                            if (isdscjob) {
                                append_page_setup_section(line, optset, 0);
                                pagesetupfound = 1;
                            }
                            /* Now the page header comes, so buffer the data,
                                because we must perhaps shut down and restart
                                the renderer */
                            passthru = 0;
                            ignorepageheader = 0;
                            optionsalsointoheader = 0;
                        }
                    }
                    else if (nestinglevel == 0 && !ignorepageheader &&
                            startswith(line->data, "%%BeginPageSetup")) {
                        /* Start of the page header, up to %%EndPageSetup
                        nothing of the page will be drawn, page-specific
                        option settngs (as letter-head paper for page 1)
                        go here*/
                        _log("\nFound: %%%%BeginPageSetup\n");
                        passthru = 0;
                        inpageheader = 1;
                        postscriptsection = PS_SECTION_PAGESETUP;
                        optionsalsointoheader = (ooo110 && currentpage == 1) ? 1 : 0;
                    }
                    else if (nestinglevel == 0 && !ignorepageheader &&
                            startswith(line->data, "%%BeginPageSetup")) {
                        /* End of the page header, the page is ready to be printed */
                        _log("Found: %%%%EndPageSetup\n");
                        _log("End of page header\n");
                        /* We cannot for sure say that the page header ends here
                        OpenOffice.org puts (due to a bug) a "%%BeginSetup...
                        %%EndSetup" section after the first "%%Page:...". It
                        is possible that CUPS inserts a "%%BeginPageSetup...
                        %%EndPageSetup" before this section, which means that
                        the options in the "%%BeginSetup...%%EndSetup"
                        section are after the "%%EndPageSetup", so we
                        continue for searching options up to the buffer size
                        limit $maxlinesforpageoptions. */
                        passthru = 0;
                        inpageheader = 0;
                        optionsalsointoheader = 0;
                    }
                    else if (nestinglevel == 0 && !optionreplaced && (!passthru || !isdscjob) &&
                            ((linetype = line_type(line->data)) &&
                            (linetype == LT_BEGIN_FEATURE || linetype == LT_FOOMATIC_RIP_OPTION_SETTING))) {

                        /* parse */
                        if (linetype == LT_BEGIN_FEATURE) {
                            dstrcpy(tmp, line->data);
                            p = strtok(tmp->data, " \t"); /* %%BeginFeature: */
                            p = strtok(NULL, " \t="); /* Option */
                            if (*p == '*') p++;
                            strlcpy(optionname, p, 128);
                            p = strtok(NULL, " \t\r\n"); /* value */
                            strlcpy(value, p, 128);
                        }
                        else { /* LT_FOOMATIC_RIP_OPTION_SETTING */
                            dstrcpy(tmp, line->data);
                            p = strstr(tmp->data, "FoomaticRIPOptionSetting:");
                            p = strtok(p, " \t");  /* FoomaticRIPOptionSetting */
                            p = strtok(NULL, " \t="); /* Option */
                            strlcpy(optionname, p, 128);
                            p = strtok(NULL, " \t\r\n"); /* value */
                            if (*p == '@') { /* fromcomposite */
                                p++;
                                fromcomposite = 1;
                            }
                            else
                                fromcomposite = 0;
                            strlcpy(value, p, 128);
                        }

                        /* Mark that we are in a "Feature" section */
                        if (linetype == LT_BEGIN_FEATURE) {
                            infeature = 1;
                            dstrclear(linesafterlastbeginfeature);
                        }

                        /* OK, we have an option.  If it's not a
                        Postscript-style option (ie, it's command-line or
                        JCL) then we should note that fact, since the
                        attribute-to-filter option passing in CUPS is kind of
                        funky, especially wrt boolean options. */
                        _log("Found: %s", line->data);
                        if ((o = find_option(optionname))) {
                            _log("   Option: %s=%s%s", optionname, fromcomposite ? "From" : "", value);
                            if (spooler == SPOOLER_CUPS &&
                                linetype == LT_BEGIN_FEATURE &&
                                !option_get_value(o, optionset("notfirst")) &&
                                strcmp(option_get_value(o, optset), value) != 0 &&
                                (inheader || option_get_section(o) == SECTION_PAGESETUP)) {

                                /* We have the first occurence of an option
                                setting and the spooler is CUPS, so this
                                setting is inserted by "pstops" or
                                "imagetops". The value from the command
                                line was not inserted by "pstops" or
                                "imagetops" so it seems to be not under
                                the choices in the PPD. Possible
                                reasons:

                                - "pstops" and "imagetops" ignore settings
                                of numerical or string options which are
                                not one of the choices in the PPD file,
                                and inserts the default value instead.

                                - On the command line an option was applied
                                only to selected pages:
                                "-o <page ranges>:<option>=<values>
                                This is not supported by CUPS, so not
                                taken care of by "pstops".

                                We must fix this here by replacing the
                                setting inserted by "pstops" or "imagetops"
                                with the exact setting given on the command
                                line. */

                                /* $arg->{$optionset} is already
                                range-checked, so do not check again here
                                Insert DSC comment */
                                pdest = (inheader && isdscjob) ? psheader : psfifo;
                                if (option_is_ps_command(o)) {
                                    /* PostScript option, insert the code */

                                    option_get_command(tmp, o, optset, -1);
                                    if (!(val = option_get_value(o, optset)))
                                        val = "";

                                    if (o->type == TYPE_BOOL)
                                        dstrcatf(pdest, "%%%%BeginFeature: *%s %s\n", o->name,
                                                val && !strcmp(val, "1") ? "True" : "False");
                                    else
                                        dstrcatf(pdest, "%%%%BeginFeature: *%s %s\n", o->name, val);

                                    dstrcatf(pdest, "%s\n", tmp->data);
                                }
                                else {   /* Command line or JCL option */
                                    val = option_get_value(o, optset);
                                    dstrcatf(pdest, "%%%% FoomaticRIPOptionSetting: %s=%s\n",
                                            o->name, val ? val : "");
                                }
                                val = option_get_value(o, optset);
                                _log(" --> Correcting numerical/string option to %s=%s (Command line argument)\n",
                                    o->name, val ? val : "");
                                /* We have replaced this option on the FIFO */
                                optionreplaced = 1;
                            }

                            /* Mark that we have already found this option */
                            o->notfirst = 1;
                            if (!optionreplaced) {
                                if (o->style != 'G') {
                                    /* Controlled by '<Composite>' setting of
                                    a member option of a composite option */
                                    if (fromcomposite) {
                                        dstrcpyf(tmp, "From%s", value);
                                        strlcpy(value, tmp->data, 128);
                                    }

                                    /* Non PostScript option
                                    Check whether it is valid */
                                    if (option_set_value(o, optset, value)) {
                                        _log("Setting option\n");
                                        strlcpy(value, option_get_value(o, optset), 128);
                                        if (optionsalsointoheader)
                                            option_set_value(o, optionset("header"), value);
                                        if (o->type == TYPE_ENUM &&
                                                (!strcmp(o->name, "PageSize") || !strcmp(o->name, "PageRegion")) &&
                                                startswith(value, "Custom") &&
                                                linetype == LT_FOOMATIC_RIP_OPTION_SETTING) {
                                            /* Custom Page size */
                                            width = height = 0.0;
                                            p = linesafterlastbeginfeature->data;
                                            while (*p && isspace(*p)) p++;
                                            width = strtod(p, &p);
                                            while (*p && isspace(*p)) p++;
                                            height = strtod(p, &p);
                                            if (width && height) {
                                                dstrcpyf(tmp, "%s.%fx%f", value, width, height);
                                                strlcpy(value, tmp->data, 128);
                                                option_set_value(o, optset, value);
                                                if (optionsalsointoheader)
                                                    option_set_value(o, optionset("header"), value);
                                            }
                                        }
                                        /* For a composite option insert the
                                        code from the member options with
                                        current setting "From<composite>"
                                        The code from the member options
                                        is chosen according to the setting
                                        of the composite option. */
                                        if (option_is_composite(o) && linetype == LT_FOOMATIC_RIP_OPTION_SETTING) {
                                            build_commandline(optset); /* TODO can this be removed? */

                                            /* TODO merge section and ps_section */
                                            if (postscriptsection == PS_SECTION_JCLSETUP)
                                                option_get_command(tmp, o, optset, SECTION_JCLSETUP);
                                            else if (postscriptsection == PS_SECTION_PROLOG)
                                                option_get_command(tmp, o, optset, SECTION_PROLOG);
                                            else if (postscriptsection == PS_SECTION_SETUP)
                                                option_get_command(tmp, o, optset, SECTION_DOCUMENTSETUP);
                                            else if (postscriptsection == PS_SECTION_PAGESETUP)
                                                option_get_command(tmp, o, optset, SECTION_PAGESETUP);
                                            dstrcat(line, tmp->data);
                                        }
                                    }
                                    else
                                        _log(" --> Invalid option setting found in job\n");
                                }
                                else if (fromcomposite) {
                                    /* PostScript option, but we have to look up
                                    the PostScript code to be inserted from
                                    the setting of a composite option, as
                                    this option is set to "Controlled by
                                    '<Composite>'". */
                                    /* Set the option */
                                    dstrcpyf(tmp, "From%s", value);
                                    strlcpy(value, tmp->data, 128);
                                    if (option_set_value(o, optset, value)) {
                                        _log(" --> Looking up setting in composite option %s\n", value);
                                        if (optionsalsointoheader)
                                            option_set_value(o, optionset("header"), value);
                                        /* update composite options */
                                        build_commandline(optset);
                                        /* Substitute PostScript comment by the real code */
                                        /* TODO what exactly is the next line doing? */
                                        /* dstrcpy(line, o->compositesubst->data); */
                                    }
                                    else
                                        _log(" --> Invalid option setting found in job\n");
                                }
                                else
                                    /* it is a PostScript style option with
                                    the code readily inserted, no option
                                    for the renderer command line/JCL to set,
                                    no lookup of a composite option needed,
                                    so nothing to do here... */
                                    _log(" --> Option will be set by PostScript interpreter\n");
                            }
                        }
                        else
                            /* This option is unknown to us, WTF? */
                            _log("Unknown option %s=%s found in the job\n", optionname, value);
                    }
                    else if (nestinglevel == 0 && startswith(line->data, "%%EndFeature")) {
                        /* End of feature */
                        infeature = 0;
                        /* If the option setting was replaced, it ends here,
                        too, and the next option is not necessarily also replaced */
                        optionreplaced = 0;
                        dstrclear(linesafterlastbeginfeature);
                    }
                    else if (nestinglevel == 0 && isdscjob && !prologfound &&
                                startswith(line->data, "%%Begin")) {
                        /* In some PostScript files (especially when generated
                        by "dvips" of TeX/LaTeX) the "%%BeginProlog" is
                        missing, so assume that it was before the current
                        line (the first line starting with "%%Begin". */
                        _log("Job claims to be DSC-conforming, but \"%%%%BeginProlog\" "
                            "was missing before first line with another"
                            "\"%%%%BeginProlog\" comment (is this a TeX/LaTeX/dvips-generated"
                            " PostScript file?). Assuming start of \"Prolog\" here.\n");
                        /* Beginning of Prolog */
                        inprolog = 1;
                        nondsclines = 0;
                        /* Insert options for "Prolog" before the current line */
                        dstrcpyf(tmp, "%%%%BeginProlog\n");
                        append_prolog_section(tmp, optset, 0);
                        dstrprepend(line, tmp->data);
                        prologfound = 1;
                    }
                    else if (startswith(ignore_whitespace(line->data), "%") ||
                            startswith(ignore_whitespace(line->data), "$"))
                        /* This is an unknown PostScript comment or a blank
                        line, no active code */
                        ignoreline = 1;
                }
                else {
                    /* This line is active PostScript code */
                    if (infeature)
                        /* Collect coe in a "%%BeginFeature: ... %%EndFeature"
                        section, to get the values for a custom option
                        setting */
                        dstrcat(linesafterlastbeginfeature, line->data);

                    if (inheader) {
                        if (!inprolog && !insetup) {
                            /* Outside the "Prolog" and "Setup" section
                            a correct DSC-conforming document has no
                            active PostScript code, so consider the
                            file as non-DSC-conforming when there are
                            too many of such lines. */
                            nondsclines++;
                            if (nondsclines > MAX_NON_DSC_LINES_IN_HEADER) {
                                /* Consider document as not DSC-conforming */
                                _log("This job seems not to be DSC-conforming, "
                                    "DSC-comment for next section not found, "
                                    "stopping to parse the rest, passing it "
                                    "directly to the renderer.\n");
                                /* Stop scanning for further option settings */
                                maxlines = 1;
                                isdscjob = 0;
                                /* Insert defaults and command line settings in
                                the beginning of the job or after the last valid
                                section */
                                dstrclear(tmp);
                                if (prologfound)
                                    append_prolog_section(tmp, optset, 1);
                                if (setupfound)
                                    append_setup_section(tmp, optset, 1);
                                if (pagesetupfound)
                                    append_page_setup_section(tmp, optset, 1);
                                dstrinsert(psheader, line_start(psheader->data, insertoptions), tmp->data);

                                prologfound = 1;
                                setupfound = 1;
                                pagesetupfound = 1;
                            }
                        }
                    }
                    else if (!inpageheader) {
                        /* PostScript code inside a page, but not between
                        "%%BeginPageSetup" and "%%EndPageSetup", so
                        we are perhaps already drawing onto a page now */
                        if (startswith(onelinebefore->data, "%%Page"))
                            _log("No page header or page header not DSC-conforming\n");
                        /* Stop buffering lines to search for options
                        placed not DSC-conforming */
                        if (line_count(psfifo->data) >= MAX_LINES_FOR_PAGE_OPTIONS) {
                            _log("Stopping search for page header options\n");
                            passthru = 1;
                            /* If there comes a page header now, ignore it */
                            ignorepageheader = 1;
                            optionsalsointoheader = 0;
                        }
                    }
                }
            }

            /* Debug Info */
            if (lastpassthru != passthru) {
                if (passthru)
                    _log("Found: %s --> Output goes directly to the renderer now.\n\n", line->data);
                else
                    _log("Found: %s --> Output goes to the FIFO buffer now.\n\n", line->data);
            }

            /* We are in an option which was replaced, do not output the current line */
            if (optionreplaced)
                dstrclear(line);

            /* If we are in a "%%BeginSetup...%%EndSetup" section after
            the first "%%Page:..." and the current line belongs to
            an option setting, we have to copy the line also to the
            @psheader. */
            if (optionsalsointoheader && (infeature || startswith(line->data, "%%EndFeature")))
                dstrcat(psheader, line->data);

            /* Store or send the current line */
            if (inheader && isdscjob) {
                /* We are still in the PostScript header, collect all lines
                in @psheader */
                dstrcat(psheader, line->data);
            }
            else {
                if (passthru && isdscjob) {
                    if (!lastpassthru) {
                        /* We enter passthru mode with this line, so the
                        command line can have changed, check it and
                        close the renderer if needed */
                        if (rendererpid && !optionset_equal(optionset("currentpage"), optionset("previouspage"), 0)) {
                            _log("Command line/JCL options changed, restarting renderer\n");
                            retval = close_renderer_handle(rendererhandle, rendererpid);
                            if (retval != EXIT_PRINTED) {
                                _log("Error closing renderer\n");
                                exit(retval);
                            }
                            rendererpid = 0;
                        }
                    }

                    /* Flush psfifo and send line directly to the renderer */
                    if (!rendererpid) {
                        /* No renderer running, start it */
                        dstrcpy(tmp, psheader->data);
                        dstrcat(tmp, psfifo->data);
                        get_renderer_handle(tmp, &rendererhandle, &rendererpid);
                        if (retval != EXIT_PRINTED) {
                            _log("Error opening renderer\n");
                            exit(retval);
                        }
                        /* psfifo is sent out, flush it */
                        dstrclear(psfifo);
                    }

                    if (!isempty(psfifo->data)) {
                        /* Send psfifo to renderer */
                        write(rendererhandle, psfifo->data, psfifo->len);
                        /* flush psfifo */
                        dstrclear(psfifo);
                    }

                    /* Send line to renderer */
                    if (!printprevpage) {
                        write(rendererhandle, line->data, line->len);

                        while (fgetdstr(line, stdin) > 0) {
                            if (startswith(line->data, "%%")) {
                                _log("Found: %s", line->data);
                                _log(" --> Continue DSC parsing now.\n\n");
                                saved = 1;
                                break;
                            }
                            else {
                                write(rendererhandle, line->data, line->len);
                                linect++;
                            }
                        }
                    }
                }
                else {
                    /* Push the line onto the stack to split up later */
                    dstrcat(psfifo, line->data);
                }
            }

            if (!printprevpage)
                linect++;
        }
        else {
            /* EOF! */
            more_stuff = 0;

            /* No PostScript header in the whole file? Then it's not
            PostScript, convert it.
            We open the file converter here when the file has less
            lines than the amount which we search for the PostScript
            header ($maxlinestopsstart). */
            if (linect <= nonpslines) {
                /* This is not a PostScript job, we must convert it */
                _log("\nJob does not start with \"%%!\", is it PostScript?\n"
                     "Starting file converter\n");

                /* Reset all variables but conserve the data which
                we already have read */
                jobhasjcl = 0;
                linect = 0;
                maxlines = 1000;
                dstrclear(onelinebefore);
                dstrclear(twolinesbefore);
                dstrclear(line);

                dstrcpy(tmp, psheader->data);
                dstrcat(tmp, psfifo->data);
                dstrclear(psfifo);
                dstrclear(psheader);

                /* Start the file conversion filter */
                if (!fileconverter_pid) {
                    get_fileconverter_handle(tmp->data, &fileconverter_handle, &fileconverter_pid);
                    if (retval != EXIT_PRINTED) {
                        _log("Error opening file converter\n");
                        exit(retval);
                    }
                }
                else {
                    _log("File conversion filter probably crashed\n");
                    exit(EXIT_JOBERR);
                }

                /* Read the further data from the file converter and
                not from STDIN */
                if (close(STDIN_FILENO) != 0) {
                    _log("Couldn't close STDIN\n");
                    exit(EXIT_PRNERR_NORETRY_BAD_SETTINGS);
                }

                if (dup2(fileconverter_handle, STDIN_FILENO) == -1) {
                    _log("Couldn't dup fileconverterhandle\n");
                    exit(EXIT_PRNERR_NORETRY_BAD_SETTINGS);
                }

                /* Now we have new (converted) stuff in STDIN, so
                continue in the loop */
                more_stuff = 1;
            }
        }

        lastpassthru = passthru;

        if (!ignoreline && !printprevpage) {
            dstrcpy(twolinesbefore, onelinebefore->data);
            dstrcpy(onelinebefore, line->data);
        }

    } while ((maxlines == 0 || linect < maxlines) && more_stuff != 0);

    /* Some buffer still containing data? Send it out to the renderer */
    if (more_stuff || inheader || !isempty(psfifo->data)) {
        /* Flush psfifo and send the remaining data to the renderer, this
        only happens with non-DSC-conforming jobs or non-Foomatic PPDs */
        if (more_stuff)
            _log("Stopped parsing the PostScript data, "
                 "sending rest directly to the renderer.\n");
        else
            _log("Flushing FIFO.\n");

        if (inheader) {
            /* No page initialized yet? Copy the "header" option set into the
            "currentpage" option set, so that the renderer will find the
            options settings. */
            optionset_copy_values(optionset("header"), optionset("currentpage"));
            optset = optionset("currentpage");

            /* If not done yet, insert defaults and command line settings
            in the beginning of the job or after the last valid section */
            dstrclear(tmp);
            if (prologfound)
                append_prolog_section(tmp, optset, 1);
            if (setupfound)
                append_setup_section(tmp, optset, 1);
            if (pagesetupfound)
                append_page_setup_section(tmp, optset, 1);
            dstrinsert(psheader, line_start(psheader->data, insertoptions), tmp->data);

            prologfound = 1;
            setupfound = 1;
            pagesetupfound = 1;
        }

        if (rendererpid > 0 && !optionset_equal(optionset("currentpage"), optionset("previouspage"), 0)) {
            _log("Command line/JCL options changed, restarting renderer\n");
            retval = close_renderer_handle(rendererhandle, rendererpid);
            if (retval != EXIT_PRINTED) {
                _log("Error closing renderer\n");
                exit(retval);
            }
            rendererpid = 0;
        }

        if (!rendererpid) {
            dstrcpy(tmp, psheader->data);
            dstrcat(tmp, psfifo->data);
            get_renderer_handle(tmp, &rendererhandle, &rendererpid);
            if (retval != EXIT_PRINTED) {
                _log("Error opening renderer\n");
                exit(retval);
            }
            /* We have sent psfifo now */
            dstrclear(psfifo);
        }

        if (psfifo->len) {
            /* Send psfifo to the renderer */
            write(rendererhandle, psfifo->data, psfifo->len);
            dstrclear(psfifo);
        }

        /* Print the rest of the input data */
        if (more_stuff) {
            while (fgetdstr(tmp, stdin))
                write(rendererhandle, tmp->data, tmp->len);
        }
    }

    /*  At every "%%Page:..." comment we have saved the PostScript state
    and we have increased the page number. So if the page number is
    non-zero we had at least one "%%Page:..." comment and so we have
    to give a restore the PostScript state.
    if ($currentpage > 0) {
        print $rendererhandle "foomatic-saved-state restore\n";
    } */

    /* Close the renderer */
    if (rendererpid) {
        retval = close_renderer_handle(rendererhandle, rendererpid);
        if (retval != EXIT_PRINTED) {
            _log("Error closing renderer\n");
            exit(retval);
        }
        rendererpid = 0;
    }

    /* Close the file converter (if it was used) */
    if (fileconverter_pid) {
        retval = close_fileconverter_handle(fileconverter_handle, fileconverter_pid);
        if (retval != EXIT_PRINTED) {
            _log("Error closing file converter\n");
            exit(retval);
        }
        fileconverter_pid = 0;
    }

    free_dstr(line);
    free_dstr(onelinebefore);
    free_dstr(twolinesbefore);
    free_dstr(psheader);
    free_dstr(psfifo);
    free_dstr(tmp);
}

void write_output(void *data, size_t len)
{
    /* TODO
     * - all output should be written with this function
     * - take postpipe into account
     */
    fwrite(data, len, 1, stdout);
}

enum FileType {
    UNKNOWN_FILE,
    PDF_FILE,
    PS_FILE
};

int guess_file_type(const char *begin, size_t len, int *startpos)
{
    const char * p;

    p = memchr(begin, '%', len);
    if (!p)
        return UNKNOWN_FILE;
    *startpos = p - begin;
    if (!memcmp(p, "%!", 2))
        return PS_FILE;
    else if (!memcmp(p, "%PDF-1.", 7))
        return PDF_FILE;
}

int print_file(const char *filename)
{
    FILE *file;
    char buf[8192];
    int type;
    char tmpfile[PATH_MAX];
    int startpos, res;

    if (!strcasecmp(filename, "<STDIN>"))
        file = stdin;
    else {
        file = fopen(filename, "r");
        if (!file) {
            _log("Could not open \"%s\" for reading\n", filename);
            return 1;
        }
    }

    fread(buf, 1, sizeof(buf), file);
    type = guess_file_type(buf, sizeof(buf), &startpos);
    if (startpos > 0) {
        jobhasjcl = 1;
        write_output(buf, startpos);
    }

    switch (type) {
        case UNKNOWN_FILE:
            break;

        case PDF_FILE:
            res = print_pdf(file, filename, startpos);
            return res;

        case PS_FILE:
            /* If we do not print standard input, open the file to print */
            if (file != stdin) {
                fclose(file); /* make print_ps_file() use the open filehandle */

                freopen(filename, "r", stdin);
                if (!stdin) {
                    _log("Cannot open %s, skipping.\n", filename);
                    return 1;
                }
            }
            print_ps_file();
            break;
    }
    fclose(file);
}

void signal_terminate(int signal)
{
    rip_die(EXIT_PRINTED, "Caught termination signal: Job canceled\n");
}

int main(int argc, char** argv)
{
    int i, j;
    int verbose = 0, quiet = 0, showdocs = 0;
    const char* str;
    char *p, *filename;
    const char *path;
    FILE *genpdqfile = NULL;
    FILE *ppdfh = NULL;
    char tmp[1024], pstoraster[256];
    int havefilter, havepstoraster;
    char user_default_path [256];
    dstr_t *filelist = create_dstr();
    char programdir[256];
    struct passwd *passwd;

    list_t * arglist = list_create_from_array(argc -1, (void**)&argv[1]);

    optstr = create_dstr();
    currentcmd = create_dstr();
    jclprepend = create_dstr();
    jclappend = create_dstr();
    postpipe = create_dstr();

    options_init();

    signal(SIGTERM, signal_terminate);


    /* set the time once to keep output consistent */
    curtime = time(NULL);
    fill_timestrings(&curtime_strings, curtime);


    strlcpy(programdir, argv[0], 256);
    p = strrchr(programdir, '/');
    if (p)
        *++p = '\0';
    else
        programdir[0] = '\0';

    /* spooler specific file converters */
    snprintf(cups_fileconverter, 512, "%stexttops '%s' '%s' '%s' '%s' "
         "page-top=36 page-bottom=36 page-left=36 page-right=36 "
         "nolandscape cpi=12 lpi=7 columns=1 wrap %s'",
         programdir,
         argc > 0 ? argv[1] : "1",
         argc > 1 ? argv[2] : "unknown",
         argc > 2 ? argv[3] : "foomatic-rip",
         argc > 3 ? argv[4] : "1",
         argc > 4 ? argv[5] : "");


    i = 1024;
    cwd = NULL;
    do {
        free(cwd);
        cwd = malloc(i);
    } while (!getcwd(cwd, i));

    gethostname(jobhost, 128);
    passwd = getpwuid(getuid());
    if (passwd)
        strlcpy(jobuser, passwd->pw_name, 128);
    snprintf(jobtitle, 128, "%s@%s", jobuser, jobhost);

    /* Path for personal Foomatic configuration */
    strlcpy(user_default_path, getenv("HOME"), 256);
    strlcat(user_default_path, "/.foomatic/", 256);
    debug = 0;
    strcpy(execpath, "/usr/local/bin:/usr/bin:/bin");
    strcpy(cupsfilterpath, "/usr/local/lib/cups/filter:/usr/local/lib/cups/filter:/usr/local/libexec/cups/filter:/opt/cups/filter:/usr/lib/cups/filter");
    strcpy(fileconverter, "");

    read_config_file(CONFIG_PATH "/filter.conf");

    if (!isempty(execpath))
        setenv("PATH", execpath, 1);

    /* Command line options for verbosity */
    if (arglist_remove_flag(arglist, "-v"))
        verbose = 1;
    if (arglist_remove_flag(arglist, "-q"))
        quiet = 1;
    if (arglist_remove_flag(arglist, "-d"))
        showdocs = 1;
    if (arglist_remove_flag(arglist, "--debug"))
        debug = 1;

    if (debug)
        logh = fopen(LOG_FILE ".log", "r"); /* insecure, use for debugging only */
    else if (quiet && !verbose)
        logh = NULL; /* Quiet mode, do not log */
    else
        logh = stderr; /* Default: log to stderr */

    /* Start debug logging */
    if (debug) {
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
            for (i = 1; i < argc -1; i++)
                _log("\'%s\', ", argv[i]);
            _log("\'%s\'\n", argv[i]);
        }
    }

    if (getenv("PPD")) {
        strncpy_omit(ppdfile, getenv("PPD"), 256, omit_specialchars);
        spooler = SPOOLER_CUPS;
    }

    if (getenv("SPOOLER_KEY")) {
        spooler = SPOOLER_SOLARIS;
        /* set the printer name from the ppd file name */
        strncpy_omit(ppdfile, getenv("PPD"), 256, omit_specialchars);
        file_basename(printer, ppdfile, 256);
        /* TODO read attribute file*/
    }

    if (getenv("PPR_VERSION"))
        spooler = SPOOLER_PPR;

    if (getenv("PPR_RIPOPTS")) {
        /* PPR 1.5 allows the user to specify options for the PPR RIP with the
           "--ripopts" option on the "ppr" command line. They are provided to
           the RIP via the "PPR_RIPOPTS" environment variable. */
        dstrcatf(optstr, "%s ", getenv("PPR_RIPOPTS"));
        spooler = SPOOLER_PPR;
    }

    if (getenv("LPOPTS")) { /* "LPOPTS": Option settings for some LPD implementations (ex: GNUlpr) */
        spooler = SPOOLER_GNULPR;
        dstrcatf(optstr, "%s ", getenv("LPOPTS"));
    }

    /* Check for LPRng first so we do not pick up bogus ppd files by the -ppd option */
    if (arglist_remove_flag(arglist, "--lprng"))
        spooler = SPOOLER_LPRNG;

    /* 'PRINTCAP_ENTRY' environment variable is : LPRng
       the :ppd=/path/to/ppdfile printcap entry should be used */
    if (getenv("PRINTCAP_ENTRY")) {
        spooler = SPOOLER_LPRNG;
        if ((str = strstr(getenv("PRINTCAP_ENTRY"), "ppd=")))
            str += 4;
        else if ((str = strstr(getenv("PRINTCAP_ENTRY"), "ppdfile=")));
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
        while ((str = arglist_get_value(arglist, "-p"))) {
            strncpy_omit(ppdfile, str, 256, omit_shellescapes);
            arglist_remove(arglist, "-p");
        }
    }
    while ((str = arglist_get_value(arglist, "--ppd"))) {
        strncpy_omit(ppdfile, str, 256, omit_shellescapes);
        arglist_remove(arglist, "--ppd");
    }

    /* Check for LPD/GNUlpr by typical options which the spooler puts onto
       the filter's command line (options "-w": text width, "-l": text
       length, "-i": indent, "-x", "-y": graphics size, "-c": raw printing,
       "-n": user name, "-h": host name) */
    if ((str = arglist_get_value(arglist, "-h"))) {
        if (spooler != SPOOLER_GNULPR && spooler != SPOOLER_LPRNG)
            spooler = SPOOLER_LPD;
        strncpy(jobhost, str, 127);
        jobhost[127] = '\0';
        arglist_remove(arglist, "-h");
    }
    if ((str = arglist_get_value(arglist, "-n"))) {
        if (spooler != SPOOLER_GNULPR && spooler != SPOOLER_LPRNG)
            spooler = SPOOLER_LPD;

        strncpy(jobuser, str, 127);
        jobuser[127] = '\0';
        arglist_remove(arglist, "-n");
    }
    if (arglist_remove(arglist, "-w") ||
        arglist_remove(arglist, "-l") ||
        arglist_remove(arglist, "-x") ||
        arglist_remove(arglist, "-y") ||
        arglist_remove(arglist, "-i") ||
        arglist_remove_flag(arglist, "-c")) {
            if (spooler != SPOOLER_GNULPR && spooler != SPOOLER_LPRNG)
                spooler = SPOOLER_LPD;
    }
    /* LPRng delivers the option settings via the "-Z" argument */
    if ((str = arglist_get_value(arglist, "-Z"))) {
        spooler = SPOOLER_LPRNG;
        dstrcatf(optstr, "%s ", str);
        arglist_remove(arglist, "-Z");
    }
    /* Job title and options for stock LPD */
    if ((str = arglist_get_value(arglist, "-j")) || (str = arglist_get_value(arglist, "-J"))) {
        strncpy_omit(jobtitle, str, 128, omit_shellescapes);
        if (spooler == SPOOLER_LPD)
             dstrcatf(optstr, "%s ", jobtitle);
         if (!arglist_remove(arglist, "-j"))
            arglist_remove(arglist, "-J");
    }
    /* Check for CPS */
    if (arglist_remove_flag(arglist, "--cps") > 0)
        spooler = SPOOLER_CPS;

    /* Options for spooler-less printing, CPS, or PDQ */
    while ((str = arglist_get_value(arglist, "-o"))) {
        strncpy_omit(tmp, str, 1024, omit_shellescapes);
        dstrcatf(optstr, "%s ", tmp);
        arglist_remove(arglist, "-o");
        /* If we don't print as PPR RIP or as CPS filter, we print
           without spooler (we check for PDQ later) */
        if (spooler != SPOOLER_PPR && spooler != SPOOLER_CPS)
            spooler = SPOOLER_DIRECT;
    }

    /* Printer for spooler-less printing or PDQ */
    if ((str = arglist_get_value(arglist, "-d"))) {
        strncpy_omit(printer, str, 256, omit_shellescapes);
        arglist_remove(arglist, "-d");
    }

    /* Printer for spooler-less printing, PDQ, or LPRng */
    if ((str = arglist_get_value(arglist, "-P"))) {
        strncpy_omit(printer, argv[i +1], 256, omit_shellescapes);
        arglist_remove(arglist, "-P");
    }

    /* Were we called from a PDQ wrapper? */
    if (arglist_remove_flag(arglist, "--pdq"))
        spooler = SPOOLER_PDQ;

    /* Were we called to build the PDQ driver declaration file? */
    genpdqfile = check_pdq_file(arglist);
    if (genpdqfile)
        spooler = SPOOLER_PDQ;

    /* spooler specific initialization */
    switch (spooler) {
        case SPOOLER_PPR:
            init_ppr(arglist);
            break;

        case SPOOLER_CUPS:
            init_cups(arglist, filelist);
            break;

        case SPOOLER_LPD:
        case SPOOLER_LPRNG:
        case SPOOLER_GNULPR:
            /* Get PPD file name as the last command line argument */
            if (arglist->last)
                strncpy_omit(ppdfile, (char*)arglist->last->data, 256, omit_shellescapes);
            break;

        case SPOOLER_DIRECT:
        case SPOOLER_CPS:
        case SPOOLER_PDQ:
            init_direct_cps_pdq(arglist, filelist, user_default_path);
            break;
    }

    /* Files to be printed (can be more than one for spooler-less printing) */
    /* Empty file list -> print STDIN */
    dstrtrim(filelist);
    if (filelist->len == 0)
        dstrcpyf(filelist, "<STDIN>");

    /* Check filelist */
    p = strtok(filelist->data, " ");
    while (p) {
        if (strcmp(p, "<STDIN>") != 0) {
            if (p[0] == '-') {
                _log("Invalid argument: %s", p);
                exit(EXIT_PRNERR_NORETRY_BAD_SETTINGS);
            }
            else if (access(p, R_OK) != 0) {
                _log("File %s does not exist/is not readable\n", p);
            strclr(p);
            }
        }
        p = strtok(NULL, " ");
    }

    /* When we print without spooler or with CPS do not log onto STDERR unless
       the "-v" ('Verbose') is set or the debug mode is used */
    if ((spooler == SPOOLER_DIRECT || spooler == SPOOLER_CPS || genpdqfile) && !verbose && !debug) {
        if (logh && logh != stderr)
            fclose(logh);
        logh = NULL;
    }

    /* If we are in debug mode, we do this earlier. */
    if (!debug) {
        _log("foomatic-rip version " RIP_VERSION " running...\n");
        /* Print the command line only in debug mode, Mac OS X adds very many
        options so that CUPS cannot handle the output of the command line
        in its log files. If CUPS encounters a line with more than 1024
        characters sent into its log files, it aborts the job with an error. */
        if (spooler != SPOOLER_CUPS) {
            _log("called with arguments: ");
            for (i = 1; i < argc -1; i++)
                _log("\'%s\', ", argv[i]);
            _log("\'%s\'\n", argv[i]);
        }
    }

    /* PPD File */
    /* Load the PPD file and build a data structure for the renderer's
       command line and the options */
    if (!(ppdfh = fopen(ppdfile, "r"))) {
        _log("error opening %s.\n", ppdfile);
        _log("Unable to open PPD file %s\n", ppdfile);
        exit(EXIT_PRNERR_NORETRY_BAD_SETTINGS);
    }

    read_ppd_file(ppdfile);

    /* We do not need to parse the PostScript job when we don't have
       any options. If we have options, we must check whether the
       default settings from the PPD file are valid and correct them
       if nexessary. */
    if (option_count() == 0) {
        /* We don't have any options, so we do not need to parse the
           PostScript data */
        dontparse = 1;
    }

    /* Is our PPD for a CUPS raster driver */
    if (!isempty(cupsfilter)) {
        /* Search the filter in cupsfilterpath
           The %Y is a placeholder for the option settings */
        havefilter = 0;
        path = cupsfilterpath;
        while ((path = strncpy_tochar(tmp, path, 1024, ":"))) {
            strlcat(tmp, "/", 1024);
            strlcat(tmp, cupsfilter, 1024);
            if (access(tmp, X_OK) == 0) {
                havefilter = 1;
                strlcpy(cupsfilter, tmp, 256);
                strlcat(cupsfilter, " 0 '' '' 0 '%Y%X'", 256);
                break;
            }
        }

        if (!havefilter) {
            /* We do not have the required filter, so we assume that
               rendering this job is supposed to be done on a remote
               server. So we do not define a renderer command line and
               embed only the option settings (as we had a PostScript
               printer). This way the settings are taken into account
               when the job is rendered on the server.*/
            _log("CUPS filter for this PPD file not found - assuming that job will "
                 "be rendered on a remote server. Only the PostScript of the options"
                 "will be inserted into the PostScript data stream.\n");
        }
        else {
            /* use pstoraster script if available, otherwise run Ghostscript directly */
            havepstoraster = 0;
            path = cupsfilterpath;
            while ((path = strncpy_tochar(tmp, path, 1024, ":"))) {
                strlcat(tmp, "/pstoraster", 1024);
                if (access(tmp, X_OK) == 0) {
                    havepstoraster = 1;
                    strlcpy(pstoraster, tmp, 256);
                    strlcat(pstoraster, " 0 '' '' 0 '%X'", 256);
                    break;
                }
            }
            if (!havepstoraster) {
                strcpy(pstoraster, "gs -dQUIET -dDEBUG -dPARANOIDSAFER -dNOPAUSE -dBATCH -dNOMEDIAATTRS -sDEVICE=cups -sOutputFile=-%W -");
            }

            /* build GhostScript/CUPS driver command line */
            snprintf(cmd, 1024, "%s | %s", pstoraster, cupsfilter);

            /* Set environment variables */
            setenv("PPD", ppdfile, 1);
        }
    }

    /* Was the RIP command line defined in the PPD file? If not, we assume a PostScript printer
       and do not render/translate the input data */
    if (isempty(cmd)) {
        strcpy(cmd, "cat%A%B%C%D%E%F%G%H%I%J%K%L%M%Z");
        if (dontparse) {
            /* No command line, no options, we have a raw queue, don't check
               whether the input is PostScript and ignore the "docs" option,
               simply pass the input data to the backend.*/
            dontparse = 2;
            strcpy(printer_model, "Raw queue");
        }
    }

    /* Summary for debugging */
    _log("\nParameter Summary\n"
         "-----------------\n\n"
         "Spooler: %s\n"
         "Printer: %s\n"
         "Shell: %s\n"
         "PPD file: %s\n"
         "ATTR file: %s\n"
         "Printer model: %s\n",
        spooler_name(spooler), printer, get_modern_shell(), ppdfile, attrpath, printer_model);
    /* Print the options string only in debug mode, Mac OS X adds very many
       options so that CUPS cannot handle the output of the option string
       in its log files. If CUPS encounters a line with more than 1024 characters
       sent into its log files, it aborts the job with an error.*/
    if (debug || spooler != SPOOLER_CUPS)
        _log("Options: %s\n", optstr->data);
    _log("Job title: %s\n", jobtitle);
    _log("File(s) to be printed:\n");
    _log("%s\n\n", filelist->data);
    if (getenv("GS_LIB"))
        _log("GhostScript extra search path ('GS_LIB'): %s\n", getenv("GS_LIB"));

    /* Process options from command line,
       but save the defaults for printing documentation pages first */
    optionset_copy_values(optionset("default"), optionset("userval"));
    process_cmdline_options();

    /* Were we called to build the PDQ driver declaration file? */
    if (genpdqfile) {
        print_pdq_driver(genpdqfile, optionset("userval"));
        fclose(genpdqfile);
        exit(EXIT_PRINTED);
    }

    if (spooler == SPOOLER_PPR_INT) {
        snprintf(tmp, 1024, "interfaces/%s", backend);
        if (access(tmp, X_OK) != 0) {
            _log("The backend interface %s/interfaces/%s does not exist/"
                 "is not executable!\n", cwd, backend);
            exit(EXIT_PRNERR_NORETRY_BAD_SETTINGS);
        }

        /* foomatic-rip cannot use foomatic-rip as backend */
        if (!strcmp(backend, "foomatic-rip")) {
            _log("\"foomatic-rip\" cannot use itself as backend interface!\n");
            exit(EXIT_PRNERR_NORETRY_BAD_SETTINGS);
        }

        /* Put the backend interface into the postpipe */
        /* TODO
            $postpipe = "| ( interfaces/$backend \"$ppr_printer\" ".
            "\"$ppr_address\" \"" . join(" ",@backendoptions) .
            "\" \"$ppr_jobbreak\" \"$ppr_feedback\" " .
            "\"$ppr_codes\" \"$ppr_jobname\" \"$ppr_routing\" " .
            "\"$ppr_for\" \"\" )";
        */
    }

    /* no postpipe for CUPS or PDQ, even if one is defined in the PPD file */
    if (spooler == SPOOLER_CUPS || spooler == SPOOLER_PDQ)
        dstrclear(postpipe);

    /* CPS always needs a postpipe, set the default one for local printing if none is set */
    if (spooler == SPOOLER_CPS && !postpipe->len)
        dstrcpy(postpipe, "| cat - > $LPDDEV");

    if (postpipe->len)
        _log("Ouput will be redirected to:\n%s\n", postpipe);


    /* Print documentation page when asked for */
    if (do_docs) {
        /* Don't print the supplied files, STDIN will be redirected to the
           documentation page generator */
        dstrcpyf(filelist, "<STDIN>");

        /* Start the documentation page generator */
        /* TODO tbd */
    }

    /* In debug mode save the data supposed to be fed into the
       renderer also into a file, reset the file here */
    if (debug)
        modern_system("> " LOG_FILE ".ps");


    filename = strtok_r(filelist->data, " ", &p);
    while (filename) {
        _log("\n================================================\n\n"
             "File: %s\n\n"
             "================================================\n\n", filename);

        /* Do we have a raw queue? */
        if (dontparse == 2) {
            /* Raw queue, simply pass the input into the postpipe (or to STDOUT
               when there is no postpipe) */
            _log("Raw printing, executing \"cat %s\"\n\n");
            snprintf(tmp, 1024, "cat %s", postpipe->data);
            modern_system(tmp);
            continue;
        }

        /* First, for arguments with a default, stick the default in as
           the initial value for the "header" option set, this option set
           consists of the PPD defaults, the options specified on the
           command line, and the options set in the header part of the
           PostScript file (all before the first page begins). */
        optionset_copy_values(optionset("userval"), optionset("header"));

        print_file(filename);
        filename = strtok_r(NULL, " ", &p);
    }

    /* Close documentation page generator */
    /* if (docgenerator_pid) {
        retval = close_docgenerator_handle(dogenerator_handle, docgenerator_pid);
        if (!retval != EXIT_PRINTED) {
            _log("Error closing documentation page generator\n");
            exit(retval);
        }
        docgenerator_pid = 0;
    } */

    /* Close the last input file */
    fclose(stdin);

    /* TODO dump everything in $dat when debug is turned on (necessary?) */

    _log("\nClosing foomatic-rip.\n");


    /* Cleanup */
    free_dstr(currentcmd);
    if (genpdqfile && genpdqfile != stdout)
        fclose(genpdqfile);
    free_dstr(filelist);
    free_dstr(optstr);
    options_free();
    if (logh && logh != stderr)
        fclose(logh);
    free(cwd);

    free_dstr(jclprepend);
    free_dstr(jclappend);
    if (backendoptions)
        free_dstr(backendoptions);

    list_free(arglist);

    return retval;
}

