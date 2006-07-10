dnl Process this file with autoconf to produce a configure script.
AC_INIT(foomatic-rip.in)

dnl If the user didn't specify the $sysconfdir on the command line, let it
dnl be /etc, not /usr/local/etc or /usr/etc
dnl if [[ x$sysconfdir = 'x${prefix}/etc' ]]; then
dnl   sysconfdir=/etc
dnl fi

dnl Checks for programs.
AC_PROG_INSTALL
AC_PROG_LN_S
AC_PROG_MAKE_SET

ETCSEARCHPATH=${prefix}/etc:/usr/etc:/usr/local/etc
LIBSEARCHPATH=${prefix}/lib:/usr/lib:/usr/local/lib:${prefix}/libexec:/usr/libexec:/usr/local/libexec

dnl Get the operating system and version number...
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

dnl Check for /etc/printcap
AC_PATH_DIR(PRINTCAP,printcap,/etc/printcap,$ETCSEARCHPATH)

dnl Check for /usr/lib/cups
AC_PATH_DIR(CUPS,cups,/usr/lib/cups,$LIBSEARCHPATH)

dnl Check for /usr/lib/cups/filter
AC_PATH_DIR(CUPS_FILTERS,cups/filter,/usr/lib/cups/filter,$LIBSEARCHPATH)

dnl Check for /usr/lib/cups/backend
AC_PATH_DIR(CUPS_BACKENDS,cups/backend,/usr/lib/cups/backend,$LIBSEARCHPATH)

dnl Check for /usr/lib/ppr
AC_PATH_DIR(PPR,ppr,/usr/lib/ppr,$LIBSEARCHPATH)

dnl Check for /usr/lib/ppr/interfaces
AC_PATH_DIR(PPR_INTERFACES,ppr/interfaces,/usr/lib/ppr/interfaces,$LIBSEARCHPATH)

dnl Check for /usr/lib/ppr/lib
AC_PATH_DIR(PPR_LIB,ppr/lib,/usr/lib/ppr/lib,$LIBSEARCHPATH)

AC_ARG_ENABLE(file-converter-check, [  --enable-file-converter-check  check whether a file converter is installed [default=yes]],
[if eval "test x${enableval} = xno"; then
  NOCONVERTERCHECK=1
  AC_MSG_WARN([Absence of a file converter is ignored, package will be built anyway])
fi])

dnl Check for a2ps, enscript, mpage
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

AC_OUTPUT(Makefile beh \
	  foomatic-rip foomatic-gswrapper \
	  foomatic-rip.1 foomatic-gswrapper.1)

dnl Finished
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

