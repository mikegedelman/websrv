#!/usr/bin/perl 

print "Content-Type: text/html\r\n\r\n";

my $query_string = $ENV{'QUERY_STRING'};

my @pairs = split(/&/, $query_string);

# should maybe do some more sanitizing here 
foreach(@pairs)
{
   my($key, $value) = split(/=/, $_, 2);
   $param{$key} = $value;
}

$filename = $param{'file'};

if (!-e $filename) {
   print "<!DOCTYPE HTML><html><head><title>My Histogram: Error:</title></head><body><h1>$filename: File Not Found</h1><br><a href='javascript:history.back()'>Go Back</a></body></html";
   exit;
}
if (!length $param{'q'}) {
   print "<!DOCTYPE HTML></html><head><title><My Histogram: Error:</title></head><body><h1>Error: No search terms entered.</h1><br><a href='javascript:history.back()'>Go Back</a></body></html";
   exit;
}

$html = <<END;
<!DOCTYPE HTML>
<html>
   <head>
      <title>CS 410 Webserver</title>
      <style type="text/css">
         h1 { 
               color:red;
               text-align:center;
         }

         img.displayed {
            display:block;
            margin-left: auto;
            margin-right: auto;
         }
      </style>
   </head>
   <body>
      <h1>CS 410 Webserver</h1>
      <br>
      <img class="displayed" src="/my_histogram.cgi?$query_string">
   </body>
</html>
END

print $html;

