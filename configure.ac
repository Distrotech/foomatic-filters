#                                               -*- Autoconf -*-
# Process this file with autoconf to produce a configure script.

AC_PREREQ(2.61)
AC_INIT(foomatic-rip, "4.0.6")
AM_INIT_AUTOMAKE([no-define])
AC_CONFIG_SRCDIR([options.c])

AC_DEFINE_UNQUOTED(VERSION, "`cat VERSION.full 2> /dev/null`", [Foomatic version])


# Checks for programs.
AC_PROG_CC
AC_PROG_CXX
AC_PROG_INSTALL
AC_PROG_LN_S
AC_PROG_MAKE_SET

# Checks for libraries.
AC_CHECK_LIB(m, roundf)

# Checks for header files.
AC_HEADER_STDC
AC_HEADER_SYS_WAIT
AC_CHECK_HEADERS([fcntl.h memory.h stddef.h stdlib.h string.h strings.h unistd.h])

# Checks for typedefs, structures, and compiler characteristics.
AC_C_CONST
AC_C_INLINE
AC_TYPE_PID_T
AC_TYPE_SIZE_T
AC_TYPE_SSIZE_T
AC_STRUCT_TM

# Checks for library functions.
AC_FUNC_FORK
AC_FUNC_MALLOC
AC_FUNC_REALLOC
AC_TYPE_SIGNAL
AC_FUNC_STRTOD
AC_FUNC_VPRINTF
AC_CHECK_FUNCS([dup2 getcwd gethostname regcomp setenv strcasecmp strchr strcspn strdup strncasecmp strndup strrchr strstr strcasestr strtol])


ETCSEARCHPATH=${prefix}/etc:/usr/etc:/usr/local/etc
LIBSEARCHPATH=${prefix}/lib:${prefix}/lib64:/usr/lib:/usr/lib64:/usr/local/lib:${prefix}/libexec:/usr/libexec:/usr/local/libexec

uname=`uname`
uversion=`uname -r | sed -e '1,$s/[[^0-9]]//g'`
if test x$uname = xIRIX64; then
  uname="IRIX"
fi

if test x$uname = xIRIX; then
  ETCSEARCHPATH="/usr/freeware/etc:$ETCSEARCHPATH"
  LIBSEARCHPATH="/usr/freeware/lib32:/usr/freeware/lib:$LIBSEARCHPATH"
fi

dnl Check for Perl
AC_PATH_PROG(PERL,perl)

if test -z "$PERL"; then
	AC_MSG_ERROR([Perl 5 is required to build and run]);
fi

# Check for /etc/printcap
AC_PATH_DIR(PRINTCAP,printcap,/etc/printcap,$ETCSEARCHPATH)

dnl Check for cups-config
AC_PATH_PROG(CUPS_CONFIG,cups-config)

dnl Use cups-config to find CUPS directories
if test -n "$CUPS_CONFIG" && test -z "$CUPS_SERVERBIN"; then
	CUPS_SERVERBIN=`cups-config --serverbin 2>/dev/null`
fi
if test -n "$CUPS_SERVERBIN"; then
   	CUPS=$CUPS_SERVERBIN
	CUPS_FILTERS="$CUPS_SERVERBIN/filter"
	CUPS_BACKENDS="$CUPS_SERVERBIN/backend"
	AC_SUBST(CUPS)
	AC_SUBST(CUPS_FILTERS)
	AC_SUBST(CUPS_BACKENDS)
else
	# Check for /usr/lib/cups
	AC_PATH_DIR(CUPS,cups,/usr/lib/cups,$SERVERBIB$LIBSEARCHPATH)

	# Check for /usr/lib/cups/filter
	AC_PATH_DIR(CUPS_FILTERS,cups/filter,/usr/lib/cups/filter,$FILTERS$LIBSEARCHPATH)

	# Check for /usr/lib/cups/backend
	AC_PATH_DIR(CUPS_BACKENDS,cups/backend,/usr/lib/cups/backend,$BACKENDS$LIBSEARCHPATH)
fi

# Check for /usr/lib/ppr
AC_PATH_DIR(PPR,ppr,/usr/lib/ppr,$LIBSEARCHPATH)

# Check for /usr/lib/ppr/interfaces
AC_PATH_DIR(PPR_INTERFACES,ppr/interfaces,/usr/lib/ppr/interfaces,$LIBSEARCHPATH)

# Check for /usr/lib/ppr/lib
AC_PATH_DIR(PPR_LIB,ppr/lib,/usr/lib/ppr/lib,$LIBSEARCHPATH)

AC_ARG_ENABLE(file-converter-check, [  --enable-file-converter-check  check whether a file converter is installed [default=yes]],
[if eval "test x${enableval} = xno"; then
  NOCONVERTERCHECK=1
  AC_MSG_WARN([Absence of a file converter is ignored, package will be built anyway])
fi])

# Check for a2ps, enscript, mpage
AC_PATH_PROG(A2PS,a2ps)
AC_PATH_PROG(ENSCRIPT,enscript)
AC_PATH_PROG(MPAGE,mpage)
AC_PATH_PROG(TEXTTOPS,texttops,,$CUPS_FILTERS)

AC_ARG_WITH(file-converter,[  --with-file-converter=[a2ps|enscript|mpage]
			Default: auto],
	FILECONVERTER="$withval",
	FILECONVERTER="")
AC_MSG_RESULT([file-converter: $FILECONVERTER])
AC_SUBST(FILECONVERTER)

if test "${NOCONVERTERCHECK}" = "" -a "${A2PS}" = "" -a "${ENSCRIPT}" = "" -a "${MPAGE}" = "" -a "${TEXTTOPS}" = "" ; then
	AC_MSG_ERROR([cannot find a2ps, enscript, mpage, or CUPS' texttops.  You need to have at least one installed]);
fi

AC_ARG_WITH(echo,[  --with-echo=(path to GNU compatible echo command)
			Default: echo],
	ECHO="$withval",
	ECHO="echo")
AC_MSG_RESULT([GNU Compatible Echo: $ECHO])
AC_SUBST(ECHO)

AC_ARG_WITH(execpath,[  --with-execpath=(path to various filters)
			Default: ${prefix}/bin:/usr/local/bin:/usr/bin:/bin],
	EXECPATH="$withval",
	EXECPATH="${prefix}/bin:/usr/local/bin:/usr/bin:/bin")
AC_MSG_RESULT([Execpath: $EXECPATH])
AC_SUBST(EXECPATH)

AC_CONFIG_FILES([Makefile beh foomatic-rip.1])
AC_CONFIG_HEADER(config.h)
AC_OUTPUT

# Finished
cat <<EOF
Finished configuring.
Type 'make' to build the package
Type 'make install' to install the base foomatic bits and links appropriate for
				CUPS and PPR to use foomatic
Type 'make install-main' to install the base foomatic bits.
Type 'make install-{cups|ppr}' to install the base foomatic bits and links
				appropriate for CUPS or PPR to use foomatic
Type 'make tests' to run some basic sanity tests (in place)
EOF

