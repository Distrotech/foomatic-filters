/* foomaticrip.c
 *
 * Copyright (C) 2008 Till Kamppeter <till.kamppeter@gmail.com>
 * Copyright (C) 2008 Lars Uebernickel <larsuebernickel@gmx.de>
 *
 * This file is part of foomatic-rip.
 *
 * Foomatic-rip is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * Foomatic-rip is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

#include "foomaticrip.h"
#include "util.h"
#include "options.h"
#include "pdf.h"
#include "postscript.h"
#include "process.h"
#include "spooler.h"
#include "renderer.h"
#include "fileconverter.h"

#include <stdio.h>
#include <stdlib.h>
#include <strings.h>
#include <errno.h>
#include <memory.h>
#include <ctype.h>
#include <stdarg.h>
#include <assert.h>
#include <unistd.h>
#include <sys/wait.h>
#include <math.h>
#include <signal.h>
#include <pwd.h>

#ifdef HAVE_DBUS
  #include "colord.h"
#endif

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

void close_log()
{
    if (logh && logh != stderr)
        fclose(logh);
}

int redirect_log_to_stderr()
{
    if (dup2(fileno(logh), fileno(stderr)) < 0) {
        _log("Could not dup logh to stderr\n");
        return 0;
    }
    return 1;
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


jobparams_t  *job = NULL;

jobparams_t * get_current_job()
{
    assert(job);
    return job;
}


dstr_t *postpipe;  /* command into which the output of this filter should be piped */
FILE *postpipe_fh = NULL;

FILE * open_postpipe()
{
    const char *p;

    if (postpipe_fh)
        return postpipe_fh;

    if (isempty(postpipe->data))
        return stdout;

    /* Delete possible '|' symbol in the beginning */
    p = skip_whitespace(postpipe->data);
    if (*p && *p == '|')
        p += 1;

    if (start_system_process("postpipe", p, &postpipe_fh, NULL) < 0)
        rip_die(EXIT_PRNERR_NORETRY_BAD_SETTINGS,
                "Cannot execute postpipe %s\n", postpipe->data);

    return postpipe_fh;
}


char printer_model[256] = "";
const char *accounting_prolog = NULL;
char attrpath[256] = "";


int spooler = SPOOLER_DIRECT;
int do_docs = 0;
int dontparse = 0;
int jobhasjcl;
int pdfconvertedtops;

/* Variable for PPR's backend interface name (parallel, tcpip, atalk, ...) */
char backend [64];

/* Array to collect unknown options so that they can get passed to the
backend interface of PPR. For other spoolers we ignore them. */
dstr_t *backendoptions = NULL;

/* These variables were in 'dat' before */
char colorprofile [128];
char cupsfilter[256];
char **jclprepend = NULL;
dstr_t *jclappend;

/* Set debug to 1 to enable the debug logfile for this filter; it will appear
 * as defined by LOG_FILE. It will contain status from this filter, plus the
 * renderer's stderr output. You can also add a line "debug: 1" to your
 * /etc/foomatic/filter.conf to get all your Foomatic filters into debug mode.
 * WARNING: This logfile is a security hole; do not use in production. */
int debug = 0;

/* Path to the GhostScript which foomatic-rip shall use */
char gspath[PATH_MAX] = "gs";

/* What 'echo' program to use.  It needs -e and -n.  Linux's builtin
and regular echo work fine; non-GNU platforms may need to install
gnu echo and put gecho here or something. */
char echopath[PATH_MAX] = "echo";

/* CUPS raster drivers are searched here */
char cupsfilterpath[PATH_MAX] = "/usr/local/lib/cups/filter:"
                                "/usr/local/libexec/cups/filter:"
                                "/opt/cups/filter:"
                                "/usr/lib/cups/filter";

char modern_shell[64] = "/bin/bash";

void config_set_option(const char *key, const char *value)
{
    if (strcmp(key, "debug") == 0)
        debug = atoi(value);

    /* What path to use for filter programs and such. Your printer driver must be
     * in the path, as must be the renderer, $enscriptcommand, and possibly other
     * stuff. The default path is often fine on Linux, but may not be on other
     * systems. */
    else if (strcmp(key, "execpath") == 0 && !isempty(value))
        setenv("PATH", value, 1);

    else if (strcmp(key, "cupsfilterpath") == 0)
        strlcpy(cupsfilterpath, value, PATH_MAX);
    else if (strcmp(key, "preferred_shell") == 0)
        strlcpy(modern_shell, value, 32);
    else if (strcmp(key, "textfilter") == 0)
        set_fileconverter(value);
    else if (strcmp(key, "gspath") == 0)
        strlcpy(gspath, value, PATH_MAX);
    else if (strcmp(key, "echo") == 0)
        strlcpy(echopath, value, PATH_MAX);
}

void config_from_file(const char *filename)
{
    FILE *fh;
    char line[256];
    char *key, *value;

    fh = fopen(filename, "r");
    if (fh == NULL)
        return; /* no error here, only read config file if it is present */

    while (fgets(line, 256, fh) != NULL)
    {
        key = strtok(line, " :\t\r\n");
        if (key == NULL || key[0] == '#')
            continue;
        value = strtok(NULL, " \t\r\n#");
        config_set_option(key, value);
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

    if (*p != ':' && *p != '=') { /* no value for this option */
        if (!*p)
            return NULL;
        else if (isspace(*p)) {
            *p = '\0';
            return p +1;
        }
        return p;
    }

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

/* processes job->optstr */
void process_cmdline_options()
{
    char *p, *cmdlineopts, *nextopt, *pagerange, *key, *value;
    option_t *opt, *opt2;
    int optset;
    char tmp [256];

    _log("Printing system options:\n");
    cmdlineopts = strdup(job->optstr->data);
    for (nextopt = extract_next_option(cmdlineopts, &pagerange, &key, &value);
        key;
        nextopt = extract_next_option(nextopt, &pagerange, &key, &value))
    {
        /* Consider only options which are not in the PPD file here */
        if ((opt = find_option(key)) != NULL) continue;
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
	    else if (spooler == SPOOLER_PPR_INT) {
	        /* Unknown option, pass it to PPR's backend interface */
	        if (!backendoptions)
		    backendoptions = create_dstr();
		dstrcatf(backendoptions, "%s=%s ", key, value);
	    }
	    else
	        _log("Unknown option %s=%s.\n", key, value);
        }
        /* Custom paper size */
        else if ((opt = find_option("PageSize")) && option_set_value(opt, optset, key)) {
            /* do nothing, if the value could be set, it has been set */
        }
        else
            _log("Unknown boolean option \"%s\".\n", key);
    }
    free(cmdlineopts);

    _log("Options from the PPD file:\n");
    cmdlineopts = strdup(job->optstr->data);
    for (nextopt = extract_next_option(cmdlineopts, &pagerange, &key, &value);
        key;
        nextopt = extract_next_option(nextopt, &pagerange, &key, &value))
    {
        /* Consider only PPD file options here */
        if ((opt = find_option(key)) == NULL) continue; 
        if (value)
            _log("Pondering option '%s=%s'\n", key, value);
        else
            _log("Pondering option '%s'\n", key);

        if (pagerange) {
            snprintf(tmp, 256, "pages:%s", pagerange);
            optset = optionset(tmp);

            if (opt && (option_get_section(opt) != SECTION_ANYSETUP &&
                        option_get_section(opt) != SECTION_PAGESETUP)) {
                _log("This option (%s) is not a \"PageSetup\" or \"AnySetup\" option, so it cannot be restricted to a page range.\n", key);
                continue;
            }
        }
        else
            optset = optionset("userval");

        if (value) {
	    /* Various non-standard printer-specific options */
	    if (!option_set_value(opt, optset, value)) {
	        _log("  invalid choice \"%s\", using \"%s\" instead\n", 
		     value, option_get_value(opt, optset));
	    }
        }
        /* Standard bool args:
           landscape; what to do here?
           duplex; we should just handle this one OK now? */
        else if (!prefixcasecmp(key, "no"))
            option_set_value(opt, optset, "0");
        else
            option_set_value(opt, optset, "1");
    }
    free(cmdlineopts);
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
        if (!handle)
            rip_die(EXIT_PRNERR_NORETRY_BAD_SETTINGS, "Cannot write PDQ driver declaration file.\n");
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
                "}", (unsigned int)job->time);
        if (handle != stdout) {
            fclose(handle);
            handle = NULL;
        }
        exit(EXIT_PRINTED);
    }

    return handle;
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
        make_absolute_path(job->ppdfile, 256);
        dstrcatf(cmdline, " --ppd=%s", job->ppdfile);
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
        tmp->data, /* cleaned printer_model */ (unsigned int)job->time, job->ppdfile, printer_model,
        driveropts->data, setcustompagesize->data, psfilter->data);


    free_dstr(setcustompagesize);
    free_dstr(driveropts);
    free_dstr(tmp);
    free_dstr(cmdline);
    free_dstr(psfilter);
}
#endif

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
   KID3: The rendering process. In most cases Ghostscript, "cat"
         for native PostScript printers with their manufacturer's
         PPD files.
   KID4: Put together the JCL commands and the renderer's output
         and send all that either to STDOUT or pipe it into the
         command line defined with $postpipe. */



void write_output(void *data, size_t len)
{
    const char *p = (const char *)data;
    size_t left = len;
    FILE *postpipe = open_postpipe();

    /* Remove leading whitespace */
    while (isspace(*p++) && left-- > 0)
        ;

    fwrite((void *)p, left, 1, postpipe);
    fflush(postpipe);
}

enum FileType {
    UNKNOWN_FILE,
    PDF_FILE,
    PS_FILE
};

int guess_file_type(const char *begin, size_t len, int *startpos)
{
    const char * p, * end;
    p = begin;
    end = begin + len;

    while (p < end)
    {
        p = memchr(p, '%', end - p);
	if (!p)
	    return UNKNOWN_FILE;
	*startpos = p - begin;
	if ((end - p) > 2 && !memcmp(p, "%!", 2))
	    return PS_FILE;
	else if ((end - p) > 7 && !memcmp(p, "%PDF-1.", 7))
	    return PDF_FILE;
	++ p;
    }
    *startpos = 0;
    return UNKNOWN_FILE;
}

/*
 * Prints 'filename'. If 'convert' is true, the file will be converted if it is
 * not postscript or pdf
 */
int print_file(const char *filename, int convert)
{
    FILE *file;
    char buf[8192];
    int type;
    int startpos;
    size_t n;
    FILE *fchandle = NULL;
    int fcpid = 0, ret;

    if (!strcasecmp(filename, "<STDIN>"))
        file = stdin;
    else {
        file = fopen(filename, "r");
        if (!file) {
            _log("Could not open \"%s\" for reading\n", filename);
            return 0;
        }
    }

    n = fread(buf, 1, sizeof(buf) - 1, file);
    buf[n] = '\0';
    type = guess_file_type(buf, n, &startpos);
    /* We do not use any JCL preceeded to the inputr data, as it is simply
       the PJL commands from the PPD file, and these commands we can also
       generate, end we even merge them with PJl from the driver */
    /*if (startpos > 0) {
        jobhasjcl = 1;
        write_output(buf, startpos);
    }*/
    if (file != stdin)
        rewind(file);

    if (convert) pdfconvertedtops = 0;

    switch (type) {
        case PDF_FILE:
            _log("Filetype: PDF\n");

            if (!ppd_supports_pdf())
            {
                char pdf2ps_cmd[PATH_MAX];
                FILE *out, *in;
                int renderer_pid;
		char tmpfilename[PATH_MAX] = "";

                _log("Driver does not understand PDF input, "
                     "converting to PostScript\n");

		pdfconvertedtops = 1;

		/* If reading from stdin, write everything into a temporary file */
		if (file == stdin)
                {
		    int fd;
		    FILE *tmpfile;
		    
		    snprintf(tmpfilename, PATH_MAX, "%s/foomatic-XXXXXX", temp_dir());
		    fd = mkstemp(tmpfilename);
		    if (fd < 0) {
		        _log("Could not create temporary file: %s\n", strerror(errno));
		        return EXIT_PRNERR_NORETRY_BAD_SETTINGS;
		    }
		    tmpfile = fdopen(fd, "r+");
		    copy_file(tmpfile, stdin, buf, n);
		    fclose(tmpfile);
		    
		    filename = tmpfilename;
		}

		/* If the spooler is CUPS we remove the /usr/lib/cups/filter
		   (CUPS filter directory, can be different, but ends with
		   "/cups/filter") which CUPS adds to the beginning of $PATH,
		   so that Poppler's/XPDF's pdftops filter is called and not
		   the one of CUPS, as the one of CUPS has a different command
		   line and does undesired page management operations.
		   The "-dNOINTERPOLATE" makes Ghostscript rendering
		   significantly faster.
		   Note that Ghostscript's "pswrite" output device turns text
		   into bitmaps and therefore produces huge PostScript files.
		   In addition, this output device is deprecated. Therefore
		   we use "ps2write". */
                snprintf(pdf2ps_cmd, PATH_MAX,
			 "%spdftops -level2 -origpagesizes %s - 2>/dev/null || "
			 "gs -q -sstdout=%%stderr -sDEVICE=ps2write -sOutputFile=- "
			 "-dBATCH -dNOPAUSE -dPARANOIDSAFER -dNOINTERPOLATE %s 2>/dev/null",
			 (spooler == SPOOLER_CUPS ?
			  "PATH=${PATH#*/cups/filter:} " : ""),
			 filename, filename);

                renderer_pid = start_system_process("pdf-to-ps", pdf2ps_cmd, &in, &out);

                if (dup2(fileno(out), fileno(stdin)) < 0)
                    rip_die(EXIT_PRNERR_NORETRY_BAD_SETTINGS,
                            "Couldn't dup stdout of pdf-to-ps\n");

                ret = print_file("<STDIN>", 0);

                wait_for_process(renderer_pid);
                return ret;
            }

            if (file == stdin)
                return print_pdf(stdin, buf, n, filename, startpos);
            else
                return print_pdf(file, NULL, 0, filename, startpos);

        case PS_FILE:
            _log("Filetype: PostScript\n");
            if (file == stdin)
                return print_ps(stdin, buf, n, filename);
            else
                return print_ps(file, NULL, 0, filename);

        case UNKNOWN_FILE:
            if (spooler == SPOOLER_CUPS) {
                _log("Cannot process \"%s\": Unknown filetype.\n", filename);
                return 0;
            }

            _log("Filetype unknown, trying to convert ...\n");
            get_fileconverter_handle(buf, &fchandle, &fcpid);

            /* Read further data from the file converter and not from STDIN */
            if (dup2(fileno(fchandle), fileno(stdin)) < 0)
                rip_die(EXIT_PRNERR_NORETRY_BAD_SETTINGS, "Couldn't dup fileconverterhandle\n");

            ret = print_file("<STDIN>", 0);

            if (close_fileconverter_handle(fchandle, fcpid) != EXIT_PRINTED)
                rip_die(ret, "Error closing file converter\n");
            return ret;
    }

    fclose(file);
    return 1;
}

void signal_terminate(int signal)
{
    rip_die(EXIT_PRINTED, "Caught termination signal: Job canceled\n");
}

jobparams_t * create_job()
{
    jobparams_t *job = calloc(1, sizeof(jobparams_t));
    struct passwd *passwd;

    job->optstr = create_dstr();
    job->time = time(NULL);
    strcpy(job->copies, "1");
    gethostname(job->host, 128);
    passwd = getpwuid(getuid());
    if (passwd)
        strlcpy(job->user, passwd->pw_name, 128);
    snprintf(job->title, 128, "%s@%s", job->user, job->host);

    return job;
}

void free_job(jobparams_t *job)
{
    free_dstr(job->optstr);
    free(job);
}

int main(int argc, char** argv)
{
    int i;
    int verbose = 0, quiet = 0, showdocs = 0;
    const char* str;
    char *p, *filename;
    const char *path;
    FILE *genpdqfile = NULL;
    FILE *ppdfh = NULL;
    char tmp[1024], pstoraster[256];
    int havefilter, havepstoraster;
    dstr_t *filelist;
    list_t * arglist;

    arglist = list_create_from_array(argc -1, (void**)&argv[1]);

    if (argc == 2 && (arglist_find(arglist, "--version") || arglist_find(arglist, "--help") ||
                arglist_find(arglist, "-v") || arglist_find(arglist, "-h"))) {
        printf("foomatic rip version "VERSION"\n");
        printf("\"man foomatic-rip\" for help.\n");
        list_free(arglist);
        return 0;
    }

    filelist = create_dstr();
    job = create_job();

    jclprepend = NULL;
    jclappend = create_dstr();
    postpipe = create_dstr();

    options_init();

    signal(SIGTERM, signal_terminate);
    signal(SIGINT, signal_terminate);


    config_from_file(CONFIG_PATH "/filter.conf");

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
        logh = fopen(LOG_FILE ".log", "w"); /* insecure, use for debugging only */
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
        _log("foomatic-rip version "VERSION" running...\n");

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
        strncpy(job->ppdfile, getenv("PPD"), 256);
        spooler = SPOOLER_CUPS;
    }

    if (getenv("SPOOLER_KEY")) {
        spooler = SPOOLER_SOLARIS;
        /* set the printer name from the ppd file name */
        strncpy_omit(job->ppdfile, getenv("PPD"), 256, omit_specialchars);
        file_basename(job->printer, job->ppdfile, 256);
        /* TODO read attribute file*/
    }

    if (getenv("PPR_VERSION"))
        spooler = SPOOLER_PPR;

    if (getenv("PPR_RIPOPTS")) {
        /* PPR 1.5 allows the user to specify options for the PPR RIP with the
           "--ripopts" option on the "ppr" command line. They are provided to
           the RIP via the "PPR_RIPOPTS" environment variable. */
        dstrcatf(job->optstr, "%s ", getenv("PPR_RIPOPTS"));
        spooler = SPOOLER_PPR;
    }

    if (getenv("LPOPTS")) { /* "LPOPTS": Option settings for some LPD implementations (ex: GNUlpr) */
        spooler = SPOOLER_GNULPR;
        dstrcatf(job->optstr, "%s ", getenv("LPOPTS"));
    }

    /* Check for LPRng first so we do not pick up bogus ppd files by the -ppd option */
    if (spooler != SPOOLER_CUPS && spooler != SPOOLER_PPR && 
	spooler != SPOOLER_PPR_INT) {
        if (arglist_remove_flag(arglist, "--lprng"))
            spooler = SPOOLER_LPRNG;
    }

    /* 'PRINTCAP_ENTRY' environment variable is : LPRng
       the :ppd=/path/to/ppdfile printcap entry should be used */
    if (getenv("PRINTCAP_ENTRY")) {
        spooler = SPOOLER_LPRNG;
        if ((str = strstr(getenv("PRINTCAP_ENTRY"), "ppd=")))
            str += 4;
        else if ((str = strstr(getenv("PRINTCAP_ENTRY"), "ppdfile=")))
	    str += 8;
        if (str) {
            while (isspace(*str)) str++;
            p = job->ppdfile;
            while (*str != '\0' && !isspace(*str) && *str != '\n' &&
		   *str != ':') {
                if (isprint(*str) && strchr(shellescapes, *str) == NULL)
                    *p++ = *str;
                str++;
            }
        }
    }

    /* CUPS calls foomatic-rip only with 5 or 6 positional parameters,
       not with named options, like for example "-p <string>". Also PPR
       does not used named options. */
    if (spooler != SPOOLER_CUPS && spooler != SPOOLER_PPR && 
	spooler != SPOOLER_PPR_INT) {
        /* Check for LPD/GNUlpr by typical options which the spooler puts onto
           the filter's command line (options "-w": text width, "-l": text
           length, "-i": indent, "-x", "-y": graphics size, "-c": raw printing,
           "-n": user name, "-h": host name) */
        if ((str = arglist_get_value(arglist, "-h"))) {
            if (spooler != SPOOLER_GNULPR && spooler != SPOOLER_LPRNG)
                spooler = SPOOLER_LPD;
            strncpy(job->host, str, 127);
            job->host[127] = '\0';
            arglist_remove(arglist, "-h");
        }
        if ((str = arglist_get_value(arglist, "-n"))) {
            if (spooler != SPOOLER_GNULPR && spooler != SPOOLER_LPRNG)
                spooler = SPOOLER_LPD;

            strncpy(job->user, str, 127);
            job->user[127] = '\0';
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
            dstrcatf(job->optstr, "%s ", str);
            arglist_remove(arglist, "-Z");
        }
        /* Job title and options for stock LPD */
        if ((str = arglist_get_value(arglist, "-j")) || (str = arglist_get_value(arglist, "-J"))) {
            strncpy_omit(job->title, str, 128, omit_shellescapes);
            if (spooler == SPOOLER_LPD)
                 dstrcatf(job->optstr, "%s ", job->title);
             if (!arglist_remove(arglist, "-j"))
                arglist_remove(arglist, "-J");
        }

        /* Check for CPS */
        if (arglist_remove_flag(arglist, "--cps") > 0)
            spooler = SPOOLER_CPS;

        /* PPD file name given via the command line
           allow duplicates, and use the last specified one */
        if (spooler != SPOOLER_GNULPR && spooler != SPOOLER_LPRNG &&
	    spooler != SPOOLER_LPD) {
            while ((str = arglist_get_value(arglist, "-p"))) {
                strncpy(job->ppdfile, str, 256);
                arglist_remove(arglist, "-p");
            }
	    while ((str = arglist_get_value(arglist, "--ppd"))) {
	        strncpy(job->ppdfile, str, 256);
	        arglist_remove(arglist, "--ppd");
	    }
        }

        /* Options for spooler-less printing, CPS, or PDQ */
        while ((str = arglist_get_value(arglist, "-o"))) {
            strncpy_omit(tmp, str, 1024, omit_shellescapes);
            dstrcatf(job->optstr, "%s ", tmp);
            arglist_remove(arglist, "-o");
            /* If we don't print as PPR RIP or as CPS filter, we print
               without spooler (we check for PDQ later) */
            if (spooler != SPOOLER_PPR && spooler != SPOOLER_CPS)
                spooler = SPOOLER_DIRECT;
        }

        /* Printer for spooler-less printing or PDQ */
        if ((str = arglist_get_value(arglist, "-d"))) {
            strncpy_omit(job->printer, str, 256, omit_shellescapes);
            arglist_remove(arglist, "-d");
        }

        /* Printer for spooler-less printing, PDQ, or LPRng */
        if ((str = arglist_get_value(arglist, "-P"))) {
            strncpy_omit(job->printer, str, 256, omit_shellescapes);
            arglist_remove(arglist, "-P");
        }

        /* Were we called from a PDQ wrapper? */
        if (arglist_remove_flag(arglist, "--pdq"))
            spooler = SPOOLER_PDQ;

        /* Were we called to build the PDQ driver declaration file? */
        genpdqfile = check_pdq_file(arglist);
        if (genpdqfile)
            spooler = SPOOLER_PDQ;
    }

    /* spooler specific initialization */
    switch (spooler) {
        case SPOOLER_PPR:
            init_ppr(arglist, job);
            break;

        case SPOOLER_CUPS:
            init_cups(arglist, filelist, job);
            break;

        case SPOOLER_LPRNG:
	    if (job->ppdfile[0] != '\0') break;
        case SPOOLER_LPD:
        case SPOOLER_GNULPR:
            /* Get PPD file name as the last command line argument */
            if (arglist->last)
                strncpy(job->ppdfile, (char*)arglist->last->data, 256);
            break;

        case SPOOLER_DIRECT:
        case SPOOLER_CPS:
        case SPOOLER_PDQ:
            init_direct_cps_pdq(arglist, filelist, job);
            break;
    }

    /* Files to be printed (can be more than one for spooler-less printing) */
    /* Empty file list -> print STDIN */
    dstrtrim(filelist);
    if (filelist->len == 0)
        dstrcpyf(filelist, "<STDIN>");

    /* Check filelist */
    p = strtok(strdup(filelist->data), " ");
    while (p) {
        if (strcmp(p, "<STDIN>") != 0) {
            if (p[0] == '-')
                rip_die(EXIT_PRNERR_NORETRY_BAD_SETTINGS, "Invalid argument: %s", p);
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
        _log("foomatic-rip version " VERSION " running...\n");
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
    if (!(ppdfh = fopen(job->ppdfile, "r")))
        rip_die(EXIT_PRNERR_NORETRY_BAD_SETTINGS, "Unable to open PPD file %s\n", job->ppdfile);

    read_ppd_file(job->ppdfile);

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
                /* gstoraster is the new name for pstoraster */
                strlcat(tmp, "/gstoraster", 1024);
                if (access(tmp, X_OK) == 0) {
                    havepstoraster = 1;
                    strlcpy(pstoraster, tmp, 256);
                    strlcat(pstoraster, " 0 '' '' 0 '%X'", 256);
                    break;
                }
            }
            if (!havepstoraster) {
                const char **qualifier = NULL;
                const char *icc_profile = NULL;

                qualifier = get_ppd_qualifier();
                _log("INFO: Using qualifer: '%s.%s.%s'\n",
                      qualifier[0], qualifier[1], qualifier[2]);

#ifdef HAVE_DBUS
                /* ask colord for the profile */
                icc_profile = colord_get_profile_for_device_id ((const char *) getenv("PRINTER"),
                                                                qualifier);
#endif

                /* fall back to PPD */
                if (icc_profile == NULL) {
                  _log("INFO: need to look in PPD for matching qualifer\n");
                  icc_profile = get_icc_profile_for_qualifier(qualifier);
                }

                if (icc_profile != NULL)
                  snprintf(cmd, sizeof(cmd),
                           "-sOutputICCProfile='%s'", icc_profile);
                else
                  cmd[0] = '\0';

                snprintf(pstoraster, sizeof(pstoraster), "gs -dQUIET -dDEBUG -dPARANOIDSAFER -dNOPAUSE -dBATCH -dNOINTERPOLATE -dNOMEDIAATTRS -sDEVICE=cups %s -sOutputFile=- -", cmd);
            }

            /* build Ghostscript/CUPS driver command line */
            snprintf(cmd, 1024, "%s | %s", pstoraster, cupsfilter);
            _log("INFO: Using command line: %s\n", cmd);

            /* Set environment variables */
            setenv("PPD", job->ppdfile, 1);
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
        spooler_name(spooler), job->printer, get_modern_shell(), job->ppdfile, attrpath, printer_model);
    /* Print the options string only in debug mode, Mac OS X adds very many
       options so that CUPS cannot handle the output of the option string
       in its log files. If CUPS encounters a line with more than 1024 characters
       sent into its log files, it aborts the job with an error.*/
    if (debug || spooler != SPOOLER_CUPS)
        _log("Options: %s\n", job->optstr->data);
    _log("Job title: %s\n", job->title);
    _log("File(s) to be printed:\n");
    _log("%s\n\n", filelist->data);
    if (getenv("GS_LIB"))
        _log("Ghostscript extra search path ('GS_LIB'): %s\n", getenv("GS_LIB"));

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
        if (access(tmp, X_OK) != 0)
            rip_die(EXIT_PRNERR_NORETRY_BAD_SETTINGS, "The backend interface "
                    "/interfaces/%s does not exist/ is not executable!\n", backend);

        /* foomatic-rip cannot use foomatic-rip as backend */
        if (!strcmp(backend, "foomatic-rip"))
            rip_die(EXIT_PRNERR_NORETRY_BAD_SETTINGS, "\"foomatic-rip\" cannot "
                    "use itself as backend interface!\n");

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
        run_system_process("reset-file", "> " LOG_FILE ".ps");

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
            run_system_process("raw-printer", tmp);
            continue;
        }

        /* First, for arguments with a default, stick the default in as
           the initial value for the "header" option set, this option set
           consists of the PPD defaults, the options specified on the
           command line, and the options set in the header part of the
           PostScript file (all before the first page begins). */
        optionset_copy_values(optionset("userval"), optionset("header"));

        if (!print_file(filename, 1))
	    rip_die(EXIT_PRNERR_NORETRY, "Could not print file %s\n", filename);
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
    free_job(job);
    if (genpdqfile && genpdqfile != stdout)
        fclose(genpdqfile);
    free_dstr(filelist);
    options_free();
    close_log();

    argv_free(jclprepend);
    free_dstr(jclappend);
    if (backendoptions)
        free_dstr(backendoptions);

    list_free(arglist);

    return EXIT_PRINTED;
}

