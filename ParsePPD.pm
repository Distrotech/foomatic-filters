package Foomatic::ParsePPD;

# Foomatic::ParsePPD Package
#  Provides parsing methods for PPD files

=head1 NAME

Foomatic::ParsePPD.pm - PPD Parsing Support for Foomatic 

=head1 DESCRIPTION

The Foormatic::Parse PPD package provides a large number
of utility functions for processing PPD files.

=head1 EXPORTS

 @EXPORT = qw(

    argbyname check_option_type_conflict checkarg checkdefaultoptions
    checklongnames checkoptionvalue checksetting cutguiname findalias
    fix_foomatic_options fix_lang fix_numval fix_pagesize fix_pickmany
    fix_user_options fixoptionvalue generalentries getdocinfo getmargins
	getmarginsformarginrecord getpapersize ppdfromperl htmlify
	infobyname longname normalizename ppdfromvartoperl ppdtoperl readmline
	ripdirective setnumericaldefaults sortargs sortoptions sortvals
	stringvalid syncpagesize unhexify unhtmlify valbyname

     );

    =cut

    require Exporter;

    use strict;
    no strict;

    @ISA = qw(Exporter);

    @EXPORT = qw(

	argbyname check_option_type_conflict checkarg checkdefaultoptions
	checklongnames checkoptionvalue checksetting cutguiname findalias
	fix_foomatic_options fix_lang fix_numval fix_pagesize fix_pickmany
	fix_user_options fixoptionvalue generalentries getdocinfo getmargins
	getmarginsformarginrecord getpapersize ppdfromperl htmlify
    infobyname longname normalizename ppdfromvartoperl ppdtoperl readmline
    ripdirective setnumericaldefaults sortargs sortoptions sortvals
    stringvalid syncpagesize unhexify unhtmlify valbyname

);

use strict;

use Data::Dumper;
use Time::HiRes qw( gettimeofday time );
use Foomatic::Debugging;

# if you have module in file, then use 'use FoomaticDebugging'
import FoomaticDebugging;

my $ver = "Patrick Powell - Version 1";

sub argbyname($ $);
sub check_option_type_conflict( $ $ $ );
sub checkarg( $ $ $ );
sub checkdefaultoptions( $ );
sub checklongnames( $ );
sub checkoptionvalue( $ $ $ );
sub checksetting( $ $);
sub cutguiname( $ $ );
sub findalias( $ $ );
sub fix_foomatic_options ( $ $ $ $ $ $ );
sub fix_lang( $ );
sub fix_numval( $ $ ; $ );
sub fix_pagesize( $ );
sub fix_pickmany( $ @ );
sub fix_user_options ( $ $ $ $ $ $ );
sub fixoptionvalue( $ $ $ );
sub generalentries( $ );
sub getdocinfo( $ );
sub getmargins( $ $ $ $ );
sub getmarginsformarginrecord( $ $ $ $ );
sub getpapersize( $ );
sub ppdfromperl( $ $ $ );
sub htmlify( $; $);
sub infobyname($ $);
sub longname( $ );
sub normalizename ( $ );
sub ppdfromvartoperl($ $);
sub ppdtoperl($ $ $);
sub readmline( $ $ $ );
sub ripdirective( $ $ );
sub setnumericaldefaults ( $ );
sub sortargs($);
sub sortoptions( $ $ );
sub sortvals();
sub stringvalid( $ $ );
sub syncpagesize( $ $ $ $ );
sub unhexify( $ );
sub unhtmlify($);
sub valbyname( $ $ );
sub fix_ppd_info( $ $ );


=head1 PPD FILE PARSING

The parser basically runs through the PPD file, checking options,
and producing a hash structure with indexes corresponding to the
option, option selection,  and the corresponding PostScript,
PJL, or PCL needed to implement them.

=head2 Standard Options and Option Defaults

For example:

 *OpenUI *PageSize: PickOne
 *OrderDependency: 30 AnySetup *PageSize
 *DefaultPageSize: Letter
 *PageSize Letter/Letter: "
     <</PageSize [612 792] /ImagingBBox null>> setpagedevice"
 *End
 *OrderDependency: 40 AnySetup *PageSize Env
 *PageSize EnvISOB5/B5 (ISO): "
     <</PageSize [499 709] /ImagingBBox null>> setpagedevice"
 *End
 *JCLPageSize Letter/Letter: "
     PJL SET PAGESIZE = 'LETTER'<0A>
 "
 *End
 *?PageSize: "
    save
    currentpagedevice /PageSize get aload pop
    restore
 "
 *End
 *CloseUI: *PageSize
 
 $dat = {
  'args_byname' => {
   # PageSize
   # *OpenUI *PageSize: PickOne
   # *OrderDependency: 30 AnySetup *PageSize
    'PageSize' => {
      'option' => {ps} => '1';,
      'section' => 'AnySetup',
      'order' => {ps} => '30',
      'grouptrans' => [],
      'group' => '',
      'name' => 'PageSize',
      'comment' => 'Page Size',
      'type' => 'enum'
      'argstyle' => { 'ps' => 'ps' 'pjl' => 'pjl' },
      'proto' => { 'ps' => '%s' 'pjl' => '%s' },
   # *DefaultPageSize: Letter
      'default' => 'Letter',
   # *PageSize Letter/Letter: " ... option
   # *PageSize EnvISOB5/B5 (ISO): " ... option
      'vals' => [ 'Letter', 'EnvISOB5', ],
      'vals_bylcname' => [ 'letter', 'envisob5', ],
      'vals_byname' => {
        'Letter' => {
		  'order' => {ps} => '30',
          'driverval' => {
              'ps' => '<</PageSize [612 792] /ImagingBBox null>>
                        setpagedevice',
              'pjl' => 'PJL SET PAGESIZE = \'LETTER\'<0A>' },
          'comment' => 'Letter',
          'value' => 'Letter'
        },
        'EnvISOB5' => {
          'driverval' => {
            'ps' => '<</PageSize [499 709] /ImagingBBox null>>
                         setpagedevice'
          },
          'comment' => 'B5 (ISO)',
          'value' => 'EnvISOB5'
        },
      },
   # *?PageSize: "... - query
      'query' => '
              save
              currentpagedevice /PageSize get aload pop
              ',
    },
    ...

=head2 Printer Lanuage and PPD Options

As shown in the example,
there are several places in the PPD file
entries where the language (PostScript, PCL, PJL, etc)
are used to set the fields values for the corresponding
language.

In the following sections, the term <lang> is meant to match
the Regular Expression (JCL|PJL|PXL|PCL|); a blank or empty match
implies the PostScript (PS) value.

The 'JCL' value will be replaced by 'PJL' for consistency,
and the lower case of the language will be used when
setting hash values.

=head2 Standard PPD Entries

=over 4

=item *Open(Sub|)Group: <group>[/<translation>], *Close(Sub|)Group: <group>

Open or close a group.  The current group name is used
to set the 'group' field in an option, i.e. the following
is assumed whereever an option is defined: 

$dat->{'args_byname'}{<option>}{'group'} = <group>;

=item *<lang>OpenUI *<lang><option>[/<translation>]: <type>

Open a User Interface option definition and specify the option type.
Types can be PickOne (enum), PickMany (pickmany), or Boolean (bool).

$dat->{'args_byname'}{<option>}{'type'} = <type>;
$dat->{'args_byname'}{<option>}{'option'}{<lang>} = 1;

=item *<lang>CloseUI: <option>

End of the User Interface option definition.

=item *Default<lang><option>: <default>

Set default option value.

$dat->{'args_byname'}{$argname}{'default'} = <default>;

=item *JCL(Begin|To<lang>Interpreter|End): <code>

Code to start or end a job,  and to switch interpreter mode
to recognize the specified language.

 Begin: $dat->{'pjl_support'}{'begin'} = <code>;
 End:   $dat->{'pjl_support'}{'end'} = <code>;
 To<lang>Interpreter:
  $dat->{'pjl_support'}{'tointerpreter'}{<lang>} = <code>;

=item *<lang><option> <choice>[/<translation>]: <code>

Set the code value for the <option>'s <choice> entry.
This is the standard PPD option value specification.

 $dat->{'args_byname'}{<option>}{'vals_byname'}{<choice>}
   {'translation'} = <translation>;
 $dat->{'args_byname'}{<option>}{'vals_byname'}{<choice>}
   {'code'}{<lang>} = <code>;

Note: if the <code> matches the RE '^%% FoomaticRIPOptionSetting'
then the <code> value sets:
 $dat->{'args_byname'}{<option>}{'vals_byname'}{<choice>}
   {'setting'}{<lang>} = <code>;

This form of the option is used when generating PostScript
files by drivers and other PostScript generators.  The
'code' value is used by the foomatic-rip filter for the actual
output value.

=item *<lang><option>: <code>

The PPD file contains information about various entries.
This will push the value onto the 'info_code' field:

push @{$dat->{'args_byname'}{<option>}{'info_code'}}, <code>;

=item *(NonUI)OrderDependency: <order> <section> *<lang><option> <value>

The order information for an option value in the PostScript or
other output.
Examples:

 *OrderDependency: 10 Setup *Resolution
 *OrderDependency: 10 JCLSetup *JCLResolution
 *OrderDependency: 40 Setup *PageSize Env

The <order> value is the weighting used for the option.
Smaller values come closer to the start of the option set.

The <section> tells what part of the PostScript or
other language the code is to be placed in.
For PostScript, the values can be:

=over 2

=item Prolog

The information can appear at the start of document setup section.

=item DocumentSetup

The information can appear at the start of document setup section.

=item AnySetup

The information can appear at any setup location.

=item PageSetup

The information can appear at the start of a document
page setup location.

=back

The <lang> and <option> specify that this ordering is for the
specfied language and option; if the <value> is specified, then
the weighting is value specific.

=item *?<option>: <code>

The PostScript code used to obtain the current value for this
option from the printer.

=item *(UI|NonUI)Constraints: <constraint>

This specifies a constraint value for the PPD options.
If the specified options have the indicated values, then
this combination is excluded from use.

If the constraint has the form:
 *option1 value1 *option2 value2
this sets:
 $dat->{constraint}{option1}{value1}{option2}{value2} = 1;
  (option1 < option2)
 $dat->{constraint}{option2}{value2}{option1}{value1} = 1;
  (option1 > option2)

If the constraint has the form:
 *option1 *option2 value2
this sets:
 $dat->{constraint}{option1}{'*'}{option2}{value2} = 1;

If the constraint has the form:
 *option1 value1 *option2
this sets:
 $dat->{constraint}{option2}{'*'}{option1}{value1} = 1;

If the constraint has the form:
 *option1 *option2
this sets:
 $dat->{constraint}{option1}{'*'}{option2}{'*'} = 1;

In the cases with an '*' for the option,  if the value
of the first option is 'None' or 'False', then the values of the
second option is ignored.
If the value of the first option is anything but 'None' or 'False',
then the second option is checked.

Note: some options have the language as part of the option name,
i.e. - JCLResultion, etc.  The language prefix is removed before using
the option.

=item *(JCL|PJL|PXL|PCL|)CustomPageSize True: <code>

The code to set up custom page sizes. This.

This is similar in effect to setting the
Custom value for the PageSize and PageRegion options.

=item *End

End of PPD file

=back

=head2 Special PPD Keywords

Some of the PPD file information entries have special meanings
for the foomatic-rip and other programs.

=over 4

=item *NickName: <make> <model>

Sets the printer make and model information for foomatic-rip.

=back

=head2 Foomatic PPD File Entries

The following PPD file entries have the term Foomatic
in them and are treated in a special manner

=over 4

=item *FoomaticIDs: <printer ID> <driver ID>

The printer and driver ID information.

 $dat->{'id'} = <printer ID>
 $dat->{'driver'} = <driver ID>

=item *FoomaticSystemOption <option>: <value>

This is used to set the values in the configuration database.
It is a general purpose way of passing information to the
Foomatic-RIP driver or other program.

${dat}->{foomatic_option}{$key} = $value;

For example, 

 *FoomaticSystemOption aliases: "
          duplex=Duplex&eq;DuplexNoTumble
          DuplexNoTumble=Duplex&eq;DuplexNoTumble "

=item *FoomaticRIPPostPipe: <code>

The post processor command for foomatic-rip.  Sets:

${dat}->{postpipe} = <code>

=item *FoomaticRIPCommandLine: <code>

The Raster Converter or Renderer
to be used by foomatic-rip.

The command line entries of the form %A, %B, etc., are replaced
by the corresponding command line option values.
See the *FoomaticRIPOption entry for more information.

${dat}->{cmd} = <code>

=item *FoomaticRIPDefault <option>: <value> (or 'value')

Set the default value for a Foomatic option.

$dat->{arg_byname}{$option}{default} = <value>;

=item *FoomaticRIPOptionSetting <lang><option>[=<choice>]: <code>

Set the value for the specified option.

=item *FoomaticRIPOption <lang><option>: <type> <style> <spot> [<order>[<section>]]

Example:
 *FoomaticRIPOption PageSize: enum CmdLine B
 *FoomaticRIPOption Magenta: float PS A
 *FoomaticRIPOption PrintoutMode: enum Composite A
 *FoomaticRIPOption MediaType: enum JCL A

Sets the type, style, and placement
information for the Foomatic option.

 $arg = $dat->{arg_byname}{<option>};

 $arg->{'type'} = <type>
 $arg->{'style'} = <style>
 $arg->{'spot'}{<lang>} = <spot>
 $arg->{'order'}{$language} = (<order> or 100);
 $arg->{'section'}{$language} = ( <section> or 'AnySetup');

=over 2

=item <type> = enum, bool, string, password, int, float

The option type.  By convention, all standard PPD options are
enumerated or boolean; boolean is really just an enumerated
type with two values: True and False.

=item <style> = 'ps', 'jcl', 'pjl', 'pcl', 'composite', 'cmdline'

The type of option.  The important types are composite and
cmdline. Note that 'jcl' is translated to 'pjl'.

=item <spot> = A, ..., Z

When the <type> value is cmdline, specifies a letter that will
serve as a marker in the command line or PostProcessor
command line that will be replaced by the corresponding option
value.

=back

=item *FoomaticRIPOptionAllowedChars <lang><option>: <code>

The characters allowed in the value for the STRING option.

=item *FoomaticRIPOptionAllowedRegExp <lang><option>: <re>

A regular expression (RE) the string option must match.
The RE is in html escaped format.

=item *FoomaticRIPOptionMaxLength <lang><option>: <length>

The maximum string option length.

=item *FoomaticRIPOptionPrototype <lang><option>: <format>

String and numeric (int and float) options only.
Specifies an sprintf format string to be used to
generate the final option value.

=item *FoomaticRIPOptionRange <lang><option>: <min> <max>

Maximum and minimum values for numeric options.

=back 

=cut

# ppdtoperl( $dat, $ppdfiles, $sysdeps ), 
#     Build a Perl data structure of the ppdfiles
#     splits the ppdfiles string up into files and calls
#        the PPD parser
#   $dat  - hash, will be updated with ppd file information
#   $ppdfiles - list of comma separated PPD files
#   $sysdeps - used to get the unzip executable
#
#    returns $dat

sub ppdtoperl($ $ $) {
    my ( $dat, $ppdfile, $sysdeps ) = @_;
    my ( $cmd, $gzip, $err, @cmd );

    # Load the PPD file and send it to the parser
    my @files = split /[,:]/, $ppdfile;
    my @ppd;
    foreach $ppdfile (@files){
	next if not defined $ppdfile or $ppdfile eq '';
	$cmd = $ppdfile;
	if( not -f $ppdfile ){
	    $err = "Non-existent PPD file '$ppdfile'";
	    rip_die ($err, $EXIT_PRNERR_NORETRY_BAD_SETTINGS );
	}
	if( not -f $ppdfile ){
	    $err = "Non-readable PPD file '$ppdfile'";
	    rip_die ($err, $EXIT_PRNERR_NORETRY_BAD_SETTINGS );
	}
	if($ppdfile =~ /\.gz$/i ){
	    $gzip = (( $sysdeps and $sysdeps->{'gzip'} ) or 'gzip');
	    $cmd = "$gzip -cd $ppdfile 2>&1 |";
	}
	D0("Opening PPD file '$cmd'\n");
	open PPD, $cmd or do {
	    $err =  "Error opening '$cmd' - $!";
	    rip_die ($err, $EXIT_PRNERR_NORETRY_BAD_SETTINGS );
	};
	while( <PPD> ){
	    chomp;
	    s/\015//;
	    D8("READ '$_'\n");
	    push( @ppd, $_ );
	}
	close PPD or do {
	    if( $gzip ){
		$err =  "Error from '$cmd' @ppd - error $?";
		rip_die ($err, $EXIT_PRNERR_NORETRY_BAD_SETTINGS );
	    } else {
		$err =  "Error from '$cmd' - $!";
		rip_die ($err, $EXIT_PRNERR_NORETRY_BAD_SETTINGS );
	    }
	};
    }
    if( not @ppd ){
	$err =  "Zero length PPD file '$cmd'";
	rip_die ($err, $EXIT_PRNERR_NORETRY_BAD_SETTINGS );
    }
    ppdfromvartoperl($dat, \@ppd);
    return( $dat );
}


# ppdfromvartoperl($dat, \@ppdfile ) {
#    parse the PPD file
#   $dat - updated with PPD information 
#   @ppdfile - array of lines
#
# PARSER

sub ppdfromvartoperl($ $) {

    my ($dat, $ppd) = @_;

    # Build a data structure for the renderer's command line and the
    # options

    my @currentgroup;    # We are currently in this group/subgroup
    my @currentgrouptrans;     # Translation/long name for group/subgroup
    my $isfoomatic = 0;        # Do we have a Foomatic PPD?
    my $foomatic_lang = 'ps'; # last foomatic language

    # If we have an old Foomatic 2.0.x PPD file, read its built-in Perl
    # data structure into @datablob and the default values in %ppddefaults
    # Then delete the $dat structure, replace it by the one 'eval'ed from
    # @datablob, and correct the default settings according to the ones of
    # the main PPD structure

    my @datablob;
    
    # Parse the PPD file
    for (my $i = 0; $i < @{$ppd}; $i++) {
	$_ = $ppd->[$i];
	if ( m!^\*%! or m!^\s*$! ){
	    D8("COMMENT $_");
	    next if /^$/;
	    my $argname = 'Comment';
	    my $code = $_;
	    my $arg = $dat->{'info_byname'}{$argname};
	    if( not $arg ){
		push @{$dat->{'info'}}, $argname;
		$dat->{'info_bylcname'}{lc($argname)} = $argname;
		$arg->{'name'} = $argname;
	    }
	    $arg->{'quoted'} = 0;
	    push @{$arg->{'info_code'}}, $code;
	    $arg->{'info_last'} = $code;
	    $dat->{'info_byname'}{$argname} = $arg;
	    next;
	}
	D4("LOOP: [$i] $_");
	# Foomatic should also work with PPD files downloaded under
	# Windows.
	# Parse keywords
	if (m!^\*(JCL|PJL|PXL|PCL|)CustomPageSize\s+True:\s*\"(.*)$!) {
	    # "*CustomPageSize True: <code>"
	    D4("*CustomPageSize True: <code>" );
	    # Code string can have multiple lines, read all of them
	    my $language = fix_lang($1);
	    my $code = readmline( $2, $ppd,  \$i );

	    D4("<code> $code" );
	    my $setting = 'Custom';
	    my $translation = 'Custom Size';
	    # Make sure that the argument is in the data structure
	    # in both PageSize and PageRegion
	    foreach my $argname ('PageSize', 'PageRegion'){
		my $arg = checkarg ($dat, $argname, $language);
		# Make sure that the setting is in the data structure
		my $option = checksetting ($arg, $setting);
		$arg->{'option'}{$language} = 1;
		$option->{'comment'} = $translation;

		# Store the value
		if( $code =~ m!^%% FoomaticRIPOptionSetting! ){
		    $option->{'setting'}{$language} = $code;
		} else {
		    $option->{'driverval'}{$language} = $code;
		}
	    }
	} elsif (m!^\*Open(Sub|)Group:\s*([^\s/]+)(/(.*)|)$!) {
	    # "*Open[Sub]Group: <group>[/<translation>]"
	    D4("*Open[Sub]Group: <group>[/<translation>]" );
	    my $group = $2;
	    my $grouptrans = $4;
	    $grouptrans = longname($group) if !$grouptrans;
	    D4("<group> $group <translation> $grouptrans" );
	    push(@currentgroup, $group);
	    push(@currentgrouptrans, $grouptrans);
	} elsif (m!^\*Close(Sub|)Group:\s*([^\s/]+)$!) {
	    # "*Close[Sub]Group: <group>"
	    D4("*Close[Sub]Group: <group>");
	    my $group = $2;
	    D4("<group> $group");
	    while( @currentgroup ){
		my $closing = pop(@currentgroup);
		pop(@currentgrouptrans);
		if( $closing ne $group ){
		    D0( "Bad nesting Close${1}Group *$group, should have been '$closing'");
		} else {
		    last;
		}
	    }
	} elsif (m!^\*(JCL|PJL|PXL|PCL|)OpenUI\s+\*([^:]+):\s*(\S+)\s*$!) {
	    # "*[JCL|PJL|PCL]OpenUI *<option>[/<translation>]: <type>"
	    D4("*[JCL|PJL|PCL]OpenUI *<option>[/<translation>]: <type>" );
	    my $language = $1;
	    my $argname= $2;
	    my $argtype = $3;
	    my $translation = '';
	    if ($argname=~ m!^([^:/\s]+)/([^:]*)$!) {
		$argname = $1;
		$translation = $2;
	    }
	    if( $language ){
		if( not ($argname =~ s/^$language//) ){
		    D0("missing language '$language' on option '$_'");
		}
	    }
	    $language = fix_lang($language);

	    D4("<lang> '$language' <option> '$argname', <trans> '$translation', type $argtype" );
	    # Set the argument type only if not in conflict
	    if ($argtype eq 'PickOne') {
		$argtype = 'enum';
	    } elsif ($argtype eq 'PickMany') {
		$argtype = 'pickmany';
	    } elsif ($argtype eq 'Boolean') {
		$argtype = 'bool';
	    } else {
		D0("Warning: option '$argname' unknown type '$argtype'");
	    }
	    my $type = $dat->{'args_byname'}{$argname}{'type'};
	    if ( $type and $type ne $argtype and $type ne 'enum' ){
		D0("Warning: option '$argname' type changed from'$type' to '$argtype'");
	    } else {
		$dat->{'args_byname'}{$argname}{'type'} = $argtype;
	    }
	    # OK, you can specify this as an option
	    # Make sure that the argument is in the data structure
	    my $arg = checkarg ($dat, $argname, $language);
	    # Store the values
	    $arg->{'option'}{$language} = 1;
	    $arg->{'comment'} = $translation if $translation;
	    push @{$arg->{'group'}},@currentgroup;
	    push @{$arg->{'grouptrans'}},@currentgrouptrans;
	} elsif (m!^\*(JCL|PJL|PXL|PCL|)CloseUI:\s+\*([^:/\s]+)\s*$!) {
	    # "*CloseUI *<option>"
	    D4("*(JCL|PJL|PXL|PCL|)CloseUI *<option>" );
	    D4("<option> $2" );
	} elsif (( m!^\*FoomaticRIPOption (JCL|PJL|PXL|PCL|)([^/:\s]+):(.*)$! )){
	    # "*FoomaticRIPOption (JCL|PJL|PXL|PCL|)<option>: <type> <style> <spot> [<order>[<section>]]"
	    # <type> = enum, bool, pickmany, string, password, int, float
	    # <style> = 'ps', 'pjl', 'pcl', 'composite', 'cmdline'
	    # <spot> = 'A', 'B', ...  - for command line options
	    # <order> = 0, 1, ....    - relative order weight
	    # <section> = 0, 1, ....  - section Prolog, DocumentSetup, PageSetup, JCLSetup, AnySetup
	    D4("*FoomaticRIPOption (JCL|PJL|PXL|PCL|)<option>: <type> <style> <spot> [<order>[section]]" );
	    # <order> only used for 1-choice enum options
	    my $argname = $2;
	    my( $argtype, $argstyle, $spot, $order, $section ) = split(' ', $3);

	    $order = 100 if not defined $order;
	    $section = '' if not defined $section;

	    # get the default style (language) for the foomatic options
	    $foomatic_lang = $argstyle = fix_lang( $argstyle );
	    D4("<option> $argname, <type> $argtype, <style> $argstyle, <spot> $spot <order> $order <section> $section" );
	    D4("<foomatic_lang> $argstyle");
	    # Make sure that the argument is in the data structure

	    my $arg = checkarg ($dat, $argname,  $argstyle);
	    # Store the values
	    # <type> = enum, bool, string, password, int, float
	    my $t = $arg->{'type'};
	    if( $t and $t ne $argtype and $t ne 'enum' ){
		my $err = "FoomaticRIPOption $argname type currently '$t', not redefined as $argtype";
		D0("$err\n");
	    } else {
		$arg->{'type'} = $argtype;
	    }
	    if( not defined $arg->{'section'}{$argstyle} ){
		$arg->{'section'}{$argstyle} = $section;
	    }
	    $arg->{'spot'}{$argstyle} = $spot;
	    $arg->{'order'}{$argstyle} = $order;
	    check_option_type_conflict( $arg, 'foomatic', $argstyle );
	} elsif (m!^\*FoomaticRIPOptionPrototype\s+(JCL|PJL|PXL|PCL|)([^/:\s]+):\s*\"(.*)$!) {
	    # "*FoomaticRIPOptionPrototype <option>: <code>"
	    D4("*FoomaticRIPOptionPrototype <lang><option>: <code>" );
	    # Used for numerical and string options only
	    my $language = fix_lang($1 or $foomatic_lang);

	    my $argname = $2;
	    # Code string can have multiple lines, read all of them
	    my $proto = readmline( $3, $ppd,  \$i );
	    # Make sure that the argument is in the data structure
	    D4("<lang> $language <option> $argname, <code> $proto" );
	    my $arg = checkarg ($dat, $argname, $language);
	    $arg->{'proto'}{$language} = unhtmlify($proto);
	    check_option_type_conflict( $arg, 'foomatic', $language );
	} elsif (m!^\*FoomaticRIPOptionRange\s+(JCL|PJL|PXL|PCL|)([^/:\s]+):\s*(\S+)\s+(\S+)\s*$!) {
	    # "*FoomaticRIPOptionRange <option>: <min> <max>"
	    D4("*FoomaticRIPOptionRange <lang><option>: <min> <max>" );
	    # Used for numerical options only
	    my $language = fix_lang($1 or $foomatic_lang);
	    my $argname = $2;
	    my $min = $3;
	    my $max = $4;

	    D4("OPTION RANGE <lang> $language <option> $argname <min> $min <max> $max" );
	    # Make sure that the argument is in the data structure
	    my $arg = checkarg ($dat, $argname, $language);
	    # Store the values
	    $arg->{'min'} = $min;
	    $arg->{'max'} = $max;
	    check_option_type_conflict( $arg, 'foomatic', $language );
	} elsif (m!^\*FoomaticRIPOptionMaxLength\s+(JCL|PJL|PXL|PCL|)([^/:\s]+):\s*(\S+)\s*$!) {
	    # "*FoomaticRIPOptionMaxLength <option>: <length>"
	    D4("*FoomaticRIPOptionMaxLength <option>: <length>" );
	    # Used for string options only
	    my $language = fix_lang($1 or $foomatic_lang);
	    my $argname = $2;
	    my $maxlength = $3;

	    D4("<lang> $language <option> $argname <length> $3" );
	    # Make sure that the argument is in the data structure
	    my $arg = checkarg ($dat, $argname, $language);
	    # Store the value
	    $arg->{'maxlength'} = $maxlength;
	    check_option_type_conflict( $arg, 'foomatic', $language );
	} elsif (m!^\*FoomaticRIPOptionAllowedChars\s+(JCL|PJL|PXL|PCL|)([^/:\s]+):\s*(\"?)(.*)$!) {
	    # "*FoomaticRIPOptionAllowedChars <option>: <code>"
	    D4("*FoomaticRIPOptionAllowedChars <lang><option>: <code>" );
	    # Used for string options only
	    my $language = fix_lang($1 or $foomatic_lang);
	    my $argname = $2;
	    # Code string can have multiple lines, read all of them
	    my $code = $4;
	    $code = readmline( $4, $ppd,  \$i ) if $3;

	    D4("<lang> $language <option> $argname <code> $code" );
	    # Make sure that the argument is in the data structure
	    my $arg = checkarg ($dat, $argname, $language);
	    # Store the value
	    $arg->{'allowedchars'} = unhtmlify($code);
	    check_option_type_conflict( $arg, 'foomatic', $language );
	} elsif (m!^\*FoomaticRIPOptionAllowedRegExp\s+(JCL|PJL|PXL|PCL|)([^/:\s]+):\s*(\"?)(.*)$!) {
	    # "*FoomaticRIPOptionAllowedRegExp <lang><option>: <code>"
	    D4("*FoomaticRIPOptionAllowedRegExp <lang><option>: <code>" );
	    # Used for string options only
	    my $language = fix_lang($1 or $foomatic_lang);
	    my $argname = $2;

	    # Code string can have multiple lines, read all of them
	    my $code = $4;
	    $code = readmline( $4, $ppd,  \$i ) if $3;
	    # Make sure that the argument is in the data structure
	    D4("<lang> $language <option> $argname <code> $code" );
	    my $arg = checkarg ($dat, $argname, $language);
	    # Store the value
	    $arg->{'allowedregexp'} = unhtmlify($code);
	    check_option_type_conflict( $arg, 'foomatic', $language );
	} elsif ((m!^\*FoomaticRIPOptionSetting\s+(JCL|PJL|PXL|PCL|)([^/:=\s]+)=([^/:=\s]+):\s*(\"?)(.*)$!) ||
		 (m!^\*FoomaticRIPOptionSetting\s+(JCL|PJL|PXL|PCL|)([^/:=\s]+)():\s*(\"?)(.*)$!)) {
	    # "*FoomaticRIP(JCL|PJL|PXL|PCL|)OptionSetting <option>[=<choice>]: <code>"
	    D4("*FoomaticRIP(JCL|PJL|PXL|PCL|)OptionSetting <option>[=<choice>]: <code>" );

	    # For boolean options <choice> is not given
	    # (comment in original code)
	    # I am going to assume that the 'setting' is 'True'
	    # - how do you get 'False'?  I smell a botch in the foomatic db
	    # code.  Patrick Powell

	    my $language = fix_lang($1 or $foomatic_lang);
	    my $argname = $2;
	    my $setting = $3;
	    my $code = $5;
	    $code = unhtmlify(readmline( $5, $ppd,  \$i )) if $4;
	    D4("START <foomatic_lang> $foomatic_lang <lang> $language <option> $argname <choice> $setting <code> $code" );
	    # Code string can have multiple lines, read all of them
	    $setting = 'True' if (!$setting);

	    D4("<lang> $language <option> $argname <choice> $setting <code> $code" );
	    # Make sure that the argument is in the data structure
	    my $arg = checkarg ($dat, $argname, $language);
	    # Make sure that the setting is in the data structure (enum
	    # options)
	    my $option = checksetting ($arg, $setting);
	    if( $code =~ m!^%% FoomaticRIPOptionSetting! ){
		$option->{'setting'}{$language} = ($code);
	    } else {
		$option->{'driverval'}{$language} = ($code);
	    }
	    check_option_type_conflict( $arg, 'foomatic', $language );
	} elsif (m!^\*(NonUI|)OrderDependency:\s*(.*)\s*$!) {
	    # "*OrderDependency: <order> <section> *<option>"
	    # *OrderDependency: 10 AnySetup *Resolution
	    # *OrderDependency: 10 JCLSetup *JCLResolution
	    # *OrderDependency: 10 PCLSetup *PCLResolution
	    D4("*OrderDependency: <order> <section> *<option>" );
	    my $nonui = ($1 || '');
	    my( $order, $section, $argname, $option ) = split( ' ', $2 );
	    $argname =~ s/^\*(JCL|PJL|PXL|PCL|)//;
	    my $language = fix_lang($1);
	    $option = '' if not defined $option;
	    D4("<nonui> '$nonui' <order> $order <section> $section <lang> $language <argname> $argname <option> $option" );

	    # Make sure that the argument is in the data structure
	    my $arg = checkarg ($dat, $argname, $language);
	    # Store the values
	    if( $option ){
		D0("WARNING: ignoring option '$option' in $_");
	    }
	    $arg->{'order'}{$language} = $order;
	    $arg->{'section'}{$language} = $section;
	    $arg->{'nonui'}{$language} = $nonui;
	    check_option_type_conflict( $arg, 'user', $language );
	} elsif (m!^\*Default(JCL|PJL|PXL|PCL|)([^/:\s]+):\s*("?)(.*)\s*$!) {
	    D4("*Default<lang><option>: <value>" );
	    # "*Default<option>: <value>"
	    # *DefaultJCLResolution: 600x600dpi
	    my $language= fix_lang($1);
	    my $argname = $2;
	    my $default = $4;
	    $default = readmline( $4, $ppd,  \$i ) if( $3 );
	    $default =~ s/^\s+//;
	    $default =~ s/\s+$//;
	    my $translation = '';
	    if ($default =~ m!^([^:/\s]+)/([^:]*)$!) {
		$default = $1;
		$translation = $2;
	    }

	    D4("<lang> $language <option> $argname <default> $default" );

	    # Make sure that the argument is in the data structure
	    my $arg = checkarg ($dat, $argname, $language);
	    # Store the value
	    # Store the value foomatic default value 'default'
	    my $type = $arg->{'type'} || '';
	    my $bool = $type eq 'bool';
	    if ($bool) {
		my $v = lc($default);
		if (($v eq 'true') || ($v eq 'on') || ($v eq 'yes') ||
		    ($v eq '1')) {
		    $default = 'True';
		} else {
		    $default = 'False';
		}
	    }
	    $arg->{'default'} = $default;
	    # we will ignore the comment for now
	    if( $type !~ /^(int|float|string)$/ and $default !~ /,/ ){
		my $option = checksetting ($arg, $default);
		$option->{'comment'} = $translation if $translation and $option;
	    }
	    check_option_type_conflict( $arg, 'user', $language );
	} elsif (m!^\*FoomaticRIPDefault\s+(JCL|PJL|PXL|PCL|)([^/:\s]+):\s*(\"?)(.*?)\s*$!) {
	    D4("*FoomaticRIPDefault <lang><option>: <value>" );
	    # "*FoomaticRIPDefault <lang><option>: <value>"
	    # Used for numerical options only
	    my $language = fix_lang($1 or $foomatic_lang);
	    my $argname = $2;
	    my $default = $4;
	    $default = readmline( $4, $ppd,  \$i ) if( $3 );
	    $default =~ s/^\s+//;
	    $default =~ s/\s+$//;

	    D4("<lang> $language <option> $argname <default> $default" );

	    # Make sure that the argument is in the data structure
	    my $arg = checkarg ($dat, $argname, $language );
	    # Store the value foomatic default value 'default'
	    my $type = ($arg->{'type'} || '');
	    my $bool = $type eq 'bool';
	    if ($bool) {
		my $v = lc($default);
		if (($v eq 'true') || ($v eq 'on') || ($v eq 'yes') ||
		    ($v eq '1')) {
		    $default = 'True';
		} else {
		    $default = 'False';
		}
	    }
	    checksetting ($arg, $default)
		    if( $type !~ /^(int|float|string)$/);
	    $arg->{'default'} = $default;
	    check_option_type_conflict( $arg, 'foomatic', $language );
	} elsif (m!^\*(JCL|PJL|PXL|PCL|)([^\?/:\s]+)\s+([^:]+):\s*(\"?)(.*)$!) {
	    D4("*<lang><option> <choice>[/<translation>]: <code>" );
	    # "*<lang><option> <choice>[/<translation>]: <code>"
	    #*JCLResolution 300x300dpi/300 dpi: "@PJL"
	    my $language = fix_lang($1);
	    my $argname = $2;
	    my $setting = $3;
	    my $code = $5;
	    my $translation = '';
	    $code = readmline( $5, $ppd,  \$i ) if( $4 );
	    # Code string can have multiple lines, read all of them
	    if ($setting =~ m!^([^:/\s]+)/([^:]*)$!) {
		$setting = $1;
		$translation = $2;
	    }

	    D4("<lang> $language <option> $argname <choice> $setting <translation> $translation <lang> $language  <code> $code" );
	    # Make sure that the argument is in the data structure
	    my $arg = checkarg ($dat, $argname, $language);
	    # Make sure that the setting is in the data structure (enum options)
	    my $bool = ($arg->{'type'} || '') eq 'bool';
	    if ($bool) {
		my $v = lc($setting);
		if (($v eq 'true') || ($v eq 'on') || ($v eq 'yes') ||
		    ($v eq '1')) {
		    $setting = 'True';
		} else {
		    $setting = 'False';
		}
	    }
	    my $option = checksetting ($arg, $setting);
	    $option->{'comment'} = $translation if $translation;
	    # Make sure that this argument has a default setting, even
	    # if none is defined in this PPD file
	    D4("again <option> $argname <choice> $setting  <translation> $translation <lang> $language  <code> '$code'" );
	    # Store the value
	    if( $code =~ m!^%% FoomaticRIPOptionSetting! ){
		$option->{'setting'}{$language} = ($code);
	    } else {
		$option->{'driverval'}{$language} = ($code);
	    }
	    check_option_type_conflict( $arg, 'user', $language );
	} elsif (m!^\*\?(JCL|PJL|PXL|PCL|)([^/:\s]+):\s*(\"?)(.*)$!) {
	    D4("QUERY <lang><option>: <code>" );
	    my $language = fix_lang($1);
	    my $argname = $2;
	    my $code = $4;
	    $code = readmline( $4, $ppd,  \$i ) if( $3 );
	    D4("<option> $argname <code> $code");
	    my $arg = checkarg ($dat, $argname, $language);
	    $arg->{'query'} = $code;
	} elsif (m!^\*\% COMDATA \#(.*)$!) {
	    # If we have an old Foomatic 2.0.x PPD file, collect its Perl 
	    D4('COMDATA' );
	    # data
	    push (@datablob, $1);
	} elsif (m!^\*(End)\s*$!) {
	    # "*End"
	    D4("*End '$_'" );
	    D4("<end> $1" );
	} elsif (m!^\*(UIConstraints|NonUIConstraints)\s*:\s*(\"?)(.*)$!) {
	    D8("(UI|NonUI)Constraints: <code>" );
	    my $argname = $1;
	    my $code = $3;
	    $code = readmline( $3, $ppd,  \$i ) if( $2 );
	    D8("Constraint $argname <code> '$code'" );
	    my $quoted = 0;
	    D4("<name> $argname <quoted> $quoted <code> $code");
	    my $arg = $dat->{'info_byname'}{$argname};
	    if( not $arg ){
		push @{$dat->{'info'}}, $argname;
		$dat->{'info_bylcname'}{lc($argname)} = $argname;
		$arg->{'name'} = $argname;
	    }
	    $arg->{'quoted'} = $quoted;
	    push @{$arg->{'info_code'}}, $code;
	    $arg->{'info_last'} = $code;
	    $dat->{'info_byname'}{$argname} = $arg;

	    my( $option1, $value1, $option2, $value2 );
	    if( $code =~ m!^\s*\*([^\s\*]+)\s+([^\*\s]+)\s+\*([^\s\*]+)\s+([^\*\s]+)\s*$! ){
		    ($option1,$value1,$option2,$value2) = ($1, $2, $3, $4 );
		    if( $option2 lt $option1 ){
			my $o = $option1; $option1 = $option2; $option2 = $o;
			$o = $value1; $value1 = $value2; $value2 = $o;
		    }
	    } elsif( $code =~ m!^\s*\*([^\s\*]+)(\s+)\*([^\s\*]+)\s+([^\*\s]+)\s*$! ){
		    # normal order
		    ($option1,$value1,$option2,$value2) = ($1, '*', $3, $4 );
	    } elsif( $code =~ m!^\s*\*([^\s\*]+)\s+([^\*\s]+)\s+\*([^\s\*]+)(\s*)$! ){
		    # reverse order
		    ($option1,$value1,$option2,$value2) = ($1, $2, $3, '*' );
		    my $o = $option1; $option1 = $option2; $option2 = $o;
		    $o = $value1; $value1 = $value2; $value2 = $o;
	    } elsif( $code =~ m!^\s*\*([^\s\*]+)(\s+)\*([^\s\*]+)(\s*)$! ){
		    # sort order
		    ($option1,$value1,$option2,$value2) = ($1, '*', $3, '*' );
		    if( $option2 lt $option1 ){
			my $o = $option1; $option1 = $option2; $option2 = $o;
			$o = $value1; $value1 = $value2; $value2 = $o;
		    }
	    } else {
		    D0("Bad constraint format: $_");
		    next;
	    }
	    $option1 =~ s/^(JCL|PJL|PCL)//i;
	    $option2 =~ s/^(JCL|PJL|PCL)//i;
	    $dat->{constraint}{$option1}{$value1}{$option2}{$value2} = 1;
	} elsif (m!^\*([^\?/:\s]+):\s*(\"?)(.*)$!) {
	    D4("FOUND info <name>: <code>" );
	    my $argname = $1;
	    my $code = $3;
	    $code = readmline( $3, $ppd,  \$i ) if( $2 );
	    my $quoted = ($2 ne '')?1:0;
	    D4("<name> $argname <quoted> $quoted <code> $code");
	    my $arg = $dat->{'info_byname'}{$argname};
	    if( not $arg ){
		push @{$dat->{'info'}}, $argname;
		$dat->{'info_bylcname'}{lc($argname)} = $argname;
		$arg->{'name'} = $argname;
	    }
	    $arg->{'quoted'} = $quoted;
	    push @{$arg->{'info_code'}}, $code;
	    $arg->{'info_last'} = $code;
	    $dat->{'info_byname'}{$argname} = $arg;
	} else {
	D0("WARNING: Unrecognized line in PPD file:\n[".($i-2).'] '.($ppd->[$i-2])."\n[".($i-1).'] '.($ppd->[$i-1])."\n[".($i).'] '.($ppd->[$i]));
	}
    }

    D1("AFTER Parsing " . Dumper($dat) );

    # If we have an old Foomatic 2.0.x PPD file use its Perl data structure
    if ($#datablob >= 0) {
	D0("${added_lf}You are using an old Foomatic 2.0 PPD file, consider " .
	    "upgrading.${added_lf}\n");
	my $VAR1;
	if (eval join('',@datablob)) {
	    # Overtake default settings from the main structure of the
	    undef $dat;
	    $dat = $VAR1;
	    $dat->{'jcl'} = $dat->{'pjl'};
	    $isfoomatic = 1;
	} else {
	    # Perl structure broken
	    D0("${added_lf}Unable to evaluate datablob, print job may come " .
		"out incorrectly or not at all.${added_lf}\n");
	}
    }

    # Now get the various information values and options
    my( $arg, $code );
    if( $dat->{'info'} ){
	foreach my $name ( grep { m!^JCL(Begin|To\w*Interpreter|End)$!i } @{$dat->{'info'}}) {
	    # "*JCL(Begin|ToPSInterpreter|End): <code>"
	    D4("*JCL(Begin|To.*Interpreter|End): <code> - name $name" );
	    # The printer supports PJL/JCL when there is such a line 
	    $dat->{'jcl'} = $dat->{'pjl'} = 1;
	    $arg = infobyname( $dat, $name );
	    D4("*JCL(Begin|To.*Interpreter|End):" . Dumper($arg) );
	    $code = $arg->{'info_last'};
	    $name =~ m!JCL(Begin|To(\w*|)Interpreter|End)!i;
	    my $item = $1;
	    my $language = fix_lang($2);
	    D4("<lang> $language <item> $item <code> $code" );
	    # Store the value
	    # Code string can have multiple lines, read all of them
	    if ($item =~ /Begin/i) {
		$dat->{'pjl_support'}{'begin'} = $code;
	    } elsif ($item =~ /End/i) {
		$dat->{'pjl_support'}{'end'} = $code;
	    } elsif( $item =~ /To.*Interpreter/i ){
		$dat->{'pjl_support'}{'tointerpreter'}{$language} = $code;
	    }
	}
	if( ($arg = infobyname( $dat, 'NickName')) ){
	    $code = $arg->{'info_last'};
	    $dat->{'makemodel'} = unhtmlify($code);
	    # The following fields are only valid for Foomatic PPDs
	    # they will be deleted when it turns out that this PPD
	    # is not a Foomatic PPD.
	    if ($dat->{'makemodel'} =~ /^(\S+)\s+(\S.*)$/) {
		$dat->{'make'} = $1;
		$dat->{'model'} = $2;
		$dat->{'model'} =~ s/\s+Foomatic.*$//i;
		D4("<make> $dat->{'make'} <model> $dat->{'model'}");
	    }
	}
	if( ($arg = infobyname( $dat, 'FoomaticIDs')) ){
	    D4("*FoomaticIDs: <printer ID> <driver ID>");
	    $code = $arg->{'info_last'};
	    my ($id, $driver) = split(' ', $code );
	    # Store the values
	    D4("*FoomaticIDs: '$id' '$driver'");
	    $dat->{'id'} = $id;
	    $dat->{'driver'} = $driver;
	    $isfoomatic = 1;
	}
	if( ($arg = infobyname( $dat, 'FoomaticRIPPostPipe')) ){
	    # "*FoomaticRIPPostPipe: <code>"
	    D4("*FoomaticRIPPostPipe: <code>");
	    # Store the value
	    # Code string can have multiple lines, read all of them
	    $code = $arg->{'info_last'};
	    D4("<code> $code");
	    $dat->{'postpipe'} = unhtmlify($code);
	}
	if( ($arg = infobyname( $dat, 'FoomaticRIPCommandLine')) ){
	    # "*FoomaticRIPCommandLine: <code>"
	    D4("*FoomaticRIPCommandLine: <code>" );
	    # Code string can have multiple lines, read all of them
	    $code = $arg->{'info_last'};
	    D4("*FoomaticRIPCommandLine: $code" );
	    $dat->{'cmd'} = unhtmlify($code);
	}
    }
    if( ($arg = argbyname( $dat, 'FoomaticSystemOption' )) ){
	foreach my $valname (@{$arg->{'vals'}}){
	    my $val = valbyname( $arg, $valname );
	    D1("FIXING $arg->{'name'} - $valname " . Dumper($val));
	    my $key = lc "$valname";
	    my $value = $val->{'driverval'}{'ps'};
	    $dat->{foomatic_option}{$key} = $value;
	}
    }

    # Set the defaults for the numerical options, taking into account
    # the "*FoomaticRIPDefault<option>: <value>" if they apply
    # setnumericaldefaults($dat);
    # Let the default value of a boolean option be 
    # 'True' or 'False'.  if( 0 ) is dangerous when testing
    checkdefaultoptions( $dat );


    # Extract make and model fields
    if ( not $dat->{foomatic} and not $isfoomatic) {
	$dat->{'make'} = undef;
	$dat->{'model'} = undef;
    } else {
	$dat->{foomatic} = 1;
    }

    # Some clean-up for output
    generalentries($dat);
    checklongnames($dat);
    sortoptions($dat, 1);

    return $dat;
}

#  sub checkarg
#  
#  Create and initialize and argument record
#  
#  This creates three entries:
#     $dat->{'args_byname'}{$argname} = $rec;
#     $dat->{'args_bylcname'}{$lcname} = $argname;
#     push(@{$dat->{'args'}}, $argname);
#       - the last puts them in order in $dat->{args}->[]
  
sub checkarg( $ $ $ ) {
    # Check if there is already an argument record $argname in $dat, if not,
    # create one
    my ($dat, $argname, $language) = @_;
    my $lcname = lc( $argname ); 
    my $arg = $dat->{'args_byname'}{$argname};
    $language = fix_lang($language);
    D4("checkarg $argname language '$language' " . ($arg?'DONE':''));
    $arg->{'name'} = $argname;
    # Default execution style is 'G' (PostScript) since all arguments for
    # which we don't find "*Foomatic..." keywords are usual PostScript
    # options
    $arg->{'argstyle'}{$language} = $language;
    $arg->{'type'} = 'enum' if not defined $arg->{'type'};
    # Insert record in 'args' array for browsing all arguments
    if( not defined $dat->{'args_bylcname'}{$lcname} ){
	$dat->{'args_bylcname'}{$lcname} = $argname;
	push(@{$dat->{'args'}}, $argname);
    }
    $dat->{'args_byname'}{$argname} = $arg;
    return( $arg );
}

sub checksetting( $ $) {
    # Check if there is already an choice record $setting in the $argname
    # argument in $dat, if not, create one
    my ($arg, $setting) = @_;
    $arg->{'type'} = 'enum' if not defined $arg->{'type'};
    my $option = $arg->{'vals_byname'}{$setting};
    # the argument name
    # setting record
    $option->{'value'} = $setting;
    $option->{'argname'} = $arg->{'name'};
    # Insert record in 'vals' array for browsing all settings
    $arg->{'vals_byname'}{$setting} = $option;
    my $lcname = lc($setting);
    if( not $arg->{'vals_bylcname'}{$lcname} ){
	push(@{$arg->{'vals'}}, $setting);
	$arg->{'vals_bylcname'}{$lcname} = $setting;
    }
    return($option);
}



#  Numerical Options - sub setnumericaldefaults
#  
#  The Adobe PPD specs do not support numerical options.  The
#  usual way to handle this is to specify a list of enumerated
#  options,  each corresonding to a numerical value. 
#  
#  The "*FoomaticRIPDefault <option>: <value>" allows a numerical
#  value to be specified.  If there is no *Default<option> value
#  then the "*FoomaticRIPDefault<option>" will be used.
#  
#  This way a user can select a default value with a
#  tool which only supports Adobe PPD files.
#  This tool modifies the "*Default<option>: <value>" line.
#  If the *FoomaticRIPDefault<option> had
#  priority the user's change in *Default<option>
#  would have no no effect.

sub fix_numval( $ $ ; $ ){
    my($arg, $defvalue, $gen_choicelist ) = @_;
    D2("fix_numval: $arg->{name} starting value $defvalue");
    D2("   NUM value $defvalue, min $arg->{'min'} max $arg->{'max'}");
    $arg->{'min'} = 0 if not defined $arg->{'min'};
    $arg->{'max'} = 32767 if not defined $arg->{'max'};
    $defvalue = $arg->{'min'} if not defined $defvalue;
    $defvalue = $arg->{'max'} if not defined $defvalue;
    if ($defvalue < $arg->{'min'}) {
	D0("Option $arg->{'name'} value $defvalue less than minimum $arg->{min}");
	$defvalue = $arg->{'min'};
    }
    if ($defvalue > $arg->{'max'}) {
	D0("Option $arg->{'name'} value $defvalue greater than maximum $arg->{max}");
	$defvalue = $arg->{'max'};
    }
    my $mindiff = abs($arg->{'max'} - $arg->{'min'});
    my $closestvalue;
    D4("   MINDIFF $mindiff");
    if( (not defined $arg->{'vals'})
	and (not defined $arg->{'choicelist'})
	and $gen_choicelist ){
	D4("   GEN CHOICELIST");
	if( $arg->{'type'} eq 'int' ){
	    # Real numerical options do not exist in the Adobe
	    # specification for PPD files. So we map the numerical
	    # options to enumerated options offering the minimum, the
	    # maximum, the default, and some values inbetween to the
	    # user.
	    my $min = $arg->{'min'};
	    my $max = $arg->{'max'};
	    my $second = $min + 1;
	    my $stepsize = 1;
	    if (($max - $min > 100) && ($arg->{'name'} ne 'Copies')) {
		# We don't want to have more than 100 values, but when the
		# difference between min and max is more than 100 we should
		# have at least 10 steps.
		my $mindesiredvalues = 10;
		my $maxdesiredvalues = 100;
		# Find the order of magnitude of the value range
		my $rangesize = $max - $min;
		my $log10 = log(10.0);
		my $rangeom = POSIX::floor(log($rangesize)/$log10);
		# Now find the step size
		my $trialstepsize = 10 ** $rangeom;
		my $numvalues = 0;
		while (($numvalues <= $mindesiredvalues) &&
		       ($trialstepsize > 2)) {
		    $trialstepsize /= 10;
		    $numvalues = $rangesize/$trialstepsize;
		}
		# Try to find a finer stepping
		$stepsize = $trialstepsize;
		$trialstepsize = $stepsize / 2;
		$numvalues = $rangesize/$trialstepsize;
		if ($numvalues <= $maxdesiredvalues) {
		    if ($stepsize > 20) { 
			$trialstepsize = $stepsize / 4;
			$numvalues = $rangesize/$trialstepsize;
		    }
		    if ($numvalues <= $maxdesiredvalues) {
			$trialstepsize = $stepsize / 5;
			$numvalues = $rangesize/$trialstepsize;
		    }
		    if ($numvalues <= $maxdesiredvalues) {
			$stepsize = $trialstepsize;
		    } else {
			$stepsize /= 2;
		    }
		}
		$numvalues = $rangesize/$stepsize;
		# We have the step size. Now we must find an appropriate
		# second value for the value list, so that it contains
		# the integer multiples of 10, 100, 1000, ...
		$second = $stepsize * POSIX::ceil($min / $stepsize);
		if ($second <= $min) {$second += $stepsize};
	    }
	    # Generate the choice list
	    my @choicelist;
	    push (@choicelist, $min);
	    my $item = $second;
	    while ($item < $max) {
		push (@choicelist, $item);
		$item += $stepsize;
	    }
	    push (@choicelist, $max);
	    $arg->{'choicelist'} = \@choicelist;
	}
	if( $arg->{'type'} eq 'float' ){
	    
	    # Real numerical options do not exist in the Adobe
	    # specification for PPD files. So we map the numerical
	    # options to enumerated options offering the minimum, the
	    # maximum, the default, and some values inbetween to the
	    # user.

	    my $min = $arg->{'min'};
	    my $max = $arg->{'max'};
	    # We don't want to have more than 500 values or less than 50
	    # values.
	    my $mindesiredvalues = 10;
	    my $maxdesiredvalues = 100;
	    # Find the order of magnitude of the value range
	    my $rangesize = $max - $min;
	    my $log10 = log(10.0);
	    my $rangeom = POSIX::floor(log($rangesize)/$log10);
	    # Now find the step size
	    my $trialstepsize = 10 ** $rangeom;
	    my $stepom = $rangeom; # Order of magnitude of stepsize,
	                           # needed for determining necessary number
	                           # of digits
	    my $numvalues = 0;
	    while ($numvalues <= $mindesiredvalues) {
		$trialstepsize /= 10;
		$stepom -= 1;
		$numvalues = $rangesize/$trialstepsize;
	    }
	    # Try to find a finer stepping
	    my $stepsize = $trialstepsize;
	    my $stepsizeorig = $stepsize;
	    $trialstepsize = $stepsizeorig / 2;
	    $numvalues = $rangesize/$trialstepsize;
	    if ($numvalues <= $maxdesiredvalues) {
		$stepsize = $trialstepsize;
		$trialstepsize = $stepsizeorig / 4;
		$numvalues = $rangesize/$trialstepsize;
		if ($numvalues <= $maxdesiredvalues) {
		    $stepsize = $trialstepsize;
		    $trialstepsize = $stepsizeorig / 5;
		    $numvalues = $rangesize/$trialstepsize;
		    if ($numvalues <= $maxdesiredvalues) {
			$stepsize = $trialstepsize;
		    }
		}
	    }
	    $numvalues = $rangesize/$stepsize;
	    if ($stepsize < $stepsizeorig * 0.9) {$stepom -= 1;}
	    # Determine number of digits after the decimal point for
	    # formatting the output values.
	    my $digits = 0;
	    if ($stepom < 0) {
		$digits = - $stepom;
	    }
	    # We have the step size. Now we must find an appropriate
	    # second value for the value list, so that it contains
	    # the integer multiples of 10, 100, 1000, ...
	    my $second = $stepsize * POSIX::ceil($min / $stepsize);
	    if ($second <= $min) {$second += $stepsize};
	    # Generate the choice list
	    my @choicelist;
	    my $choicestr =  sprintf("%.${digits}f", $min);
	    push (@choicelist, $choicestr);
	    my $item = $second;
	    my $i = 0;
	    my $last = '';
	    while ($item < $max){
		$choicestr =  sprintf("%.${digits}f", $item);
		# Prevent values from entering twice because of rounding
		# inacuracy
		if ($choicestr ne $last) {
		    push (@choicelist, $choicestr);
		}
		$last = $choicestr;
		$i += 1;
		$item = $second + $i * $stepsize;
	    }
	    $choicestr =  sprintf("%.${digits}f", $max);
	    # Prevent values from entering twice because of rounding
	    # inacuracy
	    if ($choicestr ne $last) {
		push (@choicelist, $choicestr);
	    }
	    $arg->{'choicelist'} = \@choicelist;
	}
    }
    if( $arg->{'vals'} and @{$arg->{'vals'}} ) {
	D4("  USING VALS " . join(',',@{$arg->{'vals'}}) );
	my $val = valbyname($arg, "$defvalue");
	if( not defined $val ){
	    for my $valname (@{$arg->{'vals'}}) {
		$val = valbyname($arg, $valname);
		D4("  VAL " . Dumper($val) );
		last if "$valname" eq "$defvalue";
		my $value;
		foreach my $lang( %{$val->{'driverval'}} ){
		    D4("   VALNAME LANG '$lang'");
		    last if defined( $value = $val->{'driverval'}{$lang} );
		}
		my $diff = abs($defvalue - $value);
		D4("   VALNAME VALS '$valname' VAL '$value' DIFF $diff");
		if ($diff <= $mindiff) {
		    $mindiff = $diff;
		    $closestvalue = $value;
		    last if $diff == 0;
		}
	    }
	} else {
	    my $value;
	    foreach my $lang( %{$val->{'driverval'}} ){
		D4("   VALNAME LANG '$lang'");
		last if defined( $value = $val->{'driverval'}{$lang} );
	    }
	    $closestvalue = $value;
	}
    } elsif( $arg->{'choicelist'} and @{$arg->{'choicelist'}} ) {
	D4("  USING CHOICELIST");
	for my $value (@{$arg->{'choicelist'}}) {
	    my $diff = abs($defvalue - $value);
	    D4("   VALNAME CHOICE VAL $value DIFF $diff");
	    if ($diff <= $mindiff) {
		$mindiff = $diff;
		$closestvalue = $value;
		last if $diff == 0;
	    }
	}
    }
    $defvalue = $closestvalue if defined $closestvalue;
    D2("fix_numval: final value $defvalue");
    return( $defvalue );
}

sub setnumericaldefaults ( $ ) {
    my ($dat) = @_;
    for my $argname (@{$dat->{'args'}}) {
	my $arg = $dat->{'args_byname'}{$argname};
	# now we check the upper and lower boundaries
	if ($arg->{'type'} =~ /^(int|float)$/) {
	    $arg->{'default'} = fix_numval( $arg, $arg->{'default'} );
	}
    }
}


# Generate a translation/longname from a shortname
sub longname( $ ) {
    my( $shortname ) = @_;
    # A space before every upper-case letter in the middle preceeded by
    # a lower-case one
    $shortname =~ s/([a-z])([A-Z])/$1 $2/g;
    # If there are three or more upper-case letters, assume the last as
    # the beginning of the next word, the others as an abbreviation
    $shortname =~ s/([A-Z][A-Z]+)([A-Z][a-z])/$1 $2/g;
    return $shortname;
}


sub checklongnames( $ ) {

    my ($dat) = @_;

    # Add missing longnames/translations
    for my $argname (@{$dat->{'args'}}) {
	my $arg = $dat->{'args_byname'}{$argname};
	if (!($arg->{'comment'})) {
	    $arg->{'comment'} = longname($arg->{'name'});
	}
	for my $valname (@{$arg->{'vals'}}) {
	    my $val = $arg->{'vals_byname'}{$valname};
	    if (!($val->{'comment'})) {
		$val->{'comment'} = longname($val->{'value'});
	    }
	}
    }
}

sub generalentries( $ ) {

    my ($dat) = @_;

    $dat->{'timestamp'} = time();
    $dat->{'compiled-at'} = localtime( $dat->{'timestamp'} );

    my $user = `whoami`; chomp $user;
    my $host = `hostname`; chomp $host;

    $dat->{'compiled-by'} = "$user\@$host";

}

# Sort the options and (optionally) the 
#  values.  Note that this fails miserably when the
#  option names are numerical values

sub sortoptions( $ $ ) {

    my ($dat, $only_options) = @_;

    # The following stuff is very awkward to implement in C, so we do
    # it here.
    D8('SORTOPTIONS');

    # Sort options with 'sortargs' function
    my @sortedarglist = map { [ sortargs($dat, $_), $_] }  @{$dat->{'args'}};
    D8("SORTOPTIONS START " . Dumper( \@sortedarglist) );
	
    @sortedarglist = sort { $a->[0] cmp $b->[0] }  @sortedarglist;
    @sortedarglist = map { $_->[1] } @sortedarglist;
    @{$dat->{'args'}} = @sortedarglist;
    D8("SORTOPTIONS END" . Dumper( \@sortedarglist) );

    return if $only_options;

    # Sort values of enumerated options with 'sortvals' function
    for my $arg (@{$dat->{'args'}}) {
	next if $arg->{'type'} !~ /^(pickmany|enum|string|password)$/;
	@{$arg->{'vals'}} = sort sortvals keys(%{$arg->{'vals_byname'}});
    }
}

# Some helper functions for reading the PPD file


# Prepare strings for being part of an HTML document by, converting
# "<" to "&lt;", ">" to "&gt;", "&" to "&amp;", "\"" to "&quot;",
# and "'" to  "&apos;" and others
sub htmlify( $; $){
    my ($toencode,$newlinestoo) = @_;
    return undef unless defined($toencode);
    $toencode =~ s{&}{&amp;}gso;
    $toencode =~ s{<}{&lt;}gso;
    $toencode =~ s{>}{&gt;}gso;
    $toencode =~ s{"}{&quot;}gso;
    $toencode =~ s{\x8b}{&#139;}gso;
    $toencode =~ s{\x9b}{&#155;}gso;
    $toencode =~ s{([='])}{ sprintf('&#x%02x;',ord($1)); }gsoex;
    if (defined $newlinestoo && $newlinestoo) {
	$toencode =~ s{\012}{&#10;}gso;
	$toencode =~ s{\015}{&#13;}gso;
    }
    return $toencode;
}

sub unhtmlify($) {
    my ($string) = @_;
    return undef unless defined($string);
    # thanks to Randal Schwartz for the correct solution to this one
    $string=~ s[&(.*?);]{
	local $_ = $1;
	/^amp$/i	? "&" :
	/^quot$/i	? '"' :
        /^gt$/i		? ">" :
	/^lt$/i		? "<" :
	/^#(\d+)$/      ? chr($1) :
	/^#x([0-9a-f]+)$/i ? chr(hex($1)) :
	$_
	}gex;
    return $string;
}

sub unhexify( $ ) {
    # Replace hex notation for unprintable characters in PPD files
    # by the actual characters ex: "<0A>" --> chr(hex("0A"))
    my ($string) = @_;
    $string=~ s[<(.*?)>]{
	pack( "H*", $1 );
    }gex;
    return $string;
}

# This function sorts the options at first by their group membership and
# then by their names appearing in the list of functional areas. This way
# it will be made easier to build the PPD file with option groups and in
# user interfaces options will appear sorted by their functionality.
    my @standardopts = (
			# The most important composite option
			'printoutmode',
			# Options which appear in the 'General' group in 
			# CUPS and similar media handling options
			'pagesize',
			'papersize',
			'mediasize',
			'inputslot',
			'papersource',
			'mediasource',
			'sheetfeeder',
			'mediafeed',
			'paperfeed',
			'manualfeed',
			'manual',
			'outputtray',
			'outputslot',
			'outtray',
			'faceup',
			'facedown',
			'mediatype',
			'papertype',
			'mediaweight',
			'paperweight',
			'duplex',
			'sides',
			'binding',
			'tumble',
			'notumble',
			'media',
			'paper',
			# Other hardware options
			'inktype',
			'ink',
			# Page choice/ordering options
			'pageset',
			'pagerange',
			'pages',
			'nup',
			'numberup',
			# Printout quality, colour/bw
			'resolution',
			'gsresolution',
			'hwresolution',
			'jclresolution',
			'fastres',
			'jclfastres',
			'quality',
			'printquality',
			'printingquality',
			'printoutquality',
			'bitsperpixel',
			'econo',
			'jclecono',
			'tonersav',
			'photomode',
			'photo',
			'colormode',
			'colourmode',
			'color',
			'colour',
			'grayscale',
			'gray',
			'monochrome',
			'mono',
			'blackonly',
			'colormodel',
			'colourmodel',
			'processcolormodel',
			'processcolourmodel',
			'printcolors',
			'printcolours',
			'outputtype',
			'outputmode',
			'printingmode',
			'printoutmode',
			'printmode',
			'mode',
			'imagetype',
			'imagemode',
			'image',
			'dithering',
			'dither',
			'halftoning',
			'halftone',
			'floydsteinberg',
			'ret$',
			'cret$',
			'photoret$',
			'smooth',
			# Adjustments
			'gammacorrection',
			'gammacorr',
			'gammageneral',
			'mastergamma',
			'stpgamma',
			'gammablack',
			'gammacyan',
			'gammamagenta',
			'gammayellow',
			'gamma',
			'density',
			'stpdensity',
			'hpljdensity',
			'tonerdensity',
			'inkdensity',
			'brightness',
			'stpbrightness',
			'saturation',
			'stpsaturation',
			'hue',
			'stphue',
			'tint',
			'stptint',
			'contrast',
			'stpcontrast',
			'black',
			'stpblack',
			'cyan',
			'stpcyan',
			'magenta',
			'stpmagenta',
			'yellow',
			'stpyellow'
			);

    my @standardgroups = (
			  'general',
			  'media',
			  'quality',
			  'imag',
			  'color',
			  'output',
			  'finish',
			  'stapl',
			  'extra',
			  'install'
			  );

sub sortargs($ $) {
    my($dat, $argname) = @_;

    # All sorting done case-insensitive and characters which are not a
    # letter or number are taken out!!

    # List of typical option names to appear at first
    # The terms must fit to the beginning of the line, terms which must fit
    # exactly must have '\$' in the end.
    my ($compare, $i, $key, $arg );

    # Argument records
    $arg = argbyname( $dat, $argname );

    # Bring the two option names into a standard form to compare them
    # in a better way
    my $first = normalizename(lc($arg->{'name'}));
    $first =~ s/[\W_]//g;

    # group names
    my $firstgr = $arg->{'group'};
    my ($gfirst);
    if( $firstgr ){
	$firstgr = join(' ', @{$firstgr} );
	for( $i = 0; $i <@standardgroups; ++$i ){
	    last if( $firstgr =~ $standardgroups[$i] );
	}
	$gfirst = $i;
    } else {
	$firstgr = '_';
	$gfirst = @standardgroups;
    }

    $key = sprintf("%03d %-20s", $gfirst, $firstgr);

    # Check whether they argument names are in the @standardopts list
    my( $sfirst );
    for ($i = 0; $i < @standardopts; $i++) {
	last if( ($first =~ /^$standardopts[$i]/));
    }
    $sfirst = $i;
    $key .= sprintf(" %03d %-20s %-20s", $sfirst, $first, $arg->{'name'});
    return($key);
}

    my @standardvals = (
			# Default setting
			'default',
			'printerdefault',
			# 'Neutral' setting
			'None$',
			# Paper sizes
			'letter$',
			#'legal',
			'a4$',
			# Paper types
			'plain',
			# Printout Modes
			'draft$',
			'draft\.gray',
			'draft\.mono',
			'draft\.',
			'draft',
			'normal$',
			'normal\.gray',
			'normal\.mono',
			'normal\.',
			'normal',
			'high$',
			'high\.gray',
			'high\.mono',
			'high\.',
			'high',
			'veryhigh$',
			'veryhigh\.gray',
			'veryhigh\.mono',
			'veryhigh\.',
			'veryhigh',
			'photo$',
			'photo\.gray',
			'photo\.mono',
			'photo\.',
			'photo',
			# Trays
			'upper',
			'top',
			'middle',
			'mid',
			'lower',
			'bottom',
			'highcapacity',
			'multipurpose',
			'tray',
			);
sub sortvals() {

    # All sorting done case-insensitive and characters which are not a letter
    # or number are taken out!!

    # List of typical choice names to appear at first
    # The terms must fit to the beginning of the line, terms which must fit
    # exactly must have '\$' in the end.

    # Do not waste time if the input strings are equal
    if ($a eq $b) {return 0;}

    # Are the two strings numbers? Compare them numerically
    if (($a =~ /^-?[\d\.]+$/) && ($b =~ /^-?[\d\.]+$/)) {
	return( $a <=> $b );
    }

    # Bring the two option names into a standard form to compare them
    # in a better way
    my $first = lc($a);
    $first =~ s/[\W_]//g;
    my $second = lc($b);
    $second =~ s/[\W_]//g;

    # Check whether they are in the @standardvals list
    my ($firstinlist, $secondinlist);
    for (my $i = 0; not $firstinlist and $i <= @standardvals; $i++) {
	if( ($first =~ /^$standardvals[$i]/) ){
	    $firstinlist = $first;
	    last;
	}
    }
    for (my $i = 0; not $secondinlist and $i <= @standardvals; $i++) {
	if( ($second =~ /^$standardvals[$i]/) ){
	    $secondinlist = $second;
	    last;
	}
    }
    if (($firstinlist) && (!$secondinlist)) {return -1};
    if (($secondinlist) && (!$firstinlist)) {return 1};
    if (($firstinlist) && ($secondinlist)) {return $first cmp $second};
	
    # Neither of the search terms in the list, compare the standard-formed 
    # strings
    my $compare = ( normalizename($first) cmp normalizename($second) );
    if ($compare != 0) {return $compare};

    # No other criteria fullfilled, compare the original input strings
    return $a cmp $b;
}


# Find an argument by name in a case-insensitive way
sub argbyname($ $) {
    my ($dat, $argname) = @_;
    if( defined( $argname ) ){
	$argname = $dat->{args_bylcname}{lc($argname)};
	$argname = $dat->{args_byname}{$argname} if defined $argname;
    }
    return $argname;
}


# Find a choice value hash by name.
# check the lowercase information and return the value hash
#
sub valbyname( $ $ ) {
    my ($arg,$valname) = @_;
    if( defined $arg and defined $valname ){
	$valname = $arg->{'vals_bylcname'}{lc($valname)};
	$valname = $arg->{'vals_byname'}{$valname} if defined $valname;
	return( $valname );
    }
    return undef;
}


# Find an argument by name in a case-insensitive way
sub infobyname($ $) {
    my ($dat, $argname) = @_;
    if( defined( $argname ) ){
	$argname = $dat->{info_bylcname}{lc($argname)};
	$argname = $dat->{info_byname}{$argname} if defined $argname;
    }
    return $argname;
}

# replace numbers with fixed 6-digit number for ease of sorting
# ie: sort { normalizename($a) cmp normalizename($b) } @foo;
sub normalizename ( $ ) {
    my( $n ) = @_;

    $n =~ s/[\d\.]+/sprintf("%013.6f", $&)/eg;
    return $n;
}


# flag an option to be a user option, a foomatic option, or both
# Note that the user option has precedence

sub check_option_type_conflict( $ $ $){
    my ($arg, $value, $language ) = @_;
    if( $value ne "user" and $value ne "foomatic" ){
	rip_die("LOGIC ERROR: option type can be 'user' or 'foomatic', not $value",
	    $EXIT_PRNERR_NORETRY_BAD_SETTINGS);
    }
    $arg->{'option_type'}{$value}{$language} = 1;
}

sub checkoptionvalue( $ $ $ ) {

    ## This function checks whether a given value is valid for a given
    ## option. If yes, it returns a cleaned value (e. g. always True
    ## False for boolean options), otherwise "undef". If $forcevalue is set,
    ## we always determine a corrected value to insert (we never return
    ## "undef") unless the option does not have any possible values

    # Is $value valid for the option named $argname?
    my ($arg, $value, $forcevalue) = @_;

    # we deal the things that do not have option values
    # these are apparently information only

    my $argname = $arg->{'name'};
    my $type = ($arg->{'type'} or 'enum');
    # try to short circuit the testing
    #D1("checkoptionvalue: argname '$argname' type '$type' value '" . ((defined $value)?$value:"UNDEF")."'");
    if ($type eq 'bool' or $type eq 'enum' or $type eq 'pickmany') {
	if( (not defined $arg->{'vals'}) or (not @{$arg->{'vals'}}) ){
	    return( $value );
	}
	my $v = valbyname( $arg, $value );
	if (defined($v)) {
	    return $v->{'value'};
	}
    }
    if ($type eq 'bool') {
	my $v = lc($value);
	if (($v eq 'true') || ($v eq 'on') || ($v eq 'yes') ||
	    ($v eq '1')) {
	    return 'True';
	} elsif (($v eq 'false') ||
		 ($v eq 'off') ||
		 ($v eq 'no') ||
		 ($v eq '0')) {
	    return 'False';
	} elsif ($forcevalue) {
	    # This maps Unknown to mean False.  Good?  Bad?
	    # It was done so in Foomatic 2.0.x, too.
	    D0("boolean option $arg->{name} bad value '$value'");
	    return 'False';
	}
    } elsif ($type eq 'enum') {
	if((($arg->{'name'} eq "PageSize") ||
		  ($arg->{'name'} eq "PageRegion")) &&
		 (defined($arg->{'vals_byname'}{'Custom'})) &&
		 ($value =~ m!^Custom\.([\d\.]+)x([\d\.]+)([A-Za-z]*)$!i)) {
	    # Custom paper size
	    return $value;
	} elsif ($forcevalue) {
	    # wtf!?  that's not a choice!
	    # Return the first entry of the list
	    D0("enum option $arg->{name} bad selection '$value'");
	    if( not defined $arg->{'vals'} or not @{$arg->{'vals'}} ){
		return( undef );
	    }
	    return $arg->{'vals'}[0];
	}
    } elsif ($type eq 'pickmany') {
	return fix_pickmany( $arg, $value );
    } elsif (($type eq 'int') ||
	     ($type eq 'float')) {
	return( fix_numval( $arg, $value ) );
    } elsif (($type eq 'string') || ($type eq 'password')) {
	if (defined($arg->{'vals_byname'}{$value})) {
	    return $value;
	}
	if (not stringvalid($arg, $value)) {
	    D0("String option $arg->{name} bad value '$value'");
	    $value = 'None';
	}
	if($forcevalue and not defined($arg->{'vals_byname'}{$value})) {
	    my $key = $value;
	    my $defcom = $value;
	    # we create an enumerated option for the string
	    $key =~ s/\W+/_/g;
	    $key =~ s/^_+|_+$//g;
	    $key = '_' if ($key eq '');
	    $defcom =~ s/:/ /g;
	    $defcom =~ s/^ +| +$//g;
	    my $option = checksetting($arg, $key);
	    $option->{'comment'} = $defcom;
	    $option->{'driverval'}{'ps'} = $value;
	    # Bring the default entry to the first position
	    my $index = 0;
	    if( $arg->{'vals'} and @{$arg->{'vals'}} ){
		@{$arg->{'vals'}} = grep { $_ ne $key } @{$arg->{'vals'}};
	    }
	    unshift(@{$arg->{vals}}, $key);
	}
	return( $value );
    }
    return undef;
}

sub stringvalid( $ $ ) {

    ## Checks whether a user-supplied value for a string option is valid
    ## It must be within the length limit, should only contain allowed
    ## characters and match the given regexp

    # Option and string
    my ($arg, $value) = @_;
    my( $len, $chars, $c );

    # Maximum length
    return 0 if not defined $value;
    if(defined($len = $arg->{'maxlength'}) && (length($value) > $len)){
	D0("String option $arg->{name} value '$value' longer than $len");
	return(0);
    }

    # Allowed characters
    $c = $chars = $arg->{'allowedchars'};
    if($chars ){
	$chars =~ s/(?<!\\)((\\\\)*)\//$2\\\//g;
	if( $value !~ m/^[$chars]*$/ ){
	    D0("String option $arg->{name} value '$value' has character not in '$c' = '$chars'");
	    return(0);
	}
    }

    # Regular expression
    $c = $chars = $arg->{'allowedregexp'};
    if ($chars ){
	$chars =~ s/(?<!\\)((\\\\)*)\//$2\\\//g;
	if( $value !~ m/$chars/ ){
	    D0("String option $arg->{name} value '$value' does not match Regular Expression '$c' = '$chars'");
	    return(0);
	}
    }
    # All checks passed
    return 1;
}

sub checkdefaultoptions( $ ) {

    ## Let the value of a boolean option be 0 or 1 instead of
    ## "True" or "False", range-check the defaults of all options and
    ## issue warnings if the values are not valid

    # Option set to be examined
    # optionset can be default
    my ($dat) = @_;

    for my $argname (@{$dat->{'args'}}) {
	my $arg = $dat->{'args_byname'}{$argname};
	if( $arg->{'option'} ){
	    $arg->{'default'} = checkoptionvalue($arg, $arg->{'default'}, 1);
	}
    }

    # If the settings for "PageSize" and "PageRegion" are different,
    # set the one for "PageRegion" to the one for "PageSize"
    if( exists $dat->{'args_byname'}{'PageSize'}
	and exists $dat->{'args_byname'}{'PageSize'} ){
	my $psize = $dat->{'args_byname'}{'PageSize'};
	my $pregion = $dat->{'args_byname'}{'PageRegion'};
	$psize->{'default'} = $pregion->{'default'} if not defined $psize->{'default'};
	$pregion->{'default'} = $psize->{'default'} if not defined $pregion->{'default'};
	$pregion->{'default'} = $psize->{'default'}
	    if( $pregion->{'default'} ne $psize->{'default'} );
    }
}

# If the PageSize or PageRegion was changed, also change the other

sub syncpagesize( $ $ $ $ ) {
    
    # Name and value of the option we set, and the option set where we
    # did the change
    my ($dat, $name, $value, $optionset ) = @_;

    # Don't do anything if we were called with an option other than
    # "PageSize" or "PageRegion"
    return 0 if (($name ne "PageSize") && ($name ne "PageRegion"));
    
    # Don't do anything if not both "PageSize" and "PageRegion" exist
    return 0 if ((!defined($dat->{'args_byname'}{'PageSize'})) ||
	       (!defined($dat->{'args_byname'}{'PageRegion'})));
    
    my $dest;
    
    # "PageSize" --> "PageRegion"
    # "PageRegion" --> "PageSize"
    if ($name eq "PageSize") {
	$dest = "PageRegion";
    }
    if ($name eq "PageRegion") {
	$dest = "PageSize";
    }
    my $arg = argbyname( $dat, $dest );
    
    # Do it!
    my $val = checkoptionvalue($arg, $value, 0);
    if( defined $val ){
	$arg->{$optionset} = $val;
    }
    return 1;
}


sub cutguiname( $ $ ) {
    
    # If $shortgui is set and $str is longer than 39 characters, return the
    # first 39 characters of $str, otherwise the complete $str. 

    my ($str, $shortgui) = @_;

    if (($shortgui) && (length($str) > 39)) {
	return substr($str, 0, 39);
    } else {
	return $str;
    }
}

sub readmline( $ $ $ ) {
    my( $line, $ppd, $ptr_i ) = @_;
    #D1("UNDEF") if( not defined $line );
    #D1("readmline: start '$line'");
    while ($line !~ m!\"!) {
	if ($line =~ m!&&$!) {
	    # line continues in next line
	    $line = substr($line, 0, -2);
	    #D1("readmline: fixed '$line'");
	    # Read next line
	} else {
	    $line .= "\n";
	}
	${$ptr_i} ++;
	$line .= $ppd->[${$ptr_i}];
	#D1("readmline: appended '$line'");
    }
    $line =~ m!^([^\"]*)\"!s;
    #D1("readmline: FINAL '$1'");
    return( $1 );
}

sub findalias( $ $ ){
	my( $dat, $name ) = @_;
	my $count = 10;
	my $lower = lc($name);
	$name = undef;
	while( defined $dat->{aliases}{$lower} ){
	    $name = $dat->{aliases}{$lower};
	    $lower = lc($name);
	    if( ! --$count ){
		rip_die("aliases loop for $name",
		    $EXIT_PRNERR_NORETRY_BAD_SETTINGS);
	    }
	}
	return $name;
}

# 
# $option = fixoptionvalue( $arg, $language, $optionset );
# 
# Determine the output string corresponding to the argument,
# for the specified language.  Use the 'optionset'
# value to make decisions.
#
#  Return value:
#    for all options but 'pickmany' type,   a string
#    is returned.  For the 'pickmany' type, a reference
#    to an array with [ 'option', 'value' ] is returned
# 

sub fixoptionvalue( $ $ $ ){

    my( $arg, $language, $optionset ) = @_;

    my $name = $arg->{'name'};
    my $type = ($arg->{'type'} or 'enum');
    my $section = $arg->{'section'}{$language};
    my $cmdvar = '';

    my $sprintfcmd = ($arg->{'proto'}{$language} or "%s");
    $sprintfcmd =~ s/\%(?!s)/\%\%/g;

    # the option name
    # the real option name
    my $lookupval = $arg->{$optionset};

	
    $lookupval = '' if not defined $lookupval and $type eq 'pickmany';
    if( not defined $lookupval ){
	D10( " fixoptionvalue - name $name 'no lookupval' optionset '$optionset' value");
	return $cmdvar;
    }
    D10( " fixoptionvalue - name $name HASVALUE optionset '$optionset' value '$lookupval'");
    my $userval = checkoptionvalue($arg, $lookupval, 0);
    if( not defined $userval ){
	D10( " fixoptionvalue - invalid $name value '$lookupval'");
	return $cmdvar;
    }
    D10( "   Doing '$name', value '$userval', type '$type', proto '$sprintfcmd'");

    # Build the command line snippet/PostScript/JCL code for the current
    # option

    if ($type eq 'int' or $type eq 'float') {
	# If defined, process the proto and stick the result into
	# the command line or postscript queue.
	$cmdvar = sprintf($sprintfcmd,
			  ($type eq 'int' 
			   ? sprintf("%d", $userval)
			   : sprintf("%f", $userval)));

    } elsif ($type eq 'pickmany' ) {
	my $newval = fix_pickmany( $arg, $userval );
	D10( " XXX  pickmany name '$name' value '$userval', newval $newval");
	foreach my $optname (split(',',$newval)){
	    my( $no, $name ) = $optname =~ m/^(no|)(.*)$/;
	    if( not $no ){
		my $opt = $arg->{'vals_byname'}{$name};
		my $val = $opt->{'driverval'}{$language};
		$cmdvar .= $val;
		D10(" XXX pickmany used optname '$optname' val '$val'");
	    }
	}
	return($cmdvar, $newval );
    } elsif ($type eq 'enum' or $type eq 'bool' ) {
	my $v = valbyname( $arg, $userval );
	if( not defined($v) and
	    $userval =~ /^Custom\.([\d\.]+)x([\d\.]+)([A-Za-z]*)$/) {
	    $v = valbyname( $arg, 'Custom' );
	}
	if( not $v ){
	    D0("LOGIC ERROR: bad value $name='$userval'");
	    return $cmdvar;
	}

	# If defined, stick the selected value into the proto and
	# thence into the commandline
	my $val = $v->{'driverval'}{$language};
	if( not defined $val ){
	    D10( "   No '$language' value for $name='$userval'");
	    return $cmdvar;
	}
	D10( "    Using '$language' value '$val'");
	$cmdvar = sprintf($sprintfcmd, $val );
	# Custom paper size
	if ($userval =~ /^Custom\.([\d\.]+)x([\d\.]+)([A-Za-z]*)$/) {
	    my $width = $1;
	    my $height = $2;
	    my $unit = $3;
	    # convert width and height to PostScript points
	    if (lc($unit) eq "in") {
		$width *= 72.0;
		$height *= 72.0;
	    } elsif (lc($unit) eq "cm") {
		$width *= (72.0/2.54);
		$height *= (72.0/2.54);
	    } elsif (lc($unit) eq "mm") {
		$width *= (72.0/25.4);
		$height *= (72.0/25.4);
	    }
	    # Round width and height
	    $width =~ s/\.[0-4].*$// or
		$width =~ s/\.[5-9].*$// and $width += 1;
	    $height =~ s/\.[0-4].*$// or
		$height =~ s/\.[5-9].*$// and $height += 1;
	    # Insert width and height into the prototype
	    if ($cmdvar =~ /^\s*pop\W/s) {
		# Custom page size for PostScript printers
		$cmdvar = "$width $height 0 0 0\n$cmdvar";
	    } else {
		# Custom page size for Foomatic/GIMP-Print
		$cmdvar =~ s/\%0/$width/ or
		    $cmdvar =~ s/(\W)0(\W)/$1$width$2/ or
		    $cmdvar =~ s/^0(\W)/$width$1/m or
		    $cmdvar =~ s/(\W)0$/$1$width/m or
		    $cmdvar =~ s/^0$/$width/m;
		$cmdvar =~ s/\%1/$height/ or
		    $cmdvar =~ s/(\W)0(\W)/$1$height$2/ or
		    $cmdvar =~ s/^0(\W)/$height$1/m or
		    $cmdvar =~ s/(\W)0$/$1$height/m or
		    $cmdvar =~ s/^0$/$height/m;
	    }
	}
    } elsif (($type eq 'string') || ($type eq 'password')) {
	# Stick the entered value into the proto and
	# thence into the commandline
	if(not defined($userval)) {
	    D10( "No string value for option $name=");
	    return undef;
	} else {
	    $cmdvar = sprintf($sprintfcmd, $userval);
	}
    } else {
	# Ignore unknown option types silently
	D10( "Not handling $name, type '$type'");
    }
    if( $language eq 'pjl' ){
	my @list = split( "\n", $cmdvar );
	@list = map {
		s/^\s+//; s/\s+$//;
		if( /^$/ ){ $_ = undef; }
		elsif( not /^\@PJL\s+/ ){ $_ = '@PJL ' . $_; }
		$_ .= "\n" if( defined $_ );
		} @list;
	$cmdvar = join('',@list);
    }
    D10( "    cmdvar '$cmdvar'");
    return $cmdvar;
}

sub fix_lang( $ ){
    my ($language) = @_;
    $language = 'ps' if not $language;
    $language = lc($language);
    $language = 'pjl' if $language eq 'jcl';
    return $language;
}

# fix pickmany option
#  We start with the current option list,  and then
#  we update it .  We return the updated option value
#

sub fix_pickmany( $ @ ){
    my( $arg, @values ) = @_;

    my $newvalue = '';
    my %used;
    my $err = 0;
    @values = grep { defined $_ } (@values);
    @values = split(/[,;:]+/, join(',',(@values)) ) if @values;
    D40("fix_pickmany: $arg->{'name'} values @values");
    foreach my $opt (@values){
	my( $no, $name ) = $opt =~ m/^(no|)(.*)$/;
	next if not $name;
	my $value = valbyname( $arg, $name );
	if( not $value ){
	    D0("pickmany option $arg->{name} bad selection '$opt'");
	    $err = 1;
	}
	$used{$value->{'value'}} = not $no;
    }
    foreach my $val_name (@{$arg->{'vals'}} ){
	if( $used{$val_name} ){
	    $newvalue  .= ",$val_name";
	} else {
	    $newvalue  .= ",no$val_name";
	}
	D40("fix_pickmany: $arg->{'name'} newvalue $newvalue");
    }
    D40("fix_pickmany: before $arg->{'name'} newvalue $newvalue");
    $newvalue =~ s/^,//;
    $newvalue = undef if $err;
    D40("fix_pickmany: final $arg->{'name'} newvalue $newvalue");
    return( $newvalue );
}

=head1 Generating a PPD File

The ppdfromperl() function will regenerate a PPD from the parsed
information.  The PPD file that is produced will be suitable
for use by various front ends.  That means that Foomatic specific
information must be translated into formats compatible with
the other front ends.
 
The following actions must be done:

=over 2

=item Entry Fixup

For the string, int, and float type of options,  a set of
enumerated choices will be generated.

 ppdfromperl( $dat, $shortgui, $update )

   $dat = data information
   $shortgui = long or short translation strings
   $update = format

If $shortgui is set, all GUI strings ("translations" in PPD files) will
be cut to a maximum length of 39 characters. This is needed by the
current (as of July 2003) version of the CUPS PostScript driver for
Windows.

If $update is set, then the PPD file will be updated with
defaults or options set by command line options.

=back

=cut

# Return a generic Adobe-compliant PPD for the "foomatic-rip" filter script
# for all spoolers.

sub ppdfromperl( $ $ $ ) {

    my ($dat, $shortgui, $update) = @_;
    # $dat is the Perl data structure of the current printer/driver combo.

    # If $shortgui is set, all GUI strings ("translations" in PPD files) will
    # be cut to a maximum length of 39 characters. This is needed by the
    # current (as of July 2003) version of the CUPS PostScript driver for
    # Windows.

    # fix the page sizes
    fix_pagesize( $dat );
    D10("Page Sizes Fixed: " . Dumper($dat) );

    fix_ppd_info( $dat, $update );

    my @headerblob; # Lines for header
    my @optionblob; # Lines for command line and options in the PPD file

    # Insert the printer/driver IDs and the command line prototype
    # right before the option descriptions
    my @printer_messages =  qw( PrinterError Status Source Message
	UIConstraints NonUIConstraints Comment );
    my @required =  (
	[ 'FileVersion',  '1.0', 1 ],
	[ 'FormatVersion',  '4.3', 1 ],
	[ 'LanguageEncoding',  'ISOLatin1', 0 ], 
	[ 'LanguageVersion',  'English', 1 ],
	[ 'Manufacturer',  'Foomatic', 1 ],
	[ 'ModelName',  '', 1 ],
	[ 'NickName',  '', 1 ],
	[ 'PCFileName',  '', 1 ],
	[ 'Product',  '', 1 ],
	[ 'PSVersion',  '', 1, 1, 1 ],
	[ 'ShortNickName',  '', 1 ],
    );

    my $filename = ($dat->{'filename'} or "UNKNOWN");
    my $out = '';

    push(@headerblob, <<EOF );
*PPD-Adobe: "4.3"
*% Foomatic Generated PPD File
*%
*% This file is published under the GNU General Public License
*%
*% Foomatic (3.0.0 or newer) generated this PPD file. It should be suitable
*% for use with all programs and environments which use PPD files for
*% printer capability information as well as "foomatic-rip" backend filter
*% script.  For information on using this, and to obtain "foomatic-rip"
*% consult http://www.linuxprinting.org/
*%
*% If downloading this file using a Web Browser, DO NOT cut and paste
*% this file from the Web Browser into an editor with your mouse. This can
*% introduce additional line breaks which lead to unexpected results.
*%
*% Instead, use the appropriate command to save the document as a file.
*% It is recommended that you save this file as '$filename'
*%
EOF
    my $used = { 'PPD-Adobe' => 1};

    push(@headerblob, "*%============ COMMENTS ==================\n\n");
    foreach my $name ('Comment'){
	my $arg = infobyname( $dat, $name );
	next if not $arg;
	next if $used->{$name};
	next if not $arg->{'info_code'};
	# remove foomatic comments
	for ( my $i = 0; $i < 50 and $i < @{$arg->{'info_code'}}; ++$i ){
		my $line = $arg->{'info_code'}->[$i];
		if( $line =~ /save this file as/ ){
		    splice @{$arg->{'info_code'}}, 0, $i+1;
		    last;
		}
	}
	for ( my $i = 0; $i < 50 and $i < @{$arg->{'info_code'}}; ++$i ){
		my $line = $arg->{'info_code'}->[$i];
		if( $line =~ /=== COMMENTS ====/ ){
		    splice @{$arg->{'info_code'}}, 0, $i;
		    last;
		}
	}
	
	for ( my $i = 0; $i < 25 and $i < @{$arg->{'info_code'}}; ++$i ){
	    push(@headerblob, $arg->{'info_code'}->[$i] . "\n");
	}
	$used->{$name} = 1;
    }
    push(@headerblob, "\n*%============== REQUIRED =================\n\n");

    foreach my $entry (@required){
	my $name = $entry->[0];
	my $default = $entry->[1];
	my $quoted = $entry->[2];
	my $use_list = $entry->[3];
	my $arg = infobyname( $dat, $name );
	my $header = "*$name";
	my $code = $arg->{'info_last'};
	if( not defined $code ){
	    if( $default ){
		D0("ppdfromperl: missing required value for '$name', using default '$default'");
		$code = $default;
		$use_list = 0;
	    } else {
		D0("ppdfromperl: missing required value for '$name'");
		next;
	    }
	}
	if( $use_list ){
	    foreach $code (@{$arg->{'info_code'}} ){
		if( $quoted ){
		    push(@headerblob, "$header: \"$code\"\n");
		} else {
		    push(@headerblob, "$header: $code\n");
		}
	    }
	} else {
	    if( $quoted ){
		push(@headerblob, "$header: \"$code\"\n");
	    } else {
		push(@headerblob, "$header: $code\n");
	    }
	}
	$used->{$name} = 1;
    }

    push(@optionblob, "\n*%============== INFO =================\n\n");

    my @plist = (sort @{$dat->{'info'}});
    D10("ppd_from_perl: INFO PLIST '@plist'");
    foreach my $name (@plist){
	next if $used->{$name};
	my $arg = infobyname( $dat, $name );
	my $quoted = $arg->{'quoted'};
	my $header = "*$name";
	my $code = $arg->{'info_last'};
	D10("ppd_from_perl: info $name, code $code");
	next if grep { $name =~ /$_/i } @printer_messages;
	my $cmdlinestr;
	if( not $quoted ){
	    $cmdlinestr = "$header: $code";
	} else {
	    if( $name !~ /foomatic/ ){
		$cmdlinestr = "$header: \"$code\"";
	    } else {
		$cmdlinestr = ripdirective($header, $code);
	    }
	    if( $cmdlinestr =~ /\n/s ){
		$cmdlinestr =~ s/$\n//;
		$cmdlinestr .= "\n*End";
	    }
	}
	D10("ppd_from_perl: info $name, out '$cmdlinestr'");
	push(@optionblob, "$cmdlinestr\n");
	$used->{$name} = 1;
    }

    push(@optionblob, "\n*%============== OPTIONS =================\n\n");

    # sort the arguments by the Group and Subgroup fields first
    my @sorted_args;
    if( $dat->{'args'} ){
	my $i = 0;
	foreach my $n (@{$dat->{'args'}}){
	    my $arg =argbyname($dat,$n);
	    my $open_ui = 0;
	    if( $arg->{'option'} ){
		$open_ui = keys %{$arg->{'option'}};
	    }
	    $open_ui = $open_ui?0:1;
	    my $g = '';
	    if( $arg->{'group'} ){
		$g = join( ' ', @{$arg->{'group'}} );
	    }
	    if( not $g ){
		$g = "_";
	    }
	    my $key = sprintf("%-40s $open_ui %05d ", $g, $i);
	    push @sorted_args, [ $key, $n];
	    ++$i;
	}
	D10("KEYS " . Dumper(@sorted_args));
	@sorted_args = sort 
		{ $a->[0] cmp $b->[0]; } @sorted_args;
	D10("sorted_args " . Dumper( \@sorted_args ) );
	@sorted_args = map { $_->[1] } @sorted_args;
    }
    D10("sorted_args FINAL @sorted_args" );

    my @groupstack; # In which group are we currently
    for my $name (@sorted_args){
	D10("OPTION '$name'");
	next if $used->{$name};
	my $arg = argbyname( $dat, $name);
	# you have to generate the following items
	# *OpenUI *PageSize/Media Size: PickOne
	# *OrderDependency: 10 AnySetup *PageSize
	# *DefaultPageSize: Letter
	# *PageSize Letter/Letter: "<<..."
	# *CloseUI: *PageSize

	$arg->{'default_out'} = 0;
	my @group;
	my $i;
	push @group, @{$arg->{'group'}} if $arg->{'group'};
	# now we check to see if we need to open or close a group
	for( $i = 0; $i < @group and $i < @groupstack; ++$i ){
	    my $newname = $group[$i];
	    my $oldname = $groupstack[$i];
	    last if $newname ne $oldname;
	}
	# now you close the groups
	my $closed;
	while( $i < @groupstack ){
	    my $close = pop @groupstack;
	    push @optionblob,
		 "*Close".(@groupstack?'Sub' : '')
			    .'Group: ' . $close . "\n";
	    $closed = 1;
	}
	push @optionblob, "\n" if $closed;
	for( ; $i < @group; ++$i ){
	    my $newname = $group[$i];
	    push @optionblob,
		 "*Open".(@groupstack?'Sub' : '')
			    .'Group: ' . $newname . "\n";
	    push @groupstack, $newname;
	}

	my $type =( $arg->{'type'} || ''); 
	my $fix_type = {
		enum =>'PickOne', pickmany=>'PickMany',
			bool =>'Boolean' };
	my $fix_lang = { ps =>'',
		pcl=>'PCL', pjl =>'JCL', pxl=>'PXL' };

	my $ty = $fix_type->{$type};
	if( $ty and my $v =$arg->{'option_type'}{'user'} ){
	    D10("ppd_from_perl: user options " . join(',', keys %{$v}) );
	    my %done;
	    foreach my $lang ( 'ps', sort keys %{$v} ){
		# *OpenUI *PageSize/Media Size: PickOne
		# *OrderDependency: 10 AnySetup *PageSize
		# *DefaultPageSize: Letter
		# *PageSize Letter/Letter: "<<..."
		# *CloseUI: *PageSize
		next if not $v->{$lang} or $done{$lang};
		my $prefix = $fix_lang->{$lang};
		if( not defined $prefix ){
		    rip_die ("ppd_from_perl: LOGIC ERROR! option '$name' has user information in language '$lang'", 
			$EXIT_PRNERR_NORETRY_BAD_SETTINGS );
		}
		fix_user_options ( $arg, $ty, $shortgui, $lang, $prefix,
		     \@optionblob );
		$done{$lang} = 1;
	    }
	}
	if( my $v =$arg->{'option_type'}{'foomatic'} ){
	    D10("ppd_from_perl: foomatic options " . join(',', keys %{$v}) );
	    my %done;
	    foreach my $lang ( 'ps', sort keys %{$v} ){
		# *FoomaticRIPOption Foo: enum ps A
		# *FoomaticRIPOptionAllowedChars Foostr: "\w\s"
		# *FoomaticRIPOptionAllowedRegExp Foostr: ".*"
		# *FoomaticRIPOptionMaxLength Foostr: 32
		# *FoomaticRIPOptionPrototype Fooint: "
		# *FoomaticRIPOptionRange Fooint: 0 100
		# *FoomaticRIPOptionSetting Foo=Value1: "
		next if not $v->{$lang} or $done{$lang};
		my $prefix = ($fix_lang->{$lang} || '' );
		fix_foomatic_options ( $arg, $type, $shortgui, $lang, $prefix,
		     \@optionblob );
		$done{$lang} = 1;
	    }
	}
	$used->{$name} = 1;
    }
    # now you close the groups
    while( @groupstack ){
	my $close = pop @groupstack;
	push @optionblob,
	     "*Close".(@groupstack?"Sub" : '')
			."Group: " . $close . "\n";
    }
    foreach my $name (@printer_messages){
	my $arg = infobyname( $dat, $name );
	next if not $arg;
	next if $used->{$name};
	next if not $arg->{'info_code'};

	my $quoted = $arg->{'quoted'};
	my $header = "*$name";
	foreach my $code (@{$arg->{'info_code'}} ){
	    if( $quoted ){
		push(@optionblob, "$header: \"$code\"\n");
	    } else {
		push(@optionblob, "$header: $code\n");
	    }
	}
	$used->{$name} = 1;
    }

    $out .= join('', (@headerblob, @optionblob) );
    $dat->{'out'} = $out;
    D10("HEADER\n$out");
}




# This splits RIP directives (PostScript comments which are
# foomatic-rip uses to build the RIP command line) into multiple lines
# of a fixed length, to avoid lines longer than 255 characters. The
# PPD specification does not allow such long lines.
sub ripdirective( $ $ ){
    my ($header, $content) = @_;
    $content = htmlify($content);
    # If possible, make lines of this length
    my $maxlength = 72;
    # Header of continuation line
    my $continueheader = '';
    # Two subsequent ampersands are not possible in an htmlified string,
    # so we can use them at the line end to mark that the current line
    # continues on the next line. A newline without this is also a newline
    # in the decoded string
    my $continuelineend = "&&";
    # output string
    my $out;
    # The colon and the quote after the header must be on the line with
    # the header
    $header .= ": \"";
    # How much of the current line is left?
    my $freelength = $maxlength - length($header) -
	length($continuelineend);
    # Add the header
    if ($freelength < 0) {
	# header longer than $maxlength, don't break it
	$out = "$header$continuelineend\n$continueheader";
	$freelength = $maxlength - length($continueheader) -
	    length($continuelineend);
    } else {
	$out = "$header";
    }
    $content .= "\"";
    # Go through every line of the $content
    for my $l (split ("\n", $content)) {
	while ($l) {
	    # Take off $maxlength portions until the string is used up
	    if (length($l) < $freelength) {
		$freelength = length($l);
	    }
	    my $line = substr($l, 0, $freelength, '');
	    # Add the portion 
	    $out .= $line;
	    # Finish the line
	    $freelength = $maxlength - length($continueheader) -
		length($continuelineend);
	    if ($l) {
		# Line continues in next line
		$out .= "$continuelineend\n$continueheader";
	    } else {
		# line ends
		$out .= "\n";
		last;
	    }
	}
    }
    # Remove trailing newline
    $out =~ s/\n$//;
    return $out;
}

sub fix_pagesize( $ ){
    my( $dat ) = @_;

    # Generate the PageRegion, ImageableArea, and PaperDimension
    # If you have a page size argument and no corresponding
    # PageRegion, ImageableArea, and PaperDimension clauses 
    # then add them.
    my $maxpagewidth = 100000;
    my $maxpageheight = 100000;

    my $pagesize = argbyname( $dat, 'PageSize' );
    my $setup = '';

    # if there is not PageSize, then
    # create a dummy PageSize.  This brutally assumes that
    # the PageRegion will be missing

    if( not $pagesize or not $pagesize->{'vals'} or
	not @{$pagesize->{'vals'}} ){
	my @lines = split( "\n", <<EOF );
*OpenUI *PageSize/Media Size: PickOne
*OrderDependency: 10 AnySetup *PageSize
*DefaultPageSize: Letter
*PageSize Letter/Letter: "<</PageSize[612 792]/ImagingBBox null>>setpagedevice"
*PageSize Legal/Legal: "<</PageSize[612 1008]/ImagingBBox null>>setpagedevice"
*PageSize A4/A4: "<</PageSize[595 842]/ImagingBBox null>>setpagedevice"
*CloseUI: *PageSize
EOF
	ppdfromvartoperl( $dat, \@lines );
	$pagesize = argbyname( $dat, 'PageSize' );
    }

    my $pageregion = argbyname( $dat, 'PageRegion' );
    if( not $pageregion or not $pageregion->{'vals'} or
	not @{$pageregion->{'vals'}} ){
	my @lines = split( "\n", <<EOF );
*OpenUI *PageRegion/Media Size: PickOne
*OrderDependency: 10 AnySetup *PageRegion
*CloseUI: *PageRegion
EOF
	ppdfromvartoperl( $dat, \@lines );
	$pagesize = argbyname( $dat, 'PageSize' );
    }

    # Generate the PageRegion, ImageableArea, and PaperDimension
    # values from the PageSize if they are not present

    my $hascustompagesize = 0;

    for my $value (@{$pagesize->{'vals'}}) {
	my $option = valbyname( $pagesize, $value );
	my $comment = ($option->{'comment'} || $value);

	if( $value eq 'Custom'){
	    D10("FOUND Custom");
	    $hascustompagesize = 1;
	    next;
	}
	# Here we have to fill in the absolute sizes of the 
	# papers. We consult a table when we could not read
	# the sizes out of the choices of the "PageSize"
	# option.
	my $size = $option->{'driverval'}{'ps'};
	if( not $size ){
	    $size = getpapersize($value);
	} else {
	    if (($size !~ /(\d+)\s+(\d+)/) &&
		# 2 positive integers separated by whitespace
		($size !~ /\-dDEVICEWIDTHPOINTS\=(\d+)\s+\-dDEVICEHEIGHTPOINTS\=(\d+)/)) {
		# "-dDEVICEWIDTHPOINTS=..."/"-dDEVICEHEIGHTPOINTS=..."
		$size = getpapersize($value);
	    } else {
		$size = "$1 $2";
	    }
	}
	$size =~ /^\s*(\d+)\s+(\d+)\s*$/;
	my $width = ($1 || 0);
	my $height = ($2 || 0);
	$maxpagewidth = $width if ($maxpagewidth < $width);
	$maxpageheight = $height if ($maxpageheight < $height);
	if (($value eq "Custom") || ($width == 0) || ($height == 0)) {
	    # This page size is either named "Custom" or
	    # at least one of its dimensions is not fixed
	    # (=0), so this printer/driver combo must
	    # support custom page sizes
	    $hascustompagesize = 1;
	    # We do not add this size to the PPD file
	    # because the Adobe standard foresees a
	    # special code block in the header of the
	    # PPD file to be inserted when a custom
	    # page size is requested.
	    next;
	}
	# Determine the unprintable margins
	# Zero margins when no margin info exists
	$setup = '';
	my $parg;
	if( not($parg = argbyname($dat, 'ImageableArea'))
		or not valbyname( $parg, $value ) ){
	    my ($left, $right, $top, $bottom) =
	    getmargins($dat, $width, $height, $value);
	    $setup .= "*ImageableArea $value/$comment: " . 
		 "\"$left $bottom $right $top\"\n";
	}
	if( not($parg = argbyname($dat, 'PaperDimension'))
		or not valbyname( $parg, $value ) ){
	    $setup .= 
		 "*PaperDimension $value/$comment: \"$size\"\n";
	}
	if( not($parg = argbyname($dat, 'PageRegion'))
		or not valbyname( $parg, $value ) ){
	    $setup .= 
		 "*PaperRegion $value/$comment: " .
		 "\"<</PageSize [$size] /ImagingBBox null>>setpagedevice\"\n";
	}
	if( $setup ){
	    my @lines = split( "\n", $setup );
	    ppdfromvartoperl( $dat, \@lines );
	    $setup ='';
	}
    }
    # Make the header entries for a custom page size
    if ($hascustompagesize) {
	D10("PROCESSING Custom");
	my $maxpaperdim = 
	    ($maxpageheight > $maxpagewidth ?
	     $maxpageheight : $maxpagewidth);
	# PostScript code from the example 6 in section 6.3
	# of Adobe's PPD V4.3 specification
	# http://partners.adobe.com/asn/developer/pdfs/tn/5003.PPD_Spec_v4.3.pdf
	# If the page size is an option for the command line
	# of GhostScript, pop the values which were put
	# on the stack and insert a comment
	# to advise the filter
	
	my $order = $pagesize->{'order'}{'ps'};
	$order = $pagesize->{'order'}{'ps'} = 100 if not defined $order;
	my $section = $pagesize->{'section'}{'ps'};
	$section = $pagesize->{'section'}{'ps'} = 'AnySetup' if not defined $section;
	my $pscode = "pop pop pop
<</PageSize [ 5 -2 roll ] /ImagingBBox null>>setpagedevice";
	my ($left, $right, $top, $bottom) =
	    getmargins($dat, 0, 0, 'Custom');
	$setup = <<EOF;
*HWMargins: $left $bottom $right $top
*VariablePaperSize: True
*MaxMediaWidth: $maxpaperdim
*MaxMediaHeight: $maxpaperdim
*CustomPageSize True: \"$pscode\"
*End
*ParamCustomPageSize Width: 1 points 36 $maxpagewidth
*ParamCustomPageSize Height: 2 points 36 $maxpageheight
*ParamCustomPageSize Orientation: 3 int 0 0
*ParamCustomPageSize WidthOffset: 4 points 0 0
*ParamCustomPageSize HeightOffset: 5 points 0 0
EOF
    } else {
	$setup = "*VariablePaperSize: False";
    }
    if( $setup ){
	my @lines = split( "\n", $setup );
	ppdfromvartoperl( $dat, \@lines );
	$setup ='';
    }
}

# Generate the user options
# *OpenUI *PageSize/Media Size: PickOne
# *OrderDependency: 10 AnySetup *PageSize
# *DefaultPageSize: Letter
# *PageSize Letter/Letter: "<<..."
# *CloseUI: *PageSize
sub fix_user_options ( $ $ $ $ $ $ ){
    my( $arg, $ty, $shortgui, $lang, $prefix, $opts ) = @_;
    my $section = ($arg->{'section'}{$lang} || '');
    my $name = $arg->{'name'};
    my $default = $arg->{'default'};
    my $type = $arg->{'type'};
    my $outval = 0;
    my $order = $arg->{'order'}{$lang};
    my $open_ui = 0;
    my $nonui = ($arg->{'nonui'}{$lang} || '');
    if( $arg->{'option'}{$lang} ){
	$open_ui = keys %{$arg->{'option'}}
    }
    D10("fix_user_options: $name lang '$lang' section '$section' open_ui '$open_ui'");
    if( $open_ui){
	my $com = ($arg->{'comment'} || $arg->{'name'});;
	push @{$opts},
	    sprintf("\n*${prefix}OpenUI *${prefix}%s/%s: %s\n",
	    $name, cutguiname($com, $shortgui), $ty);
    } elsif( not $arg->{'vals'} or not @{$arg->{'vals'}}){
	$outval = 1;
    }
    if( $section ){
	push @{$opts},
	sprintf("*%sOrderDependency: %s %s *${prefix}%s\n", 
	     $nonui, $order, $section, $name);
    }
    foreach my $value (@{$arg->{'vals'}}){
	my $option = valbyname( $arg, $value );
	my $driverval = $option->{'driverval'}{$lang};
	my $setting = $option->{'setting'}{$lang};
	if( defined $driverval or defined $setting ){
	    $outval = 1;
	    last;
	}
    }
    if( $outval and defined $default and not $arg->{'default_out'} ){
	push @{$opts},
	 sprintf("*Default${prefix}%s: %s\n", 
	     $name, $default );
	$arg->{'default_out'} = 1;
    }
    foreach my $value (@{$arg->{'vals'}}){
	my $option = valbyname( $arg, $value );
	my $comment = ($option->{'comment'} || $value);
	my $driverval = $option->{'driverval'}{$lang};
	my $setting = $option->{'setting'}{$lang};
	next if not defined $driverval and not defined $setting;
	my $str = sprintf("*${prefix}%s %s/%s: \"%s\"", 
	    $name, $value, $comment, ($setting?$setting:$driverval) );
	if( $str =~ /\n/s){
	    $str .= "\n*End";
	}
	push @{$opts}, "$str\n";
    }
    if( $open_ui){
	push @{$opts},
	    sprintf("*${prefix}CloseUI *${prefix}%s\n",
	    $name );
    }
}

# *FoomaticRIPOption Foo: enum ps A
# *FoomaticRIPDefault Foo: Value1
# *FoomaticRIPOptionAllowedChars Foostr: "\w\s"
# *FoomaticRIPOptionAllowedRegExp Foostr: ".*"
# *FoomaticRIPOptionMaxLength Foostr: 32
# *FoomaticRIPOptionPrototype Fooint: "
# *FoomaticRIPOptionRange Fooint: 0 100
# *FoomaticRIPOptionSetting Foo=Value1: "
sub fix_foomatic_options ( $ $ $ $ $ $ ){
    my( $arg, $ty, $shortgui, $lang, $prefix, $opts ) = @_;
    my $section = ($arg->{'section'}{$lang} || '');
    my $spot = ($arg->{'spot'}{$lang} || 'A');
    my $order = $arg->{'order'}{$lang};
    $order = $arg->{'order'}{$lang} = 100 if not defined $order;
    my $name = $arg->{'name'};
    my $type = $arg->{'type'};
    my $str = "$type $lang $spot ";
    D10("fix_foomatic_options: name '$name' lang '$lang' type '$type' section '$section' spot '$spot' order '$order'");
    if( $section ne 'AnySetup' ){
	$str = "$type $lang $spot " .  ($order+0). " $section";
    } elsif( $order ){
	$str = "$type $lang $spot " .  ($order+0). '';
    }
    push @{$opts},
	sprintf("\n*FoomaticRIPOption ${prefix}%s: %s\n",
	    $name, $str);
    if( defined ($str = $arg->{'default'}) and not $arg->{'default_out'} ){
	$str = ripdirective( "*FoomaticRIPDefault ${prefix}${name}",
		 $str );
	if( $str =~ /\n/s){ $str .= "\n*End"; }
	push @{$opts}, $str . "\n";
	$arg->{'default_out'} = 1;
    }
    if( ($str = $arg->{'proto'}{$lang}) ){
	$str = ripdirective( "*FoomaticRIPOptionPrototype ${prefix}${name}",
		 $str );
	if( $str =~ /\n/s){ $str .= "\n*End"; }
	push @{$opts}, $str . "\n";
    }
    if( ($str = $arg->{'allowedchars'}) ){
	$str = ripdirective( "*FoomaticRIPAllowedChars ${prefix}${name}",
		 $str );
	if( $str =~ /\n/s){ $str .= "\n*End"; }
	push @{$opts}, $str . "\n";
    }
    if( ($str = $arg->{'allowedregexp'}) ){
	$str = ripdirective( "*FoomaticRIPAllowedRegExp ${prefix}${name}",
		 $str );
	if( $str =~ /\n/s){ $str .= "\n*End"; }
	push @{$opts}, $str . "\n";
    }
    if( ($str = $arg->{'maxlength'}) ){
	$str = ripdirective( "*FoomaticRIPMaxLength ${prefix}${name}",
		 $str );
	if( $str =~ /\n/s){ $str .= "\n*End"; }
	push @{$opts}, $str . "\n";
    }
    my $min = $arg->{'min'};
    my $max = $arg->{'max'};
    if( defined $min and defined $max ){
	$str = "$min $max";
	$str = ripdirective( "*FoomaticRIPRange ${prefix}${name}",
		 $str );
	if( $str =~ /\n/s){ $str .= "\n*End"; }
	push @{$opts}, $str . "\n";
    }
    foreach my $value (@{$arg->{'vals'}}){
	my $option = valbyname( $arg, $value );
	my $driverval = $option->{'driverval'}{$lang};
	next if not defined $driverval;
	my $str = 
	    ripdirective("*FoomaticRIPOptionSetting ${prefix}${name}=${value}",
		$driverval);
	if( $str =~ /\n/s){ $str .= "\n*End"; }
	push @{$opts}, $str . "\n";
    }
}


sub getpapersize( $ ){
    my( $papersize ) = @_;
    $papersize = lc( $papersize );

    my $sizetable = {
	'germanlegalfanfold'=> '612 936',
	'halfletter'=>         '396 612',
	'letterwide'=>         '647 957',
	'lettersmall'=>        '612 792',
	'letter'=>             '612 792',
	'legal'=>              '612 1008',
	'postcard'=>           '283 416',
	'tabloid'=>            '792 1224',
	'ledger'=>             '1224 792',
	'tabloidextra'=>       '864 1296',
	'statement'=>          '396 612',
	'manual'=>             '396 612',
	'executive'=>          '522 756',
	'folio'=>              '612 936',
	'archa'=>              '648 864',
	'archb'=>              '864 1296',
	'archc'=>              '1296 1728',
	'archd'=>              '1728 2592',
	'arche'=>              '2592 3456',
	'usaarch'=>            '648 864',
	'usbarch'=>            '864 1296',
	'uscarch'=>            '1296 1728',
	'usdarch'=>            '1728 2592',
	'usearch'=>            '2592 3456',
	'b6-c4'=>              '354 918',
	'c7-6'=>               '229 459',
	'supera3-b'=>          '932 1369',
	'a3wide'=>             '936 1368',
	'a4wide'=>             '633 1008',
	'a4small'=>            '595 842',
	'sra4'=>               '637 907',
	'sra3'=>               '907 1275',
	'sra2'=>               '1275 1814',
	'sra1'=>               '1814 2551',
	'sra0'=>               '2551 3628',
	'ra4'=>                '609 864',
	'ra3'=>                '864 1218',
	'ra2'=>                '1218 1729',
	'ra1'=>                '1729 2437',
	'ra0'=>                '2437 3458',
	'a10'=>                '74 105',
	'a9'=>                 '105 148',
	'a8'=>                 '148 210',
	'a7'=>                 '210 297',
	'a6'=>                 '297 420',
	'a5'=>                 '420 595',
	'a4'=>                 '595 842',
	'a3'=>                 '842 1191',
	'a2'=>                 '1191 1684',
	'a1'=>                 '1684 2384',
	'a0'=>                 '2384 3370',
	'2a'=>                 '3370 4768',
	'4a'=>                 '4768 6749',
	'c10'=>                '79 113',
	'c9'=>                 '113 161',
	'c8'=>                 '161 229',
	'c7'=>                 '229 323',
	'c6'=>                 '323 459',
	'c5'=>                 '459 649',
	'c4'=>                 '649 918',
	'c3'=>                 '918 1298',
	'c2'=>                 '1298 1836',
	'c1'=>                 '1836 2599',
	'c0'=>                 '2599 3676',
	'b10envelope'=>        '87 124',
	'b9envelope'=>         '124 175',
	'b8envelope'=>         '175 249',
	'b7envelope'=>         '249 354',
	'b6envelope'=>         '354 498',
	'b5envelope'=>         '498 708',
	'b4envelope'=>         '708 1000',
	'b3envelope'=>         '1000 1417',
	'b2envelope'=>         '1417 2004',
	'b1envelope'=>         '2004 2834',
	'b0envelope'=>         '2834 4008',
	'b10'=>                '87 124',
	'b9'=>                 '124 175',
	'b8'=>                 '175 249',
	'b7'=>                 '249 354',
	'b6'=>                 '354 498',
	'b5'=>                 '498 708',
	'b4'=>                 '708 1000',
	'b3'=>                 '1000 1417',
	'b2'=>                 '1417 2004',
	'b1'=>                 '2004 2834',
	'b0'=>                 '2834 4008',
	'monarch'=>            '279 540',
	'dl'=>                 '311 623',
	'com10'=>              '297 684',
	'env10'=>              '297 684',
	'hagaki'=>             '283 420',
	'oufuku'=>             '420 567',
	'kaku'=>               '680 941',
	'foolscap'=>           '576 936',
	'flsa'=>               '612 936',
	'flse'=>               '648 936',
	'photo100x150'=>       '283 425',
	'photo200x300'=>       '567 850',
	'photofullbleed'=>     '298 440',
	'photo4x6'=>           '288 432',
	'photo'=>              '288 432',
	'wide'=>               '977 792',
	'card148'=>            '419 297',
	'envelope132x220'=>    '374 623',
	'envelope61/2'=>       '468 260',
	'supera'=>             '644 1008',
	'superb'=>             '936 1368',
	'fanfold5'=>           '612 792',
	'fanfold4'=>           '612 864',
	'fanfold3'=>           '684 792',
	'fanfold2'=>           '864 612',
	'fanfold1'=>           '1044 792',
	'fanfold'=>            '1071 792',
	'panoramic'=>          '595 1683',
	'archlarge'=>          '162 540',
	'standardaddr'=>       '81 252',
	'largeaddr'=>          '101 252',
	'suspensionfile'=>     '36 144',
	'videospine'=>         '54 423',
	'badge'=>              '153 288',
	'archsmall'=>          '101 540',
	'videotop'=>           '130 223',
	'diskette'=>           '153 198',
	'roll'=>               '612 0',
	'69.5mmroll'=>        '197 0',
	'76.2mmroll'=>        '216 0',
	'custom'=>             '0 0'
	};
	my @matchtable = (
	['plotter.*size.*f',   '3168 4896'],
	['plotter.*size.*e',   '2448 3168'],
	['plotter.*size.*d',   '1584 2448'],
	['plotter.*size.*c',   '1124 1584'],
	['plotter.*size.*b',   '792 1124'],
	['plotter.*size.*a',   '612 792'],
	['long.*4',            '255 581'],
	['long.*3',            '340 666'],
	['env.*10',            '297 684'],
	['com.*10',            '297 684'],
	['iso.*4b',            '5669 8016'],
	['iso.*2b',            '4008 5669'],
	['iso.*b0',            '2834 4008'],
	['iso.*b1',            '2004 2834'],
	['iso.*b2',            '1417 2004'],
	['iso.*b3',            '1000 1417'],
	['iso.*b4',            '708 1000'],
	['iso.*b5',            '498 708'],
	['iso.*b6',            '354 498'],
	['iso.*b7',            '249 354'],
	['iso.*b8',            '175 249'],
	['iso.*b9',            '124 175'],
	['iso.*b10',           '87 124'],
	['4b.*iso',            '5669 8016'],
	['2b.*iso',            '4008 5669'],
	['b0.*iso',            '2834 4008'],
	['b1.*iso',            '2004 2834'],
	['b2.*iso',            '1417 2004'],
	['b3.*iso',            '1000 1417'],
	['b4.*iso',            '708 1000'],
	['b5.*iso',            '498 708'],
	['b6.*iso',            '354 498'],
	['b7.*iso',            '249 354'],
	['b8.*iso',            '175 249'],
	['b9.*iso',            '124 175'],
	['b10.*iso',           '87 124'],
	['jis.*b0',            '2919 4127'],
	['jis.*b1',            '2063 2919'],
	['jis.*b2',            '1459 2063'],
	['jis.*b3',            '1029 1459'],
	['jis.*b4',            '727 1029'],
	['jis.*b5',            '518 727'],
	['jis.*b6',            '362 518'],
	['jis.*b7',            '257 362'],
	['jis.*b8',            '180 257'],
	['jis.*b9',            '127 180'],
	['jis.*b10',           '90 127'],
	['b0.*jis',            '2919 4127'],
	['b1.*jis',            '2063 2919'],
	['b2.*jis',            '1459 2063'],
	['b3.*jis',            '1029 1459'],
	['b4.*jis',            '727 1029'],
	['b5.*jis',            '518 727'],
	['b6.*jis',            '362 518'],
	['b7.*jis',            '257 362'],
	['b8.*jis',            '180 257'],
	['b9.*jis',            '127 180'],
	['b10.*jis',           '90 127'],
	['a2.*invit.*',        '315 414'],
	);

    # Remove prefixes which sometimes could appear
    $papersize =~ s/form_//;

    # Check whether the paper size name is in the list above
	
    if( $sizetable->{$papersize} ){
	return( $sizetable->{$papersize} );
    }
    for my $item (@matchtable) {
	if ($papersize =~ /@{$item}[0]/) {
	    return @{$item}[1];
	}
    }

    # Check if we have a "<Width>x<Height>" format, assume the numbers are
    # given in inches
    if ($papersize =~ /(\d+)x(\d+)/) {
	my $w = $1 * 72;
	my $h = $2 * 72;
	return sprintf("%d %d", $w, $h);
    }

    # Check if we have a "w<Width>h<Height>" format, assume the numbers are
    # given in points
    if ($papersize =~ /w(\d+)h(\d+)/) {
	return "$1 $2";
    }

    # Check if we have a "w<Width>" format, assume roll paper with the given
    # width in points
    if ($papersize =~ /w(\d+)/) {
	return "$1 0";
    }

    # This paper size is absolutely unknown, issue a warning
    D0("WARNING: Unknown paper size: $papersize!");
    return "0 0";
}


# Determine the margins as needed by "*ImageableArea"
sub getmarginsformarginrecord( $ $ $ $ ){
    my ($margins, $width, $height, $pagesize) = @_;
    if (!defined($margins)) {
	# No margins defined? Return zero margins
	return (0, $width, $height, 0);
    }
    # Defaults
    my $unit = 'pt';
    my $absolute = 0;
    my ($left, $right, $top, $bottom) = (0, $width, $height, 0);
    # Check the general margins and then the particular paper size
    for my $i ('_general', $pagesize) {
	# Skip a section if it is not defined
	next if (!defined($margins->{$i}));
	# Determine the factor to calculate the margin in points (pt)
	$unit = (defined($margins->{$i}{'unit'}) ?
		 $margins->{$i}{'unit'} : $unit);
	my $unitfactor = 1.0; # Default unit is points
	if ($unit =~ /^p/i) {
	    $unitfactor = 1.0;
	} elsif ($unit =~ /^in/i) {
	    $unitfactor = 72.0;
	} elsif ($unit =~ /^cm$/i) {
	    $unitfactor = 72.0/2.54;
	} elsif ($unit =~ /^mm$/i) {
	    $unitfactor = 72.0/25.4;
	} elsif ($unit =~ /^dots(\d+)dpi$/i) {
	    $unitfactor = 72.0/$1;
	}
	# Convert the values to points
	($left, $right, $top, $bottom) =
	    ((defined($margins->{$i}{'left'}) ?
	      $margins->{$i}{'left'} * $unitfactor : $left),
	     (defined($margins->{$i}{'right'}) ?
	      $margins->{$i}{'right'} * $unitfactor : $right),
	     (defined($margins->{$i}{'top'}) ?
	      $margins->{$i}{'top'} * $unitfactor : $top),
	     (defined($margins->{$i}{'bottom'}) ?
	      $margins->{$i}{'bottom'} * $unitfactor : $bottom));
	# Determine the absolute values
	$absolute = (defined($margins->{$i}{'absolute'}) ?
		     $margins->{$i}{'absolute'} : $absolute);
	if (!$absolute){
	    if (defined($margins->{$i}{'right'})) {
		$right = $width - $right;
	    }
	    if (defined($margins->{$i}{'top'})) {
		$top = $height - $top;
	    }
	}
    }
    return ($left, $right, $top, $bottom);
}

sub getmargins( $ $ $ $ ){
    my ($dat, $width, $height, $pagesize) = @_;
    # Determine the unprintable margins
    # Zero margins when no margin info exists
    my ($left, $right, $top, $bottom) =
	(0, $width, $height, 0);
    # Margins from printer database entry
    my ($pleft, $pright, $ptop, $pbottom) =
	getmarginsformarginrecord($dat->{'printermargins'}, 
				  $width, $height, $pagesize);
    # Margins from driver database entry
    my ($dleft, $dright, $dtop, $dbottom) =
	getmarginsformarginrecord($dat->{'drivermargins'}, 
				  $width, $height, $pagesize);
    # Margins from printer/driver combo
    my ($cleft, $cright, $ctop, $cbottom) =
	getmarginsformarginrecord($dat->{'combomargins'}, 
				  $width, $height, $pagesize);
    # Left margin
    if ($pleft > $left) {$left = $pleft};
    if ($dleft > $left) {$left = $dleft};
    if ($cleft > $left) {$left = $cleft};
    # Right margin
    if ($pright < $right) {$right = $pright};
    if ($dright < $right) {$right = $dright};
    if ($cright < $right) {$right = $cright};
    # Top margin
    if ($ptop < $top) {$top = $ptop};
    if ($dtop < $top) {$top = $dtop};
    if ($ctop < $top) {$top = $ctop};
    # Bottom margin
    if ($pbottom > $bottom) {$bottom = $pbottom};
    if ($dbottom > $bottom) {$bottom = $dbottom};
    if ($cbottom > $bottom) {$bottom = $cbottom};
    # If we entered with $width == 0 and $height == 0, we mean
    # relative margins, so correct the signs
    if ($width == 0) {$right = -$right};
    if ($height == 0) {$top = -$top};
    # Clean up output
    $left =~ s/^\s*-0\s*$/0/;
    $right =~ s/^\s*-0\s*$/0/;
    $top =~ s/^\s*-0\s*$/0/;
    $bottom =~ s/^\s*-0\s*$/0/;
    # Return the results
    return ($left, $right, $top, $bottom);
}

sub fix_ppd_info( $ $ ){

    my( $dat, $update ) = @_;
    my $out = '';
    my($arg, $code, $str);

    my $model = ($dat->{'model'} || '');
    my $make = ($dat->{'make'} || '');
    my $driver = ($dat->{'driver'} || '');

    $arg = infobyname( $dat, 'FoomaticRIPPostPipe');
    if( ((not $arg) or $update) and ($code = $dat->{'postpipe'}) ) {
	my $header = "*FoomaticRIPPostPipe";
	my $str = ripdirective($header, $code) . "\n";
	if ($str =~ /\n.*\n/s) { $str .= "*End\n"; }
	$out .= $str;
    }

    $arg = infobyname( $dat, 'PCFileName');
    if( (not $arg) or $update ){
	my $pcfilename = '';
	if (($dat->{'pcmodel'}) && ($dat->{'pcdriver'})) {
	    $pcfilename = uc("$dat->{'pcmodel'}$dat->{'pcdriver'}");
	} elsif( $driver ){
	    $driver =~ m!(^(.{1,8}))!;
	    $pcfilename = uc($1);
	}
	if( $pcfilename ){
	    $out .= "*PCFileName: \"${pcfilename}.PPD\"\n";
	}
    }

    my( $ieee1284, $ieeemodel, $ieeemake);

    $ieee1284 = $dat->{'general_ieee'} or $ieee1284 = $dat->{'pnp_ieee'} or
	$ieee1284 = $dat->{'par_ieee'} or $ieee1284 = $dat->{'usb_ieee'} or 
	$ieee1284 = $dat->{'snmp_ieee'};
    if ($ieee1284) {
	$ieee1284 =~ /(MDL|MODEL):([^:;]+);/;
	$ieeemodel = $2;
	$ieee1284 =~ /(MFG|MANUFACTURER):([^:;]+);/;
	$ieeemake = $2;
	$ieee1284 =~ s/;(.)/;\n  $1/gs;
	$arg = infobyname( $dat, '1284DeviceID');
	if( (not $arg) or $update ){
	    $out .= "*1284DeviceID: \"\n  " . $ieee1284 . "\n\"\n*End\n";
	}
    }

    my $pnpmodel;
    $pnpmodel = $dat->{'general_mdl'} or $dat->{'pnp_mdl'} or 
	$pnpmodel = $dat->{'par_mdl'} or $pnpmodel = $dat->{'usb_mdl'} or
	$pnpmodel = $ieeemodel or $pnpmodel = $model;
    my $pnpmake;
    $pnpmake = $dat->{'general_mfg'} or $dat->{'pnp_mfg'} or 
	$pnpmake = $dat->{'par_mfg'} or $pnpmake = $dat->{'usb_mfg'} or
	$pnpmake = $ieeemake or $pnpmake = $make;
    $arg = infobyname( $dat, 'Manufacturer');
    if( (not $arg) or $update ){
	$out .= "*Manufacturer:	\"$pnpmake\"\n";
    }

    $arg = infobyname( $dat, 'Product');
    if( (not $arg) or $update ){
	$out .= "*Product:  \"$pnpmodel\"\n";
    }

    my $filename = join('',($make, $model, $driver));
    if( $filename ){
	$filename = join('-',($make, $model, $driver));
    } else {
	$filename = infobyname($dat,"ShortNickName");
	$filename = infobyname($dat,"NickName") if not $filename;
	$filename = $filename->{'info_last'} if $filename;
	$filename = "UNKNOWN" if not $filename;
	$filename =~ s/\s/-/g;
    }
    $filename =~ s![ /\(\)]!_!g;
    $filename =~ s![\+]!plus!g;
    $filename =~ s!__+!_!g;
    $filename =~ s!_$!!;
    $filename =~ s!_-!-!;
    $filename =~ s!^_!!;

    $dat->{'filename'} = "${filename}.ppd";

    # Do we use the recommended driver?
    my $driverrecommended = '';
    if ($driver eq ($dat->{'recdriver'} or '')) {
	$driverrecommended = " (recommended)";
    }
    
    my $drivername = $driver;
    # evil special case.
    $drivername = "stp-4.0" if $driver eq 'stp';

    # Do not use "," or "+" in the *ShortNickName to make the Windows
    # PostScript drivers happy
    my $nickname =
	"$make $model Foomatic/$drivername$driverrecommended";

    $arg = infobyname( $dat, 'NickName');
    if( (not $arg) or $update ){
	$out .= "*NickName: \"$nickname\"\n";
    }

    # Remove forbidden characters (Adobe PPD spec 4.3 section 5.3)
    # Do not use "," or "+" in the *ShortNickName to make the Windows
    # PostScript drivers happy
    my $modelname = "$make $model";
    $modelname =~ s/[^A-Za-z0-9 \.\/\-\+]//gs;

    $arg = infobyname( $dat, 'ModelName');
    if( (not $arg) or $update ){
	$out .= "*ModelName: \"$modelname\"\n";
    }

    my $shortnickname = "$make $model $drivername";
    if (length($shortnickname) > 31) {
	# ShortNickName too long? Shorten it.
	my %parts;
	$parts{'make'} = $make;
	$parts{'model'} = $model;
	$parts{'driver'} = $drivername;
	# Go through the three components, begin with model name, then
	# make and then driver
	for my $part (qw/model make driver/) {
	    # Split the component into words, cutting always at the right edge
	    # of the word. Cut also at a capital in the middle of the word
	    # (ex: "S" in "PostScript").
	    my @words = split(/(?<=[a-zA-Z])(?![a-zA-Z])|(?<=[a-z])(?=[A-Z])/,
			      $parts{$part});
	    # Go through all words
	    for (@words) {
		# Do not abbreviate words of less than 4 letters
		next if ($_ !~ /[a-zA-Z]{4,}$/);
		# How many letters did we chop off
		my $abbreviated = 0;
	        while (1) {
		    # Remove the last letter
		    chop;
		    $abbreviated ++;
		    # Build the shortened component ...
		    $parts{$part} = join('', @words);
		    # ... and the ShortNickName
		    $shortnickname =
			"$parts{'make'} $parts{'model'} $parts{'driver'}";
		    # Stop if the ShostNickName has 30 characters or less
		    # (we have still to add the abbreviation point), if there
		    # is only one letter left, or if the manufacturer name
		    # is reduced to three characters. Do not accept an
		    # abbreviation of one character, as, taking the
		    # abbreviation point into account, it does not save
		    # a character.
		    last if (((length($shortnickname) <= 30) &&
			      ($abbreviated != 1)) ||
			     ($_ !~ /[a-zA-Z]{2,}$/) ||
			     ((length($parts{'make'}) <= 3) &&
			      ($abbreviated != 1)));
		}
		#Abbreviation point
		if ($abbreviated) {
		    $_ .= '.';
		}
		$parts{$part} = join('', @words);
		$shortnickname =
		    "$parts{'make'} $parts{'model'} $parts{'driver'}";
		last if (length($shortnickname) <= 31);
	    }
	    last if (length($shortnickname) <= 31);
	}
	while ((length($shortnickname) > 31) &&
	       (length($parts{'model'}) > 3)) {
	    # ShortNickName too long? Remove last words from model name.
	    $parts{'model'} =~
		s/(?<=[a-zA-Z0-9])[^a-zA-Z0-9]+[a-zA-Z0-9]*$//;
	    $shortnickname =
		"$parts{'make'} $parts{'model'}, $parts{'driver'}";
	}
	if (length($shortnickname) > 31) {
	    # If nothing else helps ...
	    $shortnickname = substr($shortnickname, 0, 31);
	}
    }

    $arg = infobyname( $dat, 'ShortNickName');
    if( (not $arg) or $update ){
	$out .= "*ShortNickName: \"$shortnickname\"\n";
    }

    $arg = infobyname($dat, 'ColorDevice' );
    if( (not $arg) or $update ){
	if ($dat->{'color'}) {
	    $out .= "*ColorDevice: True\n*DefaultColorSpace: RGB\n";
	} else {
	    $out .= "*ColorDevice: False\n*DefaultColorSpace: Gray\n";
	}
    }

    if( $update ){
	$out .= <<EOF;
*cupsVersion:	1.0
*cupsManualCopies: True
*cupsModelNumber:  2
*cupsFilter:	"application/vnd.cups-postscript 0 foomatic-rip"
*DefaultFont: Courier
*Font AvantGarde-Book: Standard "(001.006S)" Standard ROM
*Font AvantGarde-BookOblique: Standard "(001.006S)" Standard ROM
*Font AvantGarde-Demi: Standard "(001.007S)" Standard ROM
*Font AvantGarde-DemiOblique: Standard "(001.007S)" Standard ROM
*Font Bookman-Demi: Standard "(001.004S)" Standard ROM
*Font Bookman-DemiItalic: Standard "(001.004S)" Standard ROM
*Font Bookman-Light: Standard "(001.004S)" Standard ROM
*Font Bookman-LightItalic: Standard "(001.004S)" Standard ROM
*Font Courier: Standard "(002.004S)" Standard ROM
*Font Courier-Bold: Standard "(002.004S)" Standard ROM
*Font Courier-BoldOblique: Standard "(002.004S)" Standard ROM
*Font Courier-Oblique: Standard "(002.004S)" Standard ROM
*Font Helvetica: Standard "(001.006S)" Standard ROM
*Font Helvetica-Bold: Standard "(001.007S)" Standard ROM
*Font Helvetica-BoldOblique: Standard "(001.007S)" Standard ROM
*Font Helvetica-Narrow: Standard "(001.006S)" Standard ROM
*Font Helvetica-Narrow-Bold: Standard "(001.007S)" Standard ROM
*Font Helvetica-Narrow-BoldOblique: Standard "(001.007S)" Standard ROM
*Font Helvetica-Narrow-Oblique: Standard "(001.006S)" Standard ROM
*Font Helvetica-Oblique: Standard "(001.006S)" Standard ROM
*Font NewCenturySchlbk-Bold: Standard "(001.009S)" Standard ROM
*Font NewCenturySchlbk-BoldItalic: Standard "(001.007S)" Standard ROM
*Font NewCenturySchlbk-Italic: Standard "(001.006S)" Standard ROM
*Font NewCenturySchlbk-Roman: Standard "(001.007S)" Standard ROM
*Font Palatino-Bold: Standard "(001.005S)" Standard ROM
*Font Palatino-BoldItalic: Standard "(001.005S)" Standard ROM
*Font Palatino-Italic: Standard "(001.005S)" Standard ROM
*Font Palatino-Roman: Standard "(001.005S)" Standard ROM
*Font Symbol: Special "(001.007S)" Special ROM
*Font Times-Bold: Standard "(001.007S)" Standard ROM
*Font Times-BoldItalic: Standard "(001.009S)" Standard ROM
*Font Times-Italic: Standard "(001.007S)" Standard ROM
*Font Times-Roman: Standard "(001.007S)" Standard ROM
*Font ZapfChancery-MediumItalic: Standard "(001.007S)" Standard ROM
*Font ZapfDingbats: Special "(001.004S)" Standard ROM
EOF
    }
    D10("fix_ppd_info: out $out");
    my @lines = split( "\n", $out );
    if( @lines ){
	ppdfromvartoperl( $dat, \@lines );
	D10("fix_ppd_info: AFTER" . Dumper($dat) );
    }
}

1;

