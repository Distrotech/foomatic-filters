
#include "foomaticrip.h"
#include "util.h"
#include "options.h"
#include "process.h"

#include <stdlib.h>
#include <ctype.h>
#include <sys/wait.h>
#include <unistd.h>
#include <errno.h>

#include <ghostscript/iapi.h>
#include <ghostscript/ierrors.h>

#define ARRAY_LEN(a) (sizeof(a) / sizeof(a[0]))


const char *pagecountcode = 
    "/pdffile (%s) (r) file def\n"
    "pdfdict begin\n"
    "pdffile pdfopen begin\n"
    "(PageCount: ) print\n"
    "pdfpagecount == flush\n"   /* 'flush' makes sure that gs_stdout is called
                                   before gsapi_run_string returns */
    "currentdict pdfclose\n"
    "end end\n";

char gsout [256];

static int wait_for_renderer();


int gs_stdout(void *instance, const char *str, int len)
{
    int last;
    if (isempty(gsout)) {
        last = len < 256 ? len : 255;
        strncpy(gsout, str, last +1);
        gsout[last] = '\0';
    }
    return len; /* ignore everything after the first few chars */
}

int gs_stderr(void *instance, const char *str, int len)
{
    char *buf = malloc(len +1);
    strncpy(buf, str, len);
    buf[len] = '\0';
    _log("%s", buf);
    free(buf);
    return len;
}

static int pdf_count_pages(const char *filename)
{
    void *minst;
    int gsargc = 3;
    char *gsargv[] = { "", "-dNODISPLAY", "-q" };
    int pagecount;
    int exit_code;
    char code[2048];

    if (gsapi_new_instance(&minst, NULL) < 0) {
        _log("Could not create ghostscript instance\n");
        return -1;
    }
    gsapi_set_stdio(minst, NULL, gs_stdout, gs_stderr);
    if (gsapi_init_with_args(minst, gsargc, gsargv) < 0) {
        _log("Could not init ghostscript\n");
        gsapi_exit(minst);
        gsapi_delete_instance(minst);
        return -1;
    }

    snprintf(code, 2048, pagecountcode, filename);
    if (gsapi_run_string(minst, code, 0, &exit_code) == 0) {
        if (sscanf(gsout, "PageCount: %d", &pagecount) < 1)
            pagecount = -1;
    }

    gsapi_exit(minst);
    gsapi_delete_instance(minst);
    return pagecount;
}

pid_t rendererpid = 0;


static int start_renderer(const char *cmd)
{
    if (rendererpid != 0)
        wait_for_renderer();

    rendererpid = start_system_process("renderer", cmd, NULL, NULL);
    if (rendererpid < 0)
        return 0;

    return 1;
}

static int wait_for_renderer()
{
    int status;

    _log("\nClosing renderer\n");

    waitpid(rendererpid, &status, 0);

    if (!WIFEXITED(status)) {
        _log("Renderer did not finish normally.\n");
        exit(EXIT_PRNERR_NORETRY_BAD_SETTINGS);
    }

    _log("Renderer exit status: %s\n", WEXITSTATUS(status));
    if (WEXITSTATUS(status) != 0)
        exit(EXIT_PRNERR_NORETRY_BAD_SETTINGS);

    rendererpid = 0;
    return 1;
}

/*
 * Extract pages 'first' through 'last' from the pdf and write them into a
 * temporary file.
 */
static int pdf_extract_pages(char filename[PATH_MAX],
                             const char *pdffilename,
                             int first,
                             int last)
{
    void *minst;
    char filename_arg[PATH_MAX], first_arg[50], last_arg[50];
    char *gs_args[] = { "", "-dNOPAUSE", "-dBATCH", "-dPARANOIDSAFER",
        "-sDEVICE=pdfwrite", filename_arg, first_arg, last_arg };
    int exit_code;

    snprintf(filename, PATH_MAX, "%s/foomatic-XXXXXX", P_tmpdir);
    mktemp(filename);
    if (!filename[0])
        return 0;

    if (gsapi_new_instance(&minst, NULL) < 0)
    {
        _log("Could not create ghostscript instance\n");
        return 0;
    }

    snprintf(filename_arg, PATH_MAX, "-sOutputFile=%s", filename);
    snprintf(first_arg, 50, "-dFirstPage=%d", first);
    snprintf(last_arg, 50, "-dLastPage=%d", last);

    if (gsapi_init_with_args(minst, ARRAY_LEN(gs_args), gs_args) < 0)
    {
        _log("Could not init ghostscript\n");
        gsapi_exit(minst);
        gsapi_delete_instance(minst);
        return 0;
    }

    if (gsapi_run_file(minst, pdffilename, 0, &exit_code) == 0)
    {
        _log("gsapi: Could not run file %s\n", pdffilename);
        gsapi_exit(minst);
        gsapi_delete_instance(minst);
        return 0;
    }

    gsapi_exit(minst);
    gsapi_delete_instance(minst);
    return 1;
}

static int render_pages_with_generic_command(dstr_t *cmd,
                                             const char *filename,
                                             int firstpage,
                                             int lastpage)
{
    char tmpfile[PATH_MAX];
    int result;

    if (!pdf_extract_pages(tmpfile, filename, firstpage, lastpage))
        return 0;

    /* TODO it might be a good idea to give pdf command lines the possibility
     * to get the file on the command line rather than piped through stdin
     * (maybe introduce a &filename; ??) */

    dstrcatf(cmd, " < %s", tmpfile);

    result = start_renderer(cmd->data);

    unlink(tmpfile);
    return result;
}

static int render_pages_with_ghostscript(dstr_t *cmd,
                                         size_t start_gs_cmd,
                                         size_t end_gs_cmd,
                                         const char *filename,
                                         int firstpage,
                                         int lastpage)
{
    /* No need to create a temporary file, just give ghostscript the file and
     * first/last page on the command line */

    dstrinsertf(cmd, start_gs_cmd +2,
                " -dFirstPage=%d -dLastPage=%d ",
                firstpage, lastpage);

    dstrinsertf(cmd, end_gs_cmd, " %s ", filename);

    return start_renderer(cmd->data);
}

static int render_pages(const char *filename, int firstpage, int lastpage)
{
    dstr_t *cmd = create_dstr();
    size_t start, end;
    int result;

    build_commandline(optionset("currentpage"), cmd, 1);

    extract_command(&start, &end, cmd->data, "gs");
    if (start == end)
        /* command is not GhostScript */
        result = render_pages_with_generic_command(cmd,
                                                   filename,
                                                   firstpage,
                                                   lastpage);
    else
        /* GhostScript command, tell it which pages we want to render */
        result = render_pages_with_ghostscript(cmd,
                                               start,
                                               end,
                                               filename,
                                               firstpage,
                                               lastpage);

    free_dstr(cmd);
    return result;
}

static int print_pdf_file(const char *filename)
{
    int page_count, i;
    int firstpage;

    page_count = pdf_count_pages(filename);
    if (page_count <= 0)
        return 0;
    _log("File contains %d pages\n", page_count);

    optionset_copy_values(optionset("default"), optionset("currentpage"));
    optionset_copy_values(optionset("currentpage"), optionset("previouspage"));
    firstpage = 1;
    for (i = 1; i <= page_count; i++)
    {
        set_options_for_page(optionset("currentpage"), i);
        if (!optionset_equal(optionset("currentpage"), optionset("previouspage"), 1))
        {
            render_pages(filename, firstpage, i);
            firstpage = i;
        }
        optionset_copy_values(optionset("currentpage"), optionset("previouspage"));
    }
    render_pages(filename, firstpage, page_count);
    wait_for_renderer();

    return 1;
}

int print_pdf(FILE *s,
              const char *alreadyread,
              size_t len,
              const char *filename,
              size_t startpos)
{
    char tmpfilename[PATH_MAX] = "";
    int result;

    /* If reading from stdin, write everything into a temporary file */
    /* TODO don't do this if there aren't any pagerange-limited options */
    if (s == stdin)
    {
        FILE *tmpfile, *file;

        snprintf(tmpfilename, PATH_MAX, "%s/foomatic-XXXXXX", P_tmpdir);
        mktemp(tmpfilename);

        tmpfile = fopen(tmpfilename, "r");
        file = fopen(filename, "r");
        if (!file || !tmpfile)
            return 0;

        copy_file(tmpfile, file, alreadyread, len);

        fclose(file);
        fclose(tmpfile);

        filename = tmpfilename;
    }

    result = print_pdf_file(filename);

    if (!isempty(tmpfilename))
        unlink(tmpfilename);

    return result;
}

