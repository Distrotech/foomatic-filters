package Foomatic::Subprocess;

use strict;
no strict;
use vars qw( @children );

require Exporter;

@ISA = qw(Exporter);

@EXPORT = qw( run_subprocess_with_logger run_subprocess waitchildren killchildren @children );
@EXPORT_OK = qw(Test_run_subprocess);

use strict;

=head1 NAME

Foomatic::Subprocess - Subprocess Package for Foomatic 

=head1 DESCRIPTION

The FoormaticSubprocess package encapsulates a rather difficult piece
of process and subprocess management.

=head1 EXPORTS

 @EXPORT = qw( run_subprocess_with_logger run_subprocess
    waitchildren killchildren @children );
 @EXPORT_OK = qw(Test_run_subprocess);

=head1 DETAILS

When running a filter, it is necessary to construct a pipeline of processes:
the STDOUT of one process is connected to the STDIN of another.
This is more complex than it seems, as ensuring that the various pipes
are opened,  then the file descriptors are effectively 'dup2' the
right process inputs and outputs is complex.

These routines have tried to simpllify these activities.

A further complication is when the STDERR output of a program has to be
caught and labelled before writing to a log file.
This requires a
nursemaid process to handle this action.
Setting this up is complex.

Finally, you would like to kill off all processes that you know about.
Thus, we keep track of them and allow us to kill them off.

=head2 Running A Program As A Subprocess  - run_subprocess

  ($pid, $output_from_child )
    = run_subprocess( $id, $input_fd, $output_fd; *routine, @args )

This routine will run the specified routine with the indicated
arguments as a subprocess.
It takes care of setting up STDIN and STDOUT to a specified
set of file descriptors.

The $input_fd and $output_fd values are are file descriptors, i.e.,
the values retuned by the fileno() operator on a file handle:

  fileno(STDIN), $fh->fileno

If the $input_fd value is not defined, then the new process takes
its input from the parent STDIN;  by passing a file descriptor you can
cause it to take input from some other source;  this file descriptor
is bound to the STDIN of the new process. As a special case,
the "" value will not cause binding.

If the $input_fd is a reference to an array of file descriptors,
then the first one is used as the new STDIN value, and
the rest are then closed.  This allows the use of a pipe
to be passed into the child as shown in the example below.

 pipe READH, WRITEH or die "pipe() failed";
 my($pid, $fh) = run_subprocess( "filter",
	[ \*READH, \*WRITEH ], undef, "exec", "/bin/cat" );
 close( READH);
 $fd = select WRITEH; $| = 1; select $fd;
 while( <STDIN> ){
	print WRITEH $_;
 }
 # the filtered input can be read from $fh
 
Note that the above example has a fatal flaw - the system will
block when the subprocess cannot write any more data
as its output buffers are full.  You will need to combine
this with the select() facility:

 use IO::Select;
 pipe READH, WRITEH or die "pipe() failed";
 $fd = select WRITEH; $| = 1; select $fd;
 $s = IO::Select->new();
 $s->add(\*READH);
 my($pid, $fh) = run_subprocess( "filter",
	[ \*READH, \*WRITEH ], undef, "exec", "/bin/cat" );
 close( READH);
 while( <STDIN> ){
	print WRITEH $_;
	while( $s->can_read(0) ){
	    $in = <$fh>;
	}
 }
 close WRITEH;
 while( defined( $in = <$fh> ){;
   ...
 }

Similarly, if $output_fd is defined,  the STDOUT of the new
process will be bound to the file descriptor;  the
"" value will not cause binding.
However, if the $output_fd value is not defined, then a
PIPE is created, the new process STDOUT is bound to the
WRITE side of the pipe, and the READ read side is returned
as $fh.

The routine value is a perl function, the strings "exec"
or "system", or undef.
If a function is passed, it is called with the
specified arguments.  If the "exec" or "system" strings are passed
then the Perl 'exec' function is executed on the arguments:

 ($pid,$fh) = run_subprocess( "cat hosts", undef, undef,
   "exec", "cat /etc/hosts");

This is similar to:

 $fh = new FileHandle( "cat /etc/hosts |" )

But you also get the process and can kill if off if you want.

Be aware that the $SYSTEM_FD_MAX (default value 2) variable determines
the file descriptor that are NOT closed when starting a subprocess
using 'system' or 'exec'.  The default value of 2 passes STDIN,
STDOUT, and STDERR (fds 0, 1, 2) and closes all the other file
descriptors that have been opened by the Perl program.

  pipe RD,WR or die "pipe failed";
  ($pid,$fh) = run_subprocess( "strip cr", fileno(RD), undef,
        "exec", "sed -e 's/\015//g'");
  ($pid,$fh) = run_subprocess( "write"  fileno($fh), "", "exec", "write" );

In this example,
you have control of the input and output to the 'sed' process.
Both the 'sed' process and your main process can write
into the 'write' process input.

  sub mysub { print @_; }
  ($pid,$fh) = run_subprocess( "mysub" undef, undef, \&mysub, "f1", "f2" );
  
This shows how you can run a function and have its STDOUT directed
to a file.

Finally, the most bizarre form is when the subroutine value is
'undef'.  The child process will return from the routine,
and the $pid value will be 0.  This is similar to the fork()
facility,  but it now has its STDIN and STDOUT bound to the
specified file descriptors.

Needless to say,  this is almost a Swiss Army Knife of process
management.

=head2 Monitoring STDERR Output - run_subprocess_with_logger

  ($pid, $output_from_child )
    = run_subprocess_with_logger( $id, $input_fd, $output_fd,
           $header; *subroutine, @args )

This function is identical in action to the 'run_subprocess' function,
but creates an additional process that monitors the STDERR of the output
process.  This process in turn forks another process which
then has its STDIN and STDOUT bound to the specified file descriptors,
and which carries out actions specified by the 'subroutine'
and '@args' parameters.

Each time the child process generates an error message on STDERR,
the middle process will read it, prefix the $header value,
and write it to the parent process STDERR.  The middle process
will also monitor the child process for exit status, and print
status and other information.

Status reporting is done by using the D0() function from the
Foomatic::Debugging package.

  pipe RD,WR or die "pipe failed";
  ($pid,$fh) run_subprocess( "gs script", fileno(RD), "",
	"GhostScript", "exec", "gs -BATCH -o - ..." );

In this example,  we run the GhostScript interpreter as a subprocess.
It will write its output to STDOUT, which is the same STDOUT as
the parent process.  Any error messages on STDERR will be reported
with the "GhostScript:" label prefixed to them.

=head2 Children

The run_process and run_subprocess_with_logger routines
put the pid's of the children processes in the
@children array.

=head2 Kill Children - killchildren( @signallist )

 killchildren( [ @signallist ] )

 Example:
   killchildren();
   killchildren(SIGHUP);

The killchildren kills all of the children in the
@children array by sending them a set of signals.
By default, these are SIGINT, SIGKILL, and SIGQUIT.
Optionally, another set of signals can be specifed:..

  kill(SIGHUP, SIGTERM)

will send the SIGHUP and SIGTERM signals to the children processes.

=head2 Wait for Children - waitchildren

waitchildren( $nowait [,$verbose] )

This routine will wait until all of the children in the
@children array have exited and their zombie bodies have
been gathered up.

If the $nowait value is nonzero, then only processes that
have exited will be reported.

If the $verbose value is nonzero, then the exit status
of the children will also be reported.

=head1 USES

 use strict;

=cut

use strict;
use POSIX;
use IO;
use Data::Dumper;
use Foomatic::Debugging;

# children processes
@children = ();

sub killchildren(@){
    my @list= (SIGINT, SIGKILL, SIGQUIT);
    @list = @_ if( defined @_ and @_ );
    foreach my $child (@children){
	foreach my $sig (@list){
	    kill($sig, $child);
	}
    }
}

# wait for all the children to exit
sub waitchildren($;$){
    my ($nowait, $verbose) = @_;
    my( $pid, $s);
    while( not $nowait and @children ){
	@children = grep { kill 0, $_; } @children;
	D0("waitchildren '" . join(",", @children) . "'\n") if $verbose;
	if( @children ){
	    $pid = wait;
	    last if( $pid == -1 or $pid == 0);
	    ($s) = decode_status($?);
	    D0("waitchildren pid $pid $s\n") if $verbose;
	}
    }
    while( ($pid = waitpid(-1,&WNOHANG)) != -1 and $pid ){
	($s) = decode_status($?);
	D0("waitchildren pid $pid $s\n") if $verbose;
    }
}

sub run_subprocess( $ * * ; * @ ){
    my( $id, $input_fd, $output_fd, $runsub, @args ) = @_;

    my $fd;

    # flush stdout
    select( STDOUT ); $| = 1;
    if( not defined $output_fd ){
	pipe( PRIVATE_READH, PRIVATE_WRITEH ) or
	    rip_die("run_subprocess: pipe() failed - $!", $EXIT_PRNERR);
    }

    my $pid = fork();

    if( not defined $pid ){
	rip_die("run_subprocess: fork failed - $!", $EXIT_PRNERR);
    } elsif( not $pid  ){
	# child
	$0 = $id;
	@children = ();
	if( not defined $output_fd ){
	    $fd = fileno(PRIVATE_WRITEH);
	    dup2($fd,1) or 
		rip_die("run_subprocess: dup2($fd,1) failed - $!", $EXIT_PRNERR);
	    close(PRIVATE_WRITEH);
	    close(PRIVATE_READH);
	} elsif( $output_fd ne "" ){
	    $fd = fileno($output_fd);
	    dup2($fd,1) or 
		rip_die("run_subprocess: dup2($fd,1) failed - $!", $EXIT_PRNERR);
	}
	if( ref($input_fd) ){
	    #print STDERR "ref " . ref($input_fd) . "\n";
	    if( ref($input_fd) eq 'GLOB' ){
		$fd = fileno($input_fd);
	    } elsif( ref($input_fd) eq 'ARRAY' ){
		$fd = fileno($input_fd->[0]);
	    }
	    #print STDERR "FD  $fd\n";
	    close( STDIN );
	    open( STDIN, "<&$fd");
	    dup2($fd,0) or 
		rip_die("run_subprocess: dup2($fd,0) failed - $!", $EXIT_PRNERR);
	    if( ref($input_fd) eq 'GLOB' ){
		#print STDERR "close " . Dumper($input_fd);
		close($input_fd);
	    } elsif( ref($input_fd) eq 'ARRAY' ){
		for my $f (@{$input_fd}) {
		    #print STDERR "close " . Dumper($f);
		    close($f);
		}
	    }
	}
	#print STDERR "child has sub\n" if $runsub;
	if( $runsub ){
	    my $r = ref( $runsub );
	    my $retval = $EXIT_PRNERR;
	    #print STDERR "runsub REF '$r'\n";
	    if( $r eq 'CODE' ){
		exit( $runsub->(@args) );
	    } elsif( $runsub eq "system" or $runsub eq "exec" ){
		$r = join(' ', @args);
		exec( @args ) or
		    rip_die("Cannot exec '$r' $!", $EXIT_PRNERR_NORETRY);
	    }
	    rip_die("run_subprocess: unknown type ref=$r", $EXIT_PRNERR);
	}
	return( $pid, undef );
    } else {
	#D1 "run_subprocess: child $pid";
	# parent
	push @children, $pid;
	if( not defined $output_fd ){
	    close PRIVATE_WRITEH;
	    return( $pid, \*PRIVATE_READH );
	} else {
	    return( $pid, undef );
	}
    }
}


sub run_subprocess_with_logger( $ * * $ ; * @ ){
    my( $id, $input_fd, $output_fd, $legend, $runsub, @args ) = @_;
    select STDERR; $| = 1;
    select STDOUT; $| = 1;
    my( $fd, $pid1, $pid2, $fh );

    ($pid1, $fh) = run_subprocess( "$legend ($id)", $input_fd, $output_fd );

    # parent - gets the $pid1 and $fh
    if( $pid1 ){
	return( $pid1, $fh );
    }
    # child 1
    pipe( ERR_READH, ERR_WRITEH ) or 
	rip_die("run_subprocess_with_logger: pipe() failed - $!", $EXIT_PRNERR);
    $pid2 = fork();
    if( not defined $pid2 ){
	rip_die("run_subprocess_with_logger: fork() failed - $!", $EXIT_PRNERR);
    } elsif( $pid2 ){
	close( $fh ) if $fh;
	close( STDIN );
	close( STDOUT );
	close( ERR_WRITEH );
	while( <ERR_READH> ){
		D0("$legend ($id): $_");
	}
	my $pid = waitpid( $pid2, 0 );
	my ($s, $signal, $retval ) = decode_status($?);
	$retval = fix_codes(  $signal, $retval );
	D0("$legend ($id): $s");
	exit($retval);
    }
    $0 = $id;
    $fd = fileno(ERR_WRITEH);
    dup2($fd,2) or 
	rip_die("run_subprocess_with_logger: dup2($fd,2) failed - $!", $EXIT_PRNERR);
    close( ERR_READH );
    close( ERR_WRITEH );
    if( $runsub ){
	my $r = ref( $runsub );
	my $retval = $EXIT_PRNERR;
	#print STDERR "runsub REF '$r'\n";
	if( $r eq 'CODE' ){
	    exit( $runsub->(@args) );
	} elsif( $runsub eq "system" or $runsub eq "exec" ){
	    $r = join(' ', @args);
	    exec( @args ) or
		rip_die("Cannot exec '$r' $!", $EXIT_PRNERR_NORETRY);
	} else {
	    D0("ERROR unknown type '$runsub' ref='$r', returning $retval");
	    $retval = $EXIT_PRNERR;
	}
	exit($retval);
    }
    return( $pid2, undef );
}

sub sleeper {
    print STDERR "sleeper: args '" . join("', '", @_) . "'\n";
	my( $sig ) = @_;
    print STDERR "sleeping: start\n";
    sleep(0.25);
    print STDERR "sleeping: end\n";
    if( $sig ){
	    kill $sig, $$;
	    sleep(100);
    }
    exit(0);
}

sub catthis {
    print STDERR "catthis: args '" . join("', '", @_) . "'\n";
    while( <STDIN>){
	print STDOUT "catthis: $_";
    }
}

sub Test(){

    D0("TEST");
    # Test code for runsubprocess.  You need a file with a couple of lines
    # in it and redirect STDIN to it.
	my $f = "/tmp/testinput";
	open( F, ">$f" ) or die "cannot open $f";
	print F "test\nthis\ninformation\n";
	close(F);
	open( STDIN, "<$f" ) or die "cannot open $f as STDIN";

    my @lines = <STDIN>;
    print STDERR "INPUT @lines";
    seek STDIN,0,0;

    my ( $xpid, $xfd );
    
    seek STDIN,0,0;
    ($xpid, $xfd ) = run_subprocess( "test", undef, undef );
    if( not defined $xpid ){
	print STDERR "kill all subprocesses\n";
    } elsif( $xpid ){
	print STDERR "xfd " . Dumper($xfd);
	print STDERR "returned filehandle fileno " . $xfd->fileno() . "\n";
	print STDERR "STDERR parent, child $xpid\n";
	my @want = @lines;
	while( <$xfd> ){
	    print STDERR "PARENT: From child '$_' - ";
	    my $v = "CHILD " . shift @want ;
	    if( $v ne $_ ){
		print STDERR "did not get $v";
	    } else {
		print STDERR "OK\n";
	    }
	}
	my $status = waitpid($xpid, 0);
	print STDERR "pid $xpid exit status $?\n";
	print STDERR "missinglines @want\n" if( @want );
	close( $xfd );
    } else {
	print STDERR "child\n";
	sleep(0.25);
	my $i = 0;
	while( <STDIN>){
	    print STDERR "CHILD '$_'\n";
	    sleep(0.25);
	    print STDOUT "CHILD $_";
	    sleep(0.25);
	}
	exit( 1 );
    }
    
    seek STDIN,0,0;
    ($xpid, $xfd ) = run_subprocess(  "test",undef, undef, \&catthis, "test", "this" );
    if( not defined $xpid ){
	print STDERR "kill all subprocesses\n";
    } elsif( $xpid ){
	print STDERR "STDERR TEST2 parent, child $xpid\n";
	my @want = @lines;
	while( <$xfd> ){
	    print STDERR "PARENT: From child '$_' - ";
	    my $v = "catthis: " . shift @want;
	    if( $v ne $_ ){
		print STDERR "did not get $v";
	    } else {
		print STDERR "OK\n";
	    }
	}
	my $status = waitpid($xpid, 0);
	print STDERR "pid $xpid exit status $?\n";
	print STDERR "missinglines @want\n" if( @want );
	close( $xfd );
    } else {
	print STDERR "child - not supposed to do this\n";
	exit( 1 );
    }
    
    seek STDIN,0,0;
    #($xpid, $xfd ) = run_subprocess(  "test",undef, undef, "system", "echo hi" );
    ($xpid, $xfd ) = run_subprocess(  "test",undef, undef, "system", "echo hi; exit 1;" );
    if( not defined $xpid ){
	print STDERR "kill all subprocesses\n";
    } elsif( $xpid ){
	print STDERR "STDERR TEST2 parent, child $xpid\n";
	my @want = ("hi\n");
	while( <$xfd> ){
	    print STDERR "PARENT: From child '$_' - ";
	    my $v = "" . shift @want;
	    if( $v ne $_ ){
		print STDERR "did not get $v";
	    } else {
		print STDERR "OK\n";
	    }
	}
	my $status = waitpid($xpid, 0);
	print STDERR "pid $xpid exit status $?\n";
	print STDERR "missinglines @want\n" if( @want );
	close( $xfd );
    } else {
	print STDERR "child - not supposed to do this\n";
	exit( 1 );
    }
    
    pipe READH, WRITEH or die "cannot create pipe";
    seek STDIN,0,0;
    ($xpid, $xfd ) = run_subprocess(  "test",undef, \*WRITEH, \&catthis );
    if( not defined $xpid ){
	print STDERR "kill all subprocesses\n";
    } elsif( $xpid ){
	print STDERR "STDERR TEST3 parent, child $xpid\n";
	close( WRITEH );
	my @want = @lines;
	while( <READH> ){
	    print STDERR "PARENT: From child '$_' - ";
	    my $v = "catthis: " . shift @want;
	    if( $v ne $_ ){
		print STDERR "did not get $v";
	    } else {
		print STDERR "OK\n";
	    }
	}
	close( READH );
	close( $xfd ) if $xfd;
	print STDERR "missinglines @want\n" if( @want );
    } else {
	print STDERR "child - not supposed to do this\n";
	exit( 1 );
    }
    
    print STDERR "\n\nSend to child\n";
    pipe READH, WRITEH or die "cannot create pipe";
    seek STDIN,0,0;
    ($xpid, $xfd ) = run_subprocess(  "test",[\*READH, \*WRITEH], "");
    if( not defined $xpid ){
	print STDERR "kill all subprocesses\n";
    } elsif( $xpid ){
	print STDERR "STDERR TEST3 parent, child $xpid\n";
	close( READH );
	my @want = @lines;
	while( <STDIN> ){
	    print WRITEH "TO CHILD: $_";
	}
	close( WRITEH );
	close( $xfd ) if $xfd;
	waitpid($xpid,0);
    } else {
	print STDERR "child\n";
	while( <STDIN> ){
	    print STDERR "child read '$_'\n";
	}
	print STDERR "child EOF\n";
	close( STDIN );
	exit( 1 );
    }
    
    print STDERR "process $$\n";
    seek STDIN,0,0;
    print STDERR "Doing logger\n";
    ($xpid, $xfd ) = run_subprocess_with_logger(  "test",undef, undef, "LOGGER" );
    if( not defined $xpid ){
	print STDERR "kill all subprocesses\n";
    } elsif( $xpid ){
	print STDERR "STDERR TEST4 parent, child $xpid\n";
	my @want = @lines;
	while( <$xfd> ){
	    print STDERR "PARENT: From child '$_' - ";
	    my $v = "CHILD " . shift @want;
	    if( $v ne $_ ){
		print STDERR "did not get '$v'";
	    } else {
		print STDERR "OK\n";
	    }
	}
	close( $xfd ) if $xfd;
	print STDERR "missinglines @want\n" if( @want );
    } else {
	sleep(0.25);
	print STDERR "child\n";
	my $i = 0;
	while( <STDIN>){
	    print STDERR "CHILD $_";
	    sleep(0.25);
	    print STDOUT "CHILD $_";
	    sleep(0.25);
	}
	exit( 1 );
    }
    
    seek STDIN,0,0;
    print STDERR "START TEST4 WAIT\n";
    ($xpid, $xfd ) = run_subprocess(  "test",undef, undef, \&sleeper );
    waitchildren(1);

    seek STDIN,0,0;
    print STDERR "START TEST5 WAIT\n";
    ($xpid, $xfd ) = run_subprocess(  "test",undef, undef, \&sleeper, SIGINT );
    waitchildren(1);

    print STDERR "DONE\n";
    
    exit 0;

    ###################################### TEST END
}

1;
