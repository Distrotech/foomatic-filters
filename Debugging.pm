
# Provides simple debugging functions for Foomatic Applications
#  This may need to be modified based on your application

package Foomatic::Debugging;
require Exporter;

use strict;
no strict;

use vars qw(
    @EXPORT @ISA
    $EXIT_PRINTED $EXIT_PRNERR $EXIT_PRNERR_NORETRY $EXIT_JOBERR
    $EXIT_SIGNAL $EXIT_ENGAGED $EXIT_STARVED $EXIT_PRNERR_NORETRY_ACCESS_DENIED
    $EXIT_PRNERR_NOT_RESPONDING $EXIT_PRNERR_NORETRY_BAD_SETTINGS $EXIT_PRNERR_NO_SUCH_ADDRESS
    $EXIT_PRNERR_NORETRY_NO_SUCH_ADDRESS $EXIT_INCAPABLE
    $debug $spooler $printer $added_lf $logh
 );

@ISA = qw(Exporter);


@EXPORT = qw(
    InitDebugging SetDebug D0 D1 D2 D4 D8
	D10 D20 D40 D80
	D100 D200 D400 D800
    rip_die psignal decode_status fix_codes
    $EXIT_PRINTED $EXIT_PRNERR $EXIT_PRNERR_NORETRY $EXIT_JOBERR
    $EXIT_SIGNAL $EXIT_ENGAGED $EXIT_STARVED $EXIT_PRNERR_NORETRY_ACCESS_DENIED
    $EXIT_PRNERR_NOT_RESPONDING $EXIT_PRNERR_NORETRY_BAD_SETTINGS $EXIT_PRNERR_NO_SUCH_ADDRESS
    $EXIT_PRNERR_NORETRY_NO_SUCH_ADDRESS $EXIT_INCAPABLE
    $debug $spooler $printer $added_lf $logh
);

use strict;

=head1 NAME

Foomatic::Debugging - FoomaticDebugging Package

=head1 DESCRIPTION

The Foomatic::Debugging package provides a set useful functions for
debugging and controlling the actions of the foomatic programs.

=head2 Error Codes

The following error codes are available:

 Code              Value
 $EXIT_PRINTED        0          file was printed normally
 $EXIT_PRNERR         1          error, retry possible
 $EXIT_PRNERR_NORETRY 2          error with no hope of retry
                                 usually a job problem
 $EXIT_JOBERR         3          job is defective
 $EXIT_SIGNAL         4          terminated after catching signal
 $EXIT_ENGAGED        5          printer is busy (connection refused?)
 $EXIT_STARVED        6          starved for system resources
 $EXIT_PRNERR_NORETRY_ACCESS_DENIED  7
    bad password? bad port? permissions?
 $EXIT_PRNERR_NOT_RESPONDING         8
    just doesn't answer at all (turned off?)
 $EXIT_PRNERR_NORETRY_BAD_SETTINGS   9
    fault with foomatic configuration or foomatic settings
    possible conflict with PPD file or option settings
 $EXIT_PRNERR_NO_SUCH_ADDRESS        10
    address lookup failed, may be transient
 $EXIT_PRNERR_NORETRY_NO_SUCH_ADDRESS 11
    address lookup failed, not transient
 $EXIT_INCAPABLE                      50
    printer wants (lacks) features or resources

The $EXIT_PRNERR_NORETRY and $EXIT_PRNERR_NORETRY_BAD_SETTINGS status usually
requires some sort of administrative or user action in order for the job
to be processed correctly.

The $EXIT_PRNERR indicates a failure, but it is possible that the condition
will be cleared on retry. These types of conditions include the lack of
memory, file descriptors, etc., which may be temporarily exhausted on a
system.  This may also be caused by termination by a signal, or
by one of the supporting programs or processes exiting.

=cut


# Where to send debugging log output to
$logh = *STDERR;
$debug = 0;
$spooler = "";
$printer = "";
$added_lf = "\n";

# Error codes, as some spooles behave different depending on the reason why
# the RIP failed, we return an error code. As I have only found a table of
# error codes for the PPR spooler. If our spooler is really PPR, these
# definitions get overwritten by the ones of the PPR version currently in
# use.

$EXIT_PRINTED = 0;         # file was printed normally
$EXIT_PRNERR = 1;          # printer error occured
$EXIT_PRNERR_NORETRY = 2;  # printer error with no hope of retry
$EXIT_JOBERR = 3;          # job is defective
$EXIT_SIGNAL = 4;          # terminated after catching signal
$EXIT_ENGAGED = 5;         # printer is otherwise engaged (connection 
                           # refused)
$EXIT_STARVED = 6;         # starved for system resources
$EXIT_PRNERR_NORETRY_ACCESS_DENIED = 7;     # bad password? bad port
                                            # permissions?
$EXIT_PRNERR_NOT_RESPONDING = 8;            # just doesn't answer at all 
                                            # (turned off?)
$EXIT_PRNERR_NORETRY_BAD_SETTINGS = 9;      # interface settings are invalid
$EXIT_PRNERR_NO_SUCH_ADDRESS = 10;          # address lookup failed, may be 
                                            # transient
$EXIT_PRNERR_NORETRY_NO_SUCH_ADDRESS = 11;  # address lookup failed, not 
                                            # transient
$EXIT_INCAPABLE = 50;                       # printer wants (lacks) features
                                            # or resources

use POSIX;

=head2 InitDebugging - set up Debugging

$pid = InitDebugging($debug, $logfile, $quiet );

  $debug = 1 for debugging to $logfile
           0 for not logfile output
  $logfile = logfile (see below for security issues)
  $quiet = no effect if $debug = 1
           0 - all trace output written to STDERR
           1 - no trace output written to STDERR

Configure the various debugging functions based on the
value of $debug, $logfile, and $quiet.

If $debug and $logfile are set (i.e. -test true), then
the logfile is opened and  copy of all trace information is
sent to the logfile.

If $debug is false and $quiet is true, then all trace output
is suppress, otherwise the trace output will be written
to STDERR.

Note: Running this code in debug mode can open a security
loophole.  Using a file name with the string XX in it
will cause a temporary file to be created, a la the UNIX mkstemp
utility.

When running in debug mode, a subprocess is created which will
monitor the program STDERR output.  A copy will be made to the
error log by this process.

=cut

sub InitDebugging( $ $ $ ){
    my($logfile, $quiet, $pid);
    ($debug, $logfile, $quiet ) = @_;
    my $fd = select STDERR; $| = 1; select $fd;
    if ($debug and $logfile ) {
	# Grotesquely unsecure; use for debugging only
	# arrange to have STDERR also sent to log file
	$logh = undef;
	if( $logfile =~ /XX/ ){
	    ($logh = mkstemp( $logfile ));
	} else {
	    open $logh, "> ${logfile}.log"
	}
	if( not $logh ){
	    die "cannot open ${logfile}.log";
	}
	$fd = select $logh; $| = 1; select $fd;
	pipe CH_RD, CH_WR or die "pipe() failed";
	my $pid = fork();
	if( not defined $pid ){
	    die "fork() failed";
	} elsif( not $pid  ){
	    $0 = "STDERR Logger";
	    $fd = fileno(CH_RD);
	    dup2($fd,0) or die "InitDebugging: dup2($fd,0) failed";
	    close(STDIN);
	    close(STDOUT);
	    close(CH_WR);
	    while(<CH_RD>){
		print $logh "$_";
		print STDERR "$_";
	    }
	    exit(0);
	}
	$fd = fileno(CH_WR);
	dup2($fd, 2) or die "InitDebugging: dup2($fd,2) failed";
	close(CH_WR);
	close(CH_RD);
    } elsif ( not $debug and $quiet ) {
	# Quiet mode, do not log
	$logh = undef;
	open $logh, "> /dev/null";
    } else {
	# Default: log to STDERR
	$logh=*STDERR;
    }
    return( $pid );
}

=head2 Debugging and Diagnostic Output


  D0("message");
  D1("message");
  ...

The D0, D1, ... D80 routines are used for producing debugging traces based
on the (hexadecimal) values of the $debug option.
Each bit in $debug enables printing by the corresponding routine.
Thus, a $debug value of 1 would allow D1 to print,
2 would allow D2 to print,
3 would allow D1 and D2 to print,
and so forth.

The D0 is special in that it always produces output,
and is appropriate for general logging and tracing purposes.

=cut

# for comments to the log file
sub D0($){ my $lf = ""; $lf = "\n" if $_[0] !~ /\n$/; print $logh $_[0] . $lf; }
sub D1($){ print $logh $_[0] . "\n" if $debug & 1; }
sub D2($){ print $logh $_[0] . "\n" if $debug & 2; }
sub D4($){ print $logh $_[0] . "\n" if $debug & 4; }
sub D8($){ print $logh $_[0] . "\n" if $debug & 8; }
sub D10($){ print $logh $_[0] . "\n" if $debug & 0x10; }
sub D20($){ print $logh $_[0] . "\n" if $debug & 0x20; }
sub D40($){ print $logh $_[0] . "\n" if $debug & 0x40; }
sub D80($){ print $logh $_[0] . "\n" if $debug & 0x80; }
sub D100($){ print $logh $_[0] . "\n" if $debug & 0x100; }
sub D200($){ print $logh $_[0] . "\n" if $debug & 0x200; }
sub D400($){ print $logh $_[0] . "\n" if $debug & 0x400; }
sub D800($){ print $logh $_[0] . "\n" if $debug & 0x800; }

=head2 Rip-Die - error exit support

rip_die("error message", $exit_code );

Write a Good-Bye message, clean up, and then exit with the error code.
Some special cases for error logging are also handled.

=cut

sub rip_die( $ $ ) {
    my ($message, $exitstat) = @_;

    # Kill the children
    $logh = \*STDERR if( not defined $logh );
    eval { FoomaticSubprocess::killchildren(); } ;

    D0("Process '$0' dying with \"$message\", exit status: $exitstat\n");
    if ($spooler eq 'ppr_int') {
	# Special error handling for PPR intefaces
	$message =~ s/\\/\\\\/;
	$message =~ s/\"/\\\"/;
	my @messagelines = split("\n", $message);
	my $firstline = "TRUE";
	for my $line (@messagelines) {
	    system("lib/alert $printer $firstline \"$line\"");
	    $firstline = "FALSE";
	}
    } else {
	print STDERR $message . "\n";
    }
    exit $exitstat;
}


=head2 Print Signal values psignal

$name = psignal( $signal);

Determine signal name based on signal number.
Unix psignal() funtion.

Taken from the PerlCookbook

=cut

my $sig_name;
my $sig_num;

sub psignal( $ ){
    use Config;
    if( not $sig_name ){
	my %sig_num;

	unless($Config{sig_name} && $Config{sig_num}) {
	    die "No sigs?";
	}
	my @names = split ' ', $Config{sig_name};
	my @numbers = split ' ', $Config{sig_num};
	for( my $i = 0; $i < @names; ++$i ){
		$sig_name->{$numbers[$i]} = "SIG" . $names[$i];
		$sig_num->{$names[$i]} = $numbers[$i];
	}
    }
    return( $sig_name->{$_[0]} || $_[0] );
}


# decode status
#  ($status string, $signal, $exitcode ) = decode_status($)
#
#  We interpret the exit conditions and produce a nice message
#

sub decode_status($){
    my ($status) = @_;
    $status = 0 if not defined $status;
    my $out = "";
    my $x = $status >>8;
    my $s = $status & 127;
    my $c = $status & 128;
    if( $s ){
	$out = "signal " . psignal($s);
	$out .= " (core dump)" if( $c );
    } else {
	$out = "status $x";
    }
    return( $out, $s, $x);
}

#
# fix_codes
#  Based on the termination condition (signal and exit code)
#  decide what exit code to use.
#
#  if the process dies by a signal, then figure out what to use
#  based on the signal value.  Note that this can also be a
#  a 'core dump' type of exit
#
#

sub fix_codes( $ $ ){
    my( $signal, $retval );
    $retval = 0 if( not $retval );
    if( $signal ){
	if ($signal == SIGUSR1) {
	    $retval = $EXIT_PRNERR;
	} elsif ($signal == SIGUSR2) {
	    $retval = $EXIT_PRNERR_NORETRY;
	} elsif ($signal == SIGTTIN) {
	    $retval = $EXIT_ENGAGED;
	} else {
	    $retval = $EXIT_SIGNAL;
	}
    }
    return( $retval );
}

1;

