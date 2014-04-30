#!/usr/bin/perl 
#  This is to be referenced in an <img src=""> tag. It outputs an image and 
# nothing else.
#
#  We've already verified that the desired file exists, and that the search
# terms are non-empty in cs410_histogram.cgi, so we're not going to check that
# again.

use strict;

my $query_string = $ENV{'QUERY_STRING'};
my @pairs = split(/&/,$query_string);
my %param;

foreach(@pairs)
{
   my($key, $value) = split(/=/, $_, 2);
   $value =~ s/%([0-9A-F][0-9A-F])/pack("c",hex($1))/ge;
   $param{$key} = $value;
}

my $file  = $param{'file'};
my $q_str = $param{'q'};
my @q     = split(/\+/, $q_str);
my %hist;

my $max = 0;
foreach (@q) {
   my $count = `grep -o $_ $file | wc -l`;
   $hist{$_} = $count;

   if ($count > $max) {
      $max = $count;
   }
}

if ($max eq 0) {
   $max = 100;
}

my $data = "";
foreach my $search (sort { $hist{$b} <=> $hist{$a} or $a cmp $b } keys %hist) {
   $data = $data . "$search $hist{$search}" 
}

chomp $max;

my $gp_cmds = <<END;
set terminal gif
unset key
set xlabel "Search Patterns"
set ylabel "Number of Occurences"
set yrange [0:$max]
set boxwidth 0.6
set style fill solid
set style data histogram
set title "Search in File $file" font "sans,24"
plot '-' using 2:xticlabels(1) with boxes
$data
END

print "Content-Type: image/png\r\n\r\n";
open (GNUPLOT, "|gnuplot");
print GNUPLOT $gp_cmds;
