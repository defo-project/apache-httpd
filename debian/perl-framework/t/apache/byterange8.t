use strict;
use warnings FATAL => 'all';

use Apache::Test;
use Apache::TestRequest;
use Apache::TestUtil;

my $url = "/apache/chunked/byteranges.txt";
my $file = Apache::Test::vars('serverroot') . "/htdocs$url";

my $content = "";
$content .= sprintf("%04d", $_) for (1 .. 100);
t_write_file($file, $content);
my $real_clen = length($content);

#
# test cases for PR 69831
#

my @space = (" ", "\t");
my @tc;

for (my $k = 0; $k < 2; $k++) {
    for (my $i = 0; $i < 3; $i++) {
        for (my $j = 0 ; $j < 3; $j++) {
            $tc[ $k * 9 + $i * 3 + $j ] = "1-2" . $space[$k] x $i . "," .
                                           $space[$k] x $j . "3-4";
        }
    }
}

plan tests => scalar(@tc),
              need need_lwp, need_min_apache_version('2.5.1');

foreach my $range (@tc) {
    print "Sending '$range', expecting 206\n";
    my $result = GET $url, "Range" => "bytes=$range";
    ok t_cmp($result->code, 206);
}
