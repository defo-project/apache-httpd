use strict;
use warnings FATAL => 'all';

use Apache::Test;
use Apache::TestRequest;
use Apache::TestUtil;

Apache::TestRequest::module('error_document');

plan tests => 17, need_lwp;

# basic ErrorDocument tests

{
    my $response = GET '/notfound.html';
    chomp(my $content = $response->content);

    ok t_cmp($response->code,
             404,
             'notfound.html code');

    ok t_cmp($content,
             qr'per-server 404',
             'notfound.html content');
}

{
    my $response = GET '/inherit/notfound.html';
    chomp(my $content = $response->content);

    ok t_cmp($response->code,
             404,
             '/inherit/notfound.html code');

    ok t_cmp($content,
             qr'per-server 404',
             '/inherit/notfound.html content');
}

{
    my $response = GET '/redefine/notfound.html';
    chomp(my $content = $response->content);

    ok t_cmp($response->code,
             404,
             '/redefine/notfound.html code');

    ok t_cmp($content,
             'per-dir 404',
             '/redefine/notfound.html content');
}

{
    my $response = GET '/restore/notfound.html';
    chomp(my $content = $response->content);

    ok t_cmp($response->code,
             404,
             '/redefine/notfound.html code');

    # 1.3 requires quotes for hard-coded messages
    my $expected = have_min_apache_version('2.0.51') ? qr/Not Found/ :
                   have_apache(2)                    ? 'default'     : 
                   qr/Additionally, a 500/;

    ok t_cmp($content,
             $expected,
             '/redefine/notfound.html content');
}

{
    my $response = GET '/apache/notfound.html';
    chomp(my $content = $response->content);

    ok t_cmp($response->code,
             404,
             '/merge/notfound.html code');

    ok t_cmp($content,
             'testing merge',
             '/merge/notfound.html content');
}

{
    my $response = GET '/apache/etag/notfound.html';
    chomp(my $content = $response->content);

    ok t_cmp($response->code,
             404,
             '/merge/merge2/notfound.html code');

    ok t_cmp($content,
             'testing merge',
             '/merge/merge2/notfound.html content');
}

{
    my $response = GET '/bounce/notfound.html';
    chomp(my $content = $response->content);

    ok t_cmp($response->code,
             404,
             '/bounce/notfound.html code');

    ok t_cmp($content,
             qr!expire test!,
             '/bounce/notfound.html content');
}

{
    my $l = Apache::TestRequest::resolve_url('/trace/notallowed.html');
    my $req = HTTP::Request->new('TRACE', $l);
    my $ua = LWP::UserAgent->new();
    my $response = $ua->request($req);
    chomp(my $content = $response->content);

    ok t_cmp($response->code,
             405,
             '/trace/notallowed.html code');

    if (need_min_apache_version("2.5.1")) {
        ok t_cmp($content,
                 qr!The requested method TRACE is not allowed for this URL.!,
                 '/trace/notallowed.html content');
    }
    else {
        skip "Skipping test"
    }

    if (need_min_apache_version("2.5.1")) {
        ok t_cmp($content,
                 qr!Additionally, a 404 Not Found!,
                 '/trace/notallowed.html content');
    }
    else {
        skip "Skipping test"
    }

}
