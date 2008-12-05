# lsbfuncs.sh : LSB test suite specific common shell functions
#
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
#	PRODUCT:	LSB.fhs/SRC/lsblib/lsbfuncs.sh
#	AUTHOR:		Andrew Josey, The Open Group
#	DATE CREATED:	21 Dec 1998
##########################################################################

# This is $Revision: 1.6 $
#
# $Log: lsbfuncs.sh,v $
# Revision 1.6  2002/01/17 02:21:24  cyeoh
# modifies lsb_test_symlink soboth symlink "from" and "to" are followed
# for symlinks. This changes the behaviour such that if the "to" is
# a symlink but "from" does point to what "to" points to then it returns
# true.
#
# Revision 1.5  2001/07/20 11:49:47  ajosey
# add further diagnostics
#
# Revision 1.4  2001/07/20 11:27:33  ajosey
# add in more diagnostics to lsb_test_file
#
# Revision 1.3  2001/07/20 11:20:08  ajosey
# add in diagnostic to lsb_test_file
#
# Revision 1.2  2001/07/18 06:58:55  ajosey
# add header, and cvs revision stuff
#
#
#Current contents
#	lsb_setarch()
#	lsb_test_symlink()
#	lsb_test_dir()
#	lsb_test_dir_searchable()
#	lsb_test_file()
#	lsb_test_exec
#	lsb_test_device_by_name()
# These functions return the following codes
#
# 	PASS=0
# 	FAIL=1
# 	WARNING=2
# 	UNRESOLVED=3
#

# lsb_setarch: sets the $lsb_arch environment variable to the
# current architecture
lsb_setarch()
{
	lsb_arch=`uname -m`
	case $lsb_arch in 
		i?86)  lsb_arch=i386;;  
		sparc) lsb_arch=sparc;; 
		*) lsb_arch=unknown;;
	esac
}

# lsb_test_symlink symlink destination
#	Argument 1: symlink from name
#	Argument 2: to name
# Returns 
#	0 = PASS, yes symlink points to destination
#	1 = FAIL, 
#	3 = UNRESOLVED,Setup problem 
#
# On a failure or setup problem a diagnostic message is omitted
#
# Calls lsblib routines:
#	lsb_realpath, lsb_issymlink
#
lsb_test_symlink() {
	func_name=lsb_is_symlink
	# validate that we have two arguments
	if test $# -lt 2 
	then
	 	echo "$func_name: Missing argument"; return 3 # unresolved
	fi
	from="$1"
	to="$2"

	# cannot use test -L as not in POSIX.2 so call wrapper
	# that does an lstat()
	lsb_issymlink $from 
	rval=$?
	if test $rval -eq 3 # unresolved
	then
		return 3 # unresolved
	fi	
	if  ! test $rval -eq 0
	then
	    if ! test -e $from 
	    then
		echo "$from does not exist (expected symlink to $to)"
		return 1 # fail
	    else
		echo "$from expected to be  symlink to $to, returned non-symlink"
		return 1 # fail
	    fi
	else
	# its a symlink so lets validate where it points to
	# need to call realpath , which is a c program wrapping on realpath3c
		pathptr=`lsb_realpath $from`
		if test $? -ne 0
		then
			return 3 # unresolved
		fi
                pathptr_to=`lsb_realpath $to`
		if test $? -ne 0
		then
                        # Destination does not point anywhere
			return 3 # unresolved
		fi
		if test "$pathptr" = "$pathptr_to"
		then
			return 0 # pass
		else
			echo "$from expected to be  symlink to $to, pointed to $pathptr"
			return 1 # fail
		fi
	fi
}

# lsb_test_dir filename
# test for presence of a directory called filename
# Argument: filename to test if its a directory
# Returns: 0 PASS , it's a directory
#          1 FAIL, its  not a directory or a symlink
#          2 WARNING its a symlink 
#          3 UNRESOLVED error
lsb_test_dir() {
	func_name=lsb_test_dir
	# validate that we have one arguments
	if test $# -lt 1 
	then
		echo "$func_name: Missing argument"
		return 3 # unresolved
	fi
	
	_fname=$1
	# if it does not exist then fail
	if ! test -e $_fname
	then
		tet_infoline "$_fname: directory not found"
		return 1 # fail
	fi

	# since test -d will follow the symlink, we should check
	# for a symlink using the lstat wrapper first
	lsb_issymlink $_fname 
	rval=$?
	if test $rval -eq 3 # unresolved
	then
		return 3 # unresolved
	fi	
	if  ! test $rval -eq 0
	then
		# not a symlink 
		if test -d  $_fname
		then
			# success
			return 0 # pass
		else
			# not a symlink and not a directory
			tet_infoline "$_fname: not a directory or a symlink"
			return 1 # fail
		fi
	else
		# warning , its a symlink when we expected a directory
		return 2 # warning
	fi
}
# lsb_test_dir_searchable filename
# test for presence of a directory and that it can be searched
# Argument: filename 
# Returns: 0 PASS , it's a file and it exists
#          1 FAIL, its  not a file or a symlink
#          3 UNRESOLVED error
lsb_test_dir_searchable() {
	func_name=lsb_test_dir_searchable
	# validate that we have one arguments
	if test $# -lt 1 
	then
		echo "$func_name: Missing argument"
		return 3
	fi
	lsb_test_dir $1
	funcret=$?
	if test $funcret -eq 0 -o $funcret -eq 2
	then
		( ls $1 ) > /dev/null 2> _lsb.stderr
		if test $? -ne 0
		then
			echo "$func_name: expected be able to search directory $1, got an error"
			if test -s _lsb.stderr
			then
				cat _lsb.stderr
				rm _lsb.stderr
			fi
		else
			rm _lsb.stderr
			return 0
		fi

	else
		return $funcret
	fi
	

	
}
# lsb_test_file filename
# test for presence of a filename
# Argument: filename 
# Returns: 0 PASS , it's a file and it exists
#          1 FAIL, its  not a file or a symlink
#          2 WARNING its a symlink 
#          3 UNRESOLVED error
lsb_test_file() {
	func_name=lsb_test_file
	# validate that we have one arguments
	if test $# -lt 1 
	then
		echo "$func_name: Missing argument"
		return 3
	fi
	_fname=$1
	# if it does not exist then fail
	if ! test -e $_fname
	then
		tet_infoline "$_fname: file not found"
		return 1
	fi
	# since test -f will follow the symlink, we should check
	# for a symlink using the lstat wrapper first
	lsb_issymlink $_fname 
	rval=$?
	if test $rval -eq 3
	then
		return 3
	fi	
	if  ! test $rval -eq 0
	then
		# not a symlink 
		if test -f  $_fname
		then
			# success
			return 0
		else
			# not a symlink and not a file
			tet_infoline "$_fname: not a file or a symlink"
			return 1
		fi
	else
		# warning , its a symlink when we expected a file
		tet_infoline "$_fname: found a symlink"
		return 2
	fi
}
	

# lsb_test_exec:
# check that a utility can be executed
# if privilege is needed then an lsb_execwithpriv tool is used.
#
# if a utility needs an argument such as a file to parse that should be
# supplied.
# Returns:
# 	0 - PASS
# 	1 - Fail
#	3 - Unresolved
# 

lsb_test_exec()
{

test  $# -eq 0  && return 3 # unresolved

_ExecWithPriv=""
if test "$1" = "lsb_execwithpriv"
then
        _ExecWithPriv="$1" ; shift
        case "$1"
        in
                *) _ExecWithPriv="$_ExecWithPriv IGNORED_ARG" ; shift ;;
        esac
fi

rm -f _ltexec.stderr > /dev/null 2>&1
#( $_ExecWithPriv exec "$@" ) > /dev/null 2> _ltexec.stderr
( $_ExecWithPriv exec "$@" ) 
return $?

#if test -s _ltexec.stderr 
#then
#	cat _ltexec.stderr
#        rm -f _ltexec.stderr
#        return 1 # fail
#else
#        rm -f _ltexec.stderr
#        return 0 # pass
#fi
}

# lsb_test_device_by_name  name b|c major#  minor#
#
# test whether a device exists, the type and the expected major and
# minor number
#
# Calls :
#	lsb_devstat a wrapper to stat that when called with a device
#	name as the first argument outputs four space separated strings
#	device-name type majorno minorno
#
# Returns:
#	0 PASS
#	1 FAIL
#	3 Unresolved

lsb_test_device_by_name() {
	func_name=lsb_test_device_by_name
	# validate that we have one arguments
	if test $# -lt 4 
	then
		echo "$func_name: Missing argument(s)"
		return 3 # unresolved
	fi
	_devname=$1
	_devtype=$2
	_major=$3
	_minor=$4
	
	case $_devtype in
	b)	
		if ! test -b $_devname
		then
			return 1 # fail
		fi ;;
	c)
		if ! test -c $_devname
		then
			return 1 # fail
		fi ;;
	*)	
		echo "$func_name: unknown device type argument ($_devtype)"
		return 3 # unresolved 
		;;
	esac

	_retval=`lsb_devstat $_devname`
	set $_retval
	if test "$1" = "$_devname" -a  "$2" = "$_devtype" -a  "$3" = "$_major" -a  "$4" = "$_minor"
	then
		return 0 # pass
	else
		if ! test "$1" = "$_devname"
		then
			echo "$func_name: expected device name:\"$_devname\" got \"$1\""
		fi
		if ! test "$2" = "$_devtype"
		then
			echo "$func_name: expected device type: \"$_devtype\" got \"$2\""
		fi
		if ! test "$3" = "$_major"
		then
			echo "$func_name: expected major number: \"$_major\" got \"$3\""
		fi
		if ! test "$4" = "$_minor"
		then
			echo "$func_name: expected minor number: \"$_minor\" got \"$4\""
		fi
		return 1 # fail
	fi
		
}
