#!/usr/local/bin/perl

# element2man.pl -- creates man pages from structured comments in element
# source code
# Eddie Kohler
# Robert Morris - original make-faction-html script
#
# Copyright (c) 1999 Massachusetts Institute of Technology.
#
# This software is being provided by the copyright holders under the GNU
# General Public License, either version 2 or, at your discretion, any later
# version. For more information, see the `COPYRIGHT' file in the source
# distribution.

my(%text_processing) = ( 'AGNOSTIC' => 'agnostic', 'PUSH' => 'push',
			 'PULL' => 'pull',
			 'PUSH_TO_PULL' => 'push inputs, pull outputs',
			 'PULL_TO_PUSH' => 'pull inputs, push outputs' );
my(%section_is_array) = ( 'h' => 1, 'a' => 1 );
my $directory;
my $section = 'n';
my @all_created;

# find date
my($today) = '';
if (localtime =~ /\w* (\w*) (\d*) \S* (\d*)/) {
  $today = "$2/$1/$3";
}

my $prologue = <<'EOD;';
.de M
.BR "\\$1" "(\\$2)\\$3"
..
.de RM
.RB "\\$1" "\\$2" "(\\$3)\\$4"
..
EOD;
chomp $prologue;

sub nroffize ($@) {
  my($t) = shift @_;
  $t =~ s/\\/\\\\/g;
  $t =~ s/^(= )?\./$1\\&./gm;
  $t =~ s/^(= )?'/$1\\&'/gm;
  $t =~ s/^\s*$/.PP\n/gm;
  $t =~ s/<i>(.*?)<\/i>/\\fI$1\\fP/g;
  $t =~ s/<b>(.*?)<\/b>/\\fB$1\\fP/g;
  while ($t =~ /^\.PP\n\.PP\n/m) {
    $t =~ s/^\.PP\n\.PP\n/.PP\n/gm;
  }
  $t =~ s/^= (.*\n)/.nf\n$1.fi\n/mg;
  $t =~ s/^\.fi\n\.nf\n//mg;
  my($i);
  foreach $i (sort { length($b) <=> length($a) } @_) {
    $t =~ s/$i/\\fB$i\\fR/g;
  }
  $t;
}

sub process_comment ($$) {
  my($t, $filename) = @_;
  my(%x, $i);

  while ($t =~ m{^=(\w+)\s*([\0-\377]*?)(?=\n=\w|\Z)}mg) {
    if ($section_is_array{$1}) {
      push @{$x{$1}}, "$2\n";
    } else {
      $x{$1} .= "$2\n";
    }
  }
  
  my(@classes, %classes);
  while ($x{'c'} =~ /^\s*(\w+)\(/mg) { # configuration arguments section
    push @classes, $1 if !exists $classes{$1};
    $classes{$1} = 1;
  }
  my($classes_plural) = (@classes == 1 ? '' : 's');
  my($classes) = join(', ', @classes);

  # open new output file if necessary
  if ($directory) {
    if (!open(OUT, ">$directory/$classes[0].$section")) {
      print STDERR "$directory/$classes[0].$section: $!\n";
      return;
    }
  }
  push @all_created, $classes[0];
  
  print OUT <<"EOD;";
.\\" -*- mode: nroff -*-
.\\" Generated by \`element2man.pl' from \`$filename'
$prologue
.TH "\U$classes\E" $section "$today" "Click"
.SH "NAME"
$classes \- Click element$classes_plural
EOD;
  
  if ($x{'c'}) {
    print OUT ".SH \"SYNOPSIS\"\n";
    while ($x{'c'} =~ /^\s*(\S.*)$/mg) {
      print OUT nroffize($1, @classes), "\n.br\n";
    }
  }

  if (@classes == 1 && $processing{$classes}) {
    print OUT ".SH \"PROCESSING TYPE\"\n";
    print OUT nroffize($text_processing{$processing{$classes}}), "\n";
  }

  if ($x{'io'}) {
    print OUT ".SH \"INPUTS AND OUTPUTS\"\n";
    print OUT nroffize($x{'io'});
  }

  if ($x{'d'}) {
    print OUT ".SH \"DESCRIPTION\"\n";
    print OUT nroffize($x{'d'}, @classes);
  }

  if ($x{'n'}) {
    print OUT ".SH \"NOTES\"\n";
    print OUT nroffize($x{'n'});
  }

  if ($x{'e'}) {
    print OUT ".SH \"EXAMPLES\"\n";
    print OUT nroffize($x{'e'});
  }

  if (@{$x{'h'}}) {
    print OUT ".SH \"HANDLERS\"\n";
    print OUT "The ", $classes[0], " element installs the following additional handlers.\n";
    foreach $i (@{$x{'h'}}) {
      if ($i =~ /^(\S+)\s*(\S*)\n(.*)$/s) {
	print OUT ".TP 5\n.BR ", $1;
	print OUT " \" (", $2, ")\"" if $2;
	print OUT "\n.RS\n", nroffize($3), ".RE\n";
      }
    }
  }

  if (@{$x{'a'}}) {
    print OUT ".SH \"SEE ALSO\"\n";
    my(@related) = @{$x{'a'}};
    map(s/\s//sg, @related);
    @related = sort @related;
    my($last) = pop @related;
    print OUT map(".M $_ n ,\n", @related);
    print OUT ".M $last n\n";
  }

  # close output file & make links if appropriate
  if ($directory) {
    close OUT;
    foreach $i (@classes[1..$#classes]) {
      unlink("$directory/$i.$section");
      if (link "$directory/$classes[0].$section", "$directory/$i.$section") {
	push @all_created, $i;
      } else {
	print STDERR "$directory/$i.$section: $!\n";
      }
    }
  }
}

sub process_file ($) {
  my($filename) = @_;
  $filename =~ s/\.cc$/\.hh/;
  if (!open(IN, $filename)) {
    print STDERR "$filename: $!\n";
    return;
  }
  my $text = <IN>;
  close IN;

  foreach $_ (split(m{^class}m, $text)) {
    my($cxx_class) = (/^\s*(\w*)/);
    if (/class_name.*return\s*\"([^\"]+)\"/) {
      $class_name{$cxx_class} = $1;
      $cxx_class = $1;
    }
    if (/default_processing.*return\s+(\w*)/) {
      $processing{$cxx_class} = $1;
    }
  }

  foreach $_ (split(m{(/\*.*?\*/)}s, $text)) {
    if (/^\/\*/ && /^[\/*\s]+=/) {
      s/^\/\*\s*//g;
      s/\s*\*\/$//g;
      s/^[ \t]*\*[ \t]*//gm;
      process_comment($_, $ff);
    }
  }
}

# main program: parse options
sub read_files_from ($) {
  my($fn) = @_;
  if (open(IN, ($fn eq '-' ? "<&STDIN" : $fn))) {
    my($t) = <IN>;
    close IN;
    map { glob($_) } split(/\s+/, $t);
  } else {
    print STDERR "$fn: $!\n";
    ();
  }
}

undef $/;
my(@files, $fn, $elementlist);
while (@ARGV) {
  $_ = shift @ARGV;
  if (/^-d$/ || /^--directory$/) {
    die "not enough arguments" if !@ARGV;
    $directory = shift @ARGV;
  } elsif (/^--directory=(.*)$/) {
    $directory = $1;
  } elsif (/^-f$/ || /^--files$/) {
    die "not enough arguments" if !@ARGV;
    push @files, read_files_from(shift @ARGV);
  } elsif (/^--files=(.*)$/) {
    push @files, read_files_from($1);
  } elsif (/^-l$/ || /^--list$/) {
    $elementlist = 1;
  } elsif (/^-./) {
    die "unknown option `$_'\n";
  } elsif (/^-$/) {
    push @files, "-";
  } else {
    push @files, glob($_);
  }
}
push @files, "-" if !@files;

umask(022);
open(OUT, ">&STDOUT") if !$directory;
foreach $fn (@files) {
  process_file($fn);
}
close OUT if !$directory;

sub make_elementlist () {
  if ($directory) {
    if (!open(OUT, ">$directory/elements.$section")) {
      print STDERR "$directory/$classes[0].$section: $!\n";
      return;
    }
  }
  print OUT <<"EOD;";
.\\" -*- mode: nroff -*-
.\\" Generated by \`element2man.pl'
$prologue
.TH "ELEMENTS" $section "$today" "Click"
.SH "NAME"
elements \- documented Click element classes
.SH "DESCRIPTION"
This page lists all Click element classes that have manual page documentation.
.SH "SEE ALSO"
.nh
EOD;
  @all_created = sort @all_created;
  my($last) = pop @all_created;
  print OUT map(".M $_ n ,\n", @all_created);
  print OUT ".M $last n\n.hy\n";
  close OUT if $directory;
}
  
if ($elementlist && @all_created) {
  make_elementlist();
}
