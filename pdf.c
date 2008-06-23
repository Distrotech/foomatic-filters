
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

/*
 * 'filename' must point to a string with a length of at least PATH_MAX
 */
FILE * create_temp_file(char *filename, FILE *copyfrom, const char *alreadyread, size_t len)
{
    int fd;
    FILE *tmpfile;
    char buf[8192];

    strlcpy(filename, getenv("TMPDIR") ? getenv("TMPDIR") : "/tmp", PATH_MAX);
    strlcat(filename, "foomatic-XXXXXX", PATH_MAX);

    fd = mkstemp(filename);
    if (fd < 0) {
        perror("Could not create temporary file.");
        return NULL;
    }
    tmpfile = fdopen(fd, "r+");

    if (alreadyread)
        fwrite(alreadyread, 1, len, tmpfile);

    while (fread(buf, 1, sizeof(buf), copyfrom) > 0)
        fwrite(buf, 1, sizeof(buf), tmpfile);

    rewind(tmpfile);
    return tmpfile;
}

int wait_for_renderer()
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

int render_pages(const char *filename, int firstpage, int lastpage)
{
    dstr_t *cmd = create_dstr();
    size_t pos, start, end;
    const char *currentcmd;

    currentcmd = build_commandline(optionset("currentpage"));

    dstrcpy(cmd, currentcmd);

    extract_command(&start, &end, cmd->data, "gs");
    if (start == end) {
        /* TODO No ghostscript --> convert pdf to postscript */
        return 0;
    }

    for (pos = start +2; pos < end; pos++) {
        if (!strcmp(&cmd->data[pos], " -")) {
            cmd->data[pos +1] = ' ';
            break;
        }
        else if (!strcmp(&cmd->data[pos], " -_")) { /* buffered input */
            cmd->data[pos +1] = ' ';
            cmd->data[pos +2] = ' ';
            break;
        }
    }
    dstrinsertf(cmd, pos, " %s ", filename);
    dstrinsertf(cmd, start +2, " -dFirstPage=%d -dLastPage=%d ", firstpage, lastpage);

    if (rendererpid != 0)
        wait_for_renderer();
        /* TODO evaluate result */

    rendererpid = start_system_process("renderer", cmd->data, NULL, NULL);
    if (rendererpid < 0) {
        _log("Could not start renderer process.\n");
        return 0;
    }

    free_dstr(cmd);
    return 1;
}

int print_pdf(FILE *s, const char *alreadyread, size_t len, const char *filename, size_t startpos)
{
    int page_count, i;
    int firstpage;

    page_count = pdf_count_pages(filename);
    if (page_count <= 0)
        return 1;
    _log("File contains %d pages\n", page_count);

    optionset_copy_values(optionset("default"), optionset("currentpage"));
    optionset_copy_values(optionset("currentpage"), optionset("previouspage"));
    firstpage = 1;
    for (i = 1; i <= page_count; i++) {
        set_options_for_page(optionset("currentpage"), i);
        if (!optionset_equal(optionset("currentpage"), optionset("previouspage"), 1)) {
            render_pages(filename, firstpage, i);
            firstpage = i;
        }
        optionset_copy_values(optionset("currentpage"), optionset("previouspage"));
    }
    render_pages(filename, firstpage, page_count);
    wait_for_renderer();
    return 1;
}

