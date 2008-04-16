
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>

#include "foomaticrip.h"
#include "options.h"
#include "process.h"


/*
 * One of these fileconverters is used if the 'textfilter' option in the config file
 * is not set. (Except if the spooler is CUPS, then 'texttops' is used
 */
const char *fileconverters[][2] = {
    { "a2ps", "a2ps -1 @@--medium=@@PAGESIZE@@ @@--center-title=@@JOBTITLE@@ -o -" },
    { "enscript", "enscript -G @@-M @@PAGESIZE@@ @@-b \"Page $%|@@JOBTITLE@@ --margins=36:36:36:36 --mark-wrapped-lines=arrow --word-wrap -p-" },
    { "mpage", "mpage -o -1 @@-b @@PAGESIZE@@ @@-H -h @@JOBTITLE@@ -m36l36b36t36r -f -P- -" },
    { NULL, NULL }
};

char fileconverter[PATH_MAX] = "";

void set_fileconverter(const char *fc)
{
    int i;
    for (i = 0; fileconverters[i][0]; i++) {
        if (!strcmp(fc, fileconverters[i][0]))
            strlcpy(fileconverter, fileconverters[i][1], PATH_MAX);
    }
    if (!fileconverters[i][0])
        strlcpy(fileconverter, fc, PATH_MAX);
}

int guess_fileconverter()
{
    int i;
    for (i = 0; fileconverters[i][0]; i++) {
        if (find_in_path(fileconverters[i][0], getenv("PATH"), NULL)) {
            strlcpy(fileconverter, fileconverters[i][1], PATH_MAX);
            return 1;
        }
    }
    return 0;
}

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
    size_t len;

    len = strlen(fileconverter) +
            (pagesize ? strlen(pagesize) : 0) + 
            (jobtitle ? strlen(jobtitle) : 0) +1;
    res = malloc(len);
    res[0] = '\0';

    while ((templstart = strstr(last, "@@"))) {
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
    strlcat(res, last, len);

    return res;
}

int exec_kid2(FILE *in, FILE *out, void *user_arg)
{
    int n;
    const char *alreadyread = (const char *)user_arg;
    char tmp[1024];

    /* child, first part of the pipe, reading in the data from standard input
     * and stuffing it into the file converter after putting in the already
     * read data (in alreadyread) */

    _log("kid2: writing alreadyread\n");

    /* At first pass the data which we already read to the filter */
    fwrite(alreadyread, 1, strlen(alreadyread), out);

    _log("kid2: Then read the rest from standard input\n");
    /* Then read the rest from standard input */
    while ((n = fread(tmp, 1, 1024, stdin)) > 0)
        fwrite(tmp, 1, n, out);

    _log("kid2: Close out and stdin\n");
    fclose(out);
    fclose(stdin);

    _log("kid2 finished\n");
    return EXIT_PRINTED;
}

typedef struct {
    const char *fileconv;
    const char *alreadyread;
} kid1_userdata_t;

int exec_kid1(FILE *in, FILE *out, void *user_arg)
{
    int kid2;
    FILE *kid2out;
    int status;
    const char *fileconv = ((kid1_userdata_t *)user_arg)->fileconv;
    const char *alreadyread = ((kid1_userdata_t *)user_arg)->alreadyread;

    kid2 = start_process("kid2", exec_kid2, (void *)alreadyread, NULL, &kid2out);
    if (kid2 < 0)
        return EXIT_PRNERR_NORETRY_BAD_SETTINGS;

    if (spooler != SPOOLER_CUPS)
        _log("file converter command: %s\n", fileconv);

    if (dup2(fileno(kid2out), fileno(stdin)) < 0) {
        _log("kid1: Could not dup stdin\n");
        fclose(kid2out);
        return EXIT_PRNERR_NORETRY_BAD_SETTINGS;
    }
    if (dup2(fileno(out), fileno(stdout)) < 0) {
        _log("kid1: Could not dup stdout\n");
        fclose(kid2out);
        return EXIT_PRNERR_NORETRY_BAD_SETTINGS;
    }
    if (debug && !redirect_log_to_stderr()) {
        _log("Could not dup logh to stderr\n");
        fclose(kid2out);
        return EXIT_PRNERR_NORETRY_BAD_SETTINGS;
    }

    /* Actually run the thing... */
    status = run_system_process("fileconverter", fileconv);
    fclose(out);
    fclose(kid2out);
    fclose(stdin);
    fclose(stdout);

    if (WIFEXITED(status)) {
        if (WEXITSTATUS(status) == 0) {
            wait_for_process(kid2);
            _log("kid1 finished\n");
            return EXIT_PRINTED;
        }
    }
    else if (WIFSIGNALED(status)) {
        switch (WTERMSIG(status)) {
            case SIGUSR1: return EXIT_PRNERR;
            case SIGUSR2: return EXIT_PRNERR_NORETRY;
            case SIGTTIN: return EXIT_ENGAGED;
        }
    }
    return EXIT_PRNERR;
}


/*
 *  This function is only used when the input data is not PostScript. Then it
 *  runs a filter which converts non-PostScript files into PostScript. The user
 *  can choose which filter he wants to use. The filter command line is
 *  provided by 'fileconverter'.
 */
void get_fileconverter_handle(const char *alreadyread, FILE **fd, pid_t *pid)
{
    pid_t kid1;
    FILE *kid1out;
    const char *pagesize;
    char *fileconv;
    kid1_userdata_t kid1_userdata;

    _log("\nStarting converter for non-PostScript files\n");

    if (isempty(fileconverter) && !guess_fileconverter())
        rip_die(EXIT_PRNERR_NORETRY_BAD_SETTINGS, "Cannot convert file to "
                "Postscript (missing fileconverter).");

    /* Use wider margins so that the pages come out completely on every printer
     * model (especially HP inkjets) */
    pagesize = option_get_value(find_option("PageSize"), optionset("header"));
    if (pagesize && startswith(fileconverter, "a2ps")) {
        if (!strcasecmp(pagesize, "letter"))
            pagesize = "Letterdj";
        else if (!strcasecmp(pagesize, "a4"))
            pagesize = "A4dj";
    }

    if (do_docs)
        snprintf(get_current_job()->title, 128, "Documentation for the %s", printer_model);

    fileconv = fileconverter_from_template(fileconverter, pagesize, get_current_job()->title);

    kid1_userdata.fileconv = fileconv;
    kid1_userdata.alreadyread = alreadyread;
    kid1 = start_process("kid1", exec_kid1, &kid1_userdata, NULL, &kid1out);
    if (kid1 < 0)
        rip_die(EXIT_PRNERR_NORETRY_BAD_SETTINGS, "Cannot convert file to "
                "Postscript (Cannot fork for kid1!)\n");

    *fd = kid1out;
    *pid = kid1;

    free(fileconv);
}

int close_fileconverter_handle(FILE *fileconverter_handle, pid_t fileconverter_pid)
{
    int status;

    _log("\nClosing file converter\n");
    fclose(fileconverter_handle);

    status = wait_for_process(fileconverter_pid);
    if (WIFEXITED(status))
        return WEXITSTATUS(status);
    else
        return EXIT_PRNERR_NORETRY_BAD_SETTINGS;
}

