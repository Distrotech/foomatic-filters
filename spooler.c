/* spooler.c
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

#include "spooler.h"
#include "foomaticrip.h"
#include "util.h"
#include "options.h"
#include <stdlib.h>
#include <unistd.h>

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

/*  This piece of PostScript code (initial idea 2001 by Michael
    Allerhand (michael.allerhand at ed dot ac dot uk, vastly
    improved by Till Kamppeter in 2002) lets Ghostscript output
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
    "{                    % in case of level 2 or higher\n"
    "    currentglobal true setglobal    % define a dictioary foomaticDict\n"
    "    globaldict begin        % in global VM and establish a\n"
    "    /foomaticDict            % pages count key there\n"
    "    <<\n"
    "        /PhysPages 0\n"
    "    >>def\n"
    "    end\n"
    "    setglobal\n"
    "}if\n"
    "\n"
    "/cupsGetNumCopies { % Read the number of Copies requested for the current\n"
    "            % page\n"
    "    cupsPSLevel2\n"
    "    {\n"
    "    % PS Level 2+: Get number of copies from Page Device dictionary\n"
    "    currentpagedevice /NumCopies get\n"
    "    }\n"
    "    {\n"
    "    % PS Level 1: Number of copies not in Page Device dictionary\n"
    "    null\n"
    "    }\n"
    "    ifelse\n"
    "    % Check whether the number is defined, if it is \"null\" use #copies \n"
    "    % instead\n"
    "    dup null eq {\n"
    "    pop #copies\n"
    "    }\n"
    "    if\n"
    "    % Check whether the number is defined now, if it is still \"null\" use 1\n"
    "    % instead\n"
    "    dup null eq {\n"
    "    pop 1\n"
    "    } if\n"
    "} bind def\n"
    "\n"
    "/cupsWrite { % write a string onto standard error\n"
    "    (%stderr) (w) file\n"
    "    exch writestring\n"
    "} bind def\n"
    "\n"
    "/cupsFlush    % flush standard error to make it sort of unbuffered\n"
    "{\n"
    "    (%stderr)(w)file flushfile\n"
    "}bind def\n"
    "\n"
    "cupsPSLevel2\n"
    "{                % In language level 2, we try to do something reasonable\n"
    "  <<\n"
    "    /EndPage\n"
    "    [                    % start the array that becomes the procedure\n"
    "      currentpagedevice/EndPage 2 copy known\n"
    "      {get}                    % get the existing EndPage procedure\n"
    "      {pop pop {exch pop 2 ne}bind}ifelse    % there is none, define the default\n"
    "      /exec load                % make sure it will be executed, whatever it is\n"
    "      /dup load                    % duplicate the result value\n"
    "      {                    % true: a sheet gets printed, do accounting\n"
    "        currentglobal true setglobal        % switch to global VM ...\n"
    "        foomaticDict begin            % ... and access our special dictionary\n"
    "        PhysPages 1 add            % count the sheets printed (including this one)\n"
    "        dup /PhysPages exch def        % and save the value\n"
    "        end                    % leave our dict\n"
    "        exch setglobal                % return to previous VM\n"
    "        (PAGE: )cupsWrite             % assemble and print the accounting string ...\n"
    "        16 string cvs cupsWrite            % ... the sheet count ...\n"
    "        ( )cupsWrite                % ... a space ...\n"
    "        cupsGetNumCopies             % ... the number of copies ...\n"
    "        16 string cvs cupsWrite            % ...\n"
    "        (\\n)cupsWrite                % ... a newline\n"
    "        cupsFlush\n"
    "      }/if load\n"
    "                    % false: current page gets discarded; do nothing    \n"
    "    ]cvx bind                % make the array executable and apply bind\n"
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


void init_ppr(list_t *arglist, jobparams_t *job)
{
    size_t arg_count = list_item_count(arglist);
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
    if ((arg_count == 11 || arg_count == 10 || arg_count == 8) &&
        atoi(arglist_get(arglist, 3)) < 100 && atoi(arglist_get(arglist, 5)) < 100)
    {
        /* get all command line parameters */
        strncpy_omit(ppr_printer, arglist_get(arglist, 0), 256, omit_shellescapes);
        strlcpy(ppr_address, arglist_get(arglist, 1), 128);
        strncpy_omit(ppr_options, arglist_get(arglist, 2), 1024, omit_shellescapes);
        strlcpy(ppr_jobbreak, arglist_get(arglist, 3), 128);
        strlcpy(ppr_feedback, arglist_get(arglist, 4), 128);
        strlcpy(ppr_codes, arglist_get(arglist, 5), 128);
        strncpy_omit(ppr_jobname, arglist_get(arglist, 6), 128, omit_shellescapes);
        strncpy_omit(ppr_routing, arglist_get(arglist, 7), 128, omit_shellescapes);
        if (arg_count >= 8) {
            strlcpy(ppr_for, arglist_get(arglist, 8), 128);
            strlcpy(ppr_filetype, arglist_get(arglist, 9), 128);
            if (arg_count >= 10)
                strncpy_omit(ppr_filetoprint, arglist_get(arglist, 10), 128, omit_shellescapes);
        }

        /* Common job parameters */
        strcpy(job->printer, ppr_printer);
        strcpy(job->title, ppr_jobname);
        if (isempty(job->title) && !isempty(ppr_filetoprint))
            strcpy(job->title, ppr_filetoprint);
        dstrcatf(job->optstr, " %s %s", ppr_options, ppr_routing);

        /* Get the path of the PPD file from the queue configuration */
        snprintf(tmp, 255, "LANG=en_US; ppad show %s | grep PPDFile", ppr_printer);
        tmp[255] = '\0';
        ph = popen(tmp, "r");
        if (ph) {
            fgets(tmp, 255, ph);
            tmp[255] = '\0';
            pclose(ph);

            strncpy_omit(job->ppdfile, tmp, 255, omit_shellescapes);
            if (job->ppdfile[0] == '/') {
                strcpy(tmp, job->ppdfile);
                strcpy(job->ppdfile, "../../share/ppr/PPDFiles/");
                strncat(job->ppdfile, tmp, 200);
            }
            if ((p = strrchr(job->ppdfile, '\n')))
                *p = '\0';
        }
        else {
            job->ppdfile[0] = '\0';
        }

        /* We have PPR and run as an interface */
        spooler = SPOOLER_PPR_INT;
    }
}

void init_cups(list_t *arglist, dstr_t *filelist, jobparams_t *job)
{
    char path [1024] = "";
    char cups_jobid [128];
    char cups_user [128];
    char cups_jobtitle [128];
    char cups_copies [128];
    char cups_options [512];
    char cups_filename [256];
    char texttopspath[PATH_MAX];

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
    setenv("GS_LIB", path, 1);

    /* Get all command line parameters */
    strncpy_omit(cups_jobid, arglist_get(arglist, 0), 128, omit_shellescapes);
    strncpy_omit(cups_user, arglist_get(arglist, 1), 128, omit_shellescapes);
    strncpy_omit(cups_jobtitle, arglist_get(arglist, 2), 128, omit_shellescapes);
    strncpy_omit(cups_copies, arglist_get(arglist, 3), 128, omit_shellescapes);
    strncpy_omit(cups_options, arglist_get(arglist, 4), 512, omit_shellescapes);

    /* Common job parameters */
    strcpy(job->id, cups_jobid);
    strcpy(job->title, cups_jobtitle);
    strcpy(job->user, cups_user);
    strcpy(job->copies, cups_copies);
    dstrcatf(job->optstr, " %s", cups_options);

    /* Check for and handle inputfile vs stdin */
    if (list_item_count(arglist) > 4) {
        strncpy_omit(cups_filename, arglist_get(arglist, 5), 256, omit_shellescapes);
        if (cups_filename[0] != '-') {
            /* We get input from a file */
            dstrcatf(filelist, "%s ", cups_filename);
            _log("Getting input from file %s\n", cups_filename);
        }
    }

    accounting_prolog = accounting_prolog_code;

    /* On which queue are we printing?
       CUPS gives the PPD file the same name as the printer queue,
       so we can get the queue name from the name of the PPD file. */
    file_basename(job->printer, job->ppdfile, 256);

    /* Use cups' texttops if no fileconverter is set
     * Apply "pstops" when having used a file converter under CUPS, so CUPS
     * can stuff the default settings into the PostScript output of the file
     * converter (so all CUPS settings get also applied when one prints the
     * documentation pages (all other files we get already converted to
     * PostScript by CUPS). */
    if (isempty(fileconverter)) {
        if (find_in_path("texttops", cupsfilterpath, texttopspath)) {
            snprintf(fileconverter, PATH_MAX, "%s/texttops '%s' '%s' '%s' '%s' "
                    "page-top=36 page-bottom=36 page-left=36 page-right=36 "
                    "nolandscape cpi=12 lpi=7 columns=1 wrap %s'"
                    "| %s/pstops  '%s' '%s' '%s' '%s' '%s'",
                    texttopspath, cups_jobid, cups_user, cups_jobtitle, cups_copies, cups_options,
                    texttopspath, cups_jobid, cups_user, cups_jobtitle, cups_copies, cups_options);
        }
    }
}

void init_solaris(list_t *arglist, dstr_t *filelist, jobparams_t *job)
{
    char *str;
    int len;
    listitem_t *i;

    /* Get all command line parameters */
    strncpy_omit(job->title, arglist_get(arglist, 2), 128, omit_shellescapes);

    len = strlen(arglist_get(arglist, 4));
    str = malloc(len +1);
    strncpy_omit(str, arglist_get(arglist, 4), len, omit_shellescapes);
    dstrcatf(job->optstr, " %s", str);
    free(str);

    for (i = arglist->first; i; i = i->next)
        dstrcatf(filelist, "%s ", (char*)i->data);
}

/* used by init_direct_cps_pdq to find a ppd file */
int find_ppdfile(const char *user_default_path, jobparams_t *job)
{
    /* Search also common spooler-specific locations, this way a printer
       configured under a certain spooler can also be used without spooler */

    strcpy(job->ppdfile, job->printer);
    if (access(job->ppdfile, R_OK) == 0)
        return 1;

    /* CPS can have the PPD in the spool directory */
    if (spooler == SPOOLER_CPS) {
        snprintf(job->ppdfile, 256, "/var/spool/lpd/%s/%s.ppd", job->printer, job->printer);
        if (access(job->ppdfile, R_OK) == 0)
            return 1;
        snprintf(job->ppdfile, 256, "/var/local/spool/lpd/%s/%s.ppd", job->printer, job->printer);
        if (access(job->ppdfile, R_OK) == 0)
            return 1;
        snprintf(job->ppdfile, 256, "/var/local/lpd/%s/%s.ppd", job->printer, job->printer);
        if (access(job->ppdfile, R_OK) == 0)
            return 1;
        snprintf(job->ppdfile, 256, "/var/spool/lpd/%s.ppd", job->printer);
        if (access(job->ppdfile, R_OK) == 0)
            return 1;
        snprintf(job->ppdfile, 256, "/var/local/spool/lpd/%s.ppd", job->printer);
        if (access(job->ppdfile, R_OK) == 0)
            return 1;
        snprintf(job->ppdfile, 256, "/var/local/lpd/%s.ppd", job->printer);
        if (access(job->ppdfile, R_OK) == 0)
            return 1;
    }
    snprintf(job->ppdfile, 256, "%s.ppd", job->printer); /* current dir */
    if (access(job->ppdfile, R_OK) == 0)
        return 1;
    snprintf(job->ppdfile, 256, "%s/%s.ppd", user_default_path, job->printer); /* user dir */
    if (access(job->ppdfile, R_OK) == 0)
        return 1;
    snprintf(job->ppdfile, 256, "%s/direct/%s.ppd", CONFIG_PATH, job->printer); /* system dir */
    if (access(job->ppdfile, R_OK) == 0)
        return 1;
    snprintf(job->ppdfile, 256, "%s/%s.ppd", CONFIG_PATH, job->printer); /* system dir */
    if (access(job->ppdfile, R_OK) == 0)
        return 1;
    snprintf(job->ppdfile, 256, "/etc/cups/ppd/%s.ppd", job->printer); /* CUPS config dir */
    if (access(job->ppdfile, R_OK) == 0)
        return 1;
    snprintf(job->ppdfile, 256, "/usr/local/etc/cups/ppd/%s.ppd", job->printer); /* CUPS config dir */
    if (access(job->ppdfile, R_OK) == 0)
        return 1;
    snprintf(job->ppdfile, 256, "/usr/share/ppr/PPDFiles/%s.ppd", job->printer); /* PPR PPDs */
    if (access(job->ppdfile, R_OK) == 0)
        return 1;
    snprintf(job->ppdfile, 256, "/usr/local/share/ppr/PPDFiles/%s.ppd", job->printer); /* PPR PPDs */
    if (access(job->ppdfile, R_OK) == 0)
        return 1;

    /* nothing found */
    job->ppdfile[0] = '\0';
    return 0;
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
                strncpy_omit(dest, p + 1, destsize, omit_whitespace_newline);
                if (dest[0])
                    break;
            }
        }
    }
    fclose(fh);
    return dest[0] != '\0';
}

/* tries to find a default printer name in various config files and copies the
 * result into the global var 'printer'. Returns success */
int find_default_printer(const char *user_default_path, jobparams_t *job)
{
    char configfile [1024];
    char *key = "default";

    if (configfile_find_option("./.directconfig", key, job->printer, 256))
        return 1;
    if (configfile_find_option("./directconfig", key, job->printer, 256))
        return 1;
    if (configfile_find_option("./.config", key, job->printer, 256))
        return 1;
    strlcpy(configfile, user_default_path, 1024);
    strlcat(configfile, "/direct/.config", 1024);
    if (configfile_find_option(configfile, key, job->printer, 256))
        return 1;
    strlcpy(configfile, user_default_path, 1024);
    strlcat(configfile, "/direct.conf", 1024);
    if (configfile_find_option(configfile, key, job->printer, 256))
        return 1;
    if (configfile_find_option(CONFIG_PATH "/direct/.config", key, job->printer, 256))
        return 1;
    if (configfile_find_option(CONFIG_PATH "/direct.conf", key, job->printer, 256))
        return 1;

    return 0;
}

void init_direct_cps_pdq(list_t *arglist, dstr_t *filelist, jobparams_t *job)
{
    char tmp [1024];
    listitem_t *i;
    char user_default_path [PATH_MAX];

    strlcpy(user_default_path, getenv("HOME"), 256);
    strlcat(user_default_path, "/.foomatic/", 256);

    /* Which files do we want to print? */
    for (i = arglist->first; i; i = i->next) {
        strncpy_omit(tmp, (char*)i->data, 1024, omit_shellescapes);
        dstrcatf(filelist, "%s ", tmp);
    }

    if (job->ppdfile[0] == '\0') {
        if (job->printer[0] == '\0') {
            /* No printer definition file selected, check whether we have a
               default printer defined */
            find_default_printer(user_default_path, job);
        }

        /* Neither in a config file nor on the command line a printer was selected */
        if (!job->printer[0]) {
            _log("No printer definition (option \"-P <name>\") specified!\n");
            exit(EXIT_PRNERR_NORETRY_BAD_SETTINGS);
        }

        /* Search for the PPD file */
        if (!find_ppdfile(user_default_path, job)) {
            _log("There is no readable PPD file for the printer %s, is it configured?\n", job->printer);
            exit(EXIT_PRNERR_NORETRY_BAD_SETTINGS);
        }
    }
}

