
#define _GNU_SOURCE

#include <signal.h>
#include <ctype.h>
#include <stdlib.h>
#include <unistd.h>

#include "foomaticrip.h"
#include "util.h"
#include "process.h"
#include "options.h"

/*
 * Check whether we have a Ghostscript version with redirection of the standard
 * output of the PostScript programs via '-sstdout=%stderr'
 */
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

    fread(output, 1, 10, pd);
    pclose(pd);

    if (startswith(output, "hello"))
        return 1;

    return 0;
}

/*
 * Massage arguments to make ghostscript execute properly as a filter, with
 * output on stdout and errors on stderr etc.  (This function does what
 * foomatic-gswrapper used to do)
 */
void massage_gs_commandline(dstr_t *cmd)
{
    int gswithoutputredirection = test_gs_output_redirection();
    size_t start, end;
    
    extract_command(&start, &end, cmd->data, "gs");

    if (start == end) /* cmd doesn't call ghostscript */
        return;

    /* If Ghostscript does not support redirecting the standard output
       of the PostScript program to standard error with
       '-sstdout=%stderr', sen the job output data to fd 3; errors
       will be on 2(stderr) and job ps program interpreter output on
       1(stdout). */
    if (gswithoutputredirection)
        dstrreplace(cmd, "-sOutputFile=- ", "-sOutputFile=%stdout ");
    else
        dstrreplace(cmd, "-sOutputFile=- ", "-sOutputFile=/dev/fd/3 ");

    /* Use always buffered input. This works around a Ghostscript
       bug which prevents printing encrypted PDF files with Adobe
       Reader 8.1.1 and Ghostscript built as shared library
       (Ghostscript bug #689577, Ubuntu bug #172264) */
    dstrreplace(cmd, " - ", " -_ ");
    dstrreplace(cmd, " /dev/fd/0 ", " -_ ");

    /* Turn *off* -q (quiet!); now that stderr is useful! :) */
    dstrreplace(cmd, " -q ", " ");

    /* Escape any quotes, and then quote everything just to be sure...
       Escaping a single quote inside single quotes is a bit complex as the shell
       takes everything literal there. So we have to assemble it by concatinating
       different quoted strings.
       Finally we get e.g.: 'x'"'"'y' or ''"'"'xy' or 'xy'"'"'' or ... */
    /* dstrreplace(cmd, "'", "'\"'\"'"); TODO tbd */


    dstrremove(cmd, start, 2);     /* Remove 'gs' */
    if (gswithoutputredirection)
        dstrinsert(cmd, start, GS_PATH" -sstdout=%stderr ");
    else {
        dstrinsert(cmd, start, GS_PATH);
        dstrinsert(cmd, end, " 3>&1 1>&2");
    }

    /* If the renderer command line contains the "echo" command, replace the
     * "echo" by the user-chosen $myecho (important for non-GNU systems where
     * GNU echo is in a special path */
    dstrreplace(cmd, "echo", ECHO); /* TODO search for \wecho\w */
}

char * read_line(FILE *stream)
{
    char *line;
    size_t alloc = 64, len = 0;
    int c;

    line = malloc(alloc);

    while ((c = fgetc(stream)) != EOF) {
        if (len >= alloc -1) {
            alloc *= 2;
            line = realloc(line, alloc);
        }
        if (c == '\n')
            break;
        line[len] = (char)c;
        len++;
    }

    line[len] = '\0';
    return line;
}

/*
 * Read all lines containing 'jclstr' from 'stream' (actually, one more) and
 * return them in a zero terminated array.
 */
static char ** read_jcl_lines(FILE *stream, const char *jclstr)
{
    char *line;
    char **result;
    size_t alloc = 8, cnt = 0;

    result = malloc(alloc * sizeof(char *));

    /* read from the renderer output until the first non-JCL line appears */
    while ((line = read_line(stream)))
    {
        if (cnt >= alloc -1)
        {
            alloc *= 2;
            result = realloc(result, alloc);
        }
        result[cnt] = line;
        cnt++;
        if (!strstr(line, jclstr))
            break;
    }

    result[cnt] = NULL;
    return result;
}

static int jcl_keywords_equal(const char *jclline1, const char *jclline2)
{
    char *p1, *p2;

    p1 = strchrnul(skip_whitespace(jclline1), '=') -1;
    while (isspace(*p1))
        p1--;

    p2 = strchrnul(skip_whitespace(jclline1), '=') -1;
    while (isspace(*p2))
        p2--;

    return strncmp(jclline1, jclline2, p1 - jclline1) == 0;
}

/*
 * Finds the keyword of line in opts
 */
static const char * jcl_options_find_keyword(char **opts, const char *line)
{
    if (!opts)
        return NULL;

    while (*opts)
    {
        if (jcl_keywords_equal(*opts, line))
            return *opts;
        opts++;
    }
    return NULL;
}

static void argv_write(FILE *stream, char **argv, const char *sep)
{
    if (!argv)
        return;

    while (*argv)
        fprintf(stream, "%s%s", *argv++, sep);
}

/*
 * Merges 'original_opts' and 'pref_opts' and writes them to 'stream'. Header /
 * footer is taken from 'original_opts'. If both have the same options, the one
 * from 'pref_opts' is preferred
 * Returns true, if original_opts was not empty
 */
static int write_merged_jcl_options(FILE *stream,
                                    char **original_opts,
                                    char **opts,
                                    const char *jclstr)
{
    /* No JCL options in original_opts, just prepend opts */
    if (argv_count(original_opts) == 1)
    {
        fprintf(stream, "%s", jclbegin);
        argv_write(stream, opts, "\n");
        fprintf(stream, "%s\n", original_opts[0]);
        return 0;
    }

    if (argv_count(original_opts) == 2)
    {
        /* If we have only one line of JCL it is probably something like the
         * "@PJL ENTER LANGUAGE=..." line which has to be in the end, but it
         * also contains the "<esc>%-12345X" which has to be in the beginning
         * of the job */
        char *p = strstr(original_opts[0], jclstr);
        if (p)
            fwrite(original_opts[0], 1, p - original_opts[0], stream);
        else
            fprintf(stream, "%s\n", original_opts[0]);

        argv_write(stream, opts, "\n");

        if (p)
            fprintf(stream, "%s\n", p);

        fprintf(stream, "%s\n", original_opts[1]);
        return 1;
    }

    /* First, write the first line from original_opts, as it contains the JCL
     * header */
    fprintf(stream, "%s\n", *original_opts++);

    while (*opts)
        fprintf(stream, "%s\n", *opts++);

    while (*original_opts)
        if (!jcl_options_find_keyword(opts, *original_opts))
            fprintf(stream, "%s\n", *original_opts++);


    return 1;
}

void log_jcl()
{
    char **opt;

    _log("JCL: ");
    if (jclprepend)
        for (opt = jclprepend; *opt; opt++)
            _log("%s\n", *opt);

    _log("<job data> %s\n\n", jclappend->data);
}

int exec_kid4(FILE *in, FILE *out, void *user_arg)
{
    FILE *fileh = open_postpipe();
    int driverjcl;

    log_jcl();

    /* wrap the JCL around the job data, if there are any options specified...
     * Should the driver already have inserted JCL commands we merge our JCL
     * header with the one from the driver */
    if (argv_count(jclprepend) > 1)
    {
        if (!isspace(jclprepend[1][0]))
        {
            char *jclstr = strndup(jclprepend[1],
                                   strcspn(jclprepend[1], " \t\n\r"));
            char **jclheader = read_jcl_lines(in, jclstr);

            driverjcl = write_merged_jcl_options(fileh,
                                                 jclheader,
                                                 jclprepend,
                                                 jclstr);

            free(jclstr);
            argv_free(jclheader);
        }
        else
            /* No merging of JCL header possible, simply prepend it */
            argv_write(fileh, jclprepend, "\n");
    }

    /* The job data */
    copy_file(fileh, in, NULL, 0);

    /* A JCL trailer */
    if (argv_count(jclprepend) > 1 && !driverjcl)
        fwrite(jclappend->data, jclappend->len, 1, fileh);

    fclose(in);
    if (fclose(fileh) != 0)
    {
        _log("error closing postpipe\n");
        return EXIT_PRNERR_NORETRY_BAD_SETTINGS;
    }

    return EXIT_PRINTED;
}

int exec_kid3(FILE *in, FILE *out, void *user_arg)
{
    dstr_t *commandline = create_dstr();
    int kid4;
    FILE *kid4in;
    int status;

    kid4 = start_process("kid4", exec_kid4, NULL, &kid4in, NULL);
    if (kid4 < 0)
        return EXIT_PRNERR_NORETRY_BAD_SETTINGS;

    build_commandline(optionset("currentpage"), commandline, 0);
    massage_gs_commandline(commandline);
    _log("renderer command: %s\n", commandline->data);

    if (dup2(fileno(in), fileno(stdin)) < 0) {
        _log("kid3: Could not dup stdin\n");
        fclose(kid4in);
        return EXIT_PRNERR_NORETRY_BAD_SETTINGS;
    }
    if (dup2(fileno(kid4in), fileno(stdout)) < 0) {
        _log("kid3: Could not dup stdout to kid4\n");
        fclose(kid4in);
        return EXIT_PRNERR_NORETRY_BAD_SETTINGS;
    }
    if (debug && !redirect_log_to_stderr()) {
        fclose(kid4in);
        return EXIT_PRNERR_NORETRY_BAD_SETTINGS;
    }

    /*
     * In debug mode save the data supposed to be fed into the renderer also
     * into a file
     */
    if (debug) {
        dstrprepend(commandline, "tee -a " LOG_FILE ".ps | ( ");
        dstrcat(commandline, ")");
    }

    /* Actually run the thing */
    status = run_system_process("renderer", commandline->data);
    fclose(in);
    fclose(kid4in);
    fclose(stdin);
    fclose(stdout);
    free_dstr(commandline);

    if (WIFEXITED(status)) {
        switch (WEXITSTATUS(status)) {
            case 0:  /* Success! */
                /* wait for postpipe/output child */
                wait_for_process(kid4);
                _log("kid3 finished\n");
                return EXIT_PRINTED;
            case 1:
                _log("Possible error on renderer command line or PostScript error. Check options.");
                return EXIT_JOBERR;
            case 139:
                _log("The renderer may have dumped core.");
                return EXIT_JOBERR;
            case 141:
                _log("A filter used in addition to the renderer itself may have failed.");
                return EXIT_PRNERR;
            case 243:
            case 255:  /* PostScript error? */
                return EXIT_JOBERR;
        }
    }
    else if (WIFSIGNALED(status)) {
        switch (WTERMSIG(status)) {
            case SIGUSR1:
                return EXIT_PRNERR;
            case SIGUSR2:
                return EXIT_PRNERR_NORETRY;
            case SIGTTIN:
                return EXIT_ENGAGED;
        }
    }
    return EXIT_PRNERR;
}

/*
 * Run the renderer command line (and if defined also the postpipe) and returns
 * a file handle for stuffing in the PostScript data.
 */
void get_renderer_handle(const dstr_t *prepend, FILE **fd, pid_t *pid)
{
    pid_t kid3;
    FILE *kid3in;

    /* Build the command line and get the JCL commands */
    build_commandline(optionset("currentpage"), NULL, 0);

    _log("\nStarting renderer\n");
    kid3 = start_process("kid3", exec_kid3, NULL, &kid3in, NULL);
    if (kid3 < 0)
        rip_die(EXIT_PRNERR_NORETRY_BAD_SETTINGS, "Cannot fork for kid3\n");

    /* Feed the PostScript header and the FIFO contents */
    if (prepend)
        fwrite(prepend->data, prepend->len, 1, kid3in);

    /* We are the parent, return glob to the file handle */
    *fd = kid3in;
    *pid = kid3;
}

/* Close the renderer process and wait until all kid processes finish */
int close_renderer_handle(FILE *rendererhandle, pid_t rendererpid)
{
    int status;

    _log("\nClosing renderer\n");
    fclose(rendererhandle);

    status = wait_for_process(rendererpid);
    if (WIFEXITED(status))
        return WEXITSTATUS(status);
    else
        return EXIT_PRNERR_NORETRY_BAD_SETTINGS;
}

