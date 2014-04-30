#!/usr/bin/perl
print "Content-type: text/plain\n\n";

print "<html><head><title>Perl Test</title></head><body><h1>This is a Perl test!</h1><p>";

$x = $ENV{'QUERY_STRING'};
print $x;

print "</p><p>test2</p></body></html>\n";
