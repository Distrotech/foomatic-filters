# shfuncs : test suite common shell functions

##########################################################################
# (C) Copyright 1998-2001 The Open Group
#
# All rights reserved.  No part of this source code may be reproduced,
# stored in a retrieval system, or transmitted, in any form or by any
# means, electronic, mechanical, photocopying, recording or otherwise,
# except as stated in the end-user licence agreement, without the prior
# permission of the copyright owners.
#
# X/Open and the 'X' symbol are trademarks of X/Open Company Limited in
# the UK and other countries.
#
#	PROJECT:	LSB-FHS
#	PRODUCT:	LSB.fhs/SRC/common/lsblib/shfuncs.sh
#	AUTHOR:		Andrew Josey, The Open Group
#	DATE CREATED:	21 Dec 1998
#
#	Derived from the TET demo test suites
##########################################################################

# This is $Revision: 1.2 $
#
# $Log: shfuncs.sh,v $
# Revision 1.2  2001/07/18 06:58:55  ajosey
# add header, and cvs revision stuff
#


tpstart() # write test purpose banner and initialise variables
{
    tet_infoline "$*"
    FAIL=N
}

tpresult() # give test purpose result
{
    # $1 is result code to give if FAIL=N (default PASS)
    if [ $FAIL = N ]
    then
	tet_result ${1-PASS}
    else
	tet_result FAIL
    fi
}

check_exit() # execute command (saving output) and check exit code
{
    # $1 is command, $2 is expected exit code (0 or "N" for non-zero)
    eval "$1" > out.stdout 2> out.stderr
    CODE=$?
    if [ $2 = 0 -a $CODE -ne 0 ]
    then
	tet_infoline "Command ($1) gave exit code $CODE, expected 0"
	FAIL=Y
    elif [ $2 != 0 -a $CODE -eq 0 ]
    then
	tet_infoline "Command ($1) gave exit code $CODE, expected non-zero"
	FAIL=Y
    fi
}

check_exit_value() # check that $1 equates $2
{
    CODE=$1
    if [ $2 = 0 -a $CODE -ne 0 ]
    then
	tet_infoline "exit code $CODE returned, expected 0"
	FAIL=Y
    elif [ $2 != 0 -a $CODE -eq 0 ]
    then
	tet_infoline "exit code $CODE returned, expected non-zero"
	FAIL=Y
    fi
}

check_prep_exit_value() # check that $2 equates $3
{
    CODE=$2
    if [ $3 = 0 -a $CODE -ne 0 ]
    then
	tet_infoline "$1 command returned exit code $CODE, expected 0"
	FAIL=Y
    elif [ $3 != 0 -a $CODE -eq 0 ]
    then
	tet_infoline "$1 command returned exit code $CODE, expected non-zero"
	FAIL=Y
    fi
}

check_nostdout() # check that nothing went to stdout
{
    if [ -s out.stdout ]
    then
	tet_infoline "Unexpected output written to stdout, as shown below:"
	infofile out.stdout stdout:
	FAIL=Y
    fi
}

check_nostderr() # check that nothing went to stderr
{
    if [ -s out.stderr ]
    then
	tet_infoline "Unexpected output written to stderr, as shown below:"
	infofile out.stderr stderr:
	FAIL=Y
    fi
}

check_stdout() # check that a string went to stdout
{
    case $1 in
    "") 
	if [ ! -s out.stdout ]
	then
	    tet_infoline "Expected output to stdout, but none written"
	    FAIL=Y
	fi
	;;
    *)	
	grep "$1" out.stdout  2>&1 >/dev/null
	if [ $? -ne 0  ]
	then
		tet_infoline "Output written to stdout did not contain \"$1\", got below:"
		infofile out.stdout stdout:
		FAIL=Y
	fi
	;;
    esac
}

check_stderr() # check that stderr matches expected error
{
    # $1 is file containing regexp for expected error
    # if no argument supplied, just check out.stderr is not empty

    case $1 in
    "")
	if [ ! -s out.stderr ]
	then
	    tet_infoline "Expected output to stderr, but none written"
	    FAIL=Y
	fi
	;;
    *)
	expfile="$1"
	OK=Y
	exec 4<&0 0< "$expfile" 3< out.stderr
	while read expline
	do
	    if read line <&3
	    then
		if expr "$line" : "$expline" > /dev/null
		then
		    :
		else
		    OK=N
		    break
		fi
	    else
		OK=N
	    fi
	done
	exec 0<&4 3<&- 4<&-
	if [ $OK = N ]
	then
	    tet_infoline "Incorrect output written to stderr, as shown below"
	    infofile "$expfile" "expected stderr:"
	    infofile out.stderr "received stderr:"
	    FAIL=Y
	fi
	;;
    esac
}

infofile() # write file to journal using tet_infoline
{
    # $1 is file name, $2 is prefix for tet_infoline

    prefix=$2
    while read line
    do
	tet_infoline "$prefix$line"
    done < $1
}
