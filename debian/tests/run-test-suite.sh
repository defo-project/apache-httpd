#!/bin/bash

. /etc/apache2/envvars

run_tests () {
	local MPM=$1
	local PORT_OFFSET=${2:-0}
	local WORKDIR=$AUTOPKGTEST_TMP/perl-framework-$MPM
	local LOG=$AUTOPKGTEST_TMP/testlog.$MPM

	echo =============Running-with-${MPM}==========

	# Create isolated working directory for this MPM
	cp -a $AUTOPKGTEST_TMP/perl-framework $WORKDIR
	cd $WORKDIR

	rm -f apache2.conf.debian
	cp /etc/apache2/apache2.conf apache2.conf.debian
	cat /etc/apache2/mods-available/$MPM.load >> apache2.conf.debian
	ls /etc/apache2/mods-available/*.load | grep -v mpm_ | xargs cat >> apache2.conf.debian
	# these are only for tests and don't have a .load file
	for m in bucketeer case_filter case_filter_in ; do
		echo "LoadModule ${m}_module /usr/lib/apache2/modules/mod_${m}.so" >> apache2.conf.debian
	done
	# need TypesConfig from mime.conf for t/modules/filter.t
	cat /etc/apache2/mods-available/mime.conf >> apache2.conf.debian
	echo "Servername localhost" >> apache2.conf.debian
	make clean || true
	perl -p -i -e 's,^Include,#Include,' apache2.conf.debian
	chown -R $TESTUSER: $WORKDIR
	su $TESTUSER -c "perl Makefile.PL -apxs /usr/bin/apxs2 -httpd_conf $PWD/apache2.conf.debian -port \$((8529 + $PORT_OFFSET))" \
	    || return 1
	su $TESTUSER -c "t/TEST $TESTS" | tee $LOG
	if ! grep -E "^Files=[0-9]+, Tests=[0-9]+" $LOG ; then
		echo "Message about Files/Tests not found in $LOG" >&2
		return 1
	fi
	if ! grep -E "^Result: PASS" $LOG ; then
		echo "PASS message not found in $LOG" >&2
		return 1
	fi
	if grep -E "^Result: FAIL" $LOG ; then >&2
		echo "Test suite failed"
		return 1
	fi
	if grep -E "server dumped core" $LOG ; then >&2
		echo "segfault detected"
		return 1
	fi
	return 0
}

