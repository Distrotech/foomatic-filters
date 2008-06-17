
#include <signal.h>
#include <ctype.h>
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

    fread(output, 1, 1024, pd);
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
    int start, end;
    
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

int exec_kid4(FILE *in, FILE *out, void *user_arg)
{
    FILE *fileh;
    char jclstr[64];
    dstr_t *jclheader = create_dstr(); /* JCL header read from renderer output */
    int driverjcl;
    dstr_t *jclprepend_copy = create_dstr();
    char *line;
    char tmp[1024];
    int insert, commandfound;
    dstr_t *dtmp = create_dstr();
    size_t n;
    dstr_t *jclline = create_dstr();
    const char *p, *postpipe;

    dstrcpy(jclprepend_copy, jclprepend->data);

    /* Do we have a $postpipe, if yes, launch the command(s) and point our
     * output into it/them */
    postpipe = get_postpipe();
    if (!isempty(postpipe)) {
        /* Postpipe might contain a '|' in the beginning */
        for (p = postpipe; *p && isspace(*p); p++);
        if (*p && *p == '|')
            p += 1;

        if (start_system_process("postpipe", p, &fileh, NULL) < 0)
            rip_die(EXIT_PRNERR_NORETRY_BAD_SETTINGS, "Cannot execute postpipe %s\n", postpipe);
    }
    else
        fileh = stdout;

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

            while (fgetdstr(jclline, in)) {
                dstrcat(jclheader, jclline->data);
                if (!strstr(jclline->data, jclstr))
                    break;
            }

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
                             * the beginning of the job-> So we split the line
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
                fwrite(jclheader->data, jclheader->len, 1, fileh);
            }
            else {
                /* The driver didn't create a JCL header, simply
                   prepend ours and then pass on the line which we
                   already have read */
                fwrite(jclprepend->data, jclprepend->len, 1, fileh);
                fwrite(jclheader->data, jclheader->len, 1, fileh);
            }
        }
        else {
            /* No merging of JCL header possible, simply prepend it */
            fwrite(jclprepend->data, jclprepend->len, 1, fileh);
        }
    }

    /* The rest of the job data */
    while ((n = fread(tmp, 1, 1024, in)) > 0)
        fwrite(tmp, 1, n, fileh);

    /* A JCL trailer */
    if (line_count(jclprepend->data) > 1 && !driverjcl)
        fwrite(jclappend->data, jclappend->len, 1, fileh);

    fclose(stdin);
    if (fclose(fileh) != 0) {
        _log("error closing postpipe\n");
        return EXIT_PRNERR_NORETRY_BAD_SETTINGS;
    }

    free_dstr(jclheader);
    free_dstr(dtmp);
    free_dstr(jclline);

    /* Successful exit, inform main process */
    _log("kid4 finished\n");
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

    dstrcpy(commandline, currentcmd->data);
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
    build_commandline(optionset("currentpage"));

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

