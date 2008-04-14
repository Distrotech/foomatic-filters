
extern "C" {
#include "foomaticrip.h"
#include "util.h"
#include "options.h"
#include "process.h"
}

#include <ctype.h>
#include <sys/wait.h>
#include <unistd.h>
#include <Object.h>
#include <PDFDoc.h>
#include <Page.h>
#include <Stream.h>
#include <errno.h>

void extract_command(size_t *start, size_t *end, const char *cmdline, const char *cmd)
{
    char *copy = strdup(cmdline);
    char *tok = NULL;
    const char *delim = "|;";

    *start = *end = 0;
    for (tok = strtok(copy, delim); tok; tok = strtok(NULL, delim)) {
        while (*tok && isspace(*tok))
            tok++;
        if (startswith(tok, cmd)) {
            *start = tok - copy;
            *end = tok + strlen(tok) - copy;
            break;
        }
    }

    free(copy);
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
}

extern "C"
int print_pdf(FILE *s, const char *alreadyread, size_t len, const char *filename, size_t startpos)
{
    PDFDoc *doc;
    Object obj;
    int page_count, i;
    Catalog *cat;
    Page *page;
    char tmpfilename[PATH_MAX];
    FILE *tmpfile;
    int firstpage;
    FileStream *stream;

    if (s == stdin) {
        tmpfile = create_temp_file(tmpfilename, stdin, alreadyread, len);
        if (!tmpfile)
            exit(EXIT_PRNERR_NORETRY_BAD_SETTINGS);
        s = tmpfile;
    }

    obj.initNull();
    stream = new FileStream(s, startpos, gFalse, 0, &obj);

    doc = new PDFDoc(stream);
    if (!doc->isOk()) {
        printf("Could not load pdf.\n");
        return 1;
    }

    page_count = doc->getNumPages();
    cat = doc->getCatalog();
    if (!cat)
        return 1;

    optionset_copy_values(optionset("default"), optionset("currentpage"));
    optionset_copy_values(optionset("currentpage"), optionset("previouspage"));
    firstpage = 1;
    for (i = 1; i <= page_count; i++) {
        page = cat->getPage(i);

        if (!optionset_equal(optionset("currentpage"), optionset("previouspage"), 1)) {
            render_pages(filename, firstpage, i);
            firstpage = i;
        }

        optionset_copy_values(optionset("currentpage"), optionset("previouspage"));
    }

    render_pages(filename, firstpage, page_count);
    wait_for_renderer();
}

