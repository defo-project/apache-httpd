use strict;
use warnings FATAL => 'all';

use Apache::Test;
use Apache::TestRequest;
use Apache::TestUtil;
use MIME::Base64;
use Data::Dumper;
use HTTP::Response;
use Socket;

Apache::TestRequest::scheme('https');
my $vars = Apache::Test::vars();

my @ssl_test_cases = (
  [ "unmatched"      => 200, "no hop, stays on default vhost"],
  # To run without SSLVHostSNIPolicy, prefix t/TEST with env NO_TEST_SNIPOLICY=/tmp (any file or dir)
  [ "nvh"            => defined($ENV{'NO_TEST_SNIPOLICY'}) ? 421 : 200, "hop allowed by global directive"],
);

plan tests => scalar(@ssl_test_cases);


foreach my $vhosts (([$vars->{ssl_module_name} => 1])) {
  my $vhost = $vhosts->[0];

  foreach my $t (@ssl_test_cases) {
    my $host = $t->[0];
    my $expect = $t->[1];
    my $desc = $t->[2];

    my $r = GET("/", 'Host' => $host);
    ok t_cmp($r->code, $expect, $desc);
  }
}

sub escape
{
    my $in = shift;
    $in =~ s{\\}{\\\\}g;
    $in =~ s{\r}{\\r}g;
    $in =~ s{\n}{\\n}g;
    $in =~ s{\t}{\\t}g;
    $in =~ s{([\x00-\x1f])}{sprintf("\\x%02x", ord($1))}ge;
    return $in;
}

sub peer
{
   my $sock = shift;
   my $hersockaddr    = getpeername($sock);
   my ($port, $iaddr) = sockaddr_in($hersockaddr);
   my $herhostname    = gethostbyaddr($iaddr, AF_INET);
   my $herstraddr     = inet_ntoa($iaddr);
   return "$herstraddr:$port";
}
