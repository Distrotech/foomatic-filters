package Foomatic::ParsePCL;
require Exporter;

$| = 1;

use Data::Dumper;
use FileHandle;
$Data::Dumper::Indent = 1;
use lib qw(.); use Foomatic::Debugging;
use strict;
no strict;

use vars qw(
    @EXPORT @ISA
 );

@ISA = qw(Exporter);

@EXPORT = qw(
    Parse_PCL_init Parse_PCL_setup Parse_PCL
    Parse_PXL_setup Parse_PXL PXL_assembler
	Parse_PJL
);

use strict;

=head1 NAME

Foomatic::ParsePCL - PCL5, PXL (PCL6), and PJL Parser

=head1 DESCRIPTION

The Foomatic::ParsePCL package provides a set useful functions for
parsing and modifying PCL files.

=head1 USAGE

=head2 Parse_PCL_init()

This routine initializes the PCL and PXL parsers.
It resets all of the effects of Parse_PCL_setup().

=head2 Parse_PCL_setup()

 Set Up for Option Replacement:
   $status = Parse_PCL_setup( $init, $action,
    $header, $options );
  Example:
   $status = Parse_PCL_setup( 1, 1,
    "\033..watermark", "\033&u911D");

The Parse_PCL_setup routine is called to set up the option
translation and to set up a string that is sent at
the start of each page.

If the $init value is true, then the Parse_PCL_init routine
is called.  If the $init value is false, then
Parse_PCL_init
routine is not called and the various values are updated.

The $header string is put out at the start of each page.
The $option string
is parsed for PCL escape sequences, and the values
of these sequences replace ones that occur in the input file.

The $action value controls how the replacement is done.
The $action value is 1 or 2 the option values are appeneded to the
header string and are put out at the start of each page.
If $action is 0 or 1, then the options which occur in the input
are replaced by the replacment values.  If $action is 2,
then the options are deleted from the input.

   action     start of page      infile options
    0         header             replaced
    1         header+options     replaced
    2         header+options     deleted

If multiple calls to Parse_PCL_setup are made
then the $header and $option values are appeneded
to the existing values in the order shown above.
That is, the first call would result in
h1+opt1 and then next call
would have h1+opt1+h2+opt2.

=head2 Parse_PCL()

 $status = Parse_PCL( $input, $text_format,
	$input_fd, $output_fd, $newpage_callback )

 Example:
   ($status) = Parse_PCL( "", 0, \*STDIN, \*STDOUT, undef );

   sub Callback_PCL{ ... Parse_PCL_setup(...); }
   ($status) = Parse_PCL( "", \*STDIN, \*STDOUT, \&Callback_PCL );

The Parse_PCL routine is used to parse PCL
input and to optionally perform option replacement.
The input string and input file are parsed,
and options which have had a new value specified
by the setup call to Parse_PCL_setup will have their values replaced.
The parsing of the input will continue until a UEL (Universal Exit) string
or EOF on the input is encountered and the UEL followed by
additional input will be returned.  If EOF was encountered an
empty string or undef will be returned.

If the $text_format value is false (0), output will be in binary
PCL.  If $text_format is true (1), output will be text
interpretations of the PCL escape sequences.

When a command or escape sequence which would result in a
new page being started, if the
$new_callback value is defined then
it should be a reference to a routine and the
$newpage_callback routine will be called.
This routine can call Parse_PCL_setup() to set new values for
parsing.  This allows a per-page set of options to be generated
and used for parsing the input.

It should be noted that the effects of the $newpage_callback
routine will be used for subsequent pages or PCL input.

=head2 Parse_PXL_setup()

 Set Up for Option Replacement:
   $status = Parse_PCL_setup( $init, $header, $options );
  Example:
   $status = Parse_PCL_setup( 1,
     "_uint16_xy watermark",
     "_uint16_xy 1200 1200 ..." );

The Parse_PXL_setup routine is called to set up the option
translation and to set up a string that is sent at
the start of each page, i.e. - after each 'BeginPage'
operator.

If the $init value is true, then the Parse_PCL_init routine
is called.  If the $init value is false, then
Parse_PCL_init
routine is not called and the $header and $replace
values are appended to the previous set.

The $header string is put out at the start of each page.
This string must be in a text format, compatible with the
PXL_assembler() routine.

The $options string is
parsed for PXL values, attributes, and operators,
and the values of the attributes for the specified operators replace
ones that occur in the input file.

=head2 Parse_PXL()

 $status = Parse_PXL( $input, $text_format,
	$input_fd, $output_fd, $newpage_callback )

 Example
  ($status) = Parse_PXL( "", 0, \*STDIN, \*STDOUT, undef );

  sub Callback_PXL{ ... Parse_PXL_setup(...); }
  ($status) = Parse_PXL( "", \*STDIN, \*STDOUT, \&Callback_PXL );

The Parse_PXL routine is used to perform the option replacement.
The input string and input file is parsed,
and and options which have had a new value specified
by the setup call to Parse_PXL_setup will have their values replaced.

If the $text_format value is false (0), then output
will be in binary PXL format.  If it is true (1), then
it will be in text format compatible with the PXL_assembler()
routine.

The parsing of the input will continue until a UEL (Universal Exit) string
or EOF on the input is encountered and the UEL followed by
additional input will be returned.  If EOF was encountered an
empty string or undef will be returned.

When a BeginPage operator is executed, any $header PXL information
is appended at that point in the output stream, in the
appropriate BigEndian or LittleEndian format.

When a EndPage command is processed,
the $newpage_callback routine will be called.
This routine can call Parse_PXL_setup() to set new values for
parsing.  This allows a per-page set of options to be generated
and used for parsing the input.

It should be noted that the effects of the $newpage_callback
routine are used for all commands following the EndPage operator.

=head2 PXL_assembler()

  ${@output} =
	PXL_assembler( $input, $action, $big_endian )

The PXL_assembler routine is used to translate a text form
of PXL into either PXL binary output sequences or to update
the option information used by the Parse_PXL routine.

The PXL_assembler routine is used by the Parse_PXL() routine to
output header information after a BeginPage operator
and by the Parse_PXL_setup() routine to set up the
operator values.

If the $action value is 0, then binary PXL strings are generated,
and the output is returned as a reference to an array of bytes. 
If $action is 1, then the tables used by the Parse_PXL()
routine are updated.

If the $big_endian value is True, then the binary output is in
BigEndian format, otherwise it will be in LittleEndian format.

The syntax used for the PXL information is similar to that
described in the PXL XL Feature Reference Protocol Class 2.0
document.  This document is marked as Company Confidential,
but the information contained in it can be obtained from HP.

Each line of has the format:

 # comment
 "string"
 [ [numerical_value @attribute]* operator ]*

=head3 Comment

A line starting with # will be treated as a comment.

=head3 String

A string consists of a set of ASCII characters
with the standard Perl hexadecimal character escapes
- i.e. \0xNN.
The string must be on a single line.

=head3 Numerical Values

The following syntax is used for values:

 _ubyte   N   _ubyte_xy  N N    _ubyte_box   N N N N
 _uint16  N   _uint16_xy N N    _uint16_box  N N N N
 _sint16  N   _sint16_xy N N    _sint16_box  N N N N
 _uint32  N   _uint32_xy N N    _uint32_box  N N N N
 _sint32  N   _sint32_xy N N    _sint32_box  N N N N
 _real32  N   _sint32_xy N N    _sint32_box  N N N N

 _ubyte[CNT]   N * CNT   _ubyte["STRING"]
 _uint16[CNT]  N * CNT     == _ubyte[strlen] "STRING"
 _sint16[CNT]  N * CNT
 _uint32[CNT]  N * CNT
 _sint32[CNT]  N * CNT

 N is numerical value:
   standard floating point representation
     matching regular expression:
     /^([+-]?)(?=\d|\.\d)\d*(\.\d*)?([Ee]([+-]?\d+))?$/

   "XXX" or 'XXX' where each X is ASCII character.
    Each character defines a single ASCII character value.

   0xNNNN, value is corresponding hexadecimal value. 

   eEnumValueName, where eEnumValueName is the symbolic
    name for an enumerated Attribute value.
    Example:
      DuplexPageMode:
         eDuplexHorizontalBinding  0
         eDuplexVertialBinding     1

Each of these statements defines an object of the appropriate
type with the appropriate number of entries.  To be precise,
the object is a hash:
  { type=><type information>, values=>[ v1, v2, ... ] }

When binary output is generated, the appropriate byte
ordering and tags are used.

The enumerated option names and values are defined in the
PXL Feature Reference,
Version 2.0, Appendix G.

=head3 Embedded Data

The following syntax is used for embedded data:

  _data[CNT]  N * CNT bytes


When binary output is generated the appropriate embedded
data tag is added to the output stream.

=head3 Raw Data

  _raw[CNT]   N * CNT bytes

These are simply bytes which are place directly in the output
stream.

It may be nessary to do this for various purposes such
as a binary image or other item.

=head3 Attribute

The following output is used to represent an attribute tag:

 @Attribute

The appropriate binary tag and corresponding attribute
numerical value will be placed in the output stream.

=head3 Operator

The following output is used for an operator:

 Operator
 !Operator

where Operator is the name of an operator.
The appropriate operator
numerical value will be placed in the output stream.

The form !Operator is used when specifying new or overriding
attbribute values,
and causes all of the existing optional attribute values
to be removed before updating the option.
This is useful when there are several conflicting option
values to be updated.

=head3 Examples:

Input:
 # set media source
 _ubyte 1 @MediaSource BeginPage

 Output:
 0xc0 0x01 0xf8 0xf8 0x26 0x43

 _ubyte[5] 'A' 'R' 'I' 'E' 'L'  @FontName SetFont

 0xc8 0xc1 0x05 0x00 0x41 0x52 0x49 0x45
 0x4c 0xf8 0xa8 0x6f

=head2 Parse_PJL


 ($status, ${hash} ) = Parse_PJL( $input, $options, $read_fh );

This routine parses the text in $input and reads from
$readh_fh for PJL information.  This information has
the format:
   <ESC>%-12345X           UEL
   [@PJL ...[<CR>]<LF>]*   PJL

The PJL sets the various options and commands.  These
are processed and the results returned in a hash.

  $hash->{lines}[]  PJL lines, no UEL
  $hash->{SET}{key} = value
     @PJL SET KEY=VALUE
  $hash->{command}{key} = value
     @PJL INFO VALUE
  $hash->{commentset}{key} = value
     @PJL COMMENT SET KEY=VALUE

The $status return value is any text that follows the
PJL information and UEL.

=head1 IMPLEMENTATION NOTES

The information used to implement the PCL and PXL
routines were taken from
GhostPCL version 1.8 code, and used under the terms of the License.

   Portions Copyright (C) 2001 artofcode LLC.
   Portions Copyright (C) 1996, 2001 Artifex Software Inc.
   Portions Copyright (C) 1988, 2000 Aladdin Enterprises.
   This software is based in part on the work of the Independent JPEG Group.
   http://www.artifex.com/licensing/index.htm

The main source of the code for the PCL parser was the pcl/pcommand.c file.
The main source of the code for the PXL parser was the pxl/pxparse.c file.

The PXL parsing and other code was checked extensively.
Information in the Artifix Software code was compared
to various HP Documents,
most of which are marked as HP Confidential, and then to actual
output of various drivers provided for Microsoft XP and other
systems.

The methodology for parsing PCL
is quite simple.  A non-deterministic parser is used,
whose states are determined by values in a set of tables.
The FSM will use several tables to determine its next
action.  For most input scanning, the action is to simply send the character
to the output stream.  For others, such as FF and ESC, you need to call a
routine to handle them.

When an escape sequences is found, the longest escape sequence found is put into
a buffer and then the FSM works on the buffer.  The escape sequences have two
formats:

 Form 1:  ESC X
    X is 48-126   ('0'-'~')
 Form 2:  ESC X y z1 # z2 # z3 ... Zn [DATA]
    X is 33-47    ('!' - '/')
    y is 96-126   (''' - '~')
    # is [+-] digits.fraction 48-57 ('0'-'9'), 43 (+), 45 (-)
    z is 96-126   (''' - '~')
    Z is 64-94    ('@' - '^')
 
These tables used by the FSM are implemented by a set of Perl Hash
data structures; in principle, each hash entry corresponds to a
position in the escape sequence.  For example:  ESC ( d 800 D would
have an entry:

  $esc->{'('}->{'d'}->{'D'}

Each hash entry corresponding to an escape sequence has the following keys and values.

=over 2

=item Comment

A comment indicating the escape sequence corresponding the entry.

=item Options

A set of flags indicating what checks and/or additional information
is needed by the escape sequence.  The $pac_byte_data flag indicates
that the PCL escape sequence numerical value corresponds to a byte count,
and the indicated number of additonal bytes are read.

=item Replacement Value

The 'replace' field contains a replacement string for the option.
This is set up by the call to Parse_PCL specifying a new set of default
values.

=item Support Routine

The 'routine' field is a reference to a routine that is called to do
any processing that is needed to handle the PCL sequence.

=back

The PXL parser also uses a FSM.
The PXL input consists
of values, attributes to set to the specified values, and
operators which use the specified attributes.

Value setting is done using a set of options of the form:
 <valuekey>[value bytes]
The PXL parser extracts the information and generates a hash
record:
  { type=><value type>, value=>[ v1, v2, ...] }

Attributes settings are done using:
  [value] <attr_byte_flag> <a1>
  [value] <attr_int_flag>  <a1><a2>

Each attribute is assigned a unique index or key value.
The <a1> or <a1><a2> bytes are used to determine the index
of the attribute.  The attribute values are then put into an
argument hash for the operator.  To simplify debugging,
the hash uses the official name of the attribute and
stores the key as part of the value:

  $args = 
  { 'Model' =>{ value=> { value }, attr => index }.
    'Font'  =>{ value=> { value }, attr => index },
    ...
  };

When the operator is finally encountered,  it uses
the arguments in the $args hash.

If the input is being parsed to set new values,
then the new values are stored in the $replacement hash,
using the operator as an index:

  $replacement = {
    BeginPage => {
       PageSize => { ... }
       Orientation => { ... },
    }
  }

When the input is being parsed for replacement,
when the operator is processed, the $replacement
hash is checked for a set of replacement values
for the various arguments.  These are then appended
to the current argument list for the operator.
This is allowed by the PXL parsing rules, and the
last attribute value parsed is used as the value
for the operator.


=head2 UEL, End of Job, and Form Feed Processing

The Parse_PCL routine is acting as a filter, and thus has to take
into consideration some very odd interactions between the input
data stream and printer requirements.

When a new set of defaults has been specified, then these will be
placed after each Form Feed, as long as the Form Feed is not
followed by an End of Job (ESC E) or UEL sequence.

This is necessary to prevent extra pages being output.

Similary, the new defaults will be placed any End of Job (ESC E)
sequences at the start of the job.

The Parse_PXL routine does not need to perform these actions.

=head2 New Page Callback

You can specify a callback routine to be called when a new page
is to be started.  This routine can update the PCL values to be
used by calling Parse_PCL_init.

=cut

sub Parse_PCL_init();
sub Parse_PCL_setup( $ $ $ $ );
sub Parse_PCL( $ $ $ $ $ );
sub Parse_PXL( $ $ $ $ $ );
sub Parse_PXL_setup( $ $ $ );
sub PXL_assembler($ $ $ $ $);

# 
# Parsing is done by an FSM that will use several tables to determine its next
# action.  For most input scanning, the action is to simply send the character
# to the output stream.  For others, such as FF and ESC, you need to call a
# routine to handle them.
# 
# When an escape sequences is found, the longest escape sequence found is put into
# a buffer and then the FSM works on the buffer.
# 
# Form 1:  ESC X
#    X is 48-126   ('0'-'~')
# Form 2:  ESC X y z1 # z2 # z3 ... Zn [DATA]
#    X is 33-47    ('!' - '/')
#    y is 96-126   (''' - '~')
#    # is [+-] digits.fraction 48-57 ('0'-'9'), 43 (+), 45 (-)
#    z is 96-126   (''' - '~')
#    Z is 64-94    ('@' - '^')
# 


# The actions to take when reading input chars
# these will routines indicating what processing to take
# The various options listed here are used to determine the parameters
# needed for an escape sequence
#  Negative arguments may be clamped to 0, give an error, cause the 
#  command to be ignored, or be passed to the command. 
my $pca_neg_action = 3;
my $pca_neg_clamp = 0;
my $pca_neg_error = 1;
my $pca_neg_ignore = 2;
my $pca_neg_ok = 3;
#  Arguments in the range 32K to 64K-1 may be clamped; give an error, 
#  cause the command to be ignored; or be passed to the command. 
my $pca_big_action = 0xc;
my $pca_big_clamp = 0;
my $pca_big_error = 4;
my $pca_big_ignore = 8;
my $pca_big_ok = 0xc;
#  Indicate whether the command is followed by data bytes. 
my $pca_byte_data = 0x10;
my $pca_bytes = $pca_neg_error | $pca_big_ok | $pca_byte_data;
#  Indicate whether the command is allowed in raster graphics mode. 
my $pca_raster_graphics = 0x20;
#  Indicate whether the command should be called while defining a macro. 
my $pca_in_macro = 0x40;
#  Indicate whether the command is allowed in rtl mode 
my $pca_in_rtl = 0x80;

# decoding input
# the actions to take when reading an escape sequencence
my( $raw_input_actions, $two_char_escape, $parm_escape );

# the main buffer of input characters
my @PCL_input_chars;
my @PCL_input_tokens;
# the pcl_sequence found - longest one found

my $PCL_read_fh;
my $PCL_write_fh;
my $PCL_replace;
my $PCL_init_str;
my $PCL_init_pending;
my $PCL_newpage_callback;


sub Parse_PCL_read_stdin(){
    my($line, $count);
    if( $PCL_read_fh and not eof($PCL_read_fh) and  not defined ($count = read($PCL_read_fh, $line, 1024)) ){
	rip_die("Parse_PCL_read_stdin: error reading file - $!", $EXIT_PRNERR);
    } elsif( $count ){
	push @PCL_input_chars, (split(//, $line));
    }
    return( $count );
}

sub Peek_PCL_input(){
    Parse_PCL_read_stdin() if( @PCL_input_chars < 32 );
    if( @PCL_input_chars ){
	return $PCL_input_chars[0];
    }
    return( undef );
}

sub Get_PCL_input(){
    if( @PCL_input_chars or Parse_PCL_read_stdin() ){
	return shift @PCL_input_chars;
    }
    rip_die("Get_PCL_input: unexpected EOF",
	$EXIT_PRNERR_NORETRY);
}

sub Parse_PCL_read_stdin_token(){
    my($line);
    while( $PCL_read_fh and defined ($line = <$PCL_read_fh>) ){
	$_ = $line;
	chomp; s/\015//;
	s/^\s+//;
	s/\s+$//;
	next if /^\#/ or /^$/;
	push @PCL_input_tokens, split(' ');
	last if @PCL_input_tokens;
    }
    return( scalar(@PCL_input_tokens) );
}

sub Peek_PCL_input_token(){
    Parse_PCL_read_stdin_token() if( not @PCL_input_tokens );
    if( @PCL_input_tokens ){
	return $PCL_input_tokens[0];
    }
    return( undef );
}

sub Get_PCL_input_token(){
    Parse_PCL_read_stdin_token() if( not @PCL_input_tokens );
    if( @PCL_input_tokens ){
	return shift @PCL_input_tokens;
    }
    rip_die("Get_PCL_input_token: unexpected EOF",
	$EXIT_PRNERR_NORETRY);
}

my $PXL_operator_replacements = {};
my $PCL_escape_replacements = {};

my $PXL_offset = 0;
my $PCL_text_format = 0;

# the last input corresponding to the command
# the current set of arguments
my $PXL_args = {};
# the current set of values
my $PXL_values;

#
## Process a buffer of PCL XL commands. #
sub Get_PXL_input() {
    if( @PCL_input_chars or Parse_PCL_read_stdin() ){
	++$PXL_offset;
	my $c = shift @PCL_input_chars;
	return($c);
    }
    rip_die("Get_PXL_input: unexpected EOF at offset $PXL_offset",
	$EXIT_PRNERR_NORETRY);
}

sub show_esc( $ );
sub tr_esc(@);
sub tr_hex(@);


sub show_esc( $ ){
    my($v) = @_;
    my $o = ord($v);
    if( $o < 32 or $o > 126 ){
	$v = '\x' . sprintf('%02x', $o );
    }
    return($v);
}


sub tr_esc(@){
    my $line = "";
    foreach( @_ ){
	if( ref $_ eq 'ARRAY' ){
	    $line .= tr_esc( @{$_} );
	} elsif( defined $_ ){
	    $line .= join( '', map {show_esc($_)} split(//, $_));
	}
    }
    return( $line );
}


sub tr_hex(@){
    my $line = "";
    foreach( @_ ){
	if( ref $_ eq 'ARRAY' ){
	    $line .= tr_hex( @{$_} );
	} else {
	    $line .= join( '', map {sprintf('\\x%02x',ord $_)} split(//, $_));
	}
    }
    return( $line );
}


# PCL_outchar:
#   put out the single chararacter in the right format

sub PCL_outchar( $ $ ){
    my($header, $c) = @_;
    my(@out);
    push @out, $c;
    if( $PCL_init_pending ){
	unshift @out, $PCL_init_pending;
	$PCL_init_pending = "";
    }
    D100("PCL_outchar: OUT $header '" . tr_esc(@out) . '\'' );
    if( $PCL_text_format ){
	print $PCL_write_fh $header .' "' . tr_esc(@out) . "\"\n" if $PCL_write_fh;
    } else {
	print $PCL_write_fh @out if $PCL_write_fh;
    }
}

# Parse_PCL_setup( $init, $action, $header, $replace )
#   $init is true then reset
#   $header = this is put at the start of each page of the
#     document
#   $replace = replacement values
#     these are parsed and their values are used rather than
#     those in input stream
#
#   action    start of page      infile options
#   0         header             replaced
#   1         header+options     replaced
#   2         header+options     deleted


sub Parse_PCL_setup( $ $ $ $ ){
    my ( $init, $actions, $header, $replace ) =  @_;
    my @saved_input_chars = @PCL_input_chars;
    my ($saved_read_fh, $saved_write_fh, $saved_callback ) =
	( $PCL_read_fh, $PCL_write_fh, $PCL_newpage_callback );
    if( $init or not $raw_input_actions ){
	Parse_PCL_init();
    }
    $PCL_replace = ($actions or 1);
    $PCL_init_str .= $header if $header;
    $PCL_init_str .= $replace if $actions;
    D100("Parse_PCL_setup: actions $actions, using $PCL_replace, replace '" . tr_esc( $replace) . '\' init \'' . tr_esc( $PCL_init_str) . '\''  );
    Parse_PCL( $replace, 0, undef, undef, undef );
    D100("Parse_PCL_setup: after setup " . Dumper($PCL_escape_replacements)); 
    @PCL_input_chars = @saved_input_chars;
    ( $PCL_read_fh, $PCL_write_fh, $PCL_newpage_callback ) =
	($saved_read_fh, $saved_write_fh, $saved_callback );
    $PCL_replace = 0;
}

# Parse_PCL - we read from $PCL_fh and write to STDOUT
# We may read part of the input already
sub Parse_PCL( $ $ $ $ $ ){
    my ( $read, $text_format, $read_fh, $write_fh, $newpage_callback ) =  @_;
    my( $save_text_format, $save_read_fh,
	$save_write_fh, $save_newpage_callback ) =
	( $PCL_text_format, $PCL_read_fh,
	    $PCL_write_fh, $PCL_newpage_callback );
    ( $PCL_text_format,
	$PCL_read_fh, $PCL_write_fh, $PCL_newpage_callback )
     = ( $text_format,
	$read_fh, $write_fh, $newpage_callback );
    if( not $raw_input_actions ){
	Parse_PCL_init();
    }
    $PCL_replace = 0 if not defined( $PCL_replace);
    @PCL_input_chars = split(//, $read ) if defined $read;
    D100("Parse_PCL: PCL_replace $PCL_replace, text_format $text_format, starting input '" . tr_esc( @PCL_input_chars) . '\'' );
    my $status = undef;
    $PCL_init_pending = ($PCL_init_str or "");
    while( defined Peek_PCL_input() ){
	# D100("Parse_PCL: left ". (scalar @PCL_input_chars) );
	# no more input?
	# we look at the first character
	my $c1 = Get_PCL_input();
	my $action = $raw_input_actions->{$c1};
 	#D100("Parse_PCL: '" . show_esc($c1) . "', ". Dumper($action) );
	if( not defined $action ){
	    PCL_outchar( "CHAR", $c1 );
	    next;
	}
	D100("Parse_PCL: '" . show_esc($c1). "' "  . $action->{'comment'} );
	($status) = $action->{'routine'}->($c1, $action) if $action->{'routine'};
	last if $status;
    }
    $read = "";
    $read = "\033%-12345X" if $status;
    $read .= join('',@PCL_input_chars);
    ( $PCL_text_format,
	$PCL_read_fh, $PCL_write_fh, $PCL_newpage_callback )
     = ( $save_text_format,
	$save_read_fh, $save_write_fh, $save_newpage_callback );
    return( $status, $read );
}

#
# The raw input characters
#
sub DEFINE_CONTROL( $ $ $ ){
    my( $char, $comment, $routine ) = @_;
    $raw_input_actions->{$char} = { comment=>$comment, routine=>$routine };
    #if( $routine ){ $routine->(); }
}

#
# The entries here have the format ESC X - i.e. - single character escapes
#
sub DEFINE_ESCAPE_ARGS( $ $ $ $ ){
    my( $char, $comment, $options, $routine ) = @_;
    $two_char_escape->{$char}
	= { comment=>$comment, routine=>$routine, options => $options };
    #if( $routine ){ $routine->(); }
}

#
# we set up the parameterized groups
#

sub DEFINE_CLASS_COMMAND_ARGS( $ $ $ $ $ $ ){
	my( $char, $groupchar, $termchar, $comment, $options, $routine ) = @_; 
	if( (ref $termchar) eq 'ARRAY' ){
	    for my $v (@{$termchar}){
		$parm_escape->{$char}->{$groupchar}->{$v}
		    = { comment=> $comment, routine=>$routine, options => $options };
	    }
	} else {
	    $parm_escape->{$char}->{$groupchar}->{$termchar}
		= { comment=> $comment, routine=>$routine, options => $options };
	}
	# if( $routine ){ $routine->(); }
}

sub Parse_PCL_init(){
    D100("Parse_PCL_init");
    if( not $raw_input_actions ){
	D100("Parse_PCL_init - setup");
	DEFINE_CLASS_COMMAND_ARGS( '%',"\0", 'A', "Enter PCL Mode",  $pca_neg_ok|$pca_big_ok|$pca_in_macro|$pca_in_rtl, undef ); # \&pcl_enter_pcl_mode 
	DEFINE_CLASS_COMMAND_ARGS( '%',"\0", 'B', "Enter HP-GL/2 Mode",  $pca_neg_ok | $pca_big_ok | $pca_in_rtl, undef ); # \&rtl_enter_hpgl_mode 
	DEFINE_CLASS_COMMAND_ARGS( '%',"\0", 'X', "UEL",  $pca_neg_ok|$pca_big_error|$pca_in_rtl, \&pcl_exit_language  );
	DEFINE_CLASS_COMMAND_ARGS( '&', 'a', 'C', "Horizontal Cursor Position Columns",  $pca_neg_ok | $pca_big_ok, undef ); # \&horiz_cursor_pos_columns 
	DEFINE_CLASS_COMMAND_ARGS( '&', 'a', 'G', "Duplex Page Side Select",  $pca_neg_ignore|$pca_big_ignore, undef ); # \&pcl_duplex_page_side_select 
	DEFINE_CLASS_COMMAND_ARGS( '&', 'a', 'H', "Horizontal Cursor Position Decipoints",  $pca_neg_ok | $pca_big_ok, undef ); # \&horiz_cursor_pos_decipoints 
	DEFINE_CLASS_COMMAND_ARGS( '&', 'a', 'L', "Left Margin",  $pca_neg_ok | $pca_big_ignore, undef ); # \&set_left_margin 
	DEFINE_CLASS_COMMAND_ARGS( '&', 'a', 'M', "Right Margin",  $pca_neg_ok | $pca_big_ignore, undef ); # \&set_right_margin 
	DEFINE_CLASS_COMMAND_ARGS( '&', 'a', 'N', "Negative Motion",  $pca_neg_error | $pca_big_error, undef ); # \&pcl_negative_motion 
	DEFINE_CLASS_COMMAND_ARGS( '&', 'a', 'P', "Print Direction",  $pca_neg_ok | $pca_big_ignore, undef ); # \&set_print_direction 
	DEFINE_CLASS_COMMAND_ARGS( '&', 'a', 'R', "Vertical Cursor Position Rows",  $pca_neg_ok | $pca_big_clamp, undef ); # \&vert_cursor_pos_rows 
	DEFINE_CLASS_COMMAND_ARGS( '&', 'a', 'V', "Vertical Cursor Position Decipoints",  $pca_neg_ok | $pca_big_ok, undef ); # \&vert_cursor_pos_decipoints 
	DEFINE_CLASS_COMMAND_ARGS( '&', 'b', 'M', "Monochrome Printing",  $pca_neg_ok | $pca_big_ignore | $pca_in_rtl, undef ); # \&set_print_mode 
	DEFINE_CLASS_COMMAND_ARGS( '&', 'b', 'W', "Appletalk Configuration",  $pca_bytes, undef ); # \&pcl_appletalk_configuration 
	DEFINE_CLASS_COMMAND_ARGS( '&', 'c', 'T', "Text Path Direction",  $pca_neg_ok | $pca_big_error, undef ); # \&pcl_text_path_direction 
	DEFINE_CLASS_COMMAND_ARGS( '&', 'd', '@', "Disable Underline",  $pca_neg_ignore | $pca_big_ignore, undef ); # \&pcl_disable_underline 
	DEFINE_CLASS_COMMAND_ARGS( '&', 'd', 'D', "Enable Underline",  $pca_neg_ignore | $pca_big_ignore, undef ); # \&pcl_enable_underline 
	DEFINE_CLASS_COMMAND_ARGS( '&', 'f', 'S', "Push/Pop Cursor",  $pca_neg_ok | $pca_big_ignore, undef ); # \&push_pop_cursor 
	DEFINE_CLASS_COMMAND_ARGS( '&', 'f', 'X', "Macro Control",  $pca_neg_error|$pca_big_error|$pca_in_macro, undef ); # \&pcl_macro_control 
	DEFINE_CLASS_COMMAND_ARGS( '&', 'f', 'Y', "Assign Macro ID",  $pca_neg_error|$pca_big_error, undef ); # \&pcl_assign_macro_id 
	DEFINE_CLASS_COMMAND_ARGS( '&', 'k', 'G', "Line Termination",  $pca_neg_ok | $pca_big_ignore, undef ); # \&set_line_termination 
	DEFINE_CLASS_COMMAND_ARGS( '&', 'k', 'H', "Horizontal Motion Index",  $pca_neg_ok | $pca_big_clamp, undef ); # \&set_horiz_motion_index 
	DEFINE_CLASS_COMMAND_ARGS( '&', 'k', 'S', "Set Pitch Mode",  $pca_neg_error|$pca_big_error, undef ); # \&pcl_set_pitch_mode 
	DEFINE_CLASS_COMMAND_ARGS( '&', 'l', 'A', "Page Size",  $pca_neg_ok | $pca_big_ignore, undef ); # \&set_page_size 
	DEFINE_CLASS_COMMAND_ARGS( '&', 'l', 'C', "Vertical Motion Index",  $pca_neg_ok | $pca_big_ignore, undef ); # \&set_vert_motion_index 
	DEFINE_CLASS_COMMAND_ARGS( '&', 'l', 'D', "Line Spacing",  $pca_neg_ok | $pca_big_ignore, undef ); # \&set_line_spacing 
	DEFINE_CLASS_COMMAND_ARGS( '&', 'l', 'E', "Top Margin",  $pca_neg_ok | $pca_big_ignore, undef ); # \&set_top_margin 
	DEFINE_CLASS_COMMAND_ARGS( '&', 'l', 'F', "Text Length",  $pca_neg_ok | $pca_big_ignore, undef ); # \&set_text_length 
	DEFINE_CLASS_COMMAND_ARGS( '&', 'l', 'G', "Output Bin Selection",  $pca_neg_error|$pca_big_error, undef ); # \&pcl_output_bin_selection 
	DEFINE_CLASS_COMMAND_ARGS( '&', 'l', 'H', "Paper Source",  $pca_neg_ok | $pca_big_ignore, undef ); # \&set_paper_source 
	DEFINE_CLASS_COMMAND_ARGS( '&', 'l', 'L', "Perforation Skip",  $pca_neg_ok | $pca_big_ignore, undef ); # \&set_perforation_skip 
	DEFINE_CLASS_COMMAND_ARGS( '&', 'l', 'M', "Media Type",  $pca_neg_ok | $pca_big_ignore, undef ); # \&pcl_media_type 
	DEFINE_CLASS_COMMAND_ARGS( '&', 'l', 'O', "Page Orientation",  $pca_neg_ok | $pca_big_ignore, undef ); # \&set_logical_page_orientation 
	DEFINE_CLASS_COMMAND_ARGS( '&', 'l', 'S', "Simplex/Duplex Print",  $pca_neg_ignore|$pca_big_ignore, undef ); # \&pcl_simplex_duplex_print 
	DEFINE_CLASS_COMMAND_ARGS( '&', 'l', 'T', "Job Separation",  $pca_neg_error|$pca_big_error, undef ); # \&pcl_job_separation 
	DEFINE_CLASS_COMMAND_ARGS( '&', 'l', 'U', "Left Offset Registration",  $pca_neg_ok | $pca_big_ignore, undef ); # \&set_left_offset_registration 
	DEFINE_CLASS_COMMAND_ARGS( '&', 'l', 'X', "Number of Copies",  $pca_neg_ignore|$pca_big_clamp, undef ); # \&pcl_number_of_copies 
	DEFINE_CLASS_COMMAND_ARGS( '&', 'l', 'Z', "Top Offset Registration",  $pca_neg_ok | $pca_big_ignore, undef ); # \&set_top_offset_registration 
	DEFINE_CLASS_COMMAND_ARGS( '&', 'n', 'W', "Alphanumeric ID Data",  $pca_bytes, undef ); # \&pcl_alphanumeric_id_data 
	DEFINE_CLASS_COMMAND_ARGS( '&', 'p', 'C', "Palette Control",  $pca_neg_ok | $pca_big_ignore | $pca_in_rtl, undef ); # \&palette_control 
	DEFINE_CLASS_COMMAND_ARGS( '&', 'p', 'I', "Palette Control ID",  $pca_neg_ok | $pca_big_ignore | $pca_in_rtl, undef ); # \&set_ctrl_palette_id 
	DEFINE_CLASS_COMMAND_ARGS( '&', 'p', 'S', "Select Palette",  $pca_neg_ok | $pca_big_ignore | $pca_in_rtl, undef ); # \&set_sel_palette_id 
	DEFINE_CLASS_COMMAND_ARGS( '&', 'p', 'X', "Transparent Mode",  $pca_bytes, undef ); # \&pcl_transparent_mode 
	DEFINE_CLASS_COMMAND_ARGS( '&', 'r', 'F', "Flush All Pages",  $pca_neg_error|$pca_big_error, \&pcl_flush_all_pages );
	DEFINE_CLASS_COMMAND_ARGS( '&', 's', 'C', "End of Line Wrap",  $pca_neg_error|$pca_big_error, undef ); # \&pcl_end_of_line_wrap 
	DEFINE_CLASS_COMMAND_ARGS( '&', 't', 'P', "Text Parsing Method",  $pca_neg_error | $pca_big_error, undef ); # \&pcl_text_parsing_method 
	DEFINE_CLASS_COMMAND_ARGS( '&', 'u', 'D', "Set Unit of Measure",  $pca_neg_error|$pca_big_error, undef ); # \&pcl_set_unit_of_measure 
	DEFINE_CLASS_COMMAND_ARGS( '(',"\0", '@', "Default Font Primary",  $pca_neg_error|$pca_big_error, undef ); # \&pcl_select_default_font_primary 
	DEFINE_CLASS_COMMAND_ARGS( '(',"\0", 'X', "Primary Font Selection ID",  $pca_neg_error|$pca_big_error, undef ); # \&pcl_primary_font_selection_id 
	DEFINE_CLASS_COMMAND_ARGS( '(',"\0", ['A'..'W','Y'..'^'], "Primary Symbol Set",  $pca_neg_error|$pca_big_error, undef ); # \&pcl_primary_symbol_set 
	DEFINE_CLASS_COMMAND_ARGS( '(', 'f', 'W', "Define Symbol Set",  $pca_bytes, undef ); # \&pcl_define_symbol_set 
	DEFINE_CLASS_COMMAND_ARGS( '(', 's', 'B', "Primary Stroke Weight",  $pca_neg_ok|$pca_big_error, undef ); # \&pcl_primary_stroke_weight 
	DEFINE_CLASS_COMMAND_ARGS( '(', 's', 'H', "Primary Pitch",  $pca_neg_error|$pca_big_error, undef ); # \&pcl_primary_pitch 
	DEFINE_CLASS_COMMAND_ARGS( '(', 's', 'P', "Primary Spacing",  $pca_neg_ignore|$pca_big_ignore, undef ); # \&pcl_primary_spacing 
	DEFINE_CLASS_COMMAND_ARGS( '(', 's', 'S', "Primary Style",  $pca_neg_error|$pca_big_clamp, undef ); # \&pcl_primary_style 
	DEFINE_CLASS_COMMAND_ARGS( '(', 's', 'T', "Primary Typeface",  $pca_neg_error|$pca_big_ok, undef ); # \&pcl_primary_typeface 
	DEFINE_CLASS_COMMAND_ARGS( '(', 's', 'V', "Primary Height",  $pca_neg_error|$pca_big_error, undef ); # \&pcl_primary_height 
	DEFINE_CLASS_COMMAND_ARGS( '(', 's', 'W', "Character Data",  $pca_bytes, undef ); # \&pcl_character_data 
	DEFINE_CLASS_COMMAND_ARGS( ')',"\0", '@', "Default Font Secondary",  $pca_neg_error|$pca_big_error, undef ); # \&pcl_select_default_font_secondary 
	DEFINE_CLASS_COMMAND_ARGS( ')',"\0", 'X', "Secondary Font Selection ID",  $pca_neg_error|$pca_big_error, undef ); # \&pcl_secondary_font_selection_id 
	DEFINE_CLASS_COMMAND_ARGS( ')',"\0", ['A'..'W','Y'..'^'], "Secondary Symbol Set",  $pca_neg_error|$pca_big_error, undef ); # \&pcl_secondary_symbol_set 
	DEFINE_CLASS_COMMAND_ARGS( ')', 's', 'B', "Secondary Stroke Weight",  $pca_neg_ok|$pca_big_error, undef ); # \&pcl_secondary_stroke_weight 
	DEFINE_CLASS_COMMAND_ARGS( ')', 's', 'H', "Secondary Pitch",  $pca_neg_error|$pca_big_error, undef ); # \&pcl_secondary_pitch 
	DEFINE_CLASS_COMMAND_ARGS( ')', 's', 'P', "Secondary Spacing",  $pca_neg_ignore|$pca_big_ignore, undef ); # \&pcl_secondary_spacing 
	DEFINE_CLASS_COMMAND_ARGS( ')', 's', 'S', "Secondary Style",  $pca_neg_error|$pca_big_clamp, undef ); # \&pcl_secondary_style 
	DEFINE_CLASS_COMMAND_ARGS( ')', 's', 'T', "Secondary Typeface",  $pca_neg_error|$pca_big_ok, undef ); # \&pcl_secondary_typeface 
	DEFINE_CLASS_COMMAND_ARGS( ')', 's', 'V', "Secondary Height",  $pca_neg_error|$pca_big_error, undef ); # \&pcl_secondary_height 
	DEFINE_CLASS_COMMAND_ARGS( ')', 's', 'W', "Font Header",  $pca_bytes, undef ); # \&pcl_font_header 
	DEFINE_CLASS_COMMAND_ARGS( '*', 'b', 'L', "Line Path",  $pca_neg_ok | $pca_big_ignore | $pca_in_rtl, undef ); # \&set_line_path 
	DEFINE_CLASS_COMMAND_ARGS( '*', 'b', 'M', "Set Compresion Method",  $pca_raster_graphics | $pca_neg_ok | $pca_big_ignore | $pca_in_rtl, undef ); # \&set_compression_method 
	DEFINE_CLASS_COMMAND_ARGS( '*', 'b', 'V', "Transfer Raster Plane",  $pca_raster_graphics | $pca_bytes | $pca_in_rtl, undef ); # \&transfer_raster_plane 
	DEFINE_CLASS_COMMAND_ARGS( '*', 'b', 'W', "Transfer Raster Row",  $pca_raster_graphics | $pca_bytes | $pca_in_rtl, undef ); # \&transfer_raster_row 
	DEFINE_CLASS_COMMAND_ARGS( '*', 'b', 'Y', "Raster Y Offset",  $pca_raster_graphics | $pca_neg_ok | $pca_big_clamp | $pca_in_rtl, undef ); # \&raster_y_offset 
	DEFINE_CLASS_COMMAND_ARGS( '*', 'c', 'A', "Horizontal Rectangle Size Units",  $pca_neg_error | $pca_big_error | $pca_in_rtl, undef ); # \&pcl_horiz_rect_size_units 
	DEFINE_CLASS_COMMAND_ARGS( '*', 'c', 'B', "Vertical Rectangle Size Units",  $pca_neg_error | $pca_big_error | $pca_in_rtl, undef ); # \&pcl_vert_rect_size_units 
	DEFINE_CLASS_COMMAND_ARGS( '*', 'c', 'D', "Assign Font ID",  $pca_neg_error|$pca_big_error, undef ); # \&pcl_assign_font_id 
	DEFINE_CLASS_COMMAND_ARGS( '*', 'c', 'E', "Character Code",  $pca_neg_error|$pca_big_ok, undef ); # \&pcl_character_code 
	DEFINE_CLASS_COMMAND_ARGS( '*', 'c', 'F', "Font Control",  $pca_neg_error|$pca_big_error, undef ); # \&pcl_font_control 
	DEFINE_CLASS_COMMAND_ARGS( '*', 'c', 'G', "Pattern ID",  $pca_neg_ignore | $pca_big_ignore | $pca_in_rtl, undef ); # \&set_pattern_id 
	DEFINE_CLASS_COMMAND_ARGS( '*', 'c', 'H', "Horizontal Rectangle Size Decipoints",  $pca_neg_error | $pca_big_error | $pca_in_rtl, undef ); # \&pcl_horiz_rect_size_decipoints 
	DEFINE_CLASS_COMMAND_ARGS( '*', 'c', 'K', "HP-GL/2 Plot Horizontal Size",  $pca_neg_error|$pca_big_error, undef ); # \&pcl_hpgl_plot_horiz_size 
	DEFINE_CLASS_COMMAND_ARGS( '*', 'c', 'L', "HP-GL/2 Plot Vertical Size",  $pca_neg_error|$pca_big_error, undef ); # \&pcl_hpgl_plot_vert_size 
	DEFINE_CLASS_COMMAND_ARGS( '*', 'c', 'P', "Fill Rectangular Area",  $pca_neg_ignore | $pca_big_ignore | $pca_in_rtl, undef ); # \&pcl_fill_rect_area 
	DEFINE_CLASS_COMMAND_ARGS( '*', 'c', 'Q', "Pattern Control",  $pca_neg_ignore | $pca_big_ignore, undef ); # \&pattern_control 
	DEFINE_CLASS_COMMAND_ARGS( '*', 'c', 'R', "Symbol Set ID Code",  $pca_neg_error|$pca_big_error, undef ); # \&pcl_symbol_set_id_code 
	DEFINE_CLASS_COMMAND_ARGS( '*', 'c', 'S', "Symbol Set Control",  $pca_neg_ignore|$pca_big_ignore, undef ); # \&pcl_symbol_set_control 
	DEFINE_CLASS_COMMAND_ARGS( '*', 'c', 'T', "Set Picture Frame Anchor Point",  $pca_neg_error|$pca_big_error, undef ); # \&pcl_set_pic_frame_anchor_point 
	DEFINE_CLASS_COMMAND_ARGS( '*', 'c', 'V', "Vertical Rectangle Size Decipoint",  $pca_neg_error | $pca_big_error | $pca_in_rtl, undef ); # \&pcl_vert_rect_size_decipoints 
	DEFINE_CLASS_COMMAND_ARGS( '*', 'c', 'W', "Download Pattern",  $pca_bytes, undef ); # \&download_pcl_pattern 
	DEFINE_CLASS_COMMAND_ARGS( '*', 'c', 'X', "Horizontal Picture Frame Size Decipoints",  $pca_neg_error|$pca_big_error, undef ); # \&pcl_horiz_pic_frame_size_decipoints 
	DEFINE_CLASS_COMMAND_ARGS( '*', 'c', 'Y', "Vertical Picture Frame Size Decipoints",  $pca_neg_error|$pca_big_error, undef ); # \&pcl_vert_pic_frame_size_decipoints 
	DEFINE_CLASS_COMMAND_ARGS( '*', 'i', 'W', "Set Viewing Illuminant",  $pca_bytes | $pca_in_rtl, undef ); # \&set_view_illuminant 
	DEFINE_CLASS_COMMAND_ARGS( '*', 'l', 'O', "Logical Operation",  $pca_neg_ok | $pca_big_error | $pca_in_rtl, undef ); # \&pcl_logical_operation 
	DEFINE_CLASS_COMMAND_ARGS( '*', 'l', 'R', "Pixel Placement",  $pca_neg_ok | $pca_big_ignore | $pca_in_rtl, undef ); # \&pcl_pixel_placement 
	DEFINE_CLASS_COMMAND_ARGS( '*', 'l', 'W', "Color Lookup Tables",  $pca_bytes, undef ); # \&set_lookup_tbl 
	DEFINE_CLASS_COMMAND_ARGS( '*', 'm', 'W', "Download Dither Matrix",  $pca_bytes, undef ); # \&download_dither_matrix 
	DEFINE_CLASS_COMMAND_ARGS( '*', 'o', 'Q', "Print Quality",  $pca_neg_ok | $pca_big_ignore, undef ); # \&pcl_print_quality 
	DEFINE_CLASS_COMMAND_ARGS( '*', 'o', 'W', "Driver Configuration Command",  $pca_bytes | $pca_in_rtl, undef ); # \&set_driver_configuration 
	DEFINE_CLASS_COMMAND_ARGS( '*', 'p', 'P', "Push/Pop Palette",  $pca_neg_ok | $pca_big_ignore | $pca_in_rtl, undef ); # \&push_pop_palette 
	DEFINE_CLASS_COMMAND_ARGS( '*', 'p', 'R', "Pattern Reference Point",  $pca_neg_ok | $pca_big_ignore | $pca_in_rtl, undef ); # \&set_pat_ref_pt 
	DEFINE_CLASS_COMMAND_ARGS( '*', 'p', 'X', "Horizontal Cursor Position Units",  $pca_neg_ok | $pca_big_ok | $pca_in_rtl, undef ); # \&horiz_cursor_pos_units 
	DEFINE_CLASS_COMMAND_ARGS( '*', 'p', 'Y', "Vertical Cursor Position Units",  $pca_neg_ok | $pca_big_ok | $pca_in_rtl, undef ); # \&vert_cursor_pos_units 
	DEFINE_CLASS_COMMAND_ARGS( '*', 'r', 'A', "Start Raster Graphics",  $pca_raster_graphics | $pca_neg_ok | $pca_big_clamp | $pca_in_rtl, undef ); # \&start_graphics_mode 
	DEFINE_CLASS_COMMAND_ARGS( '*', 'r', 'B', "End Raster Graphics (Old)",  $pca_raster_graphics | $pca_neg_ok | $pca_big_ok | $pca_in_rtl, undef ); # \&end_graphics_mode_B 
	DEFINE_CLASS_COMMAND_ARGS( '*', 'r', 'C', "End Raster Graphics (New)",  $pca_raster_graphics | $pca_neg_ok | $pca_big_ok | $pca_in_rtl, undef ); # \&end_graphics_mode_C 
	DEFINE_CLASS_COMMAND_ARGS( '*', 'r', 'F', "Raster Graphics Presentation Mode",  $pca_raster_graphics | $pca_neg_ok | $pca_big_ignore | $pca_in_rtl, undef ); # \&set_graphics_presentation_mode 
	DEFINE_CLASS_COMMAND_ARGS( '*', 'r', 'S', "Source Raster Width",  $pca_raster_graphics | $pca_neg_ok | $pca_big_clamp | $pca_in_rtl, undef ); # \&set_src_raster_width 
	DEFINE_CLASS_COMMAND_ARGS( '*', 'r', 'T', "Source Raster_Height",  $pca_raster_graphics | $pca_neg_ok | $pca_big_clamp | $pca_in_rtl, undef ); # \&set_src_raster_height 
	DEFINE_CLASS_COMMAND_ARGS( '*', 'r', 'U', "Simple Color Mode",  $pca_neg_ok | $pca_in_rtl, undef ); # \&pcl_simple_color_space 
	DEFINE_CLASS_COMMAND_ARGS( '*', 's', 'I', "Inquire Readback Entity",  $pca_neg_error|$pca_big_error, undef ); # \&pcl_inquire_readback_entity 
	DEFINE_CLASS_COMMAND_ARGS( '*', 's', 'M', "Free Space",  $pca_neg_ok|$pca_big_ok, undef ); # \&pcl_free_space 
	DEFINE_CLASS_COMMAND_ARGS( '*', 's', 'T', "Set Readback Location Type",  $pca_neg_error|$pca_big_error, undef ); # \&pcl_set_readback_loc_type 
	DEFINE_CLASS_COMMAND_ARGS( '*', 's', 'U', "Set Readback Location Unit",  $pca_neg_error|$pca_big_error, undef ); # \&pcl_set_readback_loc_unit 
	DEFINE_CLASS_COMMAND_ARGS( '*', 's', 'X', "Echo",  $pca_neg_ok|$pca_big_error, undef ); # \&pcl_echo 
	DEFINE_CLASS_COMMAND_ARGS( '*', 't', 'H', "Destination Raster Width",  $pca_raster_graphics | $pca_neg_ok | $pca_big_ignore | $pca_in_rtl, undef ); # \&set_dest_raster_width 
	DEFINE_CLASS_COMMAND_ARGS( '*', 't', 'I', "Gamma Correction",  $pca_neg_ignore | $pca_big_ignore, undef ); # \&set_gamma_correction 
	DEFINE_CLASS_COMMAND_ARGS( '*', 't', 'J', "Render Algorithm",  $pca_neg_ok | $pca_big_ignore | $pca_in_rtl, undef ); # \&set_render_algorithm 
	DEFINE_CLASS_COMMAND_ARGS( '*', 't', 'R', "Raster Graphics Resolution",  $pca_raster_graphics | $pca_neg_ok | $pca_big_clamp | $pca_in_rtl, undef ); # \&set_graphics_resolution 
	DEFINE_CLASS_COMMAND_ARGS( '*', 't', 'V', "Destination Raster Height",  $pca_raster_graphics | $pca_neg_ok | $pca_big_ignore | $pca_in_rtl, undef ); # \&set_dest_raster_height 
	DEFINE_CLASS_COMMAND_ARGS( '*', 'v', 'A', "Color Component 1",  $pca_neg_ok | $pca_big_error | $pca_raster_graphics | $pca_in_rtl, undef ); # \&set_color_comp_1 
	DEFINE_CLASS_COMMAND_ARGS( '*', 'v', 'B', "Color Component 2",  $pca_neg_ok | $pca_big_error | $pca_raster_graphics | $pca_in_rtl, undef ); # \&set_color_comp_2 
	DEFINE_CLASS_COMMAND_ARGS( '*', 'v', 'C', "Color Component 3",  $pca_neg_ok | $pca_big_error | $pca_raster_graphics | $pca_in_rtl, undef ); # \&set_color_comp_3 
	DEFINE_CLASS_COMMAND_ARGS( '*', 'v', 'I', "Assign Color Index",  $pca_neg_ok | $pca_big_ignore | $pca_raster_graphics | $pca_in_rtl, undef ); # \&assign_color_index 
	DEFINE_CLASS_COMMAND_ARGS( '*', 'v', 'N', "Source Transparency Mode",  $pca_neg_ignore | $pca_big_ignore | $pca_in_rtl, undef ); # \&set_source_transparency_mode 
	DEFINE_CLASS_COMMAND_ARGS( '*', 'v', 'O', "Pattern Transparency Mode",  $pca_neg_ignore | $pca_big_ignore | $pca_in_rtl, undef ); # \&set_pattern_transparency_mode 
	DEFINE_CLASS_COMMAND_ARGS( '*', 'v', 'S', "Set Foreground",  $pca_neg_ok, undef ); # \&set_foreground 
	DEFINE_CLASS_COMMAND_ARGS( '*', 'v', 'T', "Select Current Pattern",  $pca_neg_ignore | $pca_big_ignore | $pca_in_rtl, undef ); # \&select_current_pattern 
	DEFINE_CLASS_COMMAND_ARGS( '*', 'v', 'W', "Configure Image Data",  $pca_bytes | $pca_in_rtl, undef ); # \&pcl_configure_image_data 
    #    DEFINE_CONTROL("\000", "(plain char)", \&pcl_plain_char);
    #    DEFINE_CONTROL("\001", "(plain char)", \&pcl_plain_char);
    #    DEFINE_CONTROL("\010", "BS", \&cmd_BS);
    #    DEFINE_CONTROL("\015", "CR", \&cmd_CR);
	DEFINE_CONTROL("\014", "FF", \&cmd_FF);
    #    DEFINE_CONTROL("\011", "HT", \&cmd_HT);
    #    DEFINE_CONTROL("\012", "LF", \&cmd_LF);
    #    DEFINE_CONTROL("\017", "SI", \&pcl_SI);
    #    DEFINE_CONTROL("\016", "SO", \&pcl_SO);
	DEFINE_CONTROL("\033", "ESC", \&cmd_ESC);
	DEFINE_ESCAPE_ARGS( '9', "Clear Horizontal Margins", 0, undef ); # \&pcl_clear_horizontal_margins 
	DEFINE_ESCAPE_ARGS( '=', "Half Line Feed", 0, undef ); # \&pcl_half_line_feed 
	DEFINE_ESCAPE_ARGS( 'A', "Print Font Page",  $pca_in_rtl, undef ); # \&pcl_print_font_page 
	DEFINE_ESCAPE_ARGS( 'E', "Printer Reset",  $pca_in_rtl, \&pcl_printer_reset );
	DEFINE_ESCAPE_ARGS( 'Y', "Enable Display Functions",  $pca_in_macro, undef ); # \&pcl_enable_display_functions 
	DEFINE_ESCAPE_ARGS( 'Z', "Disable Display Functions",  $pca_in_macro, undef ); # \&pcl_disable_display_functions 

	D800("Parse_PCL_init: raw_input_actions " . Dumper( $raw_input_actions ) );
	D800("Parse_PCL_init: two_char_escape " . Dumper( $two_char_escape ) );
	D800("Parse_PCL_init: parm_escape " . Dumper( $parm_escape ) );
    }
    $PXL_operator_replacements = {};
    foreach my $otag ( keys %{$PCL_escape_replacements} ){
	my $value = $PCL_escape_replacements->{$otag};
	delete $value->{replace} if $value;
    }
    $PCL_escape_replacements = {};
    $PCL_init_str = "";
}

# This is called when a new page is to be output.
# If nothing has been put out on the current page, then
# no header information will be put out.  The FF will cause
# a page eject.  If it is followed immediately by another
# FF, you want the page header

sub pcl_newpage(){
    if( not $PCL_init_pending and $PCL_newpage_callback ){
	$PCL_newpage_callback->();
    }
    $PCL_init_pending = $PCL_init_str;
    return 0;
}
# flush all pages
sub pcl_flush_all_pages { D100("pcl_flush_all_pages"); return pcl_newpage(); }
# printer reset ESC E
sub pcl_printer_reset { D100("pcl_printer_reset"); return pcl_newpage(); }
# UEL 
sub pcl_exit_language { D100("pcl_exit_language"); return pcl_newpage(); }


# Form Feed: print the FF and then do new page stuff
sub cmd_FF($ $){
    my( $c, $action ) =@_;
    D100("cmd_FF");
    PCL_outchar("FF", $c);
    return pcl_newpage();
}

sub cmd_ESC {
    # we now get the longest possible escape string
    my($esc) = @_;
    my @pcl_sequence = ( $esc );
    my @out;
    my($status);
    # we look for a character 48-126 for two char sequences
    my $c1 = Get_PCL_input();
    goto DONE if not defined $c1;
    push @pcl_sequence, $c1;
    my $v = ord $c1;
    D200("cmd_ESC: checking '$c1' ($v)");
    # we look for a character 48-126 for two char sequence
    if( $v >= 48 and $v <= 126 ){
	# we have a two char sequence
	my $action = $two_char_escape->{$c1};
	@out = @pcl_sequence;
	D100("cmd_ESC: found TWO CHAR " . tr_esc(@out));
	if( $PCL_replace ){
	    if( $PCL_replace == 1 ){
		$action->{replace} = join('', @pcl_sequence);
	    } else {
		$action->{replace} = '';
	    }
	    $PCL_escape_replacements->{$c1} = $action;
	} else {
	    # this is a bit tricky.
	    # If we get a page eject then we need to send the
	    # PCL_init_str AFTER the sequence
	    D100("cmd_ESC: $action->{comment} options ". sprintf("0x%x", $action->{options}));
	    my $replace;
	    if( $action->{routine} ){
		($status) = $action->{routine}->($action);
	    }
	    $replace = $action->{replace};
	    if( not defined $replace and $PCL_text_format ){
		my $line = $action->{comment};
		$line .= '  "' . tr_esc(@out) . '"';
		print $PCL_write_fh $line . "\n" if $PCL_write_fh;
		@out = ();
	    }
	    @out = ( $replace ) if defined( $replace );
	    if( not defined($status) and $PCL_init_pending ){
		unshift( @out, $PCL_init_pending );
		$PCL_init_pending = "";
	    }
	    D100("cmd_ESC: out " . tr_esc(@out));
	    if( @out ){
		if( $PCL_text_format ){
		    print $PCL_write_fh '"' . tr_esc(@out) . "\"\n" if $PCL_write_fh;
		} else {
		    print $PCL_write_fh @out if $PCL_write_fh;
		}
	    }
	}
	$two_char_escape->{$c1} = $action;
    } elsif ( $v >= 33 and $v <= 47 ){
	# we have a multi-char sequence
	my $found = 0;
	# first, we look for the end of the escape sequence
	# format is ESC <parmkey> <groupkey> [num value] <optkey>
	#   - the <groupkey> can be missing for some sequences
	#   - the [num value] is optional, and is 0 if missing
	#   - the '[num value] optkey' sections can be repeated
	#     until the terminal char ord value is 64-94 ('@'- '^')
	while( not $found and defined ($c1 = Get_PCL_input()) ){
	    push @pcl_sequence, $c1;
	    $v = ord $c1;
	    # termination is 64 - 94
	    if( $v >= 64 and $v <= 94){
		$found = 1;
	    }
	}
	D100("cmd_ESC: MULTI " . tr_esc(@pcl_sequence) );
	if( not $found ){
	    if( $PCL_init_pending ){
		unshift( @pcl_sequence, $PCL_init_pending );
		$PCL_init_pending = "";
	    }
	    if( @pcl_sequence ){
		if( $PCL_text_format ){
		    print $PCL_write_fh '"' . tr_esc(@pcl_sequence) . "\"\n" if $PCL_write_fh;
		} else {
		    print $PCL_write_fh @pcl_sequence if $PCL_write_fh;
		}
	    }
	}
	# we use the 'while' so we can break from the loop
	# format is ESC <parmkey> <groupkey> [num value] <optkey>
	#   - the <groupkey> can be missing for some sequences
	#   - the [num value] is optional, and is 0 if missing
	#   - the '[num value] optkey' sections can be repeated
	# $seq is the index into the escape string pcl_sequence
	# pcl_sequence[0] is ESC
	# pcl_sequence[1] is <parmkey>
	# pcl_sequence[2] is <groupkey> (if it is not missing)
	# This maps into 
	#  $hash->{parmkey}{groupkey}
	# or 
	#  $hash->{parmkey}{\000} if no groupkey
	while( $found ){
	    my $seq = 0;
	    ++$seq;
	    # we have to now split this up and do each part
	    my $parmkey = $pcl_sequence[$seq];
	    my $parmset = $parm_escape->{$parmkey};
	    D400("cmd_ESC: parmset " . Dumper($parmset) );
	    if( not $parmset ){
		D0("Unknown escape sequence " . tr_esc(@pcl_sequence));
		if( $PCL_init_pending ){
		    unshift( @pcl_sequence, $PCL_init_pending );
		    $PCL_init_pending = "";
		}
		if( @pcl_sequence ){
		    if( $PCL_text_format ){
			print $PCL_write_fh '"' . tr_esc(@pcl_sequence) . "\"\n" if $PCL_write_fh;
		    } else {
			print $PCL_write_fh @pcl_sequence if $PCL_write_fh;
		    }
		}
		last;
	    }
	    ++$seq;
	    # we have to check for a group char
	    my $groupkey = $pcl_sequence[$seq];
	    if( $groupkey =~ /[0-9+\-]/ ){
		$groupkey = "\000";
	    } else {
		# we move to the next character
		++$seq;
	    }
	    my $groupset = $parmset->{$groupkey};
	    D400("cmd_ESC: groupset " . Dumper($groupset) );
	    if( not $groupset ){
		D0("Unknown escape sequence " . tr_esc(@pcl_sequence));
		if( $PCL_init_pending ){
		    unshift( @pcl_sequence, $PCL_init_pending );
		    $PCL_init_pending = "";
		}
		if( @pcl_sequence ){
		    if( $PCL_text_format ){
			print $PCL_write_fh '"' . tr_esc(@pcl_sequence) . "\"\n" if $PCL_write_fh;
		    } else {
			print $PCL_write_fh @pcl_sequence if $PCL_write_fh;
		    }
		}
		last;
	    }
	    while( $seq < @pcl_sequence ){
		@out = ($pcl_sequence[0], $parmkey);
		push @out, $groupkey if( $groupkey ne "\000");
		my $value = "";
		$c1 = $pcl_sequence[$seq];
		while( ($c1 =~ m/[\d\+\-]/) ){
		    $value .= $c1;
		    ++$seq;
		    $c1 = $pcl_sequence[$seq];
		}
		my $parm = $c1;
		++$seq;
		# OK, hwe have found [num val] parm
		# we have to upper case parm if it is a lower case
		$v = ord $parm;
		if( $v >= 96 and $v <= 126 ){
		    $v -= 32; 
		    $parm = chr( $v );
		}
		my $optset = $groupset->{$parm};
		D200("cmd_ESC: optset " . Dumper($optset) );
		if( not $optset ){
		    push @out, $value, $parm;
		    D0("Unknown escape sequence " . tr_esc(@out));
		    if( $PCL_init_pending ){
			unshift( @pcl_sequence, $PCL_init_pending );
			$PCL_init_pending = "";
		    }
		    if( @out ){
			if( $PCL_text_format ){
			    print $PCL_write_fh '"' . tr_esc(@out) . "\"\n" if $PCL_write_fh;
			} else {
			    print $PCL_write_fh @out if $PCL_write_fh;
			}
		    }
		    next;
		}
		$value = "0" if( $value eq "" );
		my $nv = $value+0;
		my $opts = ($optset->{'options'} || 0);
		if( $nv < 0 and ($opts & $pca_neg_action) != $pca_neg_ok ){
		    D0("WARNING: PCL escape sequence option negative " .  tr_esc(@out,$value,$parm));
		    $value = "0";
		    $nv = 0;
		}
		if( $nv > 32767 and ($opts & $pca_big_action) != $pca_big_ok ){
		    D0("WARNING: PCL escape sequence option too large " .  tr_esc(@out,$value,$parm));
		    $value = "32767";
		    $nv = 32767;
		}
		# and we have the fixed up version now
		push @out, "$value", $parm;
		# we have data bytes following
		my @data;
		if( ($opts & $pca_byte_data) ){
		    my $i = 0;
		    while( $i < $nv and defined( $c1 = Get_PCL_input() ) ){
			push @data, $c1;
			++$i;
		    }
		    D100("cmd_ESC: with DATA " . tr_esc(@out,@data) );
		}
		if( $PCL_replace ){
		    if( $PCL_replace == 1 ){
			$optset->{replace} = join('', @out,@data);
		    } else {
			$optset->{replace} = '';
		    }
		    my @tag = ($parmkey);
		    push @tag, $groupkey if( $groupkey ne "\000");
		    push @tag, "0" if( $groupkey eq "\000");
		    push @tag, $parm;
		    my $otag = join('', @tag);
		    $PCL_escape_replacements->{$otag} = $optset;
		} else {
		    # This is a bit tricky
		    # Some PCL escape sequences can cause a page
		    # eject.  These need to be sent, but they
		    # should also cause the PCL_init_pending to
		    # be added AFTER the sequene has been output
		    if( $optset->{routine} ){
			# status will be defined if we need to hold off
			# sending output
			$status = $optset->{routine}->($nv, $optset, \@out, \@data );
		    }
		    my $replace = $optset->{replace};
		    D100("cmd_ESC: initial out " . tr_esc( @out,@data ) ) if defined($replace);
			
		    my $text;
		    if( defined( $replace ) ){
			@out = ($replace);
			@data = ();
			$text = '"' . tr_esc(@out).'"';
		    } else {
			if( $PCL_text_format ){
			    $text = "$optset->{'comment'} = $value \""
				. tr_esc(@out) . '"';
			    if( @data ){
				$text .= ' DATA "' . tr_esc(@data) . '"';
			    }
			}
		    }
		    if( not defined( $status ) and $PCL_init_pending ){
			my( @p ); 
			push @p, $PCL_init_pending;
			D100("cmd_ESC: init_pending " . tr_esc(@p) );
			if( $PCL_text_format ){
			    print $PCL_write_fh '"' . tr_esc(@p) . "\"\n" if $PCL_write_fh;
			} else {
			    print $PCL_write_fh @p if $PCL_write_fh;
			}
			$PCL_init_pending = "";
		    }
		    if( @out ){
			if( $PCL_text_format ){
			    print $PCL_write_fh $text . "\n" if $PCL_write_fh;
			} else {
			    D100("cmd_ESC: cmd out " . tr_esc(@out, @data));
			    print $PCL_write_fh (@out, @data) if $PCL_write_fh;
			}
			@out = ();
		    }
		    # so we hold off sending the status
		}
	    }
	    last;
	}
    }
 DONE:
    D100("cmd_ESC: status $status" ) if defined $status;
    return $status;
}

my $Pcount = 0;
sub Callback_PCL{
    D100("Callback_PCL");
    Parse_PCL_setup(1, 0, "HEADER${Pcount}", "&u${Pcount}D" );
    ++$Pcount;
}

sub TestParsePCL($ $ $){
    my( $debug, $test, $file) = @_;
    my $r;
	
    $Foomatic::Debugging::debug = $debug;
    my $fh;
    if( $file ){
	if( not $file or $file =~ /STDOUT/i ){
	    $fh = \*STDOUT;
	} else {
	    $fh = new FileHandle ">$file" or die "cannot open $file - $!";
	}
    }
    D0("TestParsePCL: calling Parse_PCL_setup");
    print $fh "TestParsePCL\n";
    if( $test & 0x10){
	print $fh "\ntest 0x10\n";
	$r = Parse_PCL_setup(1, 0, "HEADER", "&u911D" );
	$r = Parse_PCL("\ntest(s1WxY&ud123d456d789D(1@", 0, undef, $fh, undef);
    }
    if( $test & 0x20){
	print $fh "\ntest 0x20\n";
	$r = Parse_PCL_setup(1, 1, "HEADER", "&u911D" );
	$r = Parse_PCL("\ntest(s1WxY&ud123d456d789D(1@", 0, undef, $fh, undef);
    }
    if( $test & 0x40){
	print $fh "\ntest 0x20\n";
	$r = Parse_PCL_setup(1, 2, "HEADER", "&u911D" );
	$r = Parse_PCL("\ntest(s1WxY&ud123d456d789D(1@", 0, undef, $fh, undef);
    }
    my $t = 
	"\ntest(s1WxY&ud123d456d789D(1@".
	"\ntest(s1WxY&ud123d456d789DE%-12345X";
    if( $test & 0x01){
	print $fh "\ntest 0x01\n";
	$Pcount = 0;
	$r = Parse_PCL_setup(1, 1, "HEADER", "&u911D" );
	print $fh "\nSTART Parse " . tr_esc($t) ."\n" or die "cannot write $file - $!";
	$r = Parse_PCL( $t, 0, undef, $fh, \&Callback_PCL);
	D100( "RETURNED '" . tr_esc( split(//,$r) ) .'\'' );
    }
    if( $test & 0x02){
	print $fh "\ntest 0x02\n";
	$Pcount = 0;
	$r = Parse_PCL_setup(1, 1, "HEADER", "&u911D" );
	print $fh "\nSTART Decode " . tr_esc($t) ."\n" or die "cannot write $file - $!";
	$r = Parse_PCL( $t, 1, undef, $fh, \&Callback_PCL );
	D100( "RETURNED '" . tr_esc( split(//,$r) ) .'\'' );
    }
}

# TestParsePCL(0x300, 0x03,"/tmp/b");

# ---------------- PXL Parsing Information ---------------- #
# symbolic names for the various items

 my $pxt_ubyte = 0xc0;
 my $pxt_uint16 = 0xc1;
 my $pxt_attr_ubyte = 0xf8;
 my $pxt_attr_uint16 = 0xf9;
 my $pxt_dataLength = 0xfa;
 my $pxt_dataLengthByte = 0xfb;


# ---------------- Endian Information ---------------- #
# input is bigendian
my $PXL_big_endian = 0;
# architecture is big_endian - do a check if undefined
my $PXL_big_endian_arch;
my @PXL_saved_endian_info;

# ---------------- Enumerated values ---------------- #

my $pxeArcDirection_t = [ 'eClockWise', 'eCounterClockWise', ];
my $pxeCharSubModeArray_t = [ 'eNoSubstitution', 'eVerticalSubstitution', ];
my $pxeClipMode_t = [ 'eNonZeroWinding', 'eEvenOdd', ];
my $pxeFillMode_t = [ 'eNonZeroWinding', 'eEvenOdd', ];
my $pxeClipRegion_t = [ 'eInterior', 'eExterior', ];
my $pxeColorDepth_t = [ 'e1Bit', 'e4Bit', 'e8Bit', ];
my $pxeColorimetricColorSpace_t = [ '', '', '', '', '', 'eCRGB', ];
my $pxeColorMapping_t = [ 'eDirectPixel', 'eIndexedPixel', ];
my $pxeColorSpace_t = [ 'eNoColorSpace', 'eGray', 'eRGB', '', '', '', 'eSRGB', ];
my $pxeCompressMode_t = [ 'eNoCompression', 'eRLECompression', 'eJPEGCompression', 'eDeltaRowCompression', ];
my $pxeDataOrg_t = [ 'eBinaryHighByteFirst', 'eBinaryLowByteFirst', ];
my $pxeDataSource_t = [ 'eDefault', ];
my $pxeDataType_t = [ 'eUByte', 'eSByte', 'eUInt16', 'eSInt16', ];
my $pxeDitherMatrix_t = [ 'eDeviceBest', ];
my $pxeDuplexPageMode_t = [ 'eDuplexHorizontalBinding', 'eDuplexVerticalBinding', ];
my $pxeDuplexPageSide_t = [ 'eFrontMediaSide', 'eBackMediaSide', ];
my $pxeErrorReport_t = [ 'eNoReporting', 'eBackChannel', 'eErrorPage', 'eBackChAndErrPage', 'eNWBackChannel', 'eNWErrorPage', 'eNWBackChAndErrPage', ];
my $pxeLineCap_t = [ 'eButtCap', 'eRoundCap', 'eSquareCap', 'eTriangleCap', ];
my $pxeLineJoin_t = [ 'eMiterJoin', 'eRoundJoin', 'eBevelJoin', 'eNoJoin', ];
my $pxeMeasure_t = [ 'eInch', 'eMillimeter', 'eTenthsOfAMillimeter', ];
my $pxeMediaDestination_t = [ 'eDefaultDestination', 'eFaceDownBin', 'eFaceUpBin', 'eJobOffsetBin', ];
my $pxeMediaSize_t = [ 'eLetterPaper', 'eLegalPaper', 'eA4Paper', 'eExecPaper', 'eLedgerPaper', 'eA3Paper', 'eCOM10Envelope', 'eMonarchEnvelope', 'eC5Envelope', 'eDLEnvelope', 'eJB4Paper', 'eJB5Paper', 'eB5Envelope', 'eB5Paper', 'eJPostcard', 'eJDoublePostcard', 'eA5Paper', 'eA6Paper', 'eJB6Paper', 'eJIS8K', 'eJIS16K', 'eJISExec', '', '', '', '', '', '', '', '', '', '', '', '', '', '', '', '', '', '', '', '', '', '', '', '', '', '', '', '', '', '', '', '', '', '', '', '', '', '', '', '', '', '', '', '', '', '', '', '', '', '', '', '', '', '', '', '', '', '', '', '', '', '', '', '', '', '', '', '', '', '', '', '', '', '', 'eDefaultPaperSize', ];
my $pxeMediaSource_t = [ 'eDefaultSource', 'eAutoSelect', 'eManualFeed', 'eMultiPurposeTray', 'eUpperCassette', 'eLowerCassette', 'eEnvelopeTray', 'eThirdCassette', ];
my $pxeMediaType_t = [ 'eDefaultType', ];
my $pxeOrientation_t = [ 'ePortraitOrientation', 'eLandscapeOrientation', 'eReversePortrait', 'eReverseLandscape', 'eDefaultOrientation', ];
my $pxePatternPersistence_t = [ 'eTempPattern', 'ePagePattern', 'eSessionPattern', ];
my $pxeSimplexPageMode_t = [ 'eSimplexFrontSide', ];
my $pxeTxMode_t = [ 'eOpaque', 'eTransparent', ];
my $pxeWritingMode_t = [ 'eHorizontal', 'eVertical', ];
my $pxeAdaptiveHalftone_t = [ 'eDisableAH', 'eEnableAH', ];
my $pxeHalftoneMethod_t = [ 'eHighLPI', 'eMediumLPI', 'eLowLPI', ];
my $pxeColorTrapping_t = [ 'eDisableCT', 'eMax', 'eNormal', 'eLight', ];
my $pxeNeutralAxis_t = [ 'eTonerBlack', 'eProcessBlack', ];
my $pxeColorTreatment_t = [ 'eNoTreatment', 'eScreenMatch', 'eVivid', ];

# ---------------- Attribute values ---------------- #
my $pxd_scalar = 1;
my $pxd_xy = 2;
my $pxd_box = 4;
my $pxd_array = 8;
my $pxd_data = 0x10;
my $pxd_raw = 0x20;
my $pxd_structure = 0xff;
# Representation 
my $pxd_ubyte = 0x100;
my $pxd_uint16 = 0x200;
my $pxd_uint32 = 0x400;
my $pxd_sint16 = 0x800;
my $pxd_sint32 = 0x1000;
my $pxd_any_int = $pxd_ubyte | $pxd_uint16 | $pxd_uint32 | $pxd_sint16 | $pxd_sint32;
my $pxd_real32 = 0x2000;
my $pxd_representation = 0x3f00;

sub PXL_dump_mask( $ ){
	my($v) = @_;
	my $line = "";
	$line .= ",scalar" if $v & $pxd_scalar;
	$line .= ",xy" if $v & $pxd_xy;
	$line .= ",box" if $v & $pxd_box;
	$line .= ",array" if $v & $pxd_array;
	$line .= ",ubyte" if $v & $pxd_ubyte;
	$line .= ",uint16" if $v & $pxd_uint16;
	$line .= ",uint32" if $v & $pxd_uint32;
	$line .= ",sint16" if $v & $pxd_sint16;
	$line .= ",sint32" if $v & $pxd_sint32;
	$line .= ",real32" if $v & $pxd_real32;
	$line =~ s/^,//;
	return( $line );
}

sub checkCharAngle( $ ) { return 0; }
sub checkCharBoldValue( $ ) { return 0; }
sub checkCharScale( $ ) { return 0; }
sub checkDestinationSize( $ ) { return 0; }
sub checkDitherMatrixDataType( $ ) { return 0; }
sub checkDitherMatrixDepth( $ ) { return 0; }
sub checkDitherMatrixSize( $ ) { return 0; }
sub checkGrayLevel( $ ) { return 0; }
sub checkPageAngle( $ ) { return 0; }
sub checkPageScale( $ ) { return 0; }
sub checkRGBColor( $ ) { return 0; }
sub checkSourceHeight( $ ) { return 0; }
sub checkUnitsPerMeasure( $ ) { return 0; }

# attribute types
#  [ name, mask, limit, check_proc ]

my $px_attributes = [
  [ '',   ], # 0
  [ '',   ], # 1
  [ 'PaletteDepth', $pxd_scalar|$pxd_ubyte, $pxeColorDepth_t,  ], # PaletteDepth = 2 # 2
  [ 'ColorSpace', $pxd_scalar|$pxd_ubyte, $pxeColorSpace_t,  ], # ColorSpace # 3
  [ 'NullBrush', $pxd_scalar|$pxd_ubyte, 0,  ], # NullBrush # 4
  [ 'NullPen', $pxd_scalar|$pxd_ubyte, 0,  ], # NullPen # 5
  [ 'PaletteData', $pxd_array|$pxd_ubyte,  ], # PaletteData # 6
  [ '',   ], # 7
  [ 'PatternSelectID', $pxd_scalar|$pxd_sint16,  ], # PatternSelectID = 8 # 8
  [ 'GrayLevel', $pxd_scalar|$pxd_ubyte|$pxd_real32, undef, \&checkGrayLevel,  ], # GrayLevel # 9
  [ '',   ], # 10
  [ 'RGBColor', $pxd_array|$pxd_ubyte|$pxd_real32, undef, \&checkRGBColor,  ], # RGBColor = 11 # 11
  [ 'PatternOrigin', $pxd_xy|$pxd_sint16,  ], # PatternOrigin # 12
  [ 'NewDestinationSize', $pxd_xy|$pxd_uint16, undef, \&checkDestinationSize,  ], # NewDestinationSize # 13
  [ 'PrimaryArray', $pxd_array|$pxd_ubyte,  ], # PrimaryArray = 14 # 14
  [ 'PrimaryDepth', $pxd_scalar|$pxd_ubyte, $pxeColorDepth_t,  ], # PrimaryDepth = 15 # 15
  [ '',   ], # 16
  [ 'ColorimetricColorSpace', $pxd_scalar|$pxd_ubyte, $pxeColorimetricColorSpace_t,  ], # ColorimetricColorSpace = 17 # 17
  [ 'XYChromaticities', $pxd_array|$pxd_real32,  ], # XYChromaticities = 18 # 18
  [ 'WhiteReferencePoint', $pxd_array|$pxd_real32,  ], # WhiteReferencePoint = 19 # 19
  [ 'CRGBMinMax', $pxd_array|$pxd_real32,  ], # CRGBMinMax = 20 # 20
  [ 'GammaGain', $pxd_array|$pxd_real32,  ], # GammaGain = 21 # 21
  [ '',   ], # 22
  [ '',   ], # 23
  [ '',   ], # 24
  [ '',   ], # 25
  [ '',   ], # 26
  [ '',   ], # 27
  [ '',   ], # 28
  [ 'AllObjectTypes', $pxd_scalar|$pxd_ubyte, $pxeColorTrapping_t,  ], # AllObjects NB ColorTrapping is largest enum # 29
  [ 'TextObjects', $pxd_scalar|$pxd_ubyte, $pxeColorTrapping_t,  ], # TextObjects = 30 # 30
  [ 'VectorObjects', $pxd_scalar|$pxd_ubyte, $pxeColorTrapping_t,  ], # VectorObjects = 31 # 31
  [ 'RasterObjects', $pxd_scalar|$pxd_ubyte, $pxeColorTrapping_t,  ], # RasterObjects = 32 # 32
  [ 'DeviceMatrix', $pxd_scalar|$pxd_ubyte, $pxeDitherMatrix_t,  ], # DeviceMatrix = 33 # 33
  [ 'DitherMatrixDataType', $pxd_scalar|$pxd_ubyte, $pxeDataType_t, \&checkDitherMatrixDataType,  ], # DitherMatrixDataType # 34
  [ 'DitherOrigin', $pxd_xy|$pxd_ubyte|$pxd_uint16|$pxd_sint16,  ], # DitherOrigin # 35
  [ 'MediaDestination', $pxd_scalar|$pxd_ubyte, 255,  ], # MediaDestination # 36
  [ 'MediaSize', $pxd_array|$pxd_scalar|$pxd_ubyte, 255,  ], # MediaSize # 37
  [ 'MediaSource', $pxd_scalar|$pxd_ubyte, 255,  ], # MediaSource # 38
  [ 'MediaType', $pxd_array|$pxd_ubyte,  ], # MediaType # 39
  [ 'Orientation', $pxd_scalar|$pxd_ubyte, 255,  ], # Orientation -- illegal values only produce a warning! # 40
  [ 'PageAngle', $pxd_scalar|$pxd_uint16|$pxd_sint16, undef, \&checkPageAngle,  ], # PageAngle # 41
  [ 'PageOrigin', $pxd_xy|$pxd_ubyte|$pxd_uint16|$pxd_sint16,  ], # PageOrigin # 42
  [ 'PageScale', $pxd_xy|$pxd_ubyte|$pxd_uint16|$pxd_real32, undef, \&checkPageScale,  ], # PageScale # 43
  [ 'ROP3', $pxd_scalar|$pxd_ubyte, 255,  ], # ROP3 # 44
  [ 'TxMode', $pxd_scalar|$pxd_ubyte, $pxeTxMode_t,  ], # TxMode # 45
  [ '',   ], # 46
  [ 'CustomMediaSize', $pxd_xy|$pxd_uint16|$pxd_real32,  ], # CustomMediaSize = 47 # 47
  [ 'CustomMediaSizeUnits', $pxd_scalar|$pxd_ubyte, $pxeMeasure_t,  ], # CustomMediaSizeUnits # 48
  [ 'PageCopies', $pxd_scalar|$pxd_uint16,  ], # PageCopies # 49
  [ 'DitherMatrixSize', $pxd_xy|$pxd_uint16, undef, \&checkDitherMatrixSize,  ], # DitherMatrixSize # 50
  [ 'DitherMatrixDepth', $pxd_scalar|$pxd_ubyte, $pxeColorDepth_t, \&checkDitherMatrixDepth,  ], # DitherMatrixDepth # 51
  [ 'SimplexPageMode', $pxd_scalar|$pxd_ubyte, $pxeSimplexPageMode_t,  ], # SimplexPageMode # 52
  [ 'DuplexPageMode', $pxd_scalar|$pxd_ubyte, $pxeDuplexPageMode_t,  ], # DuplexPageMode # 53
  [ 'DuplexPageSide', $pxd_scalar|$pxd_ubyte, $pxeDuplexPageSide_t,  ], # DuplexPageSide # 54
  [ '',   ], # 55
  [ '',   ], # 56
  [ '',   ], # 57
  [ '',   ], # 58
  [ '',   ], # 59
  [ '',   ], # 60
  [ '',   ], # 61
  [ '',   ], # 62
  [ '',   ], # 63
  [ '',   ], # 64
  [ 'ArcDirection', $pxd_scalar|$pxd_ubyte, $pxeArcDirection_t,  ], # ArcDirection = 65 # 65
  [ 'BoundingBox', $pxd_box|$pxd_ubyte|$pxd_uint16|$pxd_sint16,  ], # BoundingBox # 66
  [ 'DashOffset', $pxd_scalar|$pxd_ubyte|$pxd_uint16|$pxd_sint16,  ], # DashOffset # 67
  [ 'EllipseDimension', $pxd_xy|$pxd_ubyte|$pxd_uint16,  ], # EllipseDimension # 68
  [ 'EndPoint', $pxd_xy|$pxd_ubyte|$pxd_uint16|$pxd_sint16,  ], # EndPoint # 69
  [ 'FillMode', $pxd_scalar|$pxd_ubyte, $pxeFillMode_t,  ], # FillMode # 70
  [ 'LineCapStyle', $pxd_scalar|$pxd_ubyte, $pxeLineCap_t,  ], # LineCapStyle # 71
  [ 'LineJoinStyle', $pxd_scalar|$pxd_ubyte, $pxeLineJoin_t,  ], # LineJoinStyle # 72
  [ 'MiterLength', $pxd_scalar|$pxd_ubyte|$pxd_uint16,  ], # MiterLength # 73
  [ 'LineDashStyle', $pxd_array|$pxd_ubyte|$pxd_uint16|$pxd_sint16,  ], # LineDashStyle # 74
  [ 'PenWidth', $pxd_scalar|$pxd_ubyte|$pxd_uint16,  ], # PenWidth # 75
  [ 'Point', $pxd_xy|$pxd_ubyte|$pxd_uint16|$pxd_sint16,  ], # Point # 76
  [ 'NumberOfPoints', $pxd_scalar|$pxd_ubyte|$pxd_uint16,  ], # NumberOfPoints # 77
  [ 'SolidLine', $pxd_scalar|$pxd_ubyte, 0,  ], # SolidLine # 78
  [ 'StartPoint', $pxd_xy|$pxd_ubyte|$pxd_uint16|$pxd_sint16,  ], # StartPoint # 79
  [ 'PointType', $pxd_scalar|$pxd_ubyte, $pxeDataType_t,  ], # PointType # 80
  [ 'ControlPoint1', $pxd_xy|$pxd_ubyte|$pxd_uint16|$pxd_sint16,  ], # ControlPoint1 # 81
  [ 'ControlPoint2', $pxd_xy|$pxd_ubyte|$pxd_uint16|$pxd_sint16,  ], # ControlPoint2 # 82
  [ 'ClipRegion', $pxd_scalar|$pxd_ubyte, $pxeClipRegion_t,  ], # ClipRegion # 83
  [ 'ClipMode', $pxd_scalar|$pxd_ubyte, $pxeClipMode_t,  ], # ClipMode # 84
  [ '',   ], # 85
  [ '',   ], # 86
  [ '',   ], # 87
  [ '',   ], # 88
  [ '',   ], # 89
  [ '',   ], # 90
  [ '',   ], # 91
  [ '',   ], # 92
  [ '',   ], # 93
  [ '',   ], # 94
  [ '',   ], # 95
  [ '',   ], # 96
  [ '',   ], # 97
  [ 'ColorDepth', $pxd_scalar|$pxd_ubyte, $pxeColorDepth_t,  ], # ColorDepth = 98 # 98
  [ 'BlockHeight', $pxd_scalar|$pxd_uint16,  ], # BlockHeight # 99
  [ 'ColorMapping', $pxd_scalar|$pxd_ubyte, $pxeColorMapping_t,  ], # ColorMapping # 100
  [ 'CompressMode', $pxd_scalar|$pxd_ubyte, $pxeCompressMode_t,  ], # CompressMode # 101
  [ 'DestinationBox', $pxd_box|$pxd_uint16,  ], # DestinationBox # 102
  [ 'DestinationSize', $pxd_xy|$pxd_uint16, undef, \&checkDestinationSize,  ], # DestinationSize # 103
  [ 'PatternPersistence', $pxd_scalar|$pxd_ubyte, $pxePatternPersistence_t,  ], # PatternPersistence # 104
  [ 'PatternDefineID', $pxd_scalar|$pxd_sint16,  ], # PatternDefineID # 105
  [ '',   ], # 106
  [ 'SourceHeight', $pxd_scalar|$pxd_uint16, undef, \&checkSourceHeight,  ], # SourceHeight = 107 # 107
  [ 'SourceWidth', $pxd_scalar|$pxd_uint16, undef, \&checkSourceWidth,  ], # SourceWidth # 108
  [ 'StartLine', $pxd_scalar|$pxd_uint16,  ], # StartLine # 109
  [ 'PadBytesMultiple', $pxd_scalar|$pxd_ubyte, 255,  ], # PadBytesMultiple # 110
  [ 'BlockByteLength', $pxd_scalar|$pxd_uint32,  ], # BlockByteLength # 111
  [ '',   ], # 112
  [ '',   ], # 113
  [ '',   ], # 114
  [ 'NumberOfScanLines', $pxd_scalar|$pxd_uint16,  ], # NumberOfScanLines = 115 # 115
  [ '',   ], # 116
  [ '',   ], # 117
  [ '',   ], # 118
  [ '',   ], # 119
  [ 'ColorTreatment', $pxd_scalar|$pxd_ubyte,  ], # 120
  [ '',   ], # 121
  [ '',   ], # 122
  [ '',   ], # 123
  [ '',   ], # 124
  [ '',   ], # 125
  [ '',   ], # 126
  [ '',   ], # 127
  [ '',   ], # 128
  [ 'CommentData', $pxd_array|$pxd_ubyte|$pxd_uint16,  ], # CommentData = 129 # 129
  [ 'DataOrg', $pxd_scalar|$pxd_ubyte, $pxeDataOrg_t,  ], # DataOrg # 130
  [ '',   ], # 131
  [ '',   ], # 132
  [ '',   ], # 133
  [ 'Measure', $pxd_scalar|$pxd_ubyte, $pxeMeasure_t,  ], # Measure = 134 # 134
  [ '',   ], # 135
  [ 'SourceType', $pxd_scalar|$pxd_ubyte, $pxeDataSource_t,  ], # SourceType = 136 # 136
  [ 'UnitsPerMeasure', $pxd_xy|$pxd_uint16|$pxd_real32, undef, \&checkUnitsPerMeasure,  ], # UnitsPerMeasure # 137
  [ '',   ], # 138
  [ 'StreamName', $pxd_array|$pxd_ubyte|$pxd_uint16,  ], # StreamName = 139 # 139
  [ 'StreamDataLength', $pxd_scalar|$pxd_uint32,  ], # StreamDataLength # 140
  [ '',   ], # 141
  [ '',   ], # 142
  [ 'ErrorReport', $pxd_scalar|$pxd_ubyte, $pxeErrorReport_t,  ], # ErrorReport = 143 # 143
  [ '',   ], # 144
  [ 'VUExtension', $pxd_scalar|$pxd_uint32|$pxd_sint32,  ], # VUExtension = 145 # 145
  [ '',   ], # 146
  [ 'VUAttr1', $pxd_scalar|$pxd_ubyte,  ], # VUAttr1 = 147 # 147
  [ '',   ], # 148
  [ '',   ], # 149
  [ '',   ], # 150
  [ '',   ], # 151
  [ '',   ], # 152
  [ '',   ], # 153
  [ '',   ], # 154
  [ '',   ], # 155
  [ '',   ], # 156
  [ '',   ], # 157
  [ '',   ], # 158
  [ '',   ], # 159
  [ '',   ], # 160
  [ 'CharAngle', $pxd_scalar|$pxd_uint16|$pxd_sint16|$pxd_real32, undef, \&checkCharAngle,  ], # CharAngle = 161 # 161
  [ 'CharCode', $pxd_scalar|$pxd_ubyte|$pxd_uint16,  ], # CharCode # 162
  [ 'CharDataSize', $pxd_scalar|$pxd_uint16|$pxd_uint32,  ], # CharDataSize HP spec says us - driver sometimes emits ul # 163
  [ 'CharScale', $pxd_xy|$pxd_ubyte|$pxd_uint16|$pxd_real32, undef, \&checkCharScale,  ], # CharScale # 164
  [ 'CharShear', $pxd_xy|$pxd_ubyte|$pxd_uint16|$pxd_sint16|$pxd_real32, undef, \&checkCharShear,  ], # CharShear # 165
  [ 'CharSize', $pxd_scalar|$pxd_ubyte|$pxd_uint16|$pxd_real32,  ], # CharSize # 166
  [ 'FontHeaderLength', $pxd_scalar|$pxd_uint16,  ], # FontHeaderLength # 167
  [ 'FontName', $pxd_array|$pxd_ubyte|$pxd_uint16,  ], # FontName # 168
  [ 'FontFormat', $pxd_scalar|$pxd_ubyte, 0,  ], # FontFormat # 169
  [ 'SymbolSet', $pxd_scalar|$pxd_uint16,  ], # SymbolSet # 170
  [ 'TextData', $pxd_array|$pxd_ubyte|$pxd_uint16,  ], # TextData # 171
  [ 'CharSubModeArray', $pxd_array|$pxd_ubyte,  ], # CharSubModeArray # 172
  [ 'WritingMode', $pxd_scalar|$pxd_ubyte, $pxeWritingMode_t,  ], # WritingMode # 173
  [ '',   ], # 174
  [ 'XSpacingData', $pxd_array|$pxd_ubyte|$pxd_uint16|$pxd_sint16,  ], # XSpacingData = 175 # 175
  [ 'YSpacingData', $pxd_array|$pxd_ubyte|$pxd_uint16|$pxd_sint16,  ], # YSpacingData # 176
  [ 'CharBoldValue', $pxd_scalar|$pxd_real32, undef, \&checkCharBoldValue,  ], # CharBoldValue # 177
];

# ---------------- Operator definitions ---------------- #

sub pxBeginPage {
    if( $PCL_init_str ){
	if( $PCL_text_format ){
	    print $PCL_write_fh (" ", $PCL_init_str)
		if $PCL_write_fh;
	} else {
	    print $PCL_write_fh
		PXL_assembler( $PCL_init_str, 1, $PXL_big_endian,
			undef, undef )
		if $PCL_write_fh;
	}
    }
    return 0;
}

sub pxEndPage {
    if( $PCL_newpage_callback ){
	$PCL_newpage_callback->();
    }
    return 0;
}

sub pxOpenDataSource {
    my $v = ($PXL_args->{'DataOrg'}{'value'}[0] || 0);
    # eBinaryHighByteFirst = 0
    # eBinaryLowByteFirst  = 1
    #D800("pxOpenDataSource - value $v " . Dumper($PXL_args) );
    D100("pxOpenDataSource - DataOrg $v [eBinaryHighByteFirst,eBinaryLowByteFirst  ]");
    push @PXL_saved_endian_info, $PXL_big_endian;
    $PXL_big_endian = ($v?0:1);
    return 0;
}
sub pxCloseDataSource {
    # D100("pxCloseDataSource " . Dumper($PXL_args) );
    $PXL_big_endian = pop @PXL_saved_endian_info;
    return 0;
}

# ---------------- Tag Decoding ---------------- #

my $px_tag_info = [
#[00]
  ['Null',], ['',], ['',], ['',],
  ['',], ['',], ['',], ['',],
#[08]
  ['',], ['HT',], ['LF',], ['VT',],
  ['FF',], ['CR',], ['',], ['',],
#[10]
  ['',], ['',], ['',], ['',],
  ['',], ['',], ['',], ['',],
#[18]
  ['',], ['',], ['',], ['ESC',\&PXL_check_UEL],
  ['',], ['',], ['',], ['',],
#[20]
  ['Space',], ['',], ['',], ['',],
  ['',], ['',], ['',], ['',],
  ['',], ['',], ['',], ['',],
  ['',], ['',], ['',], ['',],
#[30]
  ['',], ['',], ['',], ['',],
  ['',], ['',], ['',], ['',],
  ['',], ['',], ['',], ['',],
  ['',], ['',], ['',], ['',],
#[40]
  ['',],
  ['BeginSession',\&Fix_operator, undef,   [ 'Measure', 'UnitsPerMeasure' ], [ 'ErrorReport' ]],  # \&pxBeginSession
  ['EndSession',\&Fix_operator, undef,   undef, undef],  # \&pxEndSession
  ['BeginPage',\&Fix_operator,\&pxBeginPage,  [ 'Orientation' ], [ 'MediaSource', 'MediaSize', 'CustomMediaSize', 'CustomMediaSizeUnits', 'SimplexPageMode', 'DuplexPageMode', 'DuplexPageSide', 'MediaDestination', 'MediaType' ]],
  ['EndPage',\&Fix_operator,\&pxEndPage,  undef, [ 'PageCopies' ]],
  [''],
  ['VendorUnique',\&Fix_operator, undef,   [ 'VUExtension' ], [ 'VUAttr1' ]],  # \&pxVendorUnique
  ['Comment',\&Fix_operator, undef,   undef, [ 'CommentData' ]],  # \&pxComment
  ['OpenDataSource',\&Fix_operator,\&pxOpenDataSource,  [ 'SourceType', 'DataOrg' ], undef],
  ['CloseDataSource',\&Fix_operator,\&pxCloseDataSource,  undef, undef],
  [''],
  [''],
  [''],
  [''],
  [''],
  ['BeginFontHeader',\&Fix_operator, undef,   [ 'FontName', 'FontFormat' ], undef],  # \&pxBeginFontHeader
#[50]
  ['ReadFontHeader',\&Fix_operator, undef,   [ 'FontHeaderLength' ], undef],  # \&pxReadFontHeader
  ['EndFontHeader',\&Fix_operator, undef,   undef, undef],  # \&pxEndFontHeader
  ['BeginChar',\&Fix_operator, undef,   [ 'FontName' ], undef],  # \&pxBeginChar
  ['ReadChar',\&Fix_operator, undef,   [ 'CharCode', 'CharDataSize' ], undef],  # \&pxReadChar
  ['EndChar',\&Fix_operator, undef,   undef, undef],  # \&pxEndChar
  ['RemoveFont',\&Fix_operator, undef,   [ 'FontName' ], undef],  # \&pxRemoveFont
  ['SetCharAttributes',\&Fix_operator, undef,   [ 'WritingMode' ], undef],  # \&pxSetCharAttributes
  ['SetDefaultGS',\&Fix_operator, undef,   undef, undef],  # \&pxSetDefaultGS
  ['SetColorTreatment',\&Fix_operator, undef,   undef, [ 'ColorTreatment', 'AllObjectTypes', 'TextObjects', 'VectorObjects', 'RasterObjects' ]],  # \&pxSetColorTreatment
  [''],
  [''],
  ['BeginStream',\&Fix_operator, undef,   [ 'StreamName' ], undef],  # \&pxBeginStream
  ['ReadStream',\&Fix_operator, undef,   [ 'StreamDataLength' ], undef],  # \&pxReadStream
  ['EndStream',\&Fix_operator, undef,   undef, undef],  # \&pxEndStream
  ['ExecStream',\&Fix_operator, undef,   [ 'StreamName' ], undef],  # \&pxExecStream
  ['RemoveStream',\&Fix_operator, undef,   [ 'StreamName' ], undef],  # \&pxRemoveStream
#[60]
  ['PopGS',\&Fix_operator, undef,   undef, undef],  # \&pxPopGS
  ['PushGS',\&Fix_operator, undef,   undef, undef],  # \&pxPushGS
  ['SetClipReplace',\&Fix_operator, undef,   [ 'ClipRegion' ], undef],  # \&pxSetClipReplace
  ['SetBrushSource',\&Fix_operator, undef,   undef, [ 'RGBColor', 'GrayLevel', 'PrimaryArray', 'PrimaryDepth', 'NullBrush', 'PatternSelectID', 'PatternOrigin', 'NewDestinationSize' ]],  # \&pxSetBrushSource
  ['SetCharAngle',\&Fix_operator, undef,   [ 'CharAngle' ], undef],  # \&pxSetCharAngle
  ['SetCharScale',\&Fix_operator, undef,   [ 'CharScale' ], undef],  # \&pxSetCharScale
  ['SetCharShear',\&Fix_operator, undef,   [ 'CharShear' ], undef],  # \&pxSetCharShear
  ['SetClipIntersect',\&Fix_operator, undef,   [ 'ClipRegion' ], undef],  # \&pxSetClipIntersect
  ['SetClipRectangle',\&Fix_operator, undef,   [ 'ClipRegion', 'BoundingBox' ], undef],  # \&pxSetClipRectangle
  ['SetClipToPage',\&Fix_operator, undef,   undef, undef],  # \&pxSetClipToPage
  ['SetColorSpace',\&Fix_operator, undef,   undef, [ 'ColorSpace', 'ColorimetricColorSpace', 'XYChromaticities', 'WhiteReferencePoint', 'CRGBMinMax', 'GammaGain', 'PaletteDepth', 'PaletteData' ]],  # \&pxSetColorSpace
  ['SetCursor',\&Fix_operator, undef,   [ 'Point' ], undef],  # \&pxSetCursor
  ['SetCursorRel',\&Fix_operator, undef,   [ 'Point' ], undef],  # \&pxSetCursorRel
  ['SetHalftoneMethod',\&Fix_operator, undef,   undef, [ 'DitherOrigin', 'DeviceMatrix', 'DitherMatrixDataType', 'DitherMatrixSize', 'DitherMatrixDepth', 'AllObjectTypes', 'TextObjects', 'VectorObjects', 'RasterObjects' ]],  # \&pxSetHalftoneMethod
  ['SetFillMode',\&Fix_operator, undef,   [ 'FillMode' ], undef],  # \&pxSetFillMode
  ['SetFont',\&Fix_operator, undef,   [ 'FontName', 'CharSize', 'SymbolSet' ], undef],  # \&pxSetFont
#[70]
  ['SetLineDash',\&Fix_operator, undef,   undef, [ 'LineDashStyle', 'DashOffset', 'SolidLine' ]],  # \&pxSetLineDash
  ['SetLineCap',\&Fix_operator, undef,   [ 'LineCapStyle' ], undef],  # \&pxSetLineCap
  ['SetLineJoin',\&Fix_operator, undef,   [ 'LineJoinStyle' ], undef],  # \&pxSetLineJoin
  ['SetMiterLimit',\&Fix_operator, undef,   [ 'MiterLength' ], undef],  # \&pxSetMiterLimit
  ['SetPageDefaultCTM',\&Fix_operator, undef,   undef, undef],  # \&pxSetPageDefaultCTM
  ['SetPageOrigin',\&Fix_operator, undef,   [ 'PageOrigin' ], undef],  # \&pxSetPageOrigin
  ['SetPageRotation',\&Fix_operator, undef,   [ 'PageAngle' ], undef],  # \&pxSetPageRotation
  ['SetPageScale',\&Fix_operator, undef,   undef, [ 'PageScale', 'Measure', 'UnitsPerMeasure' ]],  # \&pxSetPageScale
  ['SetPaintTxMode',\&Fix_operator, undef,   [ 'TxMode' ], undef],  # \&pxSetPaintTxMode
  ['SetPenSource',\&Fix_operator, undef,   undef, [ 'RGBColor', 'GrayLevel', 'PrimaryArray', 'PrimaryDepth', 'NullPen', 'PatternSelectID', 'PatternOrigin', 'NewDestinationSize' ]],  # \&pxSetPenSource
  ['SetPenWidth',\&Fix_operator, undef,   [ 'PenWidth' ], undef],  # \&pxSetPenWidth
  ['SetROP',\&Fix_operator, undef,   [ 'ROP3' ], undef],  # \&pxSetROP
  ['SetSourceTxMode',\&Fix_operator, undef,   [ 'TxMode' ], undef],  # \&pxSetSourceTxMode
  ['SetCharBoldValue',\&Fix_operator, undef,   [ 'CharBoldValue' ], undef],  # \&pxSetCharBoldValue
  ['SetNeutralAxis',\&Fix_operator, undef,   undef, [ 'AllObjectTypes', 'TextObjects', 'VectorObjects', 'RasterObjects' ]],  # \&pxSetNeutralAxis
  ['SetClipMode',\&Fix_operator, undef,   [ 'ClipMode' ], undef],  # \&pxSetClipMode
#[80]
  ['SetPathToClip',\&Fix_operator, undef,   undef, undef],  # \&pxSetPathToClip
  ['SetCharSubMode',\&Fix_operator, undef,   [ 'CharSubModeArray' ], undef],  # \&pxSetCharSubMode
  ['BeginUserDefinedLineCap',\&Fix_operator, undef,   undef, undef],  # \&pxBeginUserDefinedLineCap
  ['pxtEndUserDefinedLineCap',\&Fix_operator, undef,   undef, undef],  # \&pxEndUserDefinedLineCap
  ['CloseSubPath',\&Fix_operator, undef,   undef, undef],  # \&pxCloseSubPath
  ['NewPath',\&Fix_operator, undef,   undef, undef],  # \&pxNewPath
  ['PaintPath',\&Fix_operator, undef,   undef, undef],  # \&pxPaintPath
  [''],
  [''], [''], [''], [''],
  [''], [''], [''], [''],
#[90]
  [''],
  ['ArcPath',\&Fix_operator, undef,   [ 'BoundingBox', 'StartPoint', 'EndPoint' ], [ 'ArcDirection' ]],  # \&pxArcPath
  ['SetColorTrapping',\&Fix_operator, undef,   ['AllObjectTypes' ], undef],  # \&pxSetColorTrapping
  ['BezierPath',\&Fix_operator, undef,   undef, [ 'NumberOfPoints', 'PointType', 'ControlPoint1', 'ControlPoint2', 'EndPoint' ]],  # \&pxBezierPath
  ['SetAdaptiveHalftoning',\&Fix_operator, undef,   undef, [ 'AllObjectTypes', 'TextObjects', 'VectorObjects', 'RasterObjects' ]],  # \&pxSetAdaptiveHalftoning
  ['BezierRelPath',\&Fix_operator, undef,   undef, [ 'NumberOfPoints', 'PointType', 'ControlPoint1', 'ControlPoint2', 'EndPoint' ]],  # \&pxBezierRelPath
  ['Chord',\&Fix_operator, undef,   [ 'BoundingBox', 'StartPoint', 'EndPoint' ], undef],  # \&pxChord
  ['ChordPath',\&Fix_operator, undef,   [ 'BoundingBox', 'StartPoint', 'EndPoint' ], undef],  # \&pxChordPath
  ['Ellipse',\&Fix_operator, undef,   [ 'BoundingBox' ], undef],  # \&pxEllipse
  ['EllipsePath',\&Fix_operator, undef,   [ 'BoundingBox' ], undef],  # \&pxEllipsePath
  [''],
  ['LinePath',\&Fix_operator, undef,   undef, [ 'EndPoint', 'NumberOfPoints', 'PointType' ]],  # \&pxLinePath
  [''],
  ['LineRelPath',\&Fix_operator, undef,   undef, [ 'EndPoint', 'NumberOfPoints', 'PointType' ]],  # \&pxLineRelPath
  ['Pie',\&Fix_operator, undef,   [ 'BoundingBox', 'StartPoint', 'EndPoint' ], undef],  # \&pxPie
  ['PiePath',\&Fix_operator, undef,   [ 'BoundingBox', 'StartPoint', 'EndPoint' ], undef],  # \&pxPiePath
#[a0]
  ['Rectangle',\&Fix_operator, undef,   [ 'BoundingBox' ], undef],  # \&pxRectangle
  ['RectanglePath',\&Fix_operator, undef,   [ 'BoundingBox' ], undef],  # \&pxRectanglePath
  ['RoundRectangle',\&Fix_operator, undef,   [ 'BoundingBox', 'EllipseDimension' ], undef],  # \&pxRoundRectangle
  ['RoundRectanglePath',\&Fix_operator, undef,   [ 'BoundingBox', 'EllipseDimension' ], undef],  # \&pxRoundRectanglePath
  [''],
  [''],
  [''],
  [''],
  ['Text',\&Fix_operator, undef,   [ 'TextData' ], [ 'XSpacingData', 'YSpacingData' ]],  # \&pxText
  ['TextPath',\&Fix_operator, undef,   [ 'TextData' ], [ 'XSpacingData', 'YSpacingData' ]],  # \&pxTextPath
  [''],
  [''],
  [''], [''], [''], [''],
#[b0]
  ['BeginImage',\&Fix_operator, undef,   [ 'ColorMapping', 'ColorDepth', 'SourceWidth', 'SourceHeight', 'DestinationSize' ], undef],  # \&pxBeginImage
  ['ReadImage',\&Fix_operator, undef,   [ 'StartLine', 'BlockHeight', 'CompressMode' ], [ 'PadBytesMultiple', 'BlockByteLength' ]],  # \&pxReadImage
  ['EndImage',\&Fix_operator, undef,   undef, undef],  # \&pxEndImage
  ['BeginRastPattern',\&Fix_operator, undef,   [ 'ColorMapping', 'ColorDepth', 'SourceWidth', 'SourceHeight', 'DestinationSize', 'PatternDefineID', 'PatternPersistence' ], undef],  # \&pxBeginRastPattern
  ['ReadRastPattern',\&Fix_operator, undef,   [ 'StartLine', 'BlockHeight', 'CompressMode' ], [ 'PadBytesMultiple', 'BlockByteLength' ]],  # \&pxReadRastPattern
  ['EndRastPattern',\&Fix_operator, undef,   undef, undef],  # \&pxEndRastPattern
  ['BeginScan',\&Fix_operator, undef,   undef, undef],  # \&pxBeginScan
  [''],
  ['EndScan',\&Fix_operator, undef,   undef, undef],  # \&pxEndScan
  ['ScanLineRel',\&Fix_operator, undef,   undef, [ 'NumberOfScanLines' ]],  # \&pxScanLineRel
  [''],
  [''],
  [''],
  [''],
  [''],
  ['Passthrough',\&Fix_operator, undef,   undef, undef],  # \&pxPassthrough
#[c0]
  ['_ubyte',\&PXL_get_value,],
  ['_uint16',\&PXL_get_value,],
  ['_uint32',\&PXL_get_value,],
  ['_sint16',\&PXL_get_value,],
  ['_sint32',\&PXL_get_value,],
  ['_real32',\&PXL_get_value,],
  ['',],
  ['',],
  ['_ubyte_array',\&PXL_get_arrayvalue,],
  ['_uint16_array',\&PXL_get_arrayvalue,],
  ['_uint32_array',\&PXL_get_arrayvalue,],
  ['_sint16_array',\&PXL_get_arrayvalue,],
  ['_sint32_array',\&PXL_get_arrayvalue,],
  ['_real32_array',\&PXL_get_arrayvalue,],
  ['',],
  ['',],
#[d0]
  ['_ubyte_xy',\&PXL_get_value,],
  ['_uint16_xy',\&PXL_get_value,],
  ['_uint32_xy',\&PXL_get_value,],
  ['_sint16_xy',\&PXL_get_value,],
  ['_sint32_xy',\&PXL_get_value,],
  ['_real32_xy',\&PXL_get_value,],
  ['',],
  ['',],
  ['',], ['',], ['',], ['',],
  ['',], ['',], ['',], ['',],
#[e0]
  ['_ubyte_box',\&PXL_get_value,],
  ['_uint16_box',\&PXL_get_value,],
  ['_uint32_box',\&PXL_get_value,],
  ['_sint16_box',\&PXL_get_value,],
  ['_sint32_box',\&PXL_get_value,],
  ['_real32_box',\&PXL_get_value,],
  ['',],
  ['',],
  ['',], ['',], ['',], ['',],
  ['',], ['',], ['',], ['',],
#[f0]
  ['',], ['',], ['',], ['',],
  ['',], ['',], ['',], ['',],
  ['_attr_ubyte',\&PXL_fix_attr],
  ['_attr_uint16',\&PXL_fix_attr],
  ['_dataLength',\&PXL_embedded_data],
  ['_dataLengthByte',\&PXL_embedded_data],
  ['',], ['',], ['',], ['',],
];

# get value conversions

sub  PXL_get_uchar(){
	return ord Get_PXL_input();
}

sub  PXL_get_uint16(){
	my $v1 = ord Get_PXL_input();
	my $v2 = ord Get_PXL_input();
	my $v;
	if( $PXL_big_endian ){
	    $v = ($v1 <<8)+$v2;
	} else {
	    $v = ($v2 <<8)+$v1;
	}
	return($v);
}

sub  PXL_get_sint16(){
	my $v = PXL_get_uint16();
	if( $v & (1<<15) ){
	    $v = $v - (1<<16);
	}
	return($v);
}

sub  PXL_get_uint32(){
	my $v1 = ord Get_PXL_input();
	my $v2 = ord Get_PXL_input();
	my $v3 = ord Get_PXL_input();
	my $v4 = ord Get_PXL_input();
	my $v;
	if( $PXL_big_endian ){
		$v = ((((($v1 << 8)+$v2)<<8)+$v3)<<8)+$v4;
	} else {
		$v = ((((($v4 << 8)+$v3)<<8)+$v2)<<8)+$v1;
	}
	return($v);
}

sub  PXL_get_sint32(){
	my $v = PXL_get_uint32();
	if( $v & (1<<31) ){
	    $v = $v - (2*(1<<31));
	}
	return($v);
}

# Note: this conversion in perl may be bogus.
#  You really need to get down to the bit level and
#  massage the IEEE floating point format.  We cheat a
#  bit here and do the following:
#   - get the MSB (big endian) representation for the value
#   - reorder the bytes if it needs to be in little endian
#     order. 
#   - assume that the unpack("f") will take the 4 bytes and
#     then put them into a floating point.
#  You can test that this works using the following script.
#    sub sp($){
#    	my ($v) = @_;
#    	my $l = pack( "f", $v );
#    	my @list = split(//, $l);
#    	foreach(@list){
#    		my $n = ord $_;
#    		print sprintf("\\x%02x", $n );
#    	}
#    	print "\n";
#    	my $nv = unpack("f", $l );
#    	print "nv $nv\n";
#    	$nl = join('',@list);
#    	$nv = unpack("f", $nl );
#    	print "nl $nv\n";
#    	my @ol = map { ord } @list;
#    	print "OL @ol\n";
#    	@ol = map { chr } @ol;
#    	$nl = join('',@ol);
#    	$nv = unpack("f", $nl );
#    	print "FROM OL $nv\n";
#    }
#    sp(1.0);
#    sp(2.0);
#    sp(-1.0);
#    
#  Note that 1.0 in a little endian architecture
#  will have the bytes ordered as:
#     \x00\x00\x80\x3f
#                  ^^^ mantissa, sign byte
#


sub  PXL_get_real32(){
	my $v = pack( "N", PXL_get_uint32());
	my @list = split(//, $v );
	if( not defined $PXL_big_endian_arch ){
	    # we need to find out which way
	    # to put the order for pack
	    my $l = pack( "f", 1.0 );
	    my @list = split(//, $l);
	    $PXL_big_endian_arch = ord($list[0]);
	}
	if( not $PXL_big_endian_arch ){
	    @list = reverse( @list );
	}
	$v = join('',@list);
	$v = unpack("f", $v);
	# D100("PXL_get_real32 - big_endian $PXL_big_endian_arch, value $v");
	return($v);
}


sub  PXL_set_real32($){
	my ($v) = @_;
	$v = pack( "f", $v );
	my @list = split(//, $v );
	# we will put them in little endian order
	if( not defined $PXL_big_endian_arch ){
	    # we need to find out which way
	    # to put the order for pack
	    my $l = pack( "f", 1.0 );
	    my @list = split(//, $l);
	    $PXL_big_endian_arch = ord($list[0]);
	}
	if( $PXL_big_endian_arch ){
	    @list = reverse( @list );
	}
	if( $PXL_big_endian ){
	    @list = reverse( @list );
	}
	# D100("PXL_get_real32 - big_endian $PXL_big_endian_arch, value $v");
	return(@list);
}

# Values are a tag byte followed by the
# the value.  Complications: you can have a
# byte, 16bit, 32 bit or real 32 bit value.
# The value is saved as
#   $PXL_values = { type => $type,
#             value=>[ v1, v2, v... ] }
#

sub PXL_fix_value( $ $ $ ){
    my( $type, $count, $ft ) = @_;
    my ($i, $value); 
    my $st = $PXL_offset;
    if( $ft == 0 ){
	$type |= $pxd_ubyte;
	for ( my $i = 0; $i < $count; ++$i ){
	    my $v = $value->[$i] = ord Get_PXL_input();
	    D100("  PXL_fix_value offset = $st [$i] $value->[$i] char '" . show_esc( chr $v ) . '\'' );
	    $st = $PXL_offset;
	}
    } elsif( $ft == 1 ){
	$type |= $pxd_uint16;
	for ( $i = 0; $i < $count; ++$i ){
	    $value->[$i] = PXL_get_uint16();
	    D100("  PXL_fix_value offset = $st [$i] $value->[$i]");
	    $st = $PXL_offset;
	}
    } elsif( $ft == 2 ){
	$type |= $pxd_sint32;
	for ( $i = 0; $i < $count;  ++$i ){
	    $value->[$i] = PXL_get_uint32();
	    D100("  PXL_fix_value offset = $st [$i] $value->[$i]");
	    $st = $PXL_offset;
	}
    } elsif( $ft == 3 ){
	$type |= $pxd_sint16;
	for ( $i = 0; $i < $count;  ++$i ){
	    $value->[$i] = PXL_get_sint16();
	    D100("  PXL_fix_value offset = $st [$i] $value->[$i]");
	    $st = $PXL_offset;
	}
    } elsif( $ft == 4 ){
	$type |= $pxd_sint32;
	for ( $i = 0; $i < $count;  ++$i ){
	    $value->[$i] = PXL_get_sint32();
	    D100("  PXL_fix_value offset = $st [$i] $value->[$i]");
	    $st = $PXL_offset;
	}
    } elsif( $ft == 5 ){
	$type |= $pxd_real32;
	for ( $i = 0; $i < $count;  ++$i ){
	    $value->[$i] = PXL_get_real32();
	    D100("  PXL_fix_value offset = $st [$i] $value->[$i]");
	    $st = $PXL_offset;
	}
    } else {
	rip_die( "PXL_fix_value: LOGIC ERROR - ft $ft",
	    $EXIT_PRNERR_NORETRY);
    }
    my $v = { type=>$type, value=>$value };
    $PXL_values = $v;
    return( $v );
}

sub PXL_get_value( $ $ ){
    my( $otag, $info ) = @_;
    my( $type, $count, $value ) ;
    my $btag = $otag >>3;
    ($type, $count ) = ($pxd_scalar, 1) if $btag == 24;
    ($type, $count ) = ($pxd_xy, 2) if $btag == 26;
    ($type, $count ) = ($pxd_box, 4) if $btag == 28;
    my $ft = $otag & 7;
    my $v = PXL_fix_value( $type, $count, $ft );
    #D800("PXL_get_value: PXL_values " . Dumper($PXL_values) );
    D800("PXL_get_value: type " . sprintf("0x%02x", $v->{type}) .  ', values = [' . join(',',@{$v->{value}}) . ']');
}

########### Output ###########3

sub PXL_put_value( $ ){
    my( $info ) = @_;
    my( $count, $unit_count, $tag ) ;
    my( $infotype, $infocount, $infovalue, @out );
    $infotype = $info->{type};
    $infovalue = $info->{value};
    $infocount = @{$infovalue};

    D800("PXL_put_value: type " . sprintf("0x%02x", $infotype) );

    ($tag, $count) = (0xc0, 1) if $infotype & $pxd_scalar;
    ($tag, $count) = (0xc8, $infocount) if $infotype & $pxd_array;
    ($tag, $count) = (0, $infocount) if $infotype & $pxd_data;
    ($tag, $count) = (0, $infocount) if $infotype & $pxd_raw;
    ($tag, $count) = (0xd0, 2) if $infotype & $pxd_xy;
    ($tag, $count) = (0xe0, 4) if $infotype & $pxd_box;
    $tag |= 0 if $infotype & $pxd_ubyte;
    $tag |= 1 if $infotype & $pxd_uint16;
    $tag |= 2 if $infotype & $pxd_uint32;
    $tag |= 3 if $infotype & $pxd_sint16;
    $tag |= 4 if $infotype & $pxd_sint32;
    $tag |= 5 if $infotype & $pxd_real32;

    $unit_count = 1;
    $unit_count = 2 if $infotype & $pxd_uint16;
    $unit_count = 4 if $infotype & $pxd_sint32;
    $unit_count = 2 if $infotype & $pxd_sint16;
    $unit_count = 4 if $infotype & $pxd_sint32;
    $unit_count = 4 if $infotype & $pxd_real32;
    D800("PXL_put_value: tag " . sprintf("0x%02x", $tag) . ", count $count, unit_count $unit_count");
    # put the tag out first
    push @out, chr $tag if $tag;
    my ($lentag, $u, @p);
    if( $infotype & $pxd_data ){
	if( $count < 256 ){
	    $lentag = chr $pxt_dataLengthByte;
	    $u = 1;
    	} else {
	    $lentag = chr $pxt_dataLength;
	    $u = 2;
	}
	push @out, $lentag;
    } elsif( $infotype & $pxd_array ){
	if( $count < 256 ){
	    $lentag = chr $pxt_ubyte;
	    $u = 1;
    	} else {
	    $lentag = chr $pxt_uint16;
	    $u = 2;
	}
	push @out, $lentag;
    }
    if( $infotype & ($pxd_data  | $pxd_array )){
	my $v = $count;
	for( my $j = 0; $j < $u; ++$j ){
	    push @p, chr ($v & 0xFF); $v = ($v >>8);
	}
	if( $PXL_big_endian ){
	    @p = reverse @p;
	}
	push @out, @p;
    }
    for( my $i = 0; $i < $count; ++$i ){
	my $v = $infovalue->[$i];
	my @p;
	if( $infotype & $pxd_real32 ){
	    @p = PXL_set_real32( $v );
	} else {
	    for( my $j = 0; $j < $unit_count; ++$j ){
		push @p, chr( $v & 0xFF); $v = ($v >> 8);
	    }
	    if( $PXL_big_endian ){
		@p = reverse @p;
	    }
	}
	push @out, @p;
    }
    D800('PXL_put_value: [' . tr_hex( @out ) . ']' );
    return( @out );
}

########### Text Output ###########3

sub PXL_put_text( $ ){
    my( $info ) = @_;
    D100("PXL_put_text: " . Dumper($info) );
    my( $count, $unit_count, $tag, $suffix ) ;
    my( $infotype, $infocount, $infovalue, $out );
    $infotype = $info->{type};
    $infovalue = $info->{value};
    $infocount = @{$infovalue};

    D800("PXL_put_text: type " . sprintf("0x%02x", $infotype) );
    $tag = "";
    $suffix = "";
    ($tag, $count) = ("", 1) if $infotype & $pxd_scalar;
    ($tag, $count) = ("_array", $infocount) if $infotype & $pxd_array;
    ($tag, $count) = ("_data", $infocount) if $infotype & $pxd_data;
    ($tag, $count) = ("_raw", $infocount) if $infotype & $pxd_raw;
    ($tag, $count) = ("_xy", 2) if $infotype & $pxd_xy;
    ($tag, $count) = ("_box", 4) if $infotype & $pxd_box;

    $tag = "_ubyte" if $infotype & $pxd_ubyte;
    $tag = "_uint16" if $infotype & $pxd_uint16;
    $tag = "_uint32" if $infotype & $pxd_uint32;
    $tag = "_sint16" if $infotype & $pxd_sint16;
    $tag = "_sint32" if $infotype & $pxd_sint32;
    $tag = "_real32" if $infotype & $pxd_real32;
    $tag = "" if $infotype & $pxd_data;
    $tag = "" if $infotype & $pxd_raw;

    if( $infotype & ($pxd_data | $pxd_array | $pxd_raw) ){
	$suffix .= "[$count]";
    }
    $out = $tag.$suffix;
    for( my $i = 0; $i < $count; ++$i ){
	my $v = $infovalue->[$i];
	$out .= " $v"
    }
    D800("PXL_put_text: $out");
    return( $out );
}


# Embedded Data Format
#  <_dataLength> <l1> <l2> <len bytes of data>
#  <_dataLengthByte > <l1> <len bytes of data>
#
sub PXL_embedded_data( $ $ ){
    my( $otag, $info ) = @_;
    my $st = $PXL_offset;
    my $length;
    if( $otag == $pxt_dataLengthByte ){
	$length = PXL_get_uchar();
    } elsif( $otag == $pxt_dataLength ){
	$length = PXL_get_uint16();
    }
    D100("PXL_embedded_data: len $length");
    my $type = $pxd_scalar;
    PXL_fix_value( $type, $length, 0 );
    D100("PXL_embedded_data: len $length " . tr_hex($PXL_values->{value}) );
    if( $PCL_text_format ){
	print $PCL_write_fh
	    " ", PXL_put_text( $PXL_values ) if $PCL_write_fh;
    } else {
	print $PCL_write_fh 
	    PXL_put_value( $PXL_values ) if $PCL_write_fh;
    }
    $PXL_args = {};
    $PXL_values = undef;
}

# Array format:
#  <data tag for array>
#    <data tag for length value> <l1> [<l2>]
#    LEN * NUMVAL bytes of values
#
sub PXL_get_arrayvalue( $ $ ){
    my( $otag, $info ) = @_;
    my $st = $PXL_offset;
    my $lentag = ord Get_PXL_input();
    my $length = 0;
    $info = $px_tag_info->[$lentag];
    if( not $info->[0] ){
	rip_die( "Parse_get_arrayvalue: bad PCL XL tag "
	. sprintf("0x%02x", $lentag ) . " at offset $st",
	    $EXIT_PRNERR_NORETRY);
    }

    D100("PXL_get_arrayvalue: offset $st, array len tag " . sprintf("0x%02x", $lentag ) . ", tagname " . $info->[0]);
    $st = $PXL_offset;
    if( $lentag == $pxt_ubyte ){
	$length = PXL_get_uchar();
    } elsif( $lentag == $pxt_uint16 ){
	$length = PXL_get_uint16();
    } else {
	rip_die( "PXL_get_arrayvalue: wrong PCL XL tag for array "
	. sprintf("0x%02x", $lentag ) . " at offset $st",
	    $EXIT_PRNERR_NORETRY);
    }
    D100("PXL_get_arrayvalue: offset $st, length $length");
    my $ft = $otag & 7;
    my $type = $pxd_array;
    my $v = PXL_fix_value( $type, $length, $ft );
    #D800("PXL_get_arrayvalue: PXL_values " . Dumper($PXL_values) );
    D800("PXL_get_arrayvalue: type " . sprintf("0x%02x", $v->{type}) .  ', values = [' . join(',',@{$v->{value}}) . ']');
}


# Attribute Format
#  <data tag for attribute value> <l1> [<l2>]
#
sub PXL_fix_attr( $ $ ){
    my( $otag, $info ) = @_;
    my $st = $PXL_offset;
    my $attr;
    if( $otag == $pxt_attr_ubyte ){
	$attr = PXL_get_uchar();
    } elsif( $otag == $pxt_attr_uint16 ){
	$attr = PXL_get_uint16();
    } else {
	rip_die( "PXL_fix_attr: wrong PCL XL tag for attribute "
	. sprintf("0x%02x", $otag ) . " at offset $st",
	    $EXIT_PRNERR_NORETRY);
    }
    $info = $px_attributes->[$attr];
    #D800("PXL_fix_attr: offset $st, attr $attr " . Dumper($info) );
    # now check to see if the value type and the required
    # type for the attribute are compatible 
    if( not defined $info or not $info->[0] ){
	rip_die( "PXL_fix_attr: undefined PCL XL attribute "
	. sprintf("0x%02x", $attr ) . " at offset $st",
	    $EXIT_PRNERR_NORETRY);
    }
    my $attrname = $info->[0];
    D100("PXL_fix_attr: offset $st, attr " . sprintf("0x%02x", $attr ) .  "  " . $attrname );
    my $v = PXL_check_attr( $attr, $info, " at offset $st" );
}

sub PXL_check_attr( $ $ $){
    my( $attr, $info, $st ) = @_;
    my $attrname = $info->[0];
    # All the bits in the value type must be in the
    # attribute type
    my $value = $PXL_values;
    if( not defined $value ){
	rip_die( "PXL_check_attr: missing value for attribute $attrname ",
	    $EXIT_PRNERR_NORETRY);
    }
    $PXL_args->{$attrname} = $value;
    # save the attribute name for later
    $value->{attr} = $attr;
    $PXL_values = undef;
    my $value_type = $value->{'type'};
    my $attr_mask = $info->[1];
    my $match = (($value_type & $attr_mask) ^ $value_type);
	D100( " value type " . sprintf("0x%02x", $value_type) .  " (" . PXL_dump_mask($value_type) . "), " .  " attr mask " . sprintf("0x%02x", $attr_mask) .  " (" . PXL_dump_mask($attr_mask) . "), " .  " match " . sprintf("0x%02x", $match)  );
    if( $match ){
	D0( "WARNING PXL_check_attr: attribute $attrname allows " .  PXL_dump_mask($attr_mask) . " and value is " .  PXL_dump_mask($value_type) . $st );
    }
    my $limit = $info->[2];
    if( defined $limit and ($value_type & $pxd_scalar) ){
	D800( " limit " . (ref $limit). ' ' . ((ref($limit) eq 'ARRAY')?( '[' . join(",",@{$limit}) . ']'):$limit));
	if( ref($limit) eq 'ARRAY' ){
	    $value->{enum} = $limit;
	    $limit = @{$limit}-1;
	}
	my $values = $value->{value};
	foreach my $v (@{$values}){
	    D100(" checking value $v against limit $limit");
	    if( $v > $limit ){
		rip_die( "PXL_check_attr: value $v exceeds limit $limit for attribute $attrname " . $st,
	    $EXIT_PRNERR_NORETRY);
	    }
	}
    }
    my $check = $info->[3];
    if( defined $check ){
	D100( " calling check routine" );
	$check->($info, $value);
    }
    return( $value );
}

#
# Fix_operator()
# check to make sure that all of the attributes
# are present.  This is done by looking at the parameters set
# There are two classes of attributes: required and optional
#  The info data structure has the format:
#  [ name, service_routine, operator_service_routine, 
#     [required], [optional] ]
#  where required is a list of the required attributes
#  and  optional is a list of the optional attributes
#
# After checking and calling the service routine, we will
# then print the output and remove all the parameters.

sub Fix_operator( $ $ $ $){
    my($otag, $info, $st, $update ) = @_;
    $st = " near offset $PXL_offset" if( not defined $st );
    my $name = $info->[0];
    my $op_service = $info->[2];
    my $req = $info->[3];
    my $opt = $info->[4];
    my ($attr, $inlist, %req_hash);

    D100("Fix_operator: name $name, using attributes '" .  join(',',(keys %{$PXL_args})) . '\'' );
    foreach $attr (keys %{$PXL_args}){
	$inlist->{$attr} = 0;
    }
    if( $req ){
	D100("Fix_operator: req attributes '" . join(',',@{$req}) . '\'');
	foreach $attr (@{$req}){
	    $req_hash{$attr} = 1;
	    if( not defined $inlist->{$attr} and not $update ){
		rip_die("Fix_operator: operator '$name' missing required " .
		" parameter '$attr' $st",
		    $EXIT_PRNERR_NORETRY);
	    }
	    if( defined $inlist->{$attr} ){
		++$inlist->{$attr};
	    }
	}
    }
    if( $opt ){
	D100("Fix_operator: opt attributes '" . join(',',@{$opt}) . '\'');
	foreach $attr (@{$opt}){
	    if( defined $inlist->{$attr} ){
		++$inlist->{$attr};
	    }
	}
    }
    # now check to see if an unknown option was specified
    foreach $attr (keys %{$PXL_args}){
	if( not $inlist->{$attr} ){
	    rip_die("Fix_operator: operator '$name' invalid " .
		" parameter '$attr' $st",
		    $EXIT_PRNERR_NORETRY);
	}
    }
    if( $update ){
	$PXL_args->{_FLUSH_} = 1 if $update == 2;
	$PXL_operator_replacements->{$otag} = $PXL_args;
    } else {
	my $replacement = $PXL_operator_replacements->{$otag};
	D100("Fix_operator: $name replacement " . Dumper($replacement) );
	if( $replacement->{_FLUSH_} ){
	    # remove all optional attributes
	    foreach $attr (keys %{$PXL_args}){
		if( not $req_hash{$attr} ){
		    delete $PXL_args->{$attr};
		}
	    }
	}
	# do the replacements, if any
	foreach $attr (keys %{$replacement}){
	    next if $attr eq '_FLUSH_';
	    $PXL_args->{$attr} = $replacement->{$attr};
	}
	foreach $attr (sort keys %{$PXL_args}){
	    my $value = $PXL_args->{$attr};
	    D100("Fix_operator: $name '$attr' value " . Dumper($value) );
	    next if not $value;
	    if( $PCL_text_format ){
		print $PCL_write_fh
		    "  ", PXL_put_text( $value ),
		    " \@${attr}\n"
		    if $PCL_write_fh;
	    } else {
		my @cmd = (
		    PXL_put_value( $value ),
			chr $pxt_attr_ubyte, chr $value->{attr} );
		D100("Fix_operator: $name '$attr' value " . Dumper(\@cmd) );
		print $PCL_write_fh (@cmd) if $PCL_write_fh;
	    }
	}
	D100("Fix_operator: setting operator $name value $otag");
	if( $PCL_text_format ){
	    print $PCL_write_fh " $name\n" if $PCL_write_fh;
	} else {
	    print $PCL_write_fh chr $otag if $PCL_write_fh;
	}
	if( $op_service ){
	    $op_service->($otag, $info);
	}
    }
    $PXL_args = {};
    $PXL_values = undef;
}


# Parse_PXL
#   we do PXL parsing

sub Parse_PXL_setup( $ $ $ ){
    my( $init, $header, $replace ) =  @_;
    my @saved_input_chars = @PCL_input_chars;
    my ($saved_read_fh, $saved_write_fh, $saved_callback ) =
	( $PCL_read_fh, $PCL_write_fh, $PCL_newpage_callback );
    if( $init or not $raw_input_actions ){
	Parse_PCL_init();
    }
    $header = "" if not defined $header;
    $replace = "" if not defined $replace;
    $PCL_init_str .= " " . $header if $header;
    D100("Parse_PCL_setup: header '$header', replace '$replace'" );
    PXL_assembler( $replace, 0, 0, undef, undef );
    D100("Parse_PCL_setup: after setup " . Dumper($PXL_operator_replacements)); 
    @PCL_input_chars = @saved_input_chars;
    ( $PCL_read_fh, $PCL_write_fh, $PCL_newpage_callback ) =
	($saved_read_fh, $saved_write_fh, $saved_callback );
}

# Parse_PXL
#   we do PXL parsing

sub Parse_PXL( $ $ $ $ $ ){
    my($read);
    ( $read, $PCL_text_format,
	$PCL_read_fh, $PCL_write_fh, $PCL_newpage_callback ) =  @_;

    # initialize the input buffer
    @PCL_input_chars = split(//, $read );
    D100("Parse_PXL: text_format $PCL_text_format, starting input '". tr_esc(@PCL_input_chars) . '\'' );
    my $status = undef;
    $PXL_offset = 0;
    $PXL_big_endian = 0;
    $PXL_args = {};
    $PXL_values = undef;
    if( defined Peek_PCL_input() ){
	my $tag;
	# we need to get the stream header for the big-endian or little endian
	# See the Stream Header format: [binding]<space>HP-PCL XL;2;0;... <nl>
	# binding
	# '  - unused, illegal
	# ( big endian
	# ) little endian

	if( $tag = Peek_PCL_input() and ($tag eq '(' or $tag eq ')') ){
	    $PXL_big_endian = ($tag eq '(');
	    my @cmd;
	    do {
		$tag = Get_PXL_input();
		push @cmd, ($tag);
	    } while( defined($tag) and $tag ne "\n" and @cmd < 256 );
	    D100("ParsePXL: Stream Header " . tr_esc( @cmd ));
	    if( $tag ne "\n" ){
		rip_die( "Parse_PXL: bad PCL XL Stream header '" . tr_esc(@cmd) . '\'',
		    $EXIT_PRNERR_NORETRY);
	    }
	    if( $PCL_text_format ){
		print $PCL_write_fh '"' .tr_esc(@cmd) . "\"\n" if $PCL_write_fh;
	    } else {
		print $PCL_write_fh @cmd if $PCL_write_fh;
	    }
	}
	while( defined Peek_PCL_input() ){
	    # we read enough to make sure we have input
	    my($st, $otag, $info, $attr, $attrname);
	    $st = $PXL_offset;
	    $tag = Get_PXL_input();
	    $otag = ord( $tag);
	    $info = $px_tag_info->[$otag];
	    if( not $info->[0] ){
		rip_die( "Parse_PXL: bad PCL XL tag "
		. sprintf("0x%02x", $otag ) . " at offset $st",
		    $EXIT_PRNERR_NORETRY);
	    }
	    D100("Parse_PXL: offset $st, tag " . sprintf("0x%02x", $otag ) . ", tagname " . $info->[0]);
	    if( not defined $info->[1] ){
		# we have a simple pass through
		next;
	    }
	    if( $otag == 0x1b ){
		my $input = join('', $tag, @PCL_input_chars);
		if( $input =~ m/^\x1b%-12345X/ ){
		    D100("Parse_PXL: UEL");
		    return( $input );
		}
		rip_die( "Parse_PXL: bad PCL XL tag "
		    . sprintf("0x%02x", $otag ) . " at offset $st",
		    $EXIT_PRNERR_NORETRY);
	    }
	    $info->[1]->($otag, $info );
	}
    }
    return( "" );
}



sub Pagecallback_PXL {
    ++$Pcount;
    D100("Pagecallback_PXL: Pcount $Pcount");
    my $replace = 
	"_ubyte $Pcount \@MediaSource BeginPage",
    my $header = "_raw[5] 'N' 'E' 'X' 'T' $Pcount ";
    Parse_PXL_setup(1, $header, $replace );
}

sub TestParsePXL($ $ $){
    $Data::Dumper::Indent = 1;
	my($debug, $text, $file) = @_;
#	print  "tag_syntax  " . Dumper( $tag_syntax );
    D0("TestParsePXL - text $text");
    $Foomatic::Debugging::debug = $debug;
    my $r = 
    "\051\040\110\120\055\120\103\114\040\130\114\073\062\073\060\073".
    "\103\157\155\155\145\156\164\040\103\157\160\171\162\151\147\150".
    "\164\040\110\145\167\154\145\164\164\055\120\141\143\153\141\162".
    "\144\040\103\157\155\160\141\156\171\040\061\071\070\071\055\062".
    "\060\060\060\056\040\126\145\162\163\151\157\156\040\064\056\062".
    "\056\061\056\070\012\321\260\004\260\004\370\211\300\000\370\206".
    "\300\003\370\217\101\300\000\370\210\300\001\370\202\110\300\000".
    "\370\050\300\001\370\046\310\301\006\000\114\105\124\124\105\122".
    "\370\045\103\300\000\370\124\177\164\323\310\000\310\000\370\052".
    "\165\303\000\000\370\051\166\325\000\000\200\077\000\000\200\077".
    "\370\053\167\151\300\001\370\003\152\300\377\370\011\143\300\000".
    "\370\005\171\300\000\370\055\170\300\000\370\055\174\300\360\370".
    "\054\173\205\343\024\005\350\003\064\041\243\004\370\102\241\206".
    "\300\001\370\003\152\300\000\370\011\143\300\001\370\055\170\300".
    "\001\370\055\174\300\374\370\054\173\205\300\000\370\055\170\300".
    "\000\370\055\174\310\301\020\000\101\162\151\141\154\040\040\040".
    "\040\040\040\040\040\040\040\040\370\250\305\000\000\047\103\370".
    "\246\301\116\002\370\252\157\323\024\005\176\004\370\114\153\311".
    "\301\004\000\124\000\145\000\163\000\164\000\370\253\310\301\004".
    "\000\146\135\124\000\370\257\250\300\001\370\055\170\300\001\370".
    "\055\174\301\001\000\370\061\104\111\102\033\045\055\061\062\063".
    "\064\065\130\100\120\112\114\040\105\117\112\040\116\101\115\105".
    "\075\042\104\157\143\165\155\145\156\164\042\015\012\033\045\055".
    "\061\062\063\064\065\130\012";
	$r = 
    "\x29\x20\x48\x50\x2d\x50\x43\x4c\x20\x58\x4c\x3b\x32\x3b\x30\x3b".
    "\x43\x6f\x6d\x6d\x65\x6e\x74\x20\x43\x6f\x70\x79\x72\x69\x67\x68".
    "\x74\x20\x48\x65\x77\x6c\x65\x74\x74\x2d\x50\x61\x63\x6b\x61\x72".
    "\x64\x20\x43\x6f\x6d\x70\x61\x6e\x79\x20\x31\x39\x38\x39\x2d\x32".
    "\x30\x30\x30\x2e\x20\x56\x65\x72\x73\x69\x6f\x6e\x20\x34\x2e\x32".
    "\x2e\x31\x2e\x38\x0a".
    "\xd1\xb0\x04\xb0\x04".
    "\xf8\x89\xc0\x00\xf8\x86".
    "\xc0\x03\xf8\x8f\x41\xc0\x00\xf8\x88\xc0\x01\xf8\x82\x48" .

    "\xc0\x00".
    "\xf8\x28\xc0\x01\xf8\x26\xc8\xc1\x06\x00\x4c\x45\x54\x54\x45\x52".
    "\xf8\x25\x43\xc0\x00\xf8\x54\x7f\x74\xd3\xc8\x00\xc8\x00\xf8\x2a".
    "\x75\xc3\x00\x00\xf8\x29\x76\xd5\x00\x00\x80\x3f\x00\x00\x80\x3f".
    "\xf8\x2b\x77\x69\xc0\x01\xf8\x03\x6a\xc0\xff\xf8\x09\x63\xc0\x00".
    "\xf8\x05\x79\xc0\x00\xf8\x2d\x78\xc0\x00\xf8\x2d\x7c\xc0\xf0\xf8".
    "\x2c\x7b\x85\xe3\x14\x05\xe8\x03\x34\x21\xa3\x04\xf8\x42\xa1\x86".
    "\xc0\x01\xf8\x03\x6a\xc0\x00\xf8\x09\x63\xc0\x01\xf8\x2d\x78\xc0".
    "\x01\xf8\x2d\x7c\xc0\xfc\xf8\x2c\x7b\x85\xc0\x00\xf8\x2d\x78\xc0".
    "\x00\xf8\x2d\x7c\xc8\xc1\x10\x00\x41\x72\x69\x61\x6c\x20\x20\x20".
    "\x20\x20\x20\x20\x20\x20\x20\x20\xf8\xa8\xc5\x00\x00\x27\x43\xf8".
    "\xa6\xc1\x4e\x02\xf8\xaa\x6f\xd3\x14\x05\x7e\x04\xf8\x4c\x6b\xc9".
    "\xc1\x04\x00\x54\x00\x65\x00\x73\x00\x74\x00\xf8\xab\xc8\xc1\x04".
    "\x00\x66\x5d\x54\x00\xf8\xaf\xa8\xc0\x01\xf8\x2d\x78\xc0\x01\xf8".
    "\x2d\x7c\xc1\x01\x00\xf8\x31\x44" .

    "\xc0\x00".
    "\xf8\x28\xc0\x01\xf8\x26\xc8\xc1\x06\x00\x4c\x45\x54\x54\x45\x52".
    "\xf8\x25\x43\xc0\x00\xf8\x54\x7f\x74\xd3\xc8\x00\xc8\x00\xf8\x2a".
    "\x75\xc3\x00\x00\xf8\x29\x76\xd5\x00\x00\x80\x3f\x00\x00\x80\x3f".
    "\xf8\x2b\x77\x69\xc0\x01\xf8\x03\x6a\xc0\xff\xf8\x09\x63\xc0\x00".
    "\xf8\x05\x79\xc0\x00\xf8\x2d\x78\xc0\x00\xf8\x2d\x7c\xc0\xf0\xf8".
    "\x2c\x7b\x85\xe3\x14\x05\xe8\x03\x34\x21\xa3\x04\xf8\x42\xa1\x86".
    "\xc0\x01\xf8\x03\x6a\xc0\x00\xf8\x09\x63\xc0\x01\xf8\x2d\x78\xc0".
    "\x01\xf8\x2d\x7c\xc0\xfc\xf8\x2c\x7b\x85\xc0\x00\xf8\x2d\x78\xc0".
    "\x00\xf8\x2d\x7c\xc8\xc1\x10\x00\x41\x72\x69\x61\x6c\x20\x20\x20".
    "\x20\x20\x20\x20\x20\x20\x20\x20\xf8\xa8\xc5\x00\x00\x27\x43\xf8".
    "\xa6\xc1\x4e\x02\xf8\xaa\x6f\xd3\x14\x05\x7e\x04\xf8\x4c\x6b\xc9".
    "\xc1\x04\x00\x54\x00\x65\x00\x73\x00\x74\x00\xf8\xab\xc8\xc1\x04".
    "\x00\x66\x5d\x54\x00\xf8\xaf\xa8\xc0\x01\xf8\x2d\x78\xc0\x01\xf8".
    "\x2d\x7c\xc1\x01\x00\xf8\x31\x44" .

    "\x49\x42\x1b\x25\x2d\x31\x32\x33".
    "\x34\x35\x58\x40\x50\x4a\x4c\x20\x45\x4f\x4a\x20\x4e\x41\x4d\x45".
    "\x3d\x22\x44\x6f\x63\x75\x6d\x65\x6e\x74\x22\x0d\x0a\x1b\x25\x2d".
    "\x31\x32\x33\x34\x35\x58\x0a";
    my $replace = 
	"_uint16_xy 1201 1202 \@UnitsPerMeasure BeginSession",
    my $header = "_raw[5] 'S' 'T' 'A' 'R' 'T'";
	my $fh;
    if( not $file or $file =~ /STDOUT/i ){
	$fh = \*STDOUT;
    } else {
	$fh = new FileHandle ">$file";
    }
    my $v;
    if( $text & 0x01){
	Parse_PXL_setup(1, $header, $replace );
	$v = Parse_PXL($r, 0, undef, $fh, \&Pagecallback_PXL );
	D100( 'Test 1 RETURNED \'' . tr_esc( split(//,$v) ) . '\'' );
    }
    if( $text & 0x02 ){
	$v = Parse_PXL($r, 1, undef, $fh, undef );
	D100( 'Test 2 RETURNED \'' . tr_esc( split(//,$v) ) . '\'' );
    }
    if( $text & 0x04 ){
	$v = Parse_PXL("", 1, \*STDIN, $fh, undef );
    	D100( 'Test 4 RETURNED \'' . tr_esc( split(//,$v) ) . '\'' );
    }
}

# TestParsePXL(0x300, 0, "/tmp/b");
# TestParsePXL(0x300, 1, "/tmp/b");
# TestParsePXL(0x300, 2, "/tmp/b");


# Translation tables - all indexes are in lower case
# Hash of PXL operators to index in $px_tag_info
my $PXL_operators;
# Hash of PXL attributes to index in $px_attributes
my $PXL_attributes;
# Hash of enum names to values
my $PXL_enums;

sub PXL_assembler( $ $ $ $ $ ){
    my ($input, $action, $big_endian, $read_fh, $write_fh ) = @_;
    my ($save_endian, $save_values, $save_args,
	$save_read_fh, $save_write_fh )
	  = ($PXL_big_endian, $PXL_values, $PXL_args,
	    $PCL_read_fh, $PCL_write_fh );
    $PXL_big_endian = $big_endian;
    $PXL_values = undef;
    $PXL_args = {};
    $PCL_read_fh = $read_fh;
    $PCL_write_fh = $write_fh;
    my @out;

    if( not $PXL_operators ){
	my($i,$name);
	for( $i = 0; $i < @{$px_tag_info}; ++$i ){
	    $name = $px_tag_info->[$i][0];
	    next if not $name;
	    my $routine = $px_tag_info->[$i][1];
	    next if not $routine;
	    $name = lc($name);
	    $PXL_operators->{$name} = $i;
	    # test for routine definitions
	    #$routine->() if ($routine = $px_tag_info->[$i][2]);
	}
	#D100("PXL_assembler: operators " . Dumper( $PXL_operators ) );
	for( $i = 0; $i < @{$px_attributes}; ++$i ){
	    $name = $px_attributes->[$i][0];
	    #D100("ATTR [$i] $name");
	    next if not $name;
	    my $limit = $px_attributes->[$i][2];
	    #D100("ENUM $name, limit " . ref($limit).' '. Dumper($limit));
	    $name = lc($name);
	    $PXL_attributes->{$name} = $i;
	    if( ref($limit) eq 'ARRAY' ){
		for( my $j = 0; $j < @{$limit}; ++$j ){
		    my $name = $limit->[$j];
		    #D100("E $name $j");
		    next if not $name;
		    $PXL_enums->{lc $name} = { value=>$j, name=>$name };
		}
	    }
	}
	#D100("PXL_assembler: attributes " . Dumper( $PXL_attributes ) );
	#D100("PXL_assembler: enums " . Dumper( $PXL_enums ) );
    }
    $input = "" if( not defined $input );
    {
	D100("PXL_assembler: input $input");
	my @lines = grep { not /^\s*\#/ } split( /\n/, $input );
	@PCL_input_tokens = ( map { split(' ') } @lines );
	D100("PXL_assembler: tokens @PCL_input_tokens");
    }
    while(defined Peek_PCL_input_token()){
	my $token = Get_PCL_input_token();
	D100("PXL_assembler: token $token");
	if( $token =~ /^_/ ){
	    # data 
	    my( $count, $tag, $type, $width, $real );

	    if( $token =~ /^(.*)_box$/i ){
		$type = $1;
		$tag = $pxd_box;
		$count = 4;
	    } elsif( $token =~ /^(.*)_xy$/i ){
		$type = $1;
		$tag = $pxd_xy;
		$count = 2;
	    } elsif( $token =~ /^(.*)_array\[(\d+|".*")\]$/i ){
		$type = $1;
		$tag = $pxd_array;
		$count = $2
	    } elsif( $token =~ /^(_data)\[(\d+|".*")\]$/i ){
		$type = $1;
		$tag = $pxd_array;
		$count = $2
	    } elsif( $token =~ /^(_raw)\[(\d+|".*")\]$/i ){
		$type = $1;
		$tag = $pxd_array;
		$count = $2
	    } else {
		$type = $token;
		$tag = $pxd_scalar;
		$count = 1;
	    }
	    $type = lc $type;
	    $real = 0;
	    if( $type eq '_ubyte'){
		$tag |= $pxd_ubyte;
		$width = 0xFF;
	    } elsif( $type eq '_data' ){
		$tag |= $pxd_data;
		$width = 0xFF;
	    } elsif( $type eq '_raw' ){
		$tag |= $pxd_raw;
		$width = 0xFF;
	    } elsif( $type eq '_uint16' ){
		$tag |= $pxd_uint16;
		$width = 0xFFFF;
	    } elsif( $type eq '_sint16' ){
		$tag |= $pxd_sint16;
	    } elsif( $type eq '_uint32' ){
		$tag |= $pxd_uint32;
		$width = 0xFFFFFFFF;
	    } elsif( $type eq '_sint32' ){
		$tag |= $pxd_sint32;
	    } elsif( $type eq '_real32' ){
		$tag |= $pxd_real32;
		$real = 1;
	    } else {
		rip_die("PXL_assembler: unknown data type '$token'",
		$EXIT_PRNERR_NORETRY );
	    }
	    my $value = [];
	    if( $count =~ /(["'])(.*)\1/ ){
		foreach my $ch ( split( //, $2) ){
		    push @{$value}, ord $ch;
		}
	    } else {
		for( my $i = 0; $i < $count; ++$i ){
		    if( not defined Peek_PCL_input_token() ){
			rip_die("PXL_assembler: missing values for operator '$token'",
			$EXIT_PRNERR_NORETRY );
		    }
		    my $v = Get_PCL_input_token();
		    D100("PXL_assembler: data token $v");
		    my $enum;
		    my $cv;
		    if( $v =~ /(['"])(.*)\1/ ){
			my @chars = split(//,$2);
			foreach my $string( @chars ){
			    $cv = ord $string;
			    push @{$value}, ord $string;
			    D100("PXL_assembler: char [$i] value $string, result $cv");
			    ++$i;
			    last if $i >= $count;
			}
			--$i;
			next;
		    } elsif( ($enum = $PXL_enums->{lc $v}) ){
			$cv = $enum->{value};
		    } elsif( $v =~ /^0x/ ){
			$cv = oct $v;
		    } elsif( $v =~
			/^([+-]?)(?=\d|\.\d)\d*(\.\d*)?([Ee]([+-]?\d+))?$/) {
			$cv = $v + 0;
		    } else {
			rip_die("PXL_assembler: bad value format '$v'",
			$EXIT_PRNERR_NORETRY );
		    }
		    $cv = int $cv if( not $real );
		    $cv &= $width if( $width );
		    D100("PXL_assembler: data [$i] value $v, result $cv");
		    push @{$value}, $cv;
		}
	    }
	    my $v = { type=>$tag, value=>$value };
	    D100("PXL_assembler: type " . sprintf("0x%02x", $tag ) . ', values [' . join(',', @{$value}) . ']');
	    $PXL_values = $v;
	    if( $action ){
		my @v = PXL_put_value( $v );
		D100("PXL_assembler: binary value " . tr_hex( @v ) );
		push @out, (@v);
	    }
	} elsif( $token =~ /^@(.*)$/ ){
	    # attribute
	    my $attr = $1;
	    my $op = $PXL_attributes->{ lc $attr };
	    if( not defined $op ){
		rip_die("PXL_assembler: unknown attribute '$attr'",
		$EXIT_PRNERR_NORETRY );
	    }
	    my $info = $px_attributes->[$op];
	    D100("PXL_assembler: attribute $attr, index $op, name $info->[0]");
	    PXL_check_attr( $op, $info, "" );
	    if( $action ){
		push @out, chr $pxt_attr_ubyte, chr $op;
	    }
	} elsif( $token =~ /^(["'])(.*)\1$/ ) {
	    my $string = $2;
	    $string =~ s/\\x(..)/chr hex($1)/eg;
	    if( $action ){
		push @out, ($string);
	    }
	} elsif( $token =~ /^(!|)(.*)$/ ){
	    my ($not, $operator) = ($1, $2);
	    my $op = $PXL_operators->{lc $operator};
	    if( not defined $op ){
		rip_die("PXL_assembler: unknown operator '$operator'",
		$EXIT_PRNERR_NORETRY );
	    }
	    if( $action ){
		push @out, chr $op;
	    } else {
		my $update = 1;
		$update = 2 if( $not );
		Fix_operator( $op, $px_tag_info->[$op], "", $update);
	    }
	}
	if( $action and $PCL_write_fh ){
	    D100("PXL_assembler: out " . tr_hex( @out ) );
	    print $PCL_write_fh @out;
	    @out = ();
	}
    }
    if( $action ){
	D100("PXL_assembler: final out " . tr_hex( @out ) );
    } else {
	D100("PXL_assembler: replace " . Dumper($PXL_operator_replacements) );
    }
    ($PXL_big_endian, $PXL_values, $PXL_args,
	$PCL_read_fh, $PCL_write_fh ) =
	($save_endian, $save_values, $save_args,
	    $save_read_fh, $save_write_fh );
    return( @out );
}

sub TestPXL_assembler($ $ $){
    my($debug, $test, $file) = @_;
    my @out;
    D0("TestPXL_assembler");
    my $fh;
    $Foomatic::Debugging::debug = $debug;
    if( not $file or $file =~ /STDOUT/i ){
	$fh = \*STDOUT;
    } else {
	$fh = new FileHandle ">$file" or die "cannot open $file - $!";
    }

    if( $test & 0x10 ){
	@out = 
	PXL_assembler("
	    \"string\\x01\"
	  # test
	   _ubyte 1 _ubyte 0x10
	  # line
	  _ubyte 0x1234 _ubyte 'A' _ubyte eSByte _ubyte 3.14 _real32 3.14", 1, 0,
	    undef, undef );
	    D100("TestPXL_assembler: out '" . tr_esc( @out ) . "'" );
	print $fh @out, "\n"; @out = ();
    }
    if( $test & 0x20 ){
	@out = PXL_assembler("_ubyte_array[2] 1 2 " .
	"_ubyte_array[\"ABC\"] _ubyte_array[2] 'xy' _ubyte_array[3] 'ab' 30 ",
	1, 0, undef, undef );
	print $fh @out, "\n"; @out = ();
    }
    if( $test & 0x40 ){
	@out = PXL_assembler("_ubyte_xy 1 2", 1, 0, undef, undef );
	print $fh @out, "\n"; @out = ();
    }
    if( $test & 0x80 ){
	@out = PXL_assembler("_ubyte_box 1 2 3 4", 1, 0, undef, undef );
	print $fh @out, "\n"; @out = ();
    }
    if( $test & 0x100 ){
	@out = PXL_assembler("_real32_box -10.0E2 2000.2 3000.3 4000.4", 1, 0, undef, undef );
	print $fh @out, "\n"; @out = ();
    }
    if( $test & 0x01 ){
	print $fh "\ntest 01\n";
	@out = PXL_assembler(
	    "_uint16_xy 1200 1200 \@UnitsPerMeasure
	    _ubyte 0 \@Measure
	    _ubyte 3 \@ErrorReport
	    !BeginSession", 0, 0, undef, undef );
	print $fh "\naction 0 returned\n", @out, "\n"; @out = ();
	@out = PXL_assembler(
	    "_uint16_xy 1200 1200 \@UnitsPerMeasure
	    _ubyte 0 \@Measure
	    _ubyte 3 \@ErrorReport
	    BeginSession", 1, 0, undef, undef );
	print $fh "\naction 1 returned\n", @out, "\n"; @out = ();
	D100("TestPXL_assembler: final out " . tr_hex( @out ) );
	print $fh "action 1 file\n"; @out = ();
	@out = PXL_assembler(
	    "_uint16_xy 1200 1200 \@UnitsPerMeasure
	    _ubyte 0 \@Measure
	    _ubyte 3 \@ErrorReport
	    BeginSession", 1, 0, undef, $fh );
	print $fh "\naction 1 after file returned\n", @out, "\n";
	D100("TestPXL_assembler: out " . tr_hex( @out ) );
    }
}


# TestPXL_assembler(0x300, 0x21, '/tmp/b');
# TestPXL_assembler(0x300, 1, '/tmp/b');
# TestPXL_assembler(0x300, 0x10, 'STDOUT');

################ PJL Parser #################
#
# ($input, $hash) = Parse_PJL($input, $read_fh);
#    $input = previously read input
#    $read_fh = read more input from this file
#
#  Returns:
#   $input = input after PJL
#   $hash = hash of information
#     $hash->{UEL_found} = 1  - UEL found
#     $hash->{SET}{key} = value  - SET information
#       from @PJL SET KEY="value"
#     $hash->{SET_SUBSET}{key}{subkey} = value
#       from @PJL SET KEY="SUBKEY=value"
#     $hash->{cmd}{key} = value
#       from @PJL JOB KEY="value" , cmd is JOB
#       from @PJL ENTER LANGUAGE="PCLXL" , cmd is ENTER
#     $hash->{info}[] - array of input PJL

sub Parse_PJL( $ $ ) {
    my( $input, $read_fh ) = @_;
    my @saved_input_chars = @PCL_input_chars;
    my ($saved_read_fh, ) =
	( $PCL_read_fh, );
    @PCL_input_chars = split(//, $input ) if defined $input;
    D100("Parse_PJL: starting input '" . tr_esc( @PCL_input_chars) . '\'' );
    my $UEL_found = 0;
    my $hash = {};
    my $c;
    $input = "";
    while( defined ($c = Peek_PCL_input()) ){
	D800("Parse_PJL: input '" . tr_esc( $c ) . '\'' );
	if( $c eq "\x1b" ){
	    # check for UEL
	    # we want ESC % - 12345 X
	    #         0   1 2 34567 8
	    Parse_PCL_read_stdin() if @PCL_input_chars < 9;
	    $input = join('',@PCL_input_chars);
	    if( $input =~ s/^\x1b%-12345X// ){
		D100("Parse_PJL: UEL found");
		$UEL_found = 1;
		$hash->{UEL_found} = 1;
		@PCL_input_chars = split(//, $input );
		$input = "";
	    } else {
		return( $input, $hash );
	    }
	} elsif ( $UEL_found and $c eq '@' ){
	    # read a PJL line
	    $input = "";
	    while( length($input) < 4
		and defined($c = Peek_PCL_input())
		and $c ne "\n" ){
		D800("Parse_PJL: input $c");
		$input .= Get_PCL_input();
	    }
	    if( $input !~ /^\@PJL/ ){
		$input .= join('',@PCL_input_chars);
		return( $input, $hash );
	    }
	    while(
		defined($c = Get_PCL_input())
		and length($input) < 256 and $c ne "\n" ){
		$input .= $c;
	    }
	    if( $c ne "\n" ){
		rip_die("Parse_PJL: PJL line too long '$input'",
		    $EXIT_PRNERR_NORETRY);
	    }
	    $input =~ s/\015//g;
	    D100("Parse_PJL: PJL '$input'");
	    push( @{$hash->{input}}, $input );
	    # now we parse the line
	    my($name, $value, $cmd );
	    if( $input =~ /^\@PJL\s+COMMENT\s+(.*)/ ){
		$input = $1;
		push( @{$hash->{COMMENT}}, $input );
		if( $input =~ /^SET\s+(.*)/ ){
		    my $v = $1;
		    if( $v =~ m/^(["'])(.*)\1\s*$/ ){
			$v = $1;
		    }
		    D100("Parse_PJL: v '$v'");
		    ($name,$value) = $v =~ m/^(.*?)=(.*)$/;
		    $hash->{COMMENTSET}{$name}=$value;
		}
	    } elsif( $input =~ /^\@PJL\s+SET\s+(.*)/ ){
		$input = $1;
		($name,$value) = split("=", $input, 2 );
		if( $value =~ m/^(["'])(.*)\1\s*$/ ){
		    $value = $2;
		}
		D100("Parse_PJL: name $name value '$value'");
		if( $value =~ m/^(.*?)=(.*)$/ ){
		    $cmd = $name;
		    ($name,$value) = ($1, $2);
		    D100("Parse_PJL: sub name $name value '$value'");
		    $hash->{SET_SUBSET}{$cmd}{$name}=$value;
		} else {
		    $hash->{SET}{$name}=$value;
		}
	    } elsif( $input =~ /^\@PJL\s+(\S+)\s+(.*)/ ){
		$cmd = $1;
		$input = $2;
		D100("Parse_PJL: $cmd input '$input'");
		my @list = split(' ', $input);
		foreach my $v (@list){
		    D100("Parse_PJL: $cmd option '$v'");
		    ($name,$value) = split("=", $v, 2 );
		    if( $value =~ m/^(["'])(.*)\1\s*$/ ){
			$value = $2;
		    }
		    D100("Parse_PJL: $cmd name $name value '$value'");
		    if( defined $value ){
			$hash->{$cmd}{$name}=$value;
		    } else {
			$hash->{$cmd}{$v}=1;
		    }
		}
	    }
	    $input = "";
	} else {
	    $input = join('',@PCL_input_chars);
	    return( $input, $hash );
	}
    }
    @PCL_input_chars = @saved_input_chars;
    ( $PCL_read_fh, ) =
	($saved_read_fh, );
    return( $input, $hash );
}

sub TestParse_PJL( $ $ ){
    my( $debug, $test ) = @_;
    my $r;
	
    $Foomatic::Debugging::debug = $debug;
    D0("TestParse_PJL");
	my( $input, $hash );
    if( $test & 0x01 ){
	( $input, $hash ) = Parse_PJL(
		"test",
	 undef );
	$input = "" if not defined($input);
	D100("TestParse_PJL: test 1 input '" . tr_esc($input) . "'");
	D100("TestParse_PJL: test 1 hash " . Dumper($hash));
    }
    if( $test & 0x01 ){
	( $input, $hash ) = Parse_PJL(
		"\01bE",
	 undef );
	$input = "" if not defined($input);
	D100("TestParse_PJL: test 1 input '" . tr_esc($input) . "'");
	D100("TestParse_PJL: test 1 hash " . Dumper($hash));
    }
    if( $test & 0x01 ){
	( $input, $hash ) = Parse_PJL(
		"\x1b%-12345X",
	 undef );
	$input = "" if not defined($input);
	D100("TestParse_PJL: test 1 input '" . tr_esc($input) . "'");
	D100("TestParse_PJL: test 1 hash " . Dumper($hash));
    }
    if( $test & 0x01 ){
	( $input, $hash ) = Parse_PJL(
		"\x1b%-12345X\@PJL\n",
	 undef );
	$input = "" if not defined($input);
	D100("TestParse_PJL: test 1 input '" . tr_esc($input) . "'");
	D100("TestParse_PJL: test 1 hash " . Dumper($hash));
    }
    if( $test & 0x01 ){
	my $pid;
	if( $pid = fork ){
	    $pid = wait();
	    D100("TestParse_PJL: exit status ".sprintf( "0x%02x",$?) );
	} else {
	( $input, $hash ) = Parse_PJL(
		"\x1b%-12345X\@PJL" . ("noend"x200),
	 undef );
	$input = "" if not defined($input);
	D100("TestParse_PJL: test 1 input '" . tr_esc($input) . "'");
	D100("TestParse_PJL: test 1 hash " . Dumper($hash));
	}
    }
    if( $test & 0x01 ){
	( $input, $hash ) = Parse_PJL(
		"\x1b%-12345X\@PJL\n". "\x1b%-12345X\@PJL\n",
	 undef );
	$input = "" if not defined($input);
	D100("TestParse_PJL: test 1 input '" . tr_esc($input) . "'");
	D100("TestParse_PJL: test 1 hash " . Dumper($hash));
    }
    if( $test & 0x01 ){
	( $input, $hash ) = Parse_PJL(
		"\x1b%-12345X\@PJL\n\x1bE",
	 undef );
	$input = "" if not defined($input);
	D100("TestParse_PJL: test 1 input '" . tr_esc($input) . "'");
	D100("TestParse_PJL: test 1 hash " . Dumper($hash));
    }
    if( $test & 0x02 ){
	( $input, $hash ) = Parse_PJL(
'%-12345X@PJL JOB NAME="Document"
@PJL COMMENT "4100 Series (PCL 6);Version 4.20.4100.430"
@PJL COMMENT "OS Info:Win9x;3.95;7.10;"
@PJL COMMENT "Application Info: WordPad MFC Application;Microsoft(R) Windows NT(R) Operating System;5.00.1691.1;"
@PJL COMMENT "Username: Computer"
@PJL COMMENT SET Option1="value1"
@PJL SET JOBATTR="JobAcct1=Computer"
@PJL SET JOBATTR="JobAcct2=PatrickHome"
@PJL SET JOBATTR="JobAcct3=workgroup"
@PJL SET JOBATTR="JobAcct4=20031231110814"
@PJL SET JOBATTR="JobAcct5=a0418ca0-3b81-11d8-812c-0000e863bf26"
@PJL SET RET=MEDIUM
@PJL SET DUPLEX=OFF
@PJL SET OUTBIN=UPPER
@PJL SET FINISH=NONE
@PJL SET PAGEPROTECT=AUTO
@PJL SET PAPER=LETTER
@PJL SET HOLD=OFF
@PJL SET RESOLUTION=600
@PJL SET BITSPERPIXEL=2
@PJL SET EDGETOEDGE=NO
@PJL ENTER LANGUAGE=PCLXL
',
	 undef );
	$input = "" if not defined($input);
	D100("TestParse_PJL: test 1 input '" . tr_esc($input) . "'");
	D100("TestParse_PJL: test 1 hash " . Dumper($hash));
    }
}

# TestParse_PJL(0x300,3);
1;

