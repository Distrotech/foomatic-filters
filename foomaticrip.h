
#ifndef foomatic_h
#define foomatic_h

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <stddef.h>
#include <time.h>

#define RIP_VERSION "4.0"

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

/* What 'echo' program to use.  It needs -e and -n.  Linux's builtin
and regular echo work fine; non-GNU platforms may need to install
gnu echo and put gecho here or something. */
#ifndef ECHO
#define ECHO "echo"
#endif

#ifndef GS_PATH
#define GS_PATH "gs"
#endif


/* Constants used by this filter
 *
 * Error codes, as some spooles behave different depending on the reason why
 * the RIP failed, we return an error code. As I have only found a table of
 * error codes for the PPR spooler. If our spooler is really PPR, these
 * definitions get overwritten by the ones of the PPR version currently in use.
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


/* Supported spoolers are currently:
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

/* The spooler from which foomatic-rip was called. set in main() */
extern int spooler;


#define PATH_MAX 1024

typedef struct {
    char printer[256];
    char id[128];
    char user[128];
    char host[128];
    char title[128];
    char ppdfile[256];
    char copies[128];
    struct dstr *optstr;
    time_t time;
} jobparams_t;


jobparams_t * get_current_job();

void _log(const char* msg, ...);
int redirect_log_to_stderr();
void rip_die(int status, const char *msg, ...);

const char * get_modern_shell();
const char * get_postpipe();

extern struct dstr *currentcmd;
extern struct dstr *jclappend;
extern struct dstr *jclprepend;
extern int jobhasjcl;
extern const char *accounting_prolog;
extern char fileconverter[PATH_MAX];
extern char cupsfilterpath[PATH_MAX];
extern int debug;
extern int do_docs;
extern char printer_model[];

#endif

