
#include "foomaticrip.h"
#include "util.h"
#include "options.h"

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

/* Filters to convert non-PostScript files */
char* fileconverters[] = {
    /* a2ps (converts also other files than text) */
    "a2ps -1 @@--medium=@@PAGESIZE@@ @@--center-title=@@JOBTITLE@@ -o -",
    /* enscript */
    "enscript -G @@-M @@PAGESIZE@@ @@-b \"Page $%|@@JOBTITLE@@ --margins=36:36:36:36 --mark-wrapped-lines=arrow --word-wrap -p-",
    /* mpage */
    "mpage -o -1 @@-b @@PAGESIZE@@ @@-H -h @@JOBTITLE@@ -m36l36b36t36r -f -P- -",
    NULL
};

char cups_fileconverter [512];

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
	"{					% in case of level 2 or higher\n"
	"	currentglobal true setglobal	% define a dictioary foomaticDict\n"
	"	globaldict begin		% in global VM and establish a\n"
	"	/foomaticDict			% pages count key there\n"
	"	<<\n"
	"		/PhysPages 0\n"
	"	>>def\n"
	"	end\n"
	"	setglobal\n"
	"}if\n"
	"\n"
	"/cupsGetNumCopies { % Read the number of Copies requested for the current\n"
	"		    % page\n"
	"    cupsPSLevel2\n"
	"    {\n"
	"	% PS Level 2+: Get number of copies from Page Device dictionary\n"
	"	currentpagedevice /NumCopies get\n"
	"    }\n"
	"    {\n"
	"	% PS Level 1: Number of copies not in Page Device dictionary\n"
	"	null\n"
	"    }\n"
	"    ifelse\n"
	"    % Check whether the number is defined, if it is \"null\" use #copies \n"
	"    % instead\n"
	"    dup null eq {\n"
	"	pop #copies\n"
	"    }\n"
	"    if\n"
	"    % Check whether the number is defined now, if it is still \"null\" use 1\n"
	"    % instead\n"
	"    dup null eq {\n"
	"	pop 1\n"
	"    } if\n"
	"} bind def\n"
	"\n"
	"/cupsWrite { % write a string onto standard error\n"
	"    (%stderr) (w) file\n"
	"    exch writestring\n"
	"} bind def\n"
	"\n"
	"/cupsFlush	% flush standard error to make it sort of unbuffered\n"
	"{\n"
	"	(%stderr)(w)file flushfile\n"
	"}bind def\n"
	"\n"
	"cupsPSLevel2\n"
	"{				% In language level 2, we try to do something reasonable\n"
	"  <<\n"
	"    /EndPage\n"
	"    [					% start the array that becomes the procedure\n"
	"      currentpagedevice/EndPage 2 copy known\n"
	"      {get}					% get the existing EndPage procedure\n"
	"      {pop pop {exch pop 2 ne}bind}ifelse	% there is none, define the default\n"
	"      /exec load				% make sure it will be executed, whatever it is\n"
	"      /dup load					% duplicate the result value\n"
	"      {					% true: a sheet gets printed, do accounting\n"
	"        currentglobal true setglobal		% switch to global VM ...\n"
	"        foomaticDict begin			% ... and access our special dictionary\n"
	"        PhysPages 1 add			% count the sheets printed (including this one)\n"
	"        dup /PhysPages exch def		% and save the value\n"
	"        end					% leave our dict\n"
	"        exch setglobal				% return to previous VM\n"
	"        (PAGE: )cupsWrite 			% assemble and print the accounting string ...\n"
	"        16 string cvs cupsWrite			% ... the sheet count ...\n"
	"        ( )cupsWrite				% ... a space ...\n"
	"        cupsGetNumCopies 			% ... the number of copies ...\n"
	"        16 string cvs cupsWrite			% ...\n"
	"        (\\n)cupsWrite				% ... a newline\n"
	"        cupsFlush\n"
	"      }/if load\n"
	"					% false: current page gets discarded; do nothing	\n"
	"    ]cvx bind				% make the array executable and apply bind\n"
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


char ppdfile[256] = "";
char printer[256] = "";
char printer_model[128] = "";
char jobid[128] = "";
char jobuser[128] = "";
char jobhost[128] = "";
char jobtitle[128] = "";
char copies[128] = "";
char postpipe[1024] = "";  /* command into which the output of this filter should be piped */
int ps_accounting = 1; /* Set to 1 to insert postscript code for page accounting (CUPS only). */
const char *accounting_prolog = NULL;
char attrpath[256] = "";
char modern_shell[256] = "";

int spooler = SPOOLER_DIRECT;
int do_docs = 0;
int dontparse = 0;
int jobhasjcl;

/* Variable for PPR's backend interface name (parallel, tcpip, atalk, ...) */
char backend [64];

/* These variables were in 'dat' before */
char colorprofile [128];
char id[128];
char driver[128];
char cmd[1024];
char cupsfilter[256];

dstr_t *optstr;

char *cwd;
struct config conf;



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
            /* TODO convert options list to a string and insert here */
            /* else if (prefixcmp(psrc, "options;"))
                repl = optstr; */
            else if (!prefixcmp(psrc, "year;"))
                repl = yearstr;
            else if (!prefixcmp(psrc, "month;"))
                repl = monstr;
            else if (!prefixcmp(psrc, "day;"))
                repl = mdaystr;
            else if (!prefixcmp(psrc, "hour;"))
                repl = hourstr;
            else if (!prefixcmp(psrc, "min;"))
                repl = minstr;
            else if (!prefixcmp(psrc, "sec;"))
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
/* TODO '>' not checked??? */
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

    char cupsfilterpath[256];    /* CUPS raster drivers are searched here */

    char fileconverter[512];     /* Command for converting non-postscript files (especially text)
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
        strncpy(conf->cupsfilterpath, value, 255);
        conf->execpath[255] = '\0';
    }
    else if (strcmp(key, "textfilter") == 0) {
        if (strcmp(value, "a2ps") == 0)
            strcpy(conf->fileconverter, fileconverters[0]);
        else if (strcmp(value, "enscript") == 0)
            strcpy(conf->fileconverter, fileconverters[1]);
        else if (strcmp(value, "mpage") == 0)
            strcpy(conf->fileconverter, fileconverters[2]);
        else {
            strncpy(conf->fileconverter, value, 512);
            conf->fileconverter[512] = '\0';
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

void parse_ppd_file(const char* filename)
{
    FILE *fh;
    size_t buf_size = 256; /* PPD line length is max 255 (excl. \0) */
    char *line, *p, *key, *argname, *value;
    option_t *opt, *current_opt = NULL;
    setting_t *setting, *setting2;
    int len;
    size_t idx, value_idx;

    fh = fopen(filename, "r");
    if (!fh) {
        _log("error opening %s\n", filename);
        exit(EXIT_PRNERR_NORETRY_BAD_SETTINGS);
    }
    _log("Parsing PPD file ...\n");

    line = (char*)malloc(buf_size);
    while (!feof(fh)) {

        fgets(line, buf_size, fh);
        if (line[0] != '*' || line[1] == '%') /* line doesn't start with a keyword or is a comment */
            continue;

        /* extract and \0-terminate 'key' string */
        if (!(p = strchr(line, ':')))
            continue;
        *p = '\0';

        /* skip whitespace */
        p += 1;
        while (isspace(*p) && *p)
            p++;

        value_idx = p - line;

        /* read next line while current line ends with '&&' or value is quoted and quotes are not yet closed */
        idx = value_idx;
        while (1) {
            idx += strcspn(&line[idx], "\r\n");
            line[idx] = '\n';
            idx++; /* leave newline */

            if (line[idx -1] == '&' && line[idx -2] == '&')
                idx -= 2;
            else if (line[value_idx] != '\"' || strchr(&line[value_idx +1], '\"'))
                break;

            if (buf_size - idx < 256) { /* PPD line length is 256 */
                buf_size += 256;
                line = realloc(line, buf_size);
            }
            fgets(&line[idx], buf_size - idx, fh);
        }

        key = &line[1];
        value = &line[value_idx];

        /* remove line end and quotes */
        if (*value == '\"') {
            value += 1;
            p = strrchr(value, '\"');
            *p = '\0';            
        }
        else {
            idx = strcspn(value, "\r\n");
            value[idx] = '\0';
        }

        /* process key/value pairs */
        if (strcmp(key, "NickName") == 0) {
            unhtmlify(printer_model, 128, value);
        }
        else if (strcmp(key, "FoomaticIDs") == 0) { /* *FoomaticIDs: <printer ID> <driver ID> */
            p = strtok(value, " \t");
            strlcpy(id, p, 128);
            p = strtok(NULL, " \t");
            strlcpy(driver, p, 128);
        }
        else if (strcmp(key, "FoomaticRIPPostPipe") == 0) {
            unhtmlify(postpipe, 1024, value);
        }
        else if (strcmp(key, "FoomaticRIPCommandLine") == 0) {
            unhtmlify(cmd, 1024, value);
        }
        else if (strcmp(key, "FoomaticNoPageAccounting") == 0) { /* Boolean value */
            if (strcasecmp(value, "true")) {
                /* Driver is not compatible with page accounting according to the
                   Foomatic database, so turn it off for this driver */
                ps_accounting = 0;
                accounting_prolog = NULL;
                _log("CUPS page accounting disabled by driver.\n");
            }
        }
        else if (strcmp(key, "cupsFilter") == 0) { /* cupsFilter: <code> */
            /* only save the filter for "application/vnd.cups-raster" */
            if (prefixcmp(value, "application/vnd.cups-raster") == 0) {
                p = strrchr(value, ' ');
                if (p)
                    unhtmlify(cupsfilter, 256, p +1);
            }
        }
        else if (strcmp(key, "CustomPageSize True") == 0) {
            opt = assure_option("PageSize");
            setting = option_assure_setting(opt, "Custom");
            strlcpy(setting->comment, "Custom Size", 128);
            
            assure_option("PageRegion");
            setting2 = option_assure_setting(opt, "Custom");
            strlcpy(setting->comment, "Custom Size", 128);
            
            if (setting && setting2 && !startswith(value, "%% FoomaticRIPOptionSetting")) {
                strlcpy(setting->driverval, value, 256);
                strlcpy(setting2->driverval, value, 256);
            }
        }
        else if (startswith(key, "OpenUI") || startswith(key, "JCLOpenUI")) {
            /* "*[JCL]OpenUI *<option>[/<translation>]: <type>" */
            if (!(argname = strchr(key, '*')))
                continue;
            argname += 1;
            p = strchr(argname, '/');
            if (p) {
                *p = '\0';
                p += 1;
            }
            current_opt = assure_option(argname);
            if (p)
                strlcpy(current_opt->comment, p, 128);

            /* Set the argument type only if not defined yet,
            a definition in "*FoomaticRIPOption" has priority */
            if (current_opt->type == TYPE_NONE) {
                if (!strcmp(value, "PickOne"))
                    current_opt->type = TYPE_ENUM;
                else if (!strcmp(value, "PickMany"))
                    current_opt->type = TYPE_PICKMANY;
                else if (!strcmp(value, "Boolean"))
                    current_opt->type = TYPE_BOOL;
            }
        }
        else if (!strcmp(key, "CloseUI") || !strcmp(key, "JCLCloseUI")) {
            /* *[JCL]CloseUI: *<option> */
            value = strchr(value, '*');
            if (!value || !current_opt || strcmp(current_opt->name, value +1) != 0)
                _log("CloseUI found without corresponding OpenUI (%s).\n", value +1);
            current_opt = NULL;
        }
        else if (prefixcmp(key, "FoomaticRIPOption ") == 0) {
            /* "*FoomaticRIPOption <option>: <type> <style> <spot> [<order>]"
               <order> only used for 1-choice enum options */
            if (!(argname = strchr(key, ' ')))
                continue;
            argname += 1;
            opt = assure_option(argname);

            p = strtok(value, " \t"); /* type */
            if (strcasecmp(p, "enum") == 0)
                opt->type = TYPE_ENUM;
            else if (strcasecmp(p, "pickmany") == 0)
                opt->type = TYPE_PICKMANY;
            else if (strcasecmp(p, "bool") == 0)
                opt->type = TYPE_BOOL;
            else if (strcasecmp(p, "int") == 0)
                opt->type = TYPE_INT;
            else if (strcasecmp(p, "float") == 0)
                opt->type = TYPE_FLOAT;
            else if (strcasecmp(p, "string") == 0 || strcasecmp(p, "password") == 0)
                opt->type = TYPE_STRING;

            p = strtok(NULL, " \t"); /* style */
            if (strcmp(p, "PS") == 0)
                opt->style = 'G';
            else if (strcmp(p, "CmdLine") == 0)
                opt->style = 'C';
            else if (strcmp(p, "JCL") == 0)
                opt->style = 'J';
            else if (strcmp(p, "Composite") == 0)
                opt->style = 'X';

            p = strtok(NULL, " \t");
            opt->spot = *p;

            /* TODO order - which format? */
        }
        else if (prefixcmp(key, "FoomaticRIPOptionPrototype") == 0) {
            /* "*FoomaticRIPOptionPrototype <option>: <code>"
               Used for numerical and string options only */
            if (!(argname = strchr(key, ' ')))
                continue;
            argname += 1;
            opt = assure_option(argname);

            unhtmlify(opt->proto, 128, value);
        }
        else if (prefixcmp(key, "FoomaticRIPOptionRange") == 0) {
            /* *FoomaticRIPOptionRange <option>: <min> <max>
               Used for numerical options only */
            if (!(argname = strchr(key, ' ')))
                continue;
            argname += 1;
            opt = assure_option(argname);

            p = strtok(value, " \t"); /* min */
            strlcpy(opt->min, p, 32);
            p = strtok(NULL, " \t"); /* max */
            strlcpy(opt->max, p, 32);
        }
        else if (prefixcmp(key, "FoomaticRIPOptionMaxLength") == 0) {
            /*  "*FoomaticRIPOptionMaxLength <option>: <length>"
                Used for string options only */
            if (!(argname = strchr(key, ' ')))
                continue;
            argname += 1;
            opt = assure_option(argname);

            opt->maxlength = atoi(value);
        }
        else if (prefixcmp(key, "FoomaticRIPOptionAllowedChars") == 0) {
            /* *FoomaticRIPOptionAllowedChars <option>: <code>
                Used for string options only */
            if (!(argname = strchr(key, ' ')))
                continue;
            argname += 1;
            opt = assure_option(argname);

            len = strlen(value) +1;
            opt->allowedchars = malloc(len);
            unhtmlify(opt->allowedchars, len, value);
        }
        else if (prefixcmp(key, "FoomaticRIPOptionAllowedRegExp") == 0) {
            /* "*FoomaticRIPOptionAllowedRegExp <option>: <code>"
               Used for string options only */
            if (!(argname = strchr(key, ' ')))
                continue;
            argname += 1;
            opt = assure_option(argname);

            len = strlen(value) +1;
            opt->allowedregexp = malloc(len);
            unhtmlify(opt->allowedregexp, len, value);
        }
        else if (strcmp(key, "OrderDependency") == 0) {
            /* OrderDependency: <order> <section> *<option> */
            if (!(argname = strchr(value, '*')))
                continue;
            argname += 1;
            opt = assure_option(argname);

            p = strtok(value, " \t");
            opt->order = atoi(p);
            p = strtok(NULL, " \t");
            strlcpy(opt->section, p, 128);
        }
        else if (prefixcmp(key, "Default") == 0) {
            /* Default<option>: <value> */
            argname = &key[7];
            opt = assure_option(argname);
			option_set_value(opt, optionset("default"), value);
        }
        else if (prefixcmp(key, "FoomaticRIPDefault") == 0) {
            /* FoomaticRIPDefault<option>: <value>
               Used for numerical options only */
            argname = &key[18];
            opt = assure_option(argname);
			option_set_value(opt, optionset("default"), value);
        }
        else if (current_opt && !prefixcmp(key, current_opt->name)) { /* current argument */
            /* *<option> <choice>[/translation]: <code> */
            opt = current_opt;

            while (*key && isalnum(*key)) key++;
            while (*key && isspace(*key)) key++; /* 'key' now points to the choice string */
            if (!key)
                continue;

            p = strchr(key, '/'); /* translation present? */
            if (p) {
                *p = '\0';
                p += 1; /* points to translation string */
            }

            if (opt->type == TYPE_BOOL) {
                /* make sure that boolean arguments always have exactly two settings: 'true' and 'false' */
                if (strcasecmp(key, "true") == 0)
                    setting = option_assure_setting(opt, "true");
                else 
                    setting = option_assure_setting(opt, "false");
                if (p)
                    strlcpy(setting->comment, p, 128);
            }
            else {
                setting = option_assure_setting(opt, key);
                if (p)
                    strlcpy(setting->comment, p, 128);
                /* Make sure that this argument has a default setting, even if
                   none is defined in the PPD file */
				if (!option_get_value(opt, optionset("default")))
					option_set_value(opt, optionset("default"), key);
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
            opt = assure_option(argname);

            if (p) {
                setting = option_assure_setting(opt, p);
                /* Make sure this argument has a default setting, even if
                   none is defined in this PPD file */
				if (!option_get_value(opt, optionset("default")))
					option_set_value(opt, optionset("default"), p);
                if (p)
                    strlcpy(setting->driverval, value, 256);
            }
            else {
                unhtmlify(opt->proto, 128, value);
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
        *p = '\0';
        p++;
    }
    else {
        *key = p;
        while (*p && *p != ':' && *p != '=' && *p != ' ') p++;
        *p = '\0';
    }

    if (*p != ':' && *p != '=') /* no value for this option */
        return NULL;

    p++; /* skip the separator sign */

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
        *p ='\0';
    }

    return p +1;
}

/* processes optstr */
void process_cmdline_options()
{
    char *p, *pagerange, *key, *value;
    option_t *opt, *opt2;
    setting_t *setting;
    int optset;
    char tmp [256];
    int width, height;
    char unit[2];

    p = extract_next_option(optstr->data, &pagerange, &key, &value);
    while (key) {
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

        if (pagerange) {
            opt = find_option(key);
            if (opt && (strcmp(opt->section, "AnySetup") || strcmp(opt->section, "PageSetup"))) {
                _log("This option (%s) is not a \"PageSetup\" or \"AnySetup\" option, so it cannot be restricted to a page range.\n", key);
                continue;
            }

            snprintf(tmp, 256, "pages:%s", pagerange);
            optset = optionset(tmp);
        }
		else 
			optset = optionset("userval");

        /* Solaris options that have no reason to be */
        if (!strcmp(key, "nobanner") || !strcmp(key, "dest") || !strcmp(key, "protocol"))
            continue;

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
                if ((opt = find_option("PageSize"))) {
                    if ((setting = option_find_setting(opt, p))) {
                        option_set_value(opt, optset, setting->value);

                        /* Keep "PageRegion" in sync */
                        opt = find_option("PageRegion");
                        if (opt && (setting = option_find_setting(opt, p)))
                            option_set_value(opt, optset, setting->value);
                    }
                    else if (startswith(p, "Custom")) {
                        option_set_value(opt, optset, p);
                        
                        /* Keep "PageRegion" in sync */
                        if ((opt = find_option("PageRegion")));
                            option_set_value(opt, optset, p);
                    }
                }
                else if ((opt = find_option("MediaType")) && (setting = option_find_setting(opt, p)))
                    option_set_value(opt, optset, setting->value);
                else if ((opt = find_option("InputSlot")) && (setting = option_find_setting(opt, p)))
                    option_set_value(opt, optset, setting->value);
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
                    /* We set "Duplex" to '1' here, the real argument setting will be done later */
                    option_set_value(opt, optset, "1");

                    /* Check the binding: "long edge" or "short edge" */
                    if (strcasestr(value, "long-edge")) {
                        if ((opt2 = find_option("Binding")) && (setting = option_find_setting(opt2, "LongEdge")))
                            option_set_value(opt2, optset, setting->value);
                        else
                            option_set_value(opt2, optset, "LongEdge");
                    }
                    else if (strcasestr(value, "short-edge")) {
                        if ((opt2 = find_option("Binding")) && (setting = option_find_setting(opt2, "ShortEdge")))
                            option_set_value(opt2, optset, setting->value);
                        else
                            option_set_value(opt2, optset, "ShortEdge");
                    }
                }
            }
            else if (!prefixcasecmp(value, "one-sided")) {
                if ((opt = find_option("Duplex")))
                    /* We set "Duplex" to '0' here, the real argument setting will be done later */
                    option_set_value(opt, optset, "0");
            }

            /*
                We should handle the other half of this option - the
                BindEdge bit.  Also, are there well-known ipp/cups
                options for Collate and StapleLocation?  These may be
                here...
            */
        }
        else {
            /* Various non-standard printer-specific options */
            if ((opt = find_option(key))) {
                /* use the choice if it is valid, otherwise ignore it */
                if (option_set_validated_value(opt, optset, value, 0))
                    sync_pagesize(opt, p, optset);
                else
                    _log("Invalid choice %s=%s.\n", opt->name, value);
            }
            else if (spooler == SPOOLER_PPR_INT) {
                /* Unknown option, pass it to PPR's backend interface */
                /* TODO */
            }
            else
                _log("Unknown option %s=%s.\n", key, value);
        }

        /* Custom paper size */
        if (sscanf(key, "%dx%d%2c", &width, &height, unit) == 3 &&
            width != 0 && height != 0 &&
            (opt = find_option("PageSize")) &&
            (setting = option_find_setting(opt, "Custom")))
        {
            snprintf(tmp, 256, "Custom.%s", tmp);
            option_set_value(opt, optset, tmp);
            /* Keep "PageRegion" in sync */
            if ((opt2 = find_option("PageRegion")))
                option_set_value(opt2, optset, tmp);
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
        p = extract_next_option(p, &pagerange, &key, &value);
    }
 
}

/* Checks whether an argument named 'name' exists and returns its index or 0 if it doesn't exist */
int find_arg(const char *name, int argc, char **argv)
{		
	while (--argc) {
		if (!strcmp(name, argv[argc]))
			return argc;
	}
	return 0;
}

int find_arg_prefix(const char *name, int argc, char **argv)
{		
	while (--argc) {
        if (!prefixcmp(argv[argc], name))
			return argc;
	}
	return 0;
}

int remove_arg(const char *name, int argc, char **argv)
{
	while (--argc) {
		if (!strcmp(name, argv[argc])) {
			argv[argc][0] = '\0';
			return 1;
		}
	}
	return 0;
}

/* Searches for an option in args and returns its value
   The value may be seperated from the key:
      - with whitespace (i.e. it is in the next argv entry)
      - with a '='
      - not at all
*/
const char * get_option_value(const char *name, int argc, char **argv)
{
	int i = argc;
	const char *p;
	while (--i) {
		if (isempty(argv[i]))
			continue;
		if (i + 1 < argc && !strcmp(name, argv[i]))
			return argv[i +1];
		else if (!prefixcmp(name, argv[i])) {
			p = &argv[i][strlen(name)];
			return *p == '=' ? p +1 : p;
		}
	}
	return NULL;	
}

int remove_option(const char *name, int argc, char **argv)
{
	int i = argc;	
	while (--i) {
        if (argv[i][0] == '\0')
            continue;
		if (i + 1 < argc && !strcmp(name, argv[i])) {
			argv[i][0] = '\0';
			argv[i +1][0] = '\0';
			return 1;
		}
		else if (!prefixcmp(name, argv[i])) {
			argv[i][0] = '\0';
			return 1;
		}
	}
	return 0;
}

/* checks whether a pdq driver declaration file should be build
   and returns an opened file handle if so */
FILE * check_pdq_file(int argc, char **argv)
{
    /* "--appendpdq=<file>" appends the data to the <file>,
       "--genpdq=<file>" creates/overwrites <file> for the data, and
       "--genpdq" writes to standard output */

	int i;
    char filename[256];
    FILE *handle;
    char *p;
    int raw, append;
	
	if ((i = find_arg_prefix("--genpdq", argc, argv))) {
		raw = 0;
		append = 0;
	}
    else if ((i = find_arg_prefix("--genrawpdq", argc, argv))) {
        raw = 1;
        append = 0;
    }
	else if ((i = find_arg_prefix("--appendpdq", argc, argv))) {
        raw = 0;
        append = 1;
    }
	else if ((i = find_arg_prefix("--appendrawpdq", argc, argv))) {
        raw = 1;
        append = 1;
    }

    if (!i)
        return NULL;

    p = strchr(argv[i], '=');
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
	argv[i][0] = '\0';	

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
                "}", (unsigned int)time(NULL));
        if (handle != stdout) {
            fclose(handle);
            handle = NULL;
        }
        exit(EXIT_PRINTED);
    }

    return handle;
}

void init_ppr(int rargc, char **rargv)
{
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
	if ((rargc == 11 || rargc == 10 || rargc == 8) && atoi(rargv[3]) < 100 && atoi(rargv[5]) < 100)
    {
        /* get all command line parameters */
        strncpy_omit(ppr_printer, rargv[0], 256, omit_shellescapes);
		strlcpy(ppr_address, rargv[1], 128);        
		strncpy_omit(ppr_options, rargv[2], 1024, omit_shellescapes);
		strlcpy(ppr_jobbreak, rargv[3], 128);
		strlcpy(ppr_feedback, rargv[4], 128);
		strlcpy(ppr_codes, rargv[5], 128);
		strncpy_omit(ppr_jobname, rargv[6], 128, omit_shellescapes);
		strncpy_omit(ppr_routing, rargv[7], 128, omit_shellescapes);
        if (rargc >= 8) {
			strlcpy(ppr_for, rargv[8], 128);
			strlcpy(ppr_filetype, rargv[9], 128);
			if (rargc >= 10)
				strncpy_omit(ppr_filetoprint, rargv[10], 128, omit_shellescapes);
        }

        /* Common job parameters */
        strcpy(printer, ppr_printer);
        strcpy(jobtitle, ppr_jobname);
        if (isempty(jobtitle) && ppr_filetoprint)
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

void init_cups(int rargc, char **rargv, dstr_t *filelist)
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
	
	/* Get all command line parameters */
	strncpy_omit(cups_jobid, rargv[0], 128, omit_shellescapes);
	strncpy_omit(cups_user, rargv[1], 128, omit_shellescapes);
	strncpy_omit(cups_jobtitle, rargv[2], 128, omit_shellescapes);
	strncpy_omit(cups_copies, rargv[3], 128, omit_shellescapes);
	strncpy_omit(cups_options, rargv[4], 512, omit_shellescapes);
	
	/* Common job parameters */
	/* TODO why is this copied into the cups_* vars in the first place? */
	strcpy(jobid, cups_jobid);
	strcpy(jobtitle, cups_jobtitle);
	strcpy(jobuser, cups_user);
	strcpy(copies, cups_copies);
    dstrcatf(optstr, " %s", cups_options);

	/* Check for and handle inputfile vs stdin */
	if (rargc > 4) {
		strncpy_omit(cups_filename, rargv[5], 256, omit_shellescapes);
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

void init_solaris(int rargc, char **rargv, dstr_t *filelist)
{
	char *str;
	int len, i;
	
	assert(rargc >= 5);
	
	/* Get all command line parameters */
	strncpy_omit(jobtitle, rargv[2], 128, omit_shellescapes);
	
	len = strlen(rargv[4]);
	str = malloc(len +1);
	strncpy_omit(str, rargv[4], len, omit_shellescapes);
    dstrcatf(optstr, " %s", str);
	free(str);
	
	for (i = 5; i < rargc; i++)
        dstrcatf(filelist, "%s ", rargv[i]);
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
   result into the global var 'printer'. Returns success */
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

void init_direct_cps_pdq(int rargc, char **rargv, dstr_t *filelist, const char *user_default_path)
{
	char tmp [1024];
    int i;
		
	/* Which files do we want to print? */
	for (i = 0; i < rargc; i++) {
		strncpy_omit(tmp, rargv[i], 1024, omit_shellescapes);
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
    option_t *opt;
    value_t *val;
    setting_t *setting, *setting_true, *setting_false;
	
	/* Construct option list */
	dstr_t *driveropts = create_dstr();

	/* Do we have a "Custom" setting for the page size?
	   The we have to insert the following into the filter_exec script. */
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

			/* Assure that the comment is not emtpy */
			if (isempty(opt->comment))
				strcpy(opt->comment, opt->name);
			
			strcpy(opt->varname, opt->name);
			strrepl(opt->varname, "-/.", '_');
			
            /* 1, if setting "PageSize=Custom" was found
               Then we must add options for page width and height */
            custompagesize = 0;
			
            if ((val = option_get_value(opt, optset)))
                strlcpy(def, val->value, 128);

			/* If the default is a custom size we have to set also
			   defaults for the width, height, and units of the page */
			if (!strcmp(opt->name, "PageSize") &&  sscanf(def, "Custom.%dx%d%2c", &pagewidth, &pageheight, pageunit))
				strcpy(def, "Custom");

			dstrcatf(driveropts, 
					"  option {\n"
					"    var = \"%s\"\n"
					"    desc = \"%s\"\n", opt->varname, opt->comment);
			
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
		tmp->data, /* cleaned printer_model */ (unsigned int)time(NULL), ppdfile, printer_model,
	    driveropts->data, setcustompagesize->data, psfilter->data);
		
	
	free_dstr(setcustompagesize);
	free_dstr(driveropts);
	free_dstr(tmp);
	free_dstr(cmdline);
	free_dstr(psfilter);
}

/* returns status - see wait(2) */
int modern_system(const char *cmd)
{
    pid_t pid;
    int status;
    
    if (!isempty(modern_shell) && strcmp(modern_shell, "/bin/sh")) {
        /* a "modern" shell other than the default shell was specified */

        pid = fork();
        if (pid < 0) {
            _log("Failed to fork()\n");
            exit(errno);
        }

        if (pid == 0) { /* child, execute commands under a modern shell */
            execl(modern_shell, "-c", cmd, (char*)NULL);
            _log("exec(%s, \"-c\", %s) failed", modern_shell, cmd);
            exit(errno);
        }
        else { /* parent, wait for the child */
            waitpid(pid, &status, 0);
            return status;
        }
    }
    else /* the system shell is modern enough */
        return system(cmd);        
}

/*  This function is only used when the input data is not
    PostScript. Then it runs a filter which converts non-PostScript
    files into PostScript. The user can choose which filter he wants
    to use. The filter command line is provided by 'fileconverter'.*/
void get_fileconverter_handle(const char *already_read, int *fd, pid_t *pid)
{
    int i, status, retval = EXIT_PRINTED;
    char tmp[1024];
    char cmd[32];
    const char *p, *p2, *lastp;
    option_t *opt;
    value_t *val;
    ssize_t count;
    
    pid_t kid1, kid2;
    int pfd_kid_message_conv[2];
    int pfd_kid1[2];
    int pfd_kid2[2];



    _log("Starting converter for non-PostScript files\n");

    /* Determine with which command non-PostScript files are converted */
    if (isempty(conf.fileconverter)) {
        if (spooler == SPOOLER_CUPS)
            strcpy(conf.fileconverter, cups_fileconverter);
        else {
            for (i = 0; fileconverters[i]; i++) {
                strncpy_tochar(cmd, fileconverters[i], 32, " \t");

                if (access(cmd, X_OK) == 0) {
                    strlcpy(conf.fileconverter, fileconverters[i], 512);
                    break;
                }
                else {
                    p = getenv("PATH");
                    while (p) {
                        strncpy_tochar(tmp, p, 256, ":");
                        strlcat(tmp, "/", 256);
                        strlcat(tmp, cmd, 256);
                        if (access(tmp, X_OK)) {
                            strlcpy(conf.fileconverter, fileconverters[i], 512);
                            break;
                        }
                        p = strchr(p, ':');
                        if (p) p++;
                    }
                }
                if (!isempty(conf.fileconverter))
                    break;
            }
        }
    }

    if (isempty(conf.fileconverter))
        strcpy(conf.fileconverter, "echo \"Cannot convert file to PostScript!\" 1>&2");

    strlcpy(tmp, conf.fileconverter, 512);
    strclr(conf.fileconverter);
    lastp = tmp;
    while ((p = strstr(lastp, "@@"))) {

        strncat(conf.fileconverter, lastp, p - lastp);

        p += 2;
        p2 = strstr(p, "@@");
        if (!p2)
            break;

        /* Insert the page size into the fileconverter */
        if (startswith(p2, "@@PAGESIZE@@")) {
            /* We always use the "header" option here, with a
               non-PostScript file we have no "currentpage" */
            if ((opt = find_option("PageSize")) && (val = option_get_value(opt, optionset("header")))) {

                strlcat(conf.fileconverter, p, p2 - p);
                /* Use wider margins so that the pages come out completely on
                every printer model (especially HP inkjets) */
                if (startswith(conf.fileconverter, "a2ps")) {
                    if (!strcasecmp(val->value, "letter"))
                        strlcat(conf.fileconverter, "Letterdj", 512);
                    else if (!strcasecmp(val->value, "a4"))
                        strlcat(conf.fileconverter, "A4dj", 512);
                }
                else
                    strlcat(conf.fileconverter, val->value, 512);
            }

            /* advance p to after the @@PAGESIZE@@ */
            p = p2 + 13;
        }

        /* Insert job title into the fileconverter */
        else if (startswith(p2, "@@JOBTITLE@@")) {
            if (do_docs)
                snprintf(jobtitle, 128, "Documentation for the %s", printer_model);

            if (strnchr(p, '\"', p2 - p) || !isempty(jobtitle)) {
                strncat(conf.fileconverter, p, p2 - p);
                if (!strnchr(p, '\"', p2 - p))
                    strlcat(conf.fileconverter, "\"", 512);
                if (!isempty(jobtitle)) 
                    escapechars(&conf.fileconverter[strlen(conf.fileconverter)], 512, jobtitle, "\"");
                strlcat(conf.fileconverter, "\"", 512);
            }
            /* advance p to after the @@JOBTITLE@@ */
            p = p2 + 13;
        }
        lastp = p;
    }
    strlcat(conf.fileconverter, lastp, 512);

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
            if (conf.debug || spooler != SPOOLER_CUPS)
                _log("file converter command: %s\n", conf.fileconverter);

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

            if (conf.debug) {
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
            status = modern_system(conf.fileconverter);
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

void print_file()
{
    /* Maximum number of lines to be read  when the documenent is not  DSC-conforming.
       "$maxlines = 0"  means that all will be read  and examined. If it is  discovered
        that the input file  is DSC-conforming, this will  be set to 0. */
    int maxlines = 1000;

    /* We set this when encountering "%%Page:" and the previous page is not printed yet.
       Then it will be printed and the new page will be prepared in the next run of the
       loop (we don't read a new line and don't increase the $linect then). */
    int printprevpage = 0;

    /* how many lines have we examined */
    int linect = 0;

    /* lines before "%!" found yet. */
    int nonpslines = 0;

    /* DSC line not precessed yet */
    int saved = 0;

    /* current line */
    dstr_t *line = create_dstr();

    int ignoreline;



    jobhasjcl = 0;

    /* We do not parse the PostScript to find Foomatic options, we check
        only whether we have PostScript. */
    if (dontparse)
        maxlines = 1;

    _log("Reading PostScript input ...\n");

#if 0
    do {
        ignoreline = 0;

        if (printprevpage || saved || fgetdstr(line, stdin)) {
            saved = 0;
            if (linect == nonpslines) {
                /* In the beginning should be the postscript leader,
                   sometimes after some JCL commands */
                if ( (line->data[0] == '%' && line->data[1] == '!') ||
                      (line->data[1] == '%' && line->data[2] == '!')) /* There can be a Windows control character before "%!" */
                {
                    nonpslines++;
                    if (maxlines == nonpslines)
                        maxlines ++;

                    /* Reset all variables but conserve the data which we have already read */
                    jobhasjcl = 1;
                    linect = 0;
                    nonpslines = 1; /* Take into account that the line of this run of the loop
                                       will be put into @psheader, so the first line read by
                                       the file converter is already the second line */
                    maxlines = 1001;

                    /* TODO if needed */
                    /* onelinebefore = ;
                    twolinesbefore = ; */

                    
                }
            }
        }
    }
#endif

    free_dstr(line);
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
    FILE *fh;
    int rargc;
    char **rargv = NULL;
    char tmp[1024], pstoraster[256];
	int havefilter, havepstoraster;
    char user_default_path [256];
    dstr_t *filelist = create_dstr();
    char programdir[256];

    optstr = create_dstr();

    strlcpy(programdir, argv[0], 256);
    p = strrchr(programdir, '/');
    if (p)
        *++p = '\0';
    else
        programdir[0] = '\0';

    /* spooler specific file converters */
    snprintf(cups_fileconverter, 512, "%stexttops '%s' '%s' '%s' '%s' '%s' "
            "page-top=36 page-bottom=36 page-left=36 page-right=36 "
            "nolandscape cpi=12 lpi=7 columns=1 wrap'",
            programdir,
            argc > 0 ? argv[1] : "",
            argc > 1 ? argv[2] : "",
            argc > 2 ? argv[3] : "",
            argc > 3 ? argv[4] : "",
            argc > 4 ? argv[5] : "");

    i = 1024;
    cwd = NULL;
    do {
        free(cwd);
        cwd = malloc(i);
    } while (!getcwd(cwd, i));

    /* Path for personal Foomatic configuration */
    strlcpy(user_default_path, getenv("HOME"), 256);
    strlcat(user_default_path, "/.foomatic/", 256);

    config_set_default_options(&conf);
    config_read_from_file(&conf, CONFIG_PATH "/filter.conf");
    if (!isempty(conf.execpath))
        setenv("PATH", conf.execpath, 1);

    /* Command line options for verbosity */
    if (remove_arg("-v", argc, argv))
        verbose = 1;
    if (remove_arg("-q", argc, argv))
        quiet = 1;
    if (remove_arg("-d", argc, argv))
        showdocs = 1;
    if (remove_arg("--debug", argc, argv))
        conf.debug = 1;

    if (conf.debug)
        logh = fopen(LOG_FILE ".log", "r"); /* insecure, use for debugging only */
    else if (quiet && !verbose)
        logh = NULL; /* Quiet mode, do not log */
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
	if (remove_arg("--lprng", argc, argv))
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
		while ((str = get_option_value("-p", argc, argv))) {
			strncpy_omit(ppdfile, str, 256, omit_shellescapes);
			remove_option("-p", argc, argv);
		}
    }
	while ((str = get_option_value("--ppd", argc, argv))) {
		strncpy_omit(ppdfile, str, 256, omit_shellescapes);
		remove_option("--ppd", argc, argv);
	}

    /* Check for LPD/GNUlpr by typical options which the spooler puts onto
       the filter's command line (options "-w": text width, "-l": text
       length, "-i": indent, "-x", "-y": graphics size, "-c": raw printing,
       "-n": user name, "-h": host name) */
    if ((str = get_option_value("-h", argc, argv))) {
        if (spooler != SPOOLER_GNULPR && spooler != SPOOLER_LPRNG)
            spooler = SPOOLER_LPD;
        strncpy(jobhost, str, 127);
        jobhost[127] = '\0';
		remove_option("-h", argc, argv);
    }
	if ((str = get_option_value("-n", argc, argv))) {
        if (spooler != SPOOLER_GNULPR && spooler != SPOOLER_LPRNG)
            spooler = SPOOLER_LPD;
        
        strncpy(jobuser, str, 127);
        jobuser[127] = '\0';
		remove_option("-n", argc, argv);
    }   
    if (remove_option("-w", argc, argv) ||
		remove_option("-l", argc, argv) ||
		remove_option("-x", argc, argv) ||
		remove_option("-y", argc, argv) ||
		remove_option("-i", argc, argv) ||
        remove_arg("-c", argc, argv)) {
            if (spooler != SPOOLER_GNULPR && spooler != SPOOLER_LPRNG)
                spooler = SPOOLER_LPD;
    }
    /* LPRng delivers the option settings via the "-Z" argument */
	if ((str = get_option_value("-Z", argc, argv))) {
        spooler = SPOOLER_LPRNG;
        dstrcatf(optstr, "%s ", str);
		remove_option("-Z", argc, argv);
	}
    /* Job title and options for stock LPD */
	if ((str = get_option_value("-j", argc, argv)) || (str = get_option_value("-J", argc, argv))) {
        strncpy_omit(jobtitle, str, 128, omit_shellescapes);
        if (spooler == SPOOLER_LPD)
             dstrcatf(optstr, "%s ", jobtitle);
 		if (!remove_option("-j", argc, argv)) remove_option("-J", argc, argv);
    }
    /* Check for CPS */
    if (remove_arg("--cps", argc, argv) > 0)
        spooler = SPOOLER_CPS;

    /* Options for spooler-less printing, CPS, or PDQ */
    while ((str = get_option_value("-o", argc, argv))) {
        dstrcatf(optstr, "%s ", str);
		remove_option("-o", argc, argv);
        /* If we don't print as PPR RIP or as CPS filter, we print
           without spooler (we check for PDQ later) */
        if (spooler != SPOOLER_PPR && spooler != SPOOLER_CPS)
            spooler = SPOOLER_DIRECT;
    }

    /* Printer for spooler-less printing or PDQ */
    if ((str = get_option_value("-d", argc, argv))) {
        strncpy_omit(printer, str, 256, omit_shellescapes);
		remove_option("-d", argc, argv);
    }

    /* Printer for spooler-less printing, PDQ, or LPRng */
    if ((str = get_option_value("-P", argc, argv))) {
        strncpy_omit(printer, argv[i +1], 256, omit_shellescapes);
		remove_option("-P", argc, argv);
    }

    /* Were we called from a PDQ wrapper? */
    if (remove_arg("--pdq", argc, argv))
        spooler = SPOOLER_PDQ;

    /* Were we called to build the PDQ driver declaration file? */
    genpdqfile = check_pdq_file(argc, argv);
    if (genpdqfile)
        spooler = SPOOLER_PDQ;
		
	/* collect remaining arguments */
	rargc = 0;
	for (i = 1; i < argc; i++) {
		if (!isempty(argv[i]))
			rargc++;
	}
	rargv = malloc(sizeof(char *) * rargc);
	for (i = 1, j = 0; i < argc; i++) {
		if (!isempty(argv[i]))
			rargv[j++] = argv[i];
	}
	
	/* spooler specific initialization */
	switch (spooler) {
		case SPOOLER_PPR:
        	init_ppr(rargc, rargv);
			break;
			
		case SPOOLER_CUPS:
			init_cups(rargc, rargv, filelist);
			break;
			
		case SPOOLER_LPD:
		case SPOOLER_LPRNG:
		case SPOOLER_GNULPR:
			/* Get PPD file name as the last command line argument */
			strncpy_omit(ppdfile, rargv[rargc -1], 256, omit_shellescapes);
			break;
			
		case SPOOLER_DIRECT:
		case SPOOLER_CPS:
		case SPOOLER_PDQ:
			init_direct_cps_pdq(rargc, rargv, filelist, user_default_path);
			break;
	}
	
	/* Files to be printed (can be more than one for spooler-less printing) */	
	/* Empty file list -> print STDIN */
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
	if ((spooler == SPOOLER_DIRECT || spooler == SPOOLER_CPS || genpdqfile) && !verbose && !conf.debug) {
		if (logh && logh != stderr)
			fclose(logh);
		logh = NULL;
	}
    
    /* If we are in debug mode, we do this earlier. */
    if (!conf.debug) {
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
	
    parse_ppd_file(ppdfile);
	
	/* We do not need to parse the PostScript job when we don't have
	   any options. If we have options, we must check whether the
	   default settings from the PPD file are valid and correct them
	   if nexessary. */
	if (option_count() == 0) {
		/* We don't have any options, so we do not need to parse the 
		   PostScript data */
		dontparse = 1;
	}
	else {
        check_options(optionset("default"));
		 /* TODO check value ranges */
	}
		
	/* Is our PPD for a CUPS raster driver */
	if (!isempty(cupsfilter)) {
		/* Search the filter in cupsfilterpath
		   The %Y is a placeholder for the option settings */
		havefilter = 0;
        path = conf.cupsfilterpath;
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
            path = conf.cupsfilterpath;
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
	_log("Parameter Summary\n"
		 "-----------------\n"
		 "Spooler: %s\n"
		 "Printer: %s\n"
		 /* "Shell: %s\n" */
		 "PPD file: %s\n"
		 "ATTR file: %s\n"
		 "Printer Model: %s\n",
		spooler_name(spooler), printer /*, modern_shell */, ppdfile, attrpath, printer_model);
	/* Print the options string only in debug mode, Mac OS X adds very many
	   options so that CUPS cannot handle the output of the option string
	   in its log files. If CUPS encounters a line with more than 1024 characters
	   sent into its log files, it aborts the job with an error.*/
	if (conf.debug || spooler != SPOOLER_CUPS)
        _log("Options: %s\n", optstr->data);
	_log("Job title: %s\n", jobtitle);
	_log("File(s) to be printed:\n");
    _log("%s\n", filelist);
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

    /* TODO can this be moved to the other initialisation */
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
        postpipe[0] = '\0';

    /* CPS always needs a postpipe, set the default one for local printing if none is set */
    if (spooler == SPOOLER_CPS && isempty(postpipe))
        strcpy(postpipe, "| cat - > $LPDDEV");

    if (!isempty(postpipe))
        _log("Ouput will be redirected to:\n%s\n", postpipe);

    
    /* Print documentation page when asked for */
    if (do_docs) {
        /* Don't print the supplied files, STDIN will be redirected to the
           documentation page generator */
        dstrcpyf(filelist, "<STDIN>");

        /* Start the documentation page generator */
        /* TODO */
    }

    /* In debug mode save the data supposed to be fed into the
       renderer also into a file, reset the file here */
    if (conf.debug)
        modern_system("> " LOG_FILE ".ps");

    
    while ((filename = strtok(filelist->data, " "))) {
        _log("================================================\n"
             "File: %s\n"
             "================================================\n", filename);

        /* If we do not print standard input, open the file to print */
        if (strcmp(filename, "<STDIN>")) {
            if (access(filename, R_OK) != 0) {
                _log("File %s missing or not readable, skipping.\n");
                continue;
            }

            fh = fopen(filename, "r");
            if (!fh) {
                _log("Cannot open %s, skipping.\n", filename);
                continue;
            }
        }
        else
            fh = stdin;

        /* Do we have a raw queue? */
        if (dontparse == 2) {
            /* Raw queue, simply pass the input into the postpipe (or to STDOUT
               when there is no postpipe) */
            _log("Raw printing, executing \"cat %s\"\n");
            snprintf(tmp, 1024, "cat %s", postpipe);
            modern_system(tmp);
            continue;
        }

        /* First, for arguments with a default, stick the default in as
           the initial value for the "header" option set, this option set
           consists of the PPD defaults, the options specified on the
           command line, and the options set in the header part of the
           PostScript file (all before the first page begins). */
        optionset_copy_values(optionset("userval"), optionset("header"));

 
        
        

        if (fh != stdin)
            fclose(fh);
    }    


	/* Cleanup */
    if (genpdqfile && genpdqfile != stdout)
        fclose(genpdqfile);
	free(rargv);
	free_dstr(filelist);
    free_dstr(optstr);
    free_optionlist();
	if (logh && logh != stderr)
		fclose(logh);
    free(cwd);
    return 0;
}
