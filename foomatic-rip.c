
/*
 * TODO
 *
 * - strcasecmp conforms to POSIX.1-2001 - do we have that on all platforms?
 *      the same goes for regex.h (needed for *FoomaticRIPOptionAllowedRegExp)
 * - strcasecmp is a GNU extension, write a replacement for non-gnu systems
 * - are POSIX regexps compatible to perls? (for *FoomaticRIPOptionAllowedRegExp)
 *
 */

/* TODO write replacements for strcasecmp etc. for non-gnu platforms */
#define _GNU_SOURCE

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
#include <regex.h>
#include <unistd.h>


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
#define NUM_FILE_CONVERTERS 3
char* fileconverters[] = {
    /* a2ps (converts also other files than text) */
    "a2ps -1 @@--medium=@@PAGESIZE@@ @@--center-title=@@JOBTITLE@@ -o -",
    /* enscript */
    "enscript -G @@-M @@PAGESIZE@@ @@-b \"Page $%|@@JOBTITLE@@ --margins=36:36:36:36 --mark-wrapped-lines=arrow --word-wrap -p-",
    /* mpage */
    "mpage -o -1 @@-b @@PAGESIZE@@ @@-H -h @@JOBTITLE@@ -m36l36b36t36r -f -P- -"
};


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


char ppdfile[256] = "";
char printer[256] = "";
char printer_model[128] = "";
char jobid[128] = "";
char jobuser[128] = "";
char jobhost[128] = "";
char jobtitle[128] = "";
char copies[128] = "";
char optstr[1024] = ""; 
char postpipe[1024] = "";  /* command into which the output of this filter should be piped */
int ps_accounting = 1; /* Set to 1 to insert postscript code for page accounting (CUPS only). */
const char *accounting_prolog = NULL;
char attrpath[256] = "";

/* TODO clean up global vars */
int spooler = SPOOLER_DIRECT;
int do_docs = 0;


int prefixcmp(const char *str, const char *prefix)
{
    return strncmp(str, prefix, strlen(prefix));
}

int prefixcasecmp(const char *str, const char *prefix)
{
    return strncasecmp(str, prefix, strlen(prefix));
}

void strlower(char *dest, size_t destlen, const char *src)
{
    char *pdest = dest;
    const char *psrc = src;
    while (*psrc && --destlen > 0)
    {
        *pdest = tolower(*psrc);
        pdest++;
        psrc++;
    }
    *pdest = '\0';
}

int isempty(const char *string)
{
    return string && string[0] == '\0';
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

/* TODO check for platforms which already have strlcpy and strlcat */

/* Copy at most size-1 characters from src to dest
   dest will always be \0 terminated (unless size == 0) 
   returns strlen(src) */
size_t strlcpy(char *dest, const char *src, size_t size)
{
	char *pdest = dest;
	const char *psrc = src;
	
	if (size) {
		while (--size && (*pdest++ = *psrc++) != '\0');
		*pdest = '\0';
	}
	if (!size)
		while (*psrc++);
	return (psrc - src -1);
}

size_t strlcat(char *dest, const char *src, size_t size)
{
	char *pdest = dest;
	const char *psrc = src;
	size_t i = size;
	size_t len;
	
	while (--i && *pdest)
		pdest++;
	len = pdest - dest;

	if (!i)
		return strlen(src) + len;
	
	while (i-- && *psrc)
		*pdest++ = *psrc++;
	*pdest = '\0';
	
	return len + (psrc - src);
}

/* Replace all occurences of each of the characters in 'chars' by 'repl' */
void strrepl(char *str, const char *chars, char repl)
{
	char *p = str;
	
	while (*p) {
		if (strchr(chars, *p))
			*p = repl;
		p++;
	}
}


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

/* Used for command line options and printing options */
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

/* searches for an option with key 'key'. If it is found, it will be returned
   and removed from the list, otherwise the return value is NULL */
struct option * option_list_take(struct option_list *list, const char *key)
{
    struct option *op;
    struct option *prev = NULL;

    for (op = list->first; op; op = op->next) {
        if (strcmp(op->key, key) == 0)
            break;
    }
    
    if (!op)
        return NULL;
    
    if (prev)
        prev->next = op->next;
    else
        list->first = op->next;

    if (op == list->last)
        list->last = prev;

    op->next = NULL;
    return op;
}

/* Removes the option with key 'key' (if it exists). Returns true if the option existed */
int option_list_remove(struct option_list *list, const char *key)
{
    struct option *op = option_list_take(list, key);
    if (op) {
        free(op);
        return 1;
    }
    return 0;
}



/* Generic string list */
struct stringlist_item {
    char* string;
    struct stringlist_item *next;
};

struct stringlist {
    struct stringlist_item *first, *last;
};

struct stringlist * create_stringlist()
{
    struct stringlist *list = malloc(sizeof(struct stringlist));
    list->first = list->last = NULL;
    return list;
}

void free_stringlist(struct stringlist *list)
{
    struct stringlist_item *node = list->first;
    struct stringlist_item *tmp;
    while (node) {
        free(node->string);
        tmp = node;
        node = node->next;
        free(tmp);
    }
    free(list);
}

void free_stringlist_item(struct stringlist_item *item)
{
    free(item->string);
    free(item);
}

int stringlist_length(struct stringlist *list)
{
    int cnt = 0;
    struct stringlist_item* node = list->first;
    while (node) {
        cnt++;
        node = node->next;
    }
    return cnt;
}

void stringlist_append(struct stringlist *list, const char *string)
{
    int len = strlen(string);
    struct stringlist_item *node  = malloc(sizeof(struct stringlist_item));
    node->string = malloc(len +1);
    strcpy(node->string, string);
    node->next = NULL;
    
    if (!list->last) {
        list->first = list->last = node;
    }
    else {
        list->last->next = node;
        list->last = node;
    }
}

struct stringlist_item * stringlist_item_at(struct stringlist *list, int idx)
{
    struct stringlist_item *it;
    for (it = list->first; it, idx > 0; it = it->next, idx--);
    return it;
}

const char * stringlist_value_at(struct stringlist *list, int idx)
{
	struct stringlist_item *it;
	for (it = list->first; it, idx > 0; it = it->next, idx--);
	return it ? it->string : NULL;
}

struct stringlist_item * stringlist_find(struct stringlist *list, const char *string)
{
    struct stringlist_item *node;
    for (node = list->first; node; node = node->next) {
        if (strcmp(node->string, string) == 0)
            return node;
    }
    return NULL;
}

struct stringlist_item * stringlist_find_prefix(struct stringlist *list, const char *prefix)
{
    struct stringlist_item *node;
    for (node = list->first; node; node = node->next) {
        if (prefixcmp(node->string, prefix) == 0)
            return node;
    }
    return NULL;
}

/* removes 'item' from 'list' and returns successor, free()s item */
struct stringlist_item * stringlist_remove_item(struct stringlist *list, struct stringlist_item *item)
{
    struct stringlist_item *pred = NULL;
    
    if (!item)
        return NULL;
    if (item == list->first)
        list->first = list->first->next;
    else
        for (pred = list->first; pred && pred->next != item; pred = pred->next);
    if (item == list->last)
        list->last = pred;

    if (pred)
        pred->next = item->next;

    free(item->string);
    free(item);
    return pred->next;
}

/* removes all occurences of 'string' in 'list'
   returns the number of items removed */
int stringlist_remove(struct stringlist *list, const char *string)
{
    struct stringlist_item *node, *pred = NULL;
    int cnt = 0;
    for (node = list->first; node; node = node->next, pred = node) {
        if (strcmp(node->string, string) == 0) {
            if (node == list->first)
                list->first = list->first->next;
            else if (pred)
                pred->next = node->next;
            if (node == list->last)
                list->last = pred;
            free(node->string);
            free(node);
            cnt++;
        }
    }
    return cnt;
}

/* removes the first item with 'string' and returns it */
struct stringlist_item * stringlist_take(struct stringlist *list, const char *string)
{
    struct stringlist_item *node, *pred = NULL;
    for (node = list->first; node && strcmp(node->string, string); node = node->next, pred = node);
    if (!node)
        return NULL;
    if (pred)
        pred->next = node->next;
    return node;
}

#if 0
struct stringlist_item * stringlist_find_int_option(struct stringlist *list, const char *optname)
{
    struct stringlist_item *node;
    for (node = list->first; node && node->next; node = node->next) {
        if (strcmp(node->string, optname) == 0 && isint(node->next->string))
            return node;
    }
    return NULL;
}

int stringlist_remove_int_option(struct stringlist *list, const char *optname)
{
    struct stringlist_item *item = stringlist_find_int_option(list, optname);
    if (item) {
        stringlist_remove_item(list, item->next);
        stringlist_remove_item(list, item);
        return 1;
    }
    return 0;
}
#endif

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

/* copies the filename in 'path' into 'dest', without the extension */
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
    while (*p != 0 && *p != '.' && --dest_size > 0) {
        *pdest++ = *p++;
    }
    *pdest = '\0';
}

/* Data structure for all the options (corresponds to $dat in foomatic > 4)
 *
 */

struct argument_setting {
    char value [128];
    char comment [128];
    char driverval [256];
};

struct argument_setting_list {
    struct argument_setting *item;
    struct argument_setting_list *next;
};

struct optionset {
    char name [128];
    struct optionset *next; /* next in global optionset list */
};
/* global list containing all option sets */
struct optionset *optionsetlist;


struct argument_value {
    struct optionset *optset;
    char value [128];
};

struct argument_value_list {
    struct argument_value *item;
    struct argument_value_list *next;
};


/* possible argument types */
#define ARG_TYPE_NONE      0
#define ARG_TYPE_ENUM      1
#define ARG_TYPE_PICKMANY  2
#define ARG_TYPE_BOOL      3
#define ARG_TYPE_INT       4
#define ARG_TYPE_FLOAT     5
#define ARG_TYPE_STRING    6

struct argument {
    char name [128];
    char comment [128];
    char style;
    char proto [128];
    int type;        /* one of the ARG_TYPE_xxx defines */
    char spot;
    int order;
    char section [128];
    char defaultvalue [128];
    char foomaticdefault [128];

    /* for numeric options */
    char min[32], max[32];

    /* for string options */
    int maxlength;
    char *allowedchars;
    char *allowedregexp;
	
	char varname [128];

    struct argument_setting_list *settings;
    struct argument_value_list *values;
};

struct argument_list {
    struct argument *arg;
    struct argument_list *next;
};

struct options {
    char id [128];
    char driver [128];
    char cmd [1024];
    char cupsfilter [256];      /* cups filter for mime type "application/vnd.cups-raster" */
    char colorprofile [128];    /* TODO substitute for %X and %W in 'cmd' (see perl version line 1850) */
    struct argument_list *arguments;
};

struct optionset * find_optionset(const char *name)
{
    struct optionset *optset = optionsetlist;
    while (optset) {
        if (strcasecmp(optset->name, name) == 0)
            return optset;
    }
    return NULL;
}

/* Searches optionset for one named 'name' and returns it. If such an optionset
   does not exist, it will be created */
struct optionset * assure_optionset(const char *name)
{
    struct optionset *optset = optionsetlist;
    while (optset) {
        if (strcasecmp(optset->name, name) == 0)
            return optset;
    }

    /* didn't find it --> create */
    optset = malloc(sizeof(struct optionset));
    strncpy(optset->name, name, 127);
    optset->name[127] = '\0';
    optset->next = optionsetlist;
    optionsetlist = optset->next;
    return optset;
}

struct argument_list * options_find_argument_list(struct options *opts, const char *argname)
{
    struct argument_list *p;
    for (p = opts->arguments; p; p = p->next) {
        if (strcmp(p->arg->name, argname) == 0)
            return p;
    }
    return NULL;
}

/* finds an argument in a case insensetive way */
struct argument * options_find_argument(struct options *opts, const char *argname)
{
    struct argument_list *p;
    for (p = opts->arguments; p; p = p->next) {
        if (strcasecmp(p->arg->name, argname) == 0)
            return p->arg;
    }
    return NULL;
}

int argument_setting_count(struct argument *arg)
{
	int cnt = 0;
	struct argument_setting_list *setting;
	
	for (setting = arg->settings; setting; setting = setting->next)
		cnt++;
	return cnt;
}

/* case insensitive */
struct argument_setting * argument_find_setting(struct argument *arg, const char *settingname)
{
    struct argument_setting_list *p;
    for (p = arg->settings; p; p = p->next) {
        if (strcasecmp(p->item->value, settingname) == 0)
            return p->item;
    }
    return NULL;
}

/* Checks if 'arg' contains a value for an optionset named 'optsetname' and if not, adds it*/
struct argument_value * argument_assure_value(struct argument *arg, struct optionset *optset)
{
    struct argument_value_list *li;
    for (li = arg->values; li; li = li->next) {
        if (li->item->optset == optset)
            return li->item;
    }

    li = malloc(sizeof(struct argument_value_list));
    li->next = arg->values;
    arg->values = li;
    li->item = malloc(sizeof(struct argument_value));
    li->item->optset = optset;
    li->item->value[0] = '\0';
    return li->item;
}

/* returns 'arg's value for 'optset' and returns it. returns NULL if 'arg' doesn't have a value for 'optset' */
struct argument_value * argument_get_value(struct argument *arg, struct optionset *optset)
{
    struct argument_value_list *li;
    for (li = arg->values; li; li = li->next) {
        if (li->item->optset == optset)
            return li->item;
    }
    return NULL;    
}

/* sets the value for 'arg' in the optionset 'optsetname' to 'value' */
struct argument_value * argument_set_value(struct argument *arg, struct optionset *optset, const char *value)
{
    struct argument_value *val = argument_assure_value(arg, optset);
    strncpy(val->value, value, 127);
    val->value[127] = '\0';
}

/* Copy one option set into another one */
void argument_copy_options(struct options *dat, const char *srcoptsetname, const char *dstoptsetname)
{
    struct optionset *srcoptset = assure_optionset(srcoptsetname);
    struct optionset *dstoptset = assure_optionset(dstoptsetname);
    struct argument_list *arg_li;
    struct argument *arg;
    struct argument_value_list *val_li;
    struct argument_value *val;

    for (arg_li = dat->arguments; arg_li; arg_li = arg_li->next) {
        arg = arg_li->arg;
        for (val_li = arg->values; val_li; val_li = val_li->next) {
            val = val_li->item;
            if (val->optset == srcoptset)
                break;
        }
        if (val_li != NULL) {
            argument_set_value(arg, dstoptset, val->value);
        }
    }
}

/* delete an option set */
void argument_delete_options(struct options *dat, const char *optsetname)
{
    struct optionset *optset = find_optionset(optsetname);
    struct argument_list *arg_li;
    struct argument *arg;
    struct argument_value_list *val_li, *prev_val_li = NULL;
            
    if (!optset)
        return;

    for (arg_li = dat->arguments; arg_li; arg_li = arg_li->next) {
        arg = arg_li->arg;
        
        val_li = arg->values;
        while (val_li) {
            if (val_li->item->optset == optset) {
                if (prev_val_li)
                    prev_val_li->next = val_li->next;
                else
                    arg->values = val_li->next;
                free(val_li->item);
                free(val_li);
                break;
            }
            prev_val_li = val_li;
            val_li = val_li->next;
        }
    }
}

/* Compare two option sets, if they are equal, return 1, otherwise 0 */
int argument_optionsets_equal(struct options *dat, const char *optsetname1, const char *optsetname2, int exceptPS)
{
    struct optionset *optset1 = find_optionset(optsetname1);
    struct optionset *optset2 = find_optionset(optsetname2);
    struct argument_list *arg_li;
    struct argument *arg;
    struct argument_value *val1, *val2;

    if (!optset1 && !optset2) /* both non-existant */
        return 1;
    else if (!optset1 || !optset2) /* one non-existant */
        return 0;

    for (arg_li = dat->arguments; arg_li; arg_li = arg_li->next) {
		arg = arg_li->arg;
        val1 = argument_get_value(arg, optset1);
        val2 = argument_get_value(arg, optset2);
        if (val1 && val2 && strcmp(val1->value, val2->value) != 0)
            return 0; /* both values exist, but are unequal */
        else if (val1 || val2)
            return 0; /* only one value exists */
        /* If no extry exists, the non-existing entries are considered as equal */
    }
    return 1;
}

/* This function checks whether a given value is valid for a given
 * option. If yes, it returns a pointer to a newly malloced string
 * containing a cleaned value, otherwise NULL
 * NOTE: If the return is non-NULL, the returned pointer must be freed
 *       by the caller of this function
 */
char * argument_check_value(struct argument *arg, const char *value, int forcevalue)
{
    struct argument_setting *setting;
    struct argument_setting_list *li;
    int ivalue, imin, imax;
    float fvalue, fmin, fmax;
    char *p;
    char tmp[256], *ptmp;

    switch (arg->type) {
        case ARG_TYPE_BOOL:
            if (strcasecmp(value, "true") == 0 ||
                strcasecmp(value, "on") == 0 ||
                strcasecmp(value, "yes") == 0 ||
                strcasecmp(value, "1") == 0)
                return strdup("1");
            else if (strcasecmp(value, "true") == 0 ||
                     strcasecmp(value, "on") == 0 ||
                     strcasecmp(value, "yes") == 0 ||
                     strcasecmp(value, "1") == 0)
                return strdup("0");
            else if (forcevalue) {
                /* This maps Unknown to mean False. Good? Bad?
                It was done so since Foomatic 2.0.x */
                _log("The value %s for %s is not a choice!\n --> Using False instead!\n", value, arg->name);
                return strdup("0");
            }
            break;

        case ARG_TYPE_ENUM:
            if (strcasecmp(value, "none") == 0)
                return strdup("None");
            else if (setting = argument_find_setting(arg, value))
                return strdup(value);
            else if ((strcmp(arg->name, "PageSize") == 0 || strcmp(arg->name, "PageRegion") == 0) &&
                      (setting = argument_find_setting(arg, "Custom")) &&
                      prefixcmp(value, "Custom.") == 0)
                /* Custom paper size */
                return strdup(value);
            else if (forcevalue) {
                /* wtf!? that's not a choice! */
                _log("The %s for %s is not a choice!\n Using %s instead!\n",
                     value, arg->name, arg->settings ? arg->settings->item->value : "<null>");
                return strdup(setting->value);
            }
            break;

        case ARG_TYPE_INT:
            ivalue = atoi(value);
            imin = atoi(arg->min);
            imax = atoi(arg->max);
            if (ivalue >= imin && ivalue <= imax)
                return strdup(value);
            else if (forcevalue) {
                if (ivalue < imin)
                    p = arg->min;
                else if (ivalue > imax)
                    p = arg->max;
                _log("The value %s for %s is out of range!\n --> Using %s instead!\n", value, arg->name, p);
                return strdup(p);
            }

        case ARG_TYPE_FLOAT:
            fvalue = atof(value);
            fmin = atof(arg->min);
            fmax = atof(arg->max);
            if (fvalue >= fmin && fvalue <= fmax)
                return strdup(value);
            else if (forcevalue) {
                if (fvalue < fmin)
                    p = arg->min;
                else if (fvalue > imax)
                    p = arg->max;
                _log("The value %s for %s is out of range!\n --> Using %s instead!\n", value, arg->name, p);
                return strdup(p);
            }
            break;

        case ARG_TYPE_STRING:
            if (setting = argument_find_setting(arg, value)) {
                _log("The %s for %s is a predefined choice\n", value, arg->name);
                return strdup(value);
            }
            else if (stringvalid(arg, value)) {
                /* Check whether the string is one of the enumerated choices */
                for (p = arg->proto, ptmp = tmp; *p; p++) {
                    if (*p == '%' && *(p +1) != 's')
                        strcpy(p, value);
                    else
                        *ptmp++ = *p;
                }

                for (li = arg->settings; li; li = li->next) {
                    if (strcmp(li->item->driverval, tmp) == 0 || strcmp(li->item->driverval, value) == 0) {
                        _log("The string %s for %s is the predefined choice for %s\n", value, arg->name, li->item->value);
                        return strdup(li->item->value);
                    }
                }

                /* "None" is mapped to the empty string */
                if (strcasecmp(value, "None") == 0) {
                    _log("Option %s: 'None' is mapped to the empty string\n", arg->name);
                    return strdup("");
                }

                /* No matching choice? Return the original string */
                return strdup(value);
            }
            else if (forcevalue) {
                if (arg->maxlength > 0) {
                    p = strndup(value, arg->maxlength -1);
                    if (stringvalid(arg, p)) {
                        _log("The string %s for %s is is longer than %d, string shortened to %s\n",
                             value, arg->name, arg->maxlength, p);
                        return p;
                    }
                    else {
                        _log("Option %s incorrectly defined in the PPD file! Exiting.\n", arg->name);
                        exit(EXIT_PRNERR_NORETRY_BAD_SETTINGS);
                    }
                }
                else if (arg->settings) {
                    /* First list item */
                    _log("The string %s for %s contains forbidden characters or does not match the reular"
                            "expression defined for this option, using predefined choince %s instead\n",
                            value, arg->name, arg->settings->item->value);
                    return strdup(arg->settings->item->value);
                }
                else {
                    _log("Option %s incorrectly defined in the PPD file! Exiting.\n", arg->name);
                    exit(EXIT_PRNERR_NORETRY_BAD_SETTINGS);
                }
            }
            break;

        default:
            return NULL;
    }
}

/* check if there already is an argument record 'arg' in 'opts', if not, create one */
static struct argument * options_checkarg(struct options *opts, const char *argname)
{
    struct argument_list *p, *last = NULL;
    struct argument *arg;

    for (p = opts->arguments; p; p = p->next) {
        if (strcmp(p->arg->name, argname) == 0)
            return p->arg;
        last = p;
    }

    arg = malloc(sizeof(struct argument));
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

    arg->min[0] = arg->max[0] = '\0';
    arg->maxlength = -1;
    arg->allowedchars = NULL;
    arg->allowedregexp = NULL;
    arg->order = 0;
    arg->section[0] = '\0';
    arg->defaultvalue[0] = '\0';
    arg->values = NULL;

    /* insert arg into opts->arguments list */
    p = malloc(sizeof(struct argument_list));
    p->arg = arg;
    p->next = NULL;
    if (!last)
        opts->arguments = p;
    else
        last->next = p;

    _log("Added option %s\n", arg->name);
    return arg;
}

void options_check_arguments(struct options *dat, const char *optsetname)
{
	struct optionset *optset;
	struct argument_list *item;
	struct argument *arg, *arg2;
	struct argument_value *val, *val2;
	char *value = NULL;
	
	if (!(optset = find_optionset(optsetname)))
		return;
	
	for (item = dat->arguments; item; item = item->next) {
		if (val = argument_get_value(item->arg, optset)) {
			value = argument_check_value(item->arg, val->value, 1);
			argument_set_value(arg, optset, value);
			free(value);
		}
	}

	/* If the settings for "PageSize" and "PageRegion" are different,
	   set the one for "PageRegion" to the one for "PageSize" and issue
       a warning.*/
	arg = options_checkarg(dat, "PageSize");
	arg2 = options_checkarg(dat, "PageRegion");
	val = argument_assure_value(arg, optset);
	val2 = argument_assure_value(arg2, optset);
	if (strcmp(val->value, val2->value) != 0) {
		_log("Settings for \"PageSize\" and \"PageRegion\" are "
			 "different:\n"
			 "   PageSize: %s\n"
			 "   PageRegion: %s\n" 
			 "Using the \"PageSize\" value \"%s\", for both.\n", 
			 val->value, val2->value, val->value);
	}
}

void options_check_defaults(struct options *dat)
{
	/*  Adobe's PPD specs do not support numerical
		options. Therefore the numerical options are mapped to
		enumerated options in the PPD file and their characteristics
		as a numerical option are stored in "*Foomatic..."
		keywords. A default must be between the enumerated
		fixed values. The default value must be given by a 
		"*FoomaticRIPDefault<option>: <value>" line in the PPD file. 
		But this value is only valid if the "official" default 
		given by a "*Default<option>: <value>" line 
		(it must be one of the enumerated values)
		points to the enumerated value which is closest to this
		value. This way a user can select a default value with a
		tool only supporting PPD files but not Foomatic extensions.
		This tool only modifies the "*Default<option>: <value>" line
		and if the "*FoomaticRIPDefault<option>: <value>" had always
		priority, the user's change in "*Default<option>: <value>"
		would have no effect. */
	
	struct argument_list *item;

	for (item = dat->arguments; item; item = item->next) {
		if (!isempty(item->arg->foomaticdefault)) {
            if (isempty(item->arg->defaultvalue))
                strcpy(item->arg->defaultvalue, item->arg->foomaticdefault);
			/* TODO the perl version for this part doesn't make sense to me ... */
		}
	}
}


/* Checks whether a user-supplied value for a string option is valid
   It must be within the length limit, should only contain allowed
   characters and match the given regexp */
int stringvalid(struct argument *arg, const char *value)
{
    const char *p;
    regex_t rx;

    /* Maximum length */
    if (arg->maxlength >= 0 && strlen(value) > arg->maxlength)
            return 0;

    /* Allowed Characters */
    if (arg->allowedchars) {
        /* TODO quote slashes? (see perl version) */
        for (p = value; *p; p++) {
            if (!strchr(arg->allowedchars, *p))
                return 0;
        }
    }

    /* Regular expression */
    if (arg->allowedregexp) {
        if (regcomp(&rx, arg->allowedregexp, 0) == 0) {
            /* TODO quote slashes? (see perl version) */
            if (regexec(&rx, value, 0, NULL, 0) != 0)
                return 0;
        }
        else {
            _log("FoomaticRIPOptionAllowedRegExp for %s could not be compiled.\n", arg->name);
            /* TODO yreturn success or failure??? */
            return 1;
        }
        regfree(&rx);
    }

    return 1;
}

const char * arg_type_string(int argtype)
{
    switch (argtype) {
        case ARG_TYPE_NONE:
            return "none";
        case ARG_TYPE_ENUM:
            return "enum";
        case ARG_TYPE_PICKMANY:
            return "pickmany";
        case ARG_TYPE_BOOL:
            return "bool";
        case ARG_TYPE_INT:
            return "int";
        case ARG_TYPE_FLOAT:
            return "float";
        case ARG_TYPE_STRING:
            return "string";
    };
    return NULL;
}

void options_free_argument(struct argument * arg)
{
    struct argument_setting_list *p = arg->settings, *tmp;
    while (p) {
        tmp = p;
        p = p->next;
        free(tmp);
    }
    free(arg->allowedchars);
    free(arg->allowedregexp);
}

/* Check if there already is a choice record 'setting' in the 'argname' argument in 'opts',
   if not, create one */
static struct argument_setting* options_checksetting(struct options *opts, const char *argname, const char *setting_name, const char *comment)
{
    struct argument_list *argitem = options_find_argument_list(opts, argname);
    struct argument_setting_list *p, *last = NULL;
    struct argument_setting *setting;

    if (!argitem) /* given argument does not exist */
        return NULL;

    /* is the setting already present */
    for (p = argitem->arg->settings; p; p = p->next) {
        if (strcmp(p->item->value, setting_name) == 0)
            return p->item;
        last = p;
    }

    setting = malloc(sizeof(struct argument_setting));
    strncpy(setting->value, setting_name, 127);
    setting->value[127] = '\0';
    if (comment) {
        strncpy(setting->comment, comment, 127);
        setting->comment[127] = '\0';
    }
    else
        setting->comment[0] = '\0';
    setting->driverval[0] = '\0';

    p = malloc(sizeof(struct argument_setting_list));
    p->item = setting;
    p->next = NULL;
    if (!last)
        argitem->arg->settings = p;
    else
        last->next = p;

    /* _log("Added setting %s to argument %s\n", setting->value, argitem->item->name); */
    return setting;
}

void free_options(struct options *opts)
{
    struct argument_list *ap = opts->arguments, *atmp;

    while (ap) {
        atmp = ap;
        ap = ap->next;
        options_free_argument(atmp->arg);
        free(atmp);
    }
    free(opts);
}

struct options* parse_file(const char* filename)
{
    struct options* opts;
    FILE *fh;
    char *line;
    int buf_size = 256;
    size_t read, line_len;
    char *p;
    char *key, *argname;
    char *value;
    char tmp[1024];
    struct argument *arg, *current_arg = NULL;
    struct argument_setting *setting, *setting2;
    int len;

    fh = fopen(filename, "r");
    if (!fh) {
        _log("error opening %s\n", filename);
        /* TODO quit gracefully */
        exit(EXIT_PRNERR_NORETRY_BAD_SETTINGS);
    }
    _log("Parsing PPD file ...\n");

    opts = malloc(sizeof(struct options));
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
                unhtmlify(opts->cupsfilter, 256, p);
            }
        }
        else if (strcmp(key, "CustomPageSize True") == 0) {
            options_checkarg(opts, "PageSize");
            options_checkarg(opts, "PageRegion");
            setting = options_checksetting(opts, "PageSize", "Custom", "Custom Size");
            setting2 = options_checksetting(opts, "PageRegion", "Custom", "Custom Size");
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
            current_arg = options_checkarg(opts, argname);
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
            arg = options_checkarg(opts, argname);

            p = strtok(value, " \t"); /* type */
            if (strcasecmp(p, "enum") == 0)
                arg->type = ARG_TYPE_ENUM;
            else if (strcasecmp(p, "pickmany") == 0)
                arg->type = ARG_TYPE_PICKMANY;
            else if (strcasecmp(p, "bool") == 0)
                arg->type = ARG_TYPE_BOOL;
            else if (strcasecmp(p, "int") == 0)
                arg->type == ARG_TYPE_INT;
            else if (strcasecmp(p, "float") == 0)
                arg->type = ARG_TYPE_FLOAT;
            else if (strcasecmp(p, "string") == 0 || strcasecmp(p, "password") == 0)
                arg->type = ARG_TYPE_STRING;

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
            arg = options_checkarg(opts, argname);

            unhtmlify(arg->proto, 128, value);
        }
        else if (prefixcmp(key, "FoomaticRIPOptionRange") == 0) {
            /* *FoomaticRIPOptionRange <option>: <min> <max>
               Used for numerical options only */
            if (!(argname = strchr(key, ' ')))
                continue;
            argname += 1;
            arg = options_checkarg(opts, argname);

            p = strtok(value, " \t"); /* min */
            strlcpy(arg->min, p, 32);
            p = strtok(NULL, " \t"); /* max */
            strlcpy(arg->max, p, 32);
        }
        else if (prefixcmp(key, "FoomaticRIPOptionMaxLength") == 0) {
            /*  "*FoomaticRIPOptionMaxLength <option>: <length>"
                Used for string options only */
            if (!(argname = strchr(key, ' ')))
                continue;
            argname += 1;
            arg = options_checkarg(opts, argname);

            arg->maxlength = atoi(value);
        }
        else if (prefixcmp(key, "FoomaticRIPOptionAllowedChars") == 0) {
            /* *FoomaticRIPOptionAllowedChars <option>: <code>
                Used for string options only */
            if (!(argname = strchr(key, ' ')))
                continue;
            argname += 1;
            arg = options_checkarg(opts, argname);

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
            arg = options_checkarg(opts, argname);

            len = strlen(value) +1;
            arg->allowedregexp = malloc(len);
            unhtmlify(arg->allowedregexp, len, value);
        }
        else if (strcmp(key, "OrderDependency") == 0) {
            /* OrderDependency: <order> <section> *<option> */
            if (!(argname = strchr(value, '*')))
                continue;
            argname += 1;
            arg = options_checkarg(opts, argname);

            p = strtok(value, " \t");
            arg->order = atoi(p);
            p = strtok(NULL, " \t");
            strncpy(arg->section, p, 127);
            arg->section[127] = '\0';
        }
        else if (prefixcmp(key, "Default") == 0) {
            /* Default<option>: <value> */
            argname = &key[7];
            arg = options_checkarg(opts, argname);
            strncpy(arg->defaultvalue, value, 127);
            arg->defaultvalue[127] = '\0';
        }
        else if (prefixcmp(key, "FoomaticRIPDefault") == 0) {
            /* FoomaticRIPDefault<option>: <value>
               Used for numerical options only */
            argname = &key[18];
            arg = options_checkarg(opts, argname);
            strncpy(arg->foomaticdefault, value, 127);
            arg->foomaticdefault[127] = '\0';
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
                    setting = options_checksetting(opts, arg->name, "true", p);
                else
                    setting = options_checksetting(opts, arg->name, "false", p);
            }
            else {
                setting = options_checksetting(opts, arg->name, key, p);
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
            arg = options_checkarg(opts, argname);

            if (p) {
                setting = options_checksetting(opts, argname, p, NULL);
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

/* Variable for PPR's backend interface name (parallel, tcpip, atalk, ...) */
char backend [64];

/* If the "PageSize" or "PageRegion" was changed, also change the other */
void sync_pagesize(struct options *opts, struct argument *arg, const char *value, struct optionset *optset)
{
    struct argument *otherarg = NULL;
    struct argument_setting *setting;

    if (strcmp(arg->name, "PageSize") == 0)
        otherarg = options_find_argument(opts, "PageRegion");
    else if (strcmp(arg->name, "PageRegion") == 0)
        otherarg = options_find_argument(opts, "PageSize");
    else
        return; /* Don't do anything if arg is not "PageSize" or "PageRegion"*/

    if (!otherarg)
        return;

    if (setting = argument_find_setting(otherarg, value))
        /* Standard paper size */
        argument_set_value(otherarg, optset, setting->value);
    else
        /* Custom paper size */
        argument_set_value(otherarg, optset, value);
}

/* processes the command line options */
void process_options(struct options *dat, struct option_list *cmdl_options)
{
    struct option *op;
    struct argument *arg, *arg2;
    struct argument_setting *setting;
    struct optionset *optset;
    char *p;
    int width, height;
    char unit[2], tmp[150];

    if (option_has_pagerange(op)) {
        strcpy(tmp, "pages:");
        strncat(tmp, op->pagerange, 120);
        optset = assure_optionset(tmp);
    }
    else 
        optset = assure_optionset("userval");

    for (op = cmdl_options->first; op; op = op->next) {
		_log("Pondering option '%s'\n", op->key);
		
        /* "docs" option to print help page */
        if (strcmp(op->key, "docs") == 0 && (!op->value[0] || strcmp(op->value, "true") == 0)) {
            do_docs = 1;
            continue;
        }
        /* "profile" option to supply a color correction profile to a CUPS raster driver */
        if (strcmp(op->key, "profile") == 0) {
            strncpy(dat->colorprofile, op->value, 127);
            dat->colorprofile[127] = '\0';
            continue;
        }
        if (option_has_pagerange(op) && (arg = options_find_argument(dat, op->key)) &&
            (!arg->section[0] || prefixcmp(arg->section, "AnySetup") != 0 || prefixcmp(arg->section, "PageSetup") != 0)) {
            _log("This option (%s) is not a \"PageSetup\" or \"AnySetup\" option, so it cannot be restricted to a page range.\n", op->key);
            continue;
        }

        /* Solaris options that have no reason to be */
        if (prefixcmp(op->key, "nobanner") == 0 ||
            prefixcmp(op->key, "dest") == 0 ||
            prefixcmp(op->key, "protocol") == 0)
            continue;

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

            p = strtok(op->value, ",");
            do {
                if (arg = options_find_argument(dat, "PageSize")) {
                    if (setting = argument_find_setting(arg, p)) {
                        argument_set_value(arg, optset, setting->value);
                        /* Keep "PageRegion" in sync */
                        if ((arg = options_find_argument(dat, "PageRegion")) && (setting = argument_find_setting(arg, p)))
                            argument_set_value(arg, optset, setting->value);
                    }
                    else if (prefixcmp(p, "Custom") == 0) {
                        argument_set_value(arg, optset, p);
                        /* Keep "PageRegion" in sync */
                        if ((arg = options_find_argument(dat, "PageRegion")))
                            argument_set_value(arg, optset, p);
                    }
                }
                else if ((arg = options_find_argument(dat, "MediaType")) && (setting = argument_find_setting(arg, p))) {
                    argument_set_value(arg, optset, setting->value);
                }
                else if ((arg = options_find_argument(dat, "InputSlot")) && (setting = argument_find_setting(arg, p))) {
                    argument_set_value(arg, optset, setting->value);
                }
                else if (strcasecmp(p, "manualfeed") == 0) {
                    /* Special case for our typical boolean manual 
                       feeder option if we didn't match an InputSlot above */
                    if ((arg = options_find_argument(dat, "ManualFeed"))) {
                        argument_set_value(arg, optset, "1");
                    }
                }
                else {
                    _log("Unknown \"media\" component: \"%s\".\n", p);
                }
            } while (p = strtok(NULL, ","));
        }
        else if (strcasecmp(op->key, "sides") == 0) {
            /* Handle the standard duplex option, mostly */
            if (prefixcasecmp(op->value, "two-sided") == 0) {
                if (arg = options_find_argument(dat, "Duplex")) {
                    /* We set "Duplex" to '1' here, the real argument setting will be done later */
                    argument_set_value(arg, optset, "1");

                    /* Check the binding: "long edge" or "short edge" */
                    if (strcasestr(op->value, "long-edge")) {
                        if ((arg2 = options_find_argument(dat, "Binding")) &&
                                (setting = argument_find_setting(arg2, "LongEdge")))
                            argument_set_value(arg2, optset, setting->value);
                        else
                            argument_set_value(arg2, optset, "LongEdge");
                    }
                    else if (strcasestr(op->value, "short-edge")) {
                        if ((arg2 = options_find_argument(dat, "Binding")) &&
                                (setting = argument_find_setting(arg2, "ShortEdge")))
                            argument_set_value(arg2, optset, setting->value);
                        else
                            argument_set_value(arg2, optset, "ShortEdge");
                    }
                }
            }
            else if (prefixcasecmp(op->value, "one-sided") == 0) {
                if (arg = options_find_argument(dat, "Duplex"))
                    /* We set "Duplex" to '0' here, the real argument setting will be done later */
                    argument_set_value(arg, optset, "0");
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
            if (arg = options_find_argument(dat, op->key)) {
                /* use the choice if it is valid, otherwise ignore it */
                if (p = argument_check_value(arg, op->value, 0)) {
                    argument_set_value(arg, optset, p);
                    sync_pagesize(dat, arg, p, optset);
                    free(p);
                }
                else
                    _log("Invalid choice %s=%s.\n", arg->name, op->value);
            }
            else if (spooler == SPOOLER_PPR_INT) {
                /* Unknown option, pass it to PPR's backend interface */
                /* TODO */
            }
            else
                _log("Unknown option %s=%s.\n", op->key, op->value);
        }

        /* Custom paper size */
        if (sscanf(op->key, "%dx%d%2c", &width, &height, unit) == 3 &&
            width != 0 && height != 0 &&
            (arg = options_find_argument(dat, "PageSize")) &&
            (setting = argument_find_setting(arg, "Custom")))
        {
            strcpy(tmp, "Custom.");
            strcat(tmp, op->key);
            argument_set_value(arg, optset, tmp);
            /* Keep "PageRegion" in sync */
            if (arg2 = options_find_argument(dat, "PageRegion"))
                argument_set_value(arg2, optset, tmp);
        }

        /* Standard bool args:
           landscape; what to do here?
           duplex; we should just handle this one OK now? */
        else if (prefixcmp(op->key, "no") == 0 && (arg = options_find_argument(dat, &op->key[3])))
            argument_set_value(arg, optset, "0");
        else if (arg = options_find_argument(dat, op->key))
            argument_set_value(arg, optset, "1");
        else
            _log("Unknown boolean option \"%s\".\n", op->key);
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
		if (!prefixcmp(name, argv[argc]))
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
		if (i + 1 < argc && !strcmp(name, argv[i])) {
			argv[i][0] = '\0';
			argv[i +1][0] = '\0';
			return 1;
		}
		else if (!prefixcmp(name, argv[i])) {
			argv[i] = '\0';
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
    time_t t;
    struct stringlist_item *item;
	
	if (i = find_arg_prefix("--genpdq", argc, argv)) {
		raw = 0;
		append = 0;
	}
    else if (i = find_arg_prefix("--genrawpdq", argc, argv)) {
        raw = 1;
        append = 0;
    }
	else if (i = find_arg_prefix("--appendpdq", argc, argv)) {
        raw = 0;
        append = 1;
    }
	else if (i = find_arg_prefix("--appendrawpdq", argc, argv)) {
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
    else
        handle = stdout;
	
	/* remove option from args */
	argv[i][0] = '\0';	

    /* Do we have a pdq driver declaration for a raw printer */
    if (raw) {
        t = time(NULL);
        p = ctime(&t);
        p[24] = '\0';

        fprintf(handle, "driver \"Raw-Printer-");
        fprintf(handle, "%s", p); /* time */
        fprintf(handle, "\" {"
                "# This PDQ driver declaration file was generated automatically by"
                "# foomatic-rip to allow raw (filter-less) printing."
                "language_driver all {"
                "# We accept all file types and pass them through without any changes"
                "filetype_regx \"\""
                "       convert_exec {"
                "       ln -s $INPUT $OUTPUT"
                "       }"
                "}"
                "filter_exec {"
                "   ln -s $INPUT $OUTPUT"
                "}"
                "}");
        if (handle != stdout) {
            fclose(handle);
            handle = NULL;
        }
        exit(EXIT_PRINTED);
    }

    return handle;
}

void init_ppr(int rargc, char **rargv, struct option_list *options)
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
        option_list_append_from_string(options, ppr_options);
        option_list_append_from_string(options, ppr_routing);

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
            if (p = strrchr(ppdfile, '\n'))
                *p = '\0';
        }
        else {
            ppdfile[0] = '\0';
        }

        /* We have PPR and run as an interface */
        spooler == SPOOLER_PPR_INT;
    }
}

void init_cups(int rargc, char **rargv, struct option_list *options, struct stringlist *filelist)
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
	option_list_append_from_string(options, cups_options);
	
	/* Check for and handle inputfile vs stdin */
	if (rargc > 4) {
		strncpy_omit(cups_filename, rargv[5], 256, omit_shellescapes);
		if (cups_filename[0] != '-') {
			/* We get input from a file */
			stringlist_append(filelist, cups_filename);
			_log("Getting input from file %s\n", cups_filename);
		}
	}
	
	accounting_prolog = ps_accounting ? accounting_prolog_code : NULL;
	
	/* On which queue are we printing?
	   CUPS gives the PPD file the same name as the printer queue,
	   so we can get the queue name from the name of the PPD file. */
	extract_filename(printer, ppdfile, 256);
}

void init_solaris(int rargc, char **rargv, struct option_list *opts, struct stringlist *filelist)
{
	char *str;
	int len, i;
	
	assert(rargc >= 5);
	
	/* Get all command line parameters */
	strncpy_omit(jobtitle, rargv[2], 128, omit_shellescapes);
	
	len = strlen(rargv[4]);
	str = malloc(len +1);
	strncpy_omit(str, rargv[4], len, omit_shellescapes);
	option_list_append_from_string(opts, str);
	free(str);
	
	for (i = 5; i < rargc; i++)
		stringlist_append(filelist, rargv[i]);
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

void init_direct_cps_pdq(int rargc, char **rargv, struct stringlist *filelist, const char *user_default_path)
{
	char tmp [1024];
    int i;
		
	/* Which files do we want to print? */
	for (i = 0; i < rargc; i++) {
		strncpy_omit(tmp, rargv[i], 1024, omit_shellescapes);
		stringlist_append(filelist, tmp);
	}
		
	if (ppdfile[0] == '\0') {
		if (printer[0] == '\0') {
			/* No printer definition file selected, check whether we have a 
			   default printer defined */
			find_default_printer(user_default_path);			
		}
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


/* Dynamic string */  /* TODO move somewhere else */
typedef struct {
	char *data;
	size_t len;
	size_t alloc;
} dstr_t;

dstr_t * create_dstr()
{
	dstr_t *ds = malloc(sizeof(dstr_t));
	ds->len = 0;
	ds->alloc = 32;
	ds->data = malloc(ds->alloc);
	ds->data[0] = '\0';
	return ds;
}

void free_dstr(dstr_t *ds)
{
	free(ds->data);
	free(ds);
}

void dstrclear(dstr_t *ds)
{
	ds->len = 0;
	ds->data[0] = '\0';
}

void dstrcatf(dstr_t *ds, const char *src, ...)
{
	va_list ap;
	size_t restlen = ds->alloc - ds->len;
	size_t srclen;
	
	va_start(ap, src);
	srclen = vsnprintf(&ds->data[ds->len], restlen, src, ap);
	va_end(ap);
	
	if (srclen > restlen) {
		do {
			ds->alloc <<= 1;
			restlen = ds->alloc - ds->len;
		} while (srclen > restlen);
		ds->data = realloc(ds->data, ds->alloc);
		
		va_start(ap, src);
		vsnprintf(&ds->data[ds->len], restlen, src, ap);
		va_end(ap);
	}
	
	ds->len += srclen;
}

/* if 'path' is relative, prepend cwd */
void make_absolute_path(char *path, int len)
{
    char *tmp, *cwd;
    
    if (path[0] != '/') {
        tmp = malloc(len +1);
        strlcpy(tmp, path, len);
        
        cwd = malloc(len);
        getcwd(cwd, len);        
        strlcpy(path, cwd, len);
        strlcat(path, tmp, len);
        
        free(tmp);
        free(cwd);
    }
}


/* Build a PDQ driver description file to use the given PPD file
   together with foomatic-rip with the PDQ printing system
   and output it into 'pdqfile' */
void print_pdq_driver(FILE *pdqfile, struct options *dat, const char *optsetname)
{
	struct optionset *optset = assure_optionset(optsetname);
	struct argument_list *item;
	struct argument *arg;
	struct argument_value *val;
	struct argument_setting_list *setting;
    struct argument_setting *set_true, *set_false;
	
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
    char *p;
    int len;

	for (item = dat->arguments; item; item = item->next) {
		arg = item->arg;
		if (arg->type == ARG_TYPE_ENUM) {
			/* Option with only one choice, omit it, foomatic-rip will set 
			   this choice anyway */
			if (argument_setting_count(arg) < 1)
				continue;
			
			/* Omit "PageRegion" option, it does the same as "PageSize" */
			if (!strcmp(arg->name, "PageRegion"))
				continue;
			
			/* Assure that the comment is not emtpy */
			if (isempty(arg->comment))
				strcpy(arg->comment, arg->name);
			
			strcpy(arg->varname, arg->name);
			strrepl(arg->varname, "-/.", '_');
			
			val = argument_assure_value(arg, optset);
			strlcpy(def, val->value, 128);
			
			/* If the default is a custom size we have to set also
			   defaults for the width, height, and units of the page */
			if (!strcmp(arg->name, "PageSize") && 
				sscanf(def, "Custom.%dx%d%2c", &pagewidth, &pageheight, pageunit)) 
			{
				strcpy(def, "Custom");
			}
			
			dstrcatf(driveropts, 
					"  option{\n"
					"    var = \"%s\"\n"
					"    desc = \"%s\"\n", arg->varname, arg->comment);
			
			/* get enumeration values for each enum arg */
			dstrclear(tmp);
			for (setting = arg->settings; setting; setting = setting->next)  {
				dstrcatf(tmp,
					"    choice \"%s_%s\"{\n"
					"      desc = \"%s\"\n"
					"      value = \" -o %s=%s\"\n"
					"    }\n", 
	 				arg->name, setting->item->value, 
	  				isempty(setting->item->comment) ? setting->item->value : setting->item->comment,
					arg->name, setting->item->value);
				
				if (!strcmp(arg->name, "PageSize") && !strcmp(setting->item->value, "Custom")) {
					custompagesize = 1;
					if (isempty(setcustompagesize->data)) {
						dstrcatf(setcustompagesize,
							"      # Custom page size settings\n"
							"      # We aren't really checking for legal vals.\n"
							"      if [ \"x${%s}\" = 'x -o %s=%s' ]; then\n"
							"        %s=\"${%s}.${PageWidth}x${PageHeight}${PageSizeUnit}\"\n",
							"      fi\n\n",
							arg->varname, arg->varname, setting->item->value, arg->varname, arg->varname);
					}
				}
			}
			
			dstrcatf(driveropts, "    default choice \"%s_%s\"\n", arg->name, def);
			dstrcatf(driveropts, tmp->data);
			dstrcatf(driveropts, "  }\n\n");
			
			if (custompagesize) {
				/* Add options to set the custom page size */
				dstrcatf(driveropts,
                    "  argument {\n"
					"    var = \"PageWidth\"\n"
					"    desc = \"Page Width (for \\\"Custom\\\" page size)\"\n"
					"    def_value \"%d\"\n"                      /* pagewidth */
					"    help = \"Minimum value: 0 Maximum value: 100000\"\n"
					"  }\n\n"
					"  argument {\n"
					"    var = \"PageHeight\"\n"
					"    desc = \"Page Height (for \\\"Custom\\\" page size)\"\n"
					"    def_value \"%d\"\n"                      /* pageheight */
					"    help = \"Minimum value: 0 Maximum value: 100000\"\n"
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
        else if (arg->type == ARG_TYPE_INT || arg->type == ARG_TYPE_FLOAT) {
            /* Assure that the comment is not emtpy */
            if (isempty(arg->comment))
                strcpy(arg->comment, arg->name);

            val = argument_assure_value(arg, optset);
            strlcpy(def, val->value, 128);            
            strcpy(arg->varname, arg->name);
            strrepl(arg->varname, "-/.", '_');
            
            dstrcatf(driveropts, 
                "  argument {\n"
                "    var = \"%s\"\n"
                "    desc = \"%s\"\n"
                "    def_value \"%s\"\n"
                "    help = Minimum value: %s, Maximum Value: %s"
                "  }\n\n",
                arg->varname, arg->comment, def, arg->min, arg->max);
        }
        else if (arg->type == ARG_TYPE_BOOL) {
            /* Assure that the comment is not emtpy */
            if (isempty(arg->comment))
                strcpy(arg->comment, arg->name);

            val = argument_assure_value(arg, optset);
            strlcpy(def, val->value, 128);
            strcpy(arg->varname, arg->name);
            strrepl(arg->varname, "-/.", '_');
            set_true = argument_find_setting(arg, "true");
            set_false = argument_find_setting(arg, "false");

            dstrcatf(driveropts,
                "  option {\n"
                "    var = \"%s\"\n"
                "    desc = \"%s\"\n", arg->varname, arg->comment);
            
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
                arg->name, set_true->comment, arg->name,
                arg->name, set_false->comment, arg->name);
        }
        else if (arg->type == ARG_TYPE_STRING) {
            /* Assure that the comment is not emtpy */
            if (isempty(arg->comment))
                strcpy(arg->comment, arg->name);

            val = argument_assure_value(arg, optset);
            strlcpy(def, val->value, 128);
            strcpy(arg->varname, arg->name);
            strrepl(arg->varname, "-/.", '_');

            dstrclear(tmp);
            if (arg->maxlength)
                dstrcatf(tmp, "Maximum Length: %s characters, ", arg->maxlength);

            dstrcatf(tmp, "Examples/special settings: ");
            for (setting = arg->settings; setting; setting = setting->next)  {
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
        "    default_choice \"nodocs\"\n", 
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
    
    for (item = dat->arguments; item; item = item->next) {
        if (!isempty(item->arg->varname)) 
            dstrcatf(cmdline, "${%s}", item->arg->varname);
    }
    dstrcatf(cmdline, "${DRIVERDOCS} ${INPUT} > ${OUTPUT}");
    
    
    /* Now we generate code to build the command line snippets for the numerical options */
	for (item = dat->arguments; item; item = item->next) {
		/* Only numerical and string options need to be treated here */
		if (arg->type != ARG_TYPE_INT && 
		    arg->type != ARG_TYPE_FLOAT && 
			arg->type != ARG_TYPE_STRING)
			continue;
		
		/* If the option's variable is non-null, put in the
		   argument.  Otherwise this option is the empty
		   string.  Error checking? */
		dstrcatf(psfilter, "      # %s\n", arg->comment);
		if (arg->type == ARG_TYPE_INT || arg->type == ARG_TYPE_FLOAT) {
			dstrcatf(psfilter,
				"      # We aren't really checking for max/min,\n"
				"      # this is done by foomatic-rip\n"
				"      if [ \"x${%s}\" != 'x' ]; then\n  ", arg->varname);
		}
		
		dstrcatf(psfilter, "      %s=\"-o %s='${%s}'\"\n", arg->varname, arg->name, arg->varname);
		
		if (arg->type == ARG_TYPE_INT || arg->type == ARG_TYPE_FLOAT)
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
	strrepl(tmp->data, " \t\n", '-');	
	
    fprintf(pdqfile,
		"driver \"%s-%s\" {\n\n"   /* TODO original code removed \W from */
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
		tmp->data, /* cleaned printer_model */
        time(NULL), ppdfile, printer_model,
	    driveropts->data, setcustompagesize->data, psfilter->data);
		
	
	free_dstr(setcustompagesize);
	free_dstr(driveropts);
	free_dstr(tmp);
	free_dstr(cmdline);
	free_dstr(psfilter);
}

int main(int argc, char** argv)
{
    struct config conf;
    struct option_list *options = create_option_list();
    struct stringlist *args = create_stringlist();
    struct stringlist_item *arg;
	struct stringlist_item *it;
    struct option* op = NULL;
	struct stringlist *filelist = create_stringlist();
    int i, j;
    int verbose = 0, quiet = 0, showdocs = 0;
    const char* str;
    char* p;
    const char *path;
    struct options * dat;
    FILE *genpdqfile = NULL;
	FILE *ppdfh = NULL;
	int rargc;
	char **rargv = NULL;
	char tmp[1024], pstoraster[256];
	int dontparse = 0;
	int havefilter, havepstoraster;
    char user_default_path [256];

    /* Path for personal Foomatic configuration */
    strlcpy(user_default_path, getenv("HOME"), 256);
    strlcat(user_default_path, "/.foomatic/", 256);

    config_set_default_options(&conf);
    config_read_from_file(&conf, CONFIG_PATH "/filter.conf");
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
            for (i = 0; i < argc -1; i++)
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
	if (remove_arg("--lprng", argc, argv))
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
		while (str = get_option_value("-p", argc, argv)) {
			strncpy_omit(ppdfile, str, 256, omit_shellescapes);
			remove_option("-p", argc, argv);
		}
    }
	while (str = get_option_value("--ppd", argc, argv)) {
		strncpy_omit(ppdfile, str, 256, omit_shellescapes);
		remove_option("--ppd", argc, argv);
	}

    /* Check for LPD/GNUlpr by typical options which the spooler puts onto
       the filter's command line (options "-w": text width, "-l": text
       length, "-i": indent, "-x", "-y": graphics size, "-c": raw printing,
       "-n": user name, "-h": host name) */
    if (str = get_option_value("-h", argc, argv)) {
        if (spooler != SPOOLER_GNULPR && spooler != SPOOLER_LPRNG)
            spooler = SPOOLER_LPD;
        strncpy(jobhost, str, 127);
        jobhost[127] = '\0';
		remove_option("-h", argc, argv);
    }
	if (str = get_option_value("-n", argc, argv)) {
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
	if (str = get_option_value("-Z", argc, argv)) {
        spooler = SPOOLER_LPRNG;
        option_list_append_from_string(options, str);
		remove_option("-Z", argc, argv);
	}
    /* Job title and options for stock LPD */
	if ((str = get_option_value("-j", argc, argv)) || (str = get_option_value("-J", argc, argv))) {
        strncpy_omit(jobtitle, str, 128, omit_shellescapes);
        if (spooler == SPOOLER_LPD) {
            op = malloc(sizeof(struct option));
            strncpy(op->key, jobtitle, 127);
            op->key[127] = '\0';
            option_list_append(options, op);
        }
		remove_option("-j", argc, argv) || remove_option("-J", argc, argv);
    }
    /* Check for CPS */
    if (remove_arg("--cps", argc, argv) > 0)
        spooler = SPOOLER_CPS;

    /* Options for spooler-less printing, CPS, or PDQ */
    while (str = get_option_value("-o", argc, argv)) {
        option_list_append_from_string(options, str);
		remove_option("-o", argc, argv);
        /* If we don't print as PPR RIP or as CPS filter, we print
           without spooler (we check for PDQ later) */
        if (spooler != SPOOLER_PPR && spooler != SPOOLER_CPS)
            spooler = SPOOLER_DIRECT;
    }

    /* Printer for spooler-less printing or PDQ */
    if (str = get_option_value("-d", argc, argv)) {
        strncpy_omit(printer, str, 256, omit_shellescapes);
		remove_option("-d", argc, argv);
    }

    /* Printer for spooler-less printing, PDQ, or LPRng */
    if (str = get_option_value("-P", argc, argv)) {
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
	for (i = 0; i < argc; i++) {
		if (argv[i])
			rargc++;
	}
	rargv = malloc(sizeof(char *) * rargc);
	for (i = 0, j = 0; i < argc; i++) {
		if (argv[i])
			rargv[j++] = argv[i];
	}
	
	/* spooler specific initialization */
	switch (spooler) {
		case SPOOLER_PPR:
        	init_ppr(rargc, rargv, options);
			break;
			
		case SPOOLER_CUPS:
			init_cups(rargc, rargv, options, filelist);
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
	if (stringlist_length(filelist) == 0)
		stringlist_append(filelist, "<STDIN>");

	/* Check filelist */
	for (it = filelist->first; it; it = it->next) {		
		if (!strcmp(it->string, "<STDIN>"))
			continue;
		if (it->string[0] == '-') {
			_log("Invalid argument: %s", it->string);
			exit(EXIT_PRNERR_NORETRY_BAD_SETTINGS);
		}
		else if (access(it->string, R_OK) != 0) {
			_log("File %s does not exist/is not readable\n", it->string);
			it = stringlist_remove_item(filelist, it);
		}
	}
	
	/* When we print without spooler or with CPS do not log onto STDERR unless  
	   the "-v" ('Verbose') is set or the debug mode is used */
	if ((spooler == SPOOLER_DIRECT || spooler == SPOOLER_CPS || genpdqfile) && !verbose && !conf.debug) {
		if (logh && logh != stderr)
			fclose(logh);
		logh = NULL;
	}
	
	/* Start logging */
	if (!conf.debug) {
		/* If we are in debug mode, we do this earlier */
		_log("foomatic-rip version " RIP_VERSION " running...\n");
		/* Print the command line only in debug mode, Mac OS X adds very many
		   options so that CUPS cannot handle the output of the command line
		   in its log files. If CUPS encounters a line with more than 1024
		   characters sent into its log files, it aborts the job with an error. */
        if (spooler != SPOOLER_CUPS) {
            _log("called with arguments: ");
            for (i = 0; i < argc -1; i++)
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
	
	/* parse ppd file */
    dat = parse_file(ppdfile);
	
	/* We do not need to parse the PostScript job when we don't have
	   any options. If we have options, we must check whether the
	   default settings from the PPD file are valid and correct them
	   if nexessary. */
	if (!dat->arguments) {
		/* We don't have any options, so we do not need to parse the 
		   PostScript data */
		dontparse = 1;
	}
	else {
		options_check_arguments(dat, "default");
		options_check_defaults(dat);					
	}
		
	/* Is our PPD for a CUPS raster driver */
	if (!isempty(dat->cupsfilter)) { 
		/* Search the filter in cupsfilterpath
		   The %Y is a placeholder for the option settings */
		havefilter = 0;
        path = conf.cupsfilterpath;
	    while (path = strncpy_tochar(tmp, path, 1024, ":")) {
			strlcat(tmp, "/", 1024);
			strlcat(tmp, dat->cupsfilter, 1024);
			if (access(tmp, X_OK) == 0) {
				havefilter = 1;
				strlcpy(dat->cupsfilter, tmp, 256);
				strlcat(dat->cupsfilter, " 0 '' '' 0 '%Y%X'", 256);
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
			while (path = strncpy_tochar(tmp, path, 1024, ":")) {
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
			snprintf(dat->cmd, 1024, "%s | %s", pstoraster, dat->cupsfilter);
			
			/* Set environment variables */
			setenv("PPD", ppdfile, 1);
		}
	}
	
	/* Was the RIP command line defined in the PPD file? If not, we assume a PostScript printer 
	   and do not render/translate the input data */
	if (isempty(dat->cmd)) {
		strcpy(dat->cmd, "cat%A%B%C%D%E%F%G%H%I%J%K%L%M%Z");
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
	if (conf.debug || spooler != SPOOLER_CUPS) {
		_log("Options: ");
		for (op = options->first; op; op = op->next) {
			if (op->value[0] == '\0')
				_log("%s ", op->key);
			else
				_log("%s=%s", op->key, op->value);
		}
		_log("\n");
	}
	_log("Job title: %s\n", jobtitle);
	_log("File(s) to be printed: ");
	for (it = filelist->first; it; it = it->next) {
		_log("%s ", it->string);
	}
	_log("\n");
	if (getenv("GS_LIB"))
		_log("GhostScript extra search path ('GS_LIB'): %s\n", getenv("GS_LIB"));
	
		
	/* Process options from command line,
	   but save the defaults for printing documentation pages first */
	argument_copy_options(dat, "default", "userval");	
    process_options(dat, options);
	
	/* Were we called to build the PDQ driver declaration file? */
	if (genpdqfile) {
		print_pdq_file(genpdqfile, dat, "userval");
        fclose(genpdqfile);
        exit(EXIT_PRINTED);
	}

	/* Cleanup */
    if (genpdqfile && genpdqfile != stdout)
        fclose(genpdqfile);
    
	free(rargv);
	free_stringlist(filelist);
    free_option_list(options);
    free_options(dat);
    free_stringlist(args);
	if (logh && logh != stderr)
		fclose(logh);
    return 0;
}
