/* ====================================================================
 * The Apache Software License, Version 1.1
 *
 * Copyright (c) 2000-2001 The Apache Software Foundation.  All rights
 * reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 *
 * 3. The end-user documentation included with the redistribution,
 *    if any, must include the following acknowledgment:
 *       "This product includes software developed by the
 *        Apache Software Foundation (http://www.apache.org/)."
 *    Alternately, this acknowledgment may appear in the software itself,
 *    if and wherever such third-party acknowledgments normally appear.
 *
 * 4. The names "Apache" and "Apache Software Foundation" must
 *    not be used to endorse or promote products derived from this
 *    software without prior written permission. For written
 *    permission, please contact apache@apache.org.
 *
 * 5. Products derived from this software may not be called "Apache",
 *    nor may "Apache" appear in their name, without prior written
 *    permission of the Apache Software Foundation.
 *
 * THIS SOFTWARE IS PROVIDED ``AS IS'' AND ANY EXPRESSED OR IMPLIED
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED.  IN NO EVENT SHALL THE APACHE SOFTWARE FOUNDATION OR
 * ITS CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF
 * USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT
 * OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 * ====================================================================
 *
 * This software consists of voluntary contributions made by many
 * individuals on behalf of the Apache Software Foundation.  For more
 * information on the Apache Software Foundation, please see
 * <http://www.apache.org/>.
 *
 * Portions of this software are based upon public domain software
 * originally written at the National Center for Supercomputing Applications,
 * University of Illinois, Urbana-Champaign.
 */

/*
 * http_log.c: Dealing with the logs and errors
 * 
 * Rob McCool
 * 
 */

#include "apr.h"
#include "apr_general.h"        /* for signal stuff */
#include "apr_strings.h"
#include "apr_errno.h"
#include "apr_thread_proc.h"
#include "apr_lib.h"
#include "apr_signal.h"

#define APR_WANT_STDIO
#define APR_WANT_STRFUNC
#include "apr_want.h"

#if APR_HAVE_STDARG_H
#include <stdarg.h>
#endif
#if APR_HAVE_UNISTD_H
#include <unistd.h>
#endif

#define CORE_PRIVATE

#include "ap_config.h"
#include "httpd.h"
#include "http_config.h"
#include "http_core.h"
#include "http_log.h"
#include "http_main.h"

typedef struct {
	char	*t_name;
	int	t_val;
} TRANS;

#ifdef HAVE_SYSLOG

static const TRANS facilities[] = {
    {"auth",	LOG_AUTH},
#ifdef LOG_AUTHPRIV
    {"authpriv",LOG_AUTHPRIV},
#endif
#ifdef LOG_CRON
    {"cron", 	LOG_CRON},
#endif
#ifdef LOG_DAEMON
    {"daemon",	LOG_DAEMON},
#endif
#ifdef LOG_FTP
    {"ftp",	LOG_FTP},
#endif
#ifdef LOG_KERN
    {"kern",	LOG_KERN},
#endif
#ifdef LOG_LPR
    {"lpr",	LOG_LPR},
#endif
#ifdef LOG_MAIL
    {"mail",	LOG_MAIL},
#endif
#ifdef LOG_NEWS
    {"news",	LOG_NEWS},
#endif
#ifdef LOG_SYSLOG
    {"syslog",	LOG_SYSLOG},
#endif
#ifdef LOG_USER
    {"user",	LOG_USER},
#endif
#ifdef LOG_UUCP
    {"uucp",	LOG_UUCP},
#endif
#ifdef LOG_LOCAL0
    {"local0",	LOG_LOCAL0},
#endif
#ifdef LOG_LOCAL1
    {"local1",	LOG_LOCAL1},
#endif
#ifdef LOG_LOCAL2
    {"local2",	LOG_LOCAL2},
#endif
#ifdef LOG_LOCAL3
    {"local3",	LOG_LOCAL3},
#endif
#ifdef LOG_LOCAL4
    {"local4",	LOG_LOCAL4},
#endif
#ifdef LOG_LOCAL5
    {"local5",	LOG_LOCAL5},
#endif
#ifdef LOG_LOCAL6
    {"local6",	LOG_LOCAL6},
#endif
#ifdef LOG_LOCAL7
    {"local7",	LOG_LOCAL7},
#endif
    {NULL,		-1},
};
#endif

static const TRANS priorities[] = {
    {"emerg",	APLOG_EMERG},
    {"alert",	APLOG_ALERT},
    {"crit",	APLOG_CRIT},
    {"error",	APLOG_ERR},
    {"warn",	APLOG_WARNING},
    {"notice",	APLOG_NOTICE},
    {"info",	APLOG_INFO},
    {"debug",	APLOG_DEBUG},
    {NULL,	-1},
};

static apr_file_t *stderr_log;

AP_DECLARE(void) ap_open_stderr_log(apr_pool_t *p)
{
    apr_file_open_stderr(&stderr_log, p);
}

static int log_child(apr_pool_t *p, const char *progname,
                     apr_file_t **fpin)
{
    /* Child process code for 'ErrorLog "|..."';
     * may want a common framework for this, since I expect it will
     * be common for other foo-loggers to want this sort of thing...
     */
    apr_status_t rc;
    apr_procattr_t *procattr;
    apr_proc_t *procnew;

#ifdef SIGHUP
    /* No concept of a child process on Win32 */
    apr_signal(SIGHUP, SIG_IGN);
#endif /* ndef SIGHUP */

    if (((rc = apr_procattr_create(&procattr, p)) == APR_SUCCESS) &&
        ((rc = apr_procattr_io_set(procattr,
                                  APR_FULL_BLOCK,
                                  APR_NO_PIPE,
                                  APR_NO_PIPE)) == APR_SUCCESS)) {
        char **args;
        const char *pname;
        
        apr_tokenize_to_argv(progname, &args, p);
        pname = apr_pstrdup(p, args[0]);
        procnew = (apr_proc_t *) apr_pcalloc(p, sizeof(*procnew));
        rc = apr_proc_create(procnew, pname, (const char * const *)args,
                                NULL, procattr, p);
    
        if (rc == APR_SUCCESS) {
            apr_pool_note_subprocess(p, procnew, kill_after_timeout);
            (*fpin) = procnew->in;
        }
    }

    return(rc);
}

static void open_error_log(server_rec *s, apr_pool_t *p)
{
    const char *fname;
    int rc;

    if (*s->error_fname == '|') {
	apr_file_t *dummy = NULL;

        /* This starts a new process... */
        rc = log_child (p, s->error_fname+1, &dummy);
        if (rc != APR_SUCCESS) {
	    ap_log_error(APLOG_MARK, APLOG_STARTUP, rc, NULL, 
                         "Couldn't start ErrorLog process");
            exit(1);
	}

        s->error_log = dummy;
    }

#ifdef HAVE_SYSLOG
    else if (!strncasecmp(s->error_fname, "syslog", 6)) {
	if ((fname = strchr(s->error_fname, ':'))) {
	    const TRANS *fac;

	    fname++;
	    for (fac = facilities; fac->t_name; fac++) {
		if (!strcasecmp(fname, fac->t_name)) {
		    openlog(ap_server_argv0, LOG_NDELAY|LOG_CONS|LOG_PID,
			    fac->t_val);
		    s->error_log = NULL;
		    return;
		}
	    }
	}
	else
	    openlog(ap_server_argv0, LOG_NDELAY|LOG_CONS|LOG_PID, LOG_LOCAL7);

	s->error_log = NULL;
    }
#endif
    else {
	fname = ap_server_root_relative(p, s->error_fname);
        rc = apr_file_open(&s->error_log, fname, 
                      APR_APPEND | APR_READ | APR_WRITE | APR_CREATE,
                      APR_OS_DEFAULT, p);
        if (rc != APR_SUCCESS) {
            ap_log_error(APLOG_MARK, APLOG_STARTUP, rc, NULL, 
                         "%s: could not open error log file %s.",
		         ap_server_argv0, fname);
            exit(1);
	}
    }
}

void ap_open_logs(server_rec *s_main, apr_pool_t *p)
{
    apr_status_t rc = APR_SUCCESS;
    server_rec *virt, *q;
    int replace_stderr;
    apr_file_t *errfile = NULL;

    open_error_log(s_main, p);

    replace_stderr = 1;
    if (s_main->error_log) {
        /* replace stderr with this new log */
        apr_file_flush(s_main->error_log);
        apr_file_open_stderr(&errfile, p);        
        if ((rc = apr_file_dup(&errfile, s_main->error_log, p)) != APR_SUCCESS) {
            ap_log_error(APLOG_MARK, APLOG_CRIT, rc, s_main,
                         "unable to replace stderr with error_log");
        } else {
            replace_stderr = 0;
        }
    }
    /* note that stderr may still need to be replaced with something
     * because it points to the old error log, or back to the tty
     * of the submitter.
     */
    if (replace_stderr && freopen("/dev/null", "w", stderr) == NULL) {
	ap_log_error(APLOG_MARK, APLOG_CRIT, errno, s_main,
	    "unable to replace stderr with /dev/null");
    }

    for (virt = s_main->next; virt; virt = virt->next) {
	if (virt->error_fname) {
	    for (q=s_main; q != virt; q = q->next)
		if (q->error_fname != NULL &&
		    strcmp(q->error_fname, virt->error_fname) == 0)
		    break;
	    if (q == virt)
		open_error_log(virt, p);
	    else 
		virt->error_log = q->error_log;
	}
	else
	    virt->error_log = s_main->error_log;
    }
}

AP_DECLARE(void) ap_error_log2stderr(server_rec *s) {
    apr_file_t *errfile = NULL;

    apr_file_open_stderr(&errfile, s->process->pool);        
    if (s->error_log != NULL) {
        apr_file_dup(&(s->error_log), errfile, s->process->pool);
    }
}

static void log_error_core(const char *file, int line, int level, 
                           apr_status_t status, const server_rec *s, 
                           const request_rec *r, apr_pool_t *pool,
                           const char *fmt, va_list args)
{
    char errstr[MAX_STRING_LEN];
    size_t len;
    apr_file_t *logf = NULL;
    const char *referer;

    if (s == NULL) {
	/*
	 * If we are doing stderr logging (startup), don't log messages that are
	 * above the default server log level unless it is a startup/shutdown
	 * notice
	 */
	if (((level & APLOG_LEVELMASK) != APLOG_NOTICE) &&
	    ((level & APLOG_LEVELMASK) > DEFAULT_LOGLEVEL))
	    return;
        logf = stderr_log;
    }
    else if (s->error_log) {
	/*
	 * If we are doing normal logging, don't log messages that are
	 * above the server log level unless it is a startup/shutdown notice
	 */
	if (((level & APLOG_LEVELMASK) != APLOG_NOTICE) && 
	    ((level & APLOG_LEVELMASK) > s->loglevel))
	    return;
	logf = s->error_log;
    }
#ifdef TPF
    else if (tpf_child) {
    /*
     * If we are doing normal logging, don't log messages that are
     * above the server log level unless it is a startup/shutdown notice
     */
    if (((level & APLOG_LEVELMASK) != APLOG_NOTICE) &&
        ((level & APLOG_LEVELMASK) > s->loglevel))
        return;
    logf = stderr;
    }
#endif /* TPF */
    else {
	/*
	 * If we are doing syslog logging, don't log messages that are
	 * above the server log level (including a startup/shutdown notice)
	 */
	if ((level & APLOG_LEVELMASK) > s->loglevel)
	    return;
	logf = NULL;
    }

    if (logf && ((level & APLOG_STARTUP) != APLOG_STARTUP)) {
	errstr[0] = '[';
	apr_ctime(errstr + 1, apr_time_now());
	errstr[1 + APR_CTIME_LEN - 1] = ']';
	errstr[1 + APR_CTIME_LEN    ] = ' ';
	len = 1 + APR_CTIME_LEN + 1;
    } else {
	len = 0;
    }

    if ((level & APLOG_STARTUP) != APLOG_STARTUP) {
        len += apr_snprintf(errstr + len, MAX_STRING_LEN - len,
	        "[%s] ", priorities[level & APLOG_LEVELMASK].t_name);
    }
#ifndef TPF
    if (file && (level & APLOG_LEVELMASK) == APLOG_DEBUG) {
#ifdef _OSD_POSIX
	char tmp[256];
	char *e = strrchr(file, '/');

	/* In OSD/POSIX, the compiler returns for __FILE__
	 * a string like: __FILE__="*POSIX(/usr/include/stdio.h)"
	 * (it even returns an absolute path for sources in
	 * the current directory). Here we try to strip this
	 * down to the basename.
	 */
	if (e != NULL && e[1] != '\0') {
	    apr_snprintf(tmp, sizeof(tmp), "%s", &e[1]);
	    e = &tmp[strlen(tmp)-1];
	    if (*e == ')')
		*e = '\0';
	    file = tmp;
	}
#endif /*_OSD_POSIX*/
	len += apr_snprintf(errstr + len, MAX_STRING_LEN - len,
		"%s(%d): ", file, line);
    }
#endif /* TPF */
    if (r && r->connection) {
	/* XXX: TODO: add a method of selecting whether logged client
	 * addresses are in dotted quad or resolved form... dotted
	 * quad is the most secure, which is why I'm implementing it
	 * first. -djg
	 */
	len += apr_snprintf(errstr + len, MAX_STRING_LEN - len,
		"[client %s] ", r->connection->remote_ip);
    }
    if (!(level & APLOG_NOERRNO)
	&& (status != 0)) {
        char buf[120];
	len += apr_snprintf(errstr + len, MAX_STRING_LEN - len,
		"(%d)%s: ", status, apr_strerror(status, buf, sizeof(buf)));
    }
    len += apr_vsnprintf(errstr + len, MAX_STRING_LEN - len, fmt, args);

    if (r && (referer = apr_table_get(r->headers_in, "Referer"))) {
        len += apr_snprintf(errstr + len, MAX_STRING_LEN - len,
                ", referer: %s", referer);
    }

    /* NULL if we are logging to syslog */
    if (logf) {
        /* Truncate for the terminator (as apr_snprintf does) */
        if (len > MAX_STRING_LEN - sizeof(APR_EOL_STR))
            len = MAX_STRING_LEN - sizeof(APR_EOL_STR);
        strcpy(errstr + len, APR_EOL_STR);
        apr_file_puts(errstr, logf);
	apr_file_flush(logf);
    }
#ifdef HAVE_SYSLOG
    else {
	syslog(level & APLOG_LEVELMASK, "%s", errstr);
    }
#endif
}
    
AP_DECLARE(void) ap_log_error(const char *file, int line, int level,
			      apr_status_t status, const server_rec *s, 
                              const char *fmt, ...)
{
    va_list args;

    va_start(args, fmt);
    log_error_core(file, line, level, status, s, NULL, NULL, fmt, args);
    va_end(args);
}

AP_DECLARE(void) ap_log_perror(const char *file, int line, int level,
			      apr_status_t status, apr_pool_t *p, 
                              const char *fmt, ...)
{
    va_list args;

    va_start(args, fmt);
    log_error_core(file, line, level, status, NULL, NULL, p, fmt, args);
    va_end(args);
}

AP_DECLARE(void) ap_log_rerror(const char *file, int line, int level,
			       apr_status_t status, const request_rec *r, 
                               const char *fmt, ...)
{
    va_list args;

    va_start(args, fmt);
    log_error_core(file, line, level, status, r->server, r, NULL, fmt, args);
    /*
     * IF the error level is 'warning' or more severe,
     * AND there isn't already error text associated with this request,
     * THEN make the message text available to ErrorDocument and
     * other error processors.  This can be disabled by stuffing
     * something, even an empty string, into the "error-notes" cell
     * before calling this routine.
     */
    va_end(args);
    va_start(args,fmt); 
    if (((level & APLOG_LEVELMASK) <= APLOG_WARNING)
	&& (apr_table_get(r->notes, "error-notes") == NULL)) {
	apr_table_setn(r->notes, "error-notes",
		      ap_escape_html(r->pool, apr_pvsprintf(r->pool, fmt, 
		      args)));
    }
    va_end(args);
}

void ap_log_pid(apr_pool_t *p, const char *fname)
{
    apr_file_t *pid_file = NULL;
    apr_finfo_t finfo;
    static pid_t saved_pid = -1;
    pid_t mypid;
    apr_status_t rv;

    if (!fname) 
	return;

    fname = ap_server_root_relative(p, fname);
    mypid = getpid();
    if (mypid != saved_pid 
         && apr_stat(&finfo, fname, APR_FINFO_MTIME, p) == APR_SUCCESS) {
      /* WINCH and HUP call this on each restart.
       * Only warn on first time through for this pid.
       *
       * XXX: Could just write first time through too, although
       *      that may screw up scripts written to do something
       *      based on the last modification time of the pid file.
       */
        ap_log_perror(APLOG_MARK, APLOG_NOERRNO|APLOG_WARNING, 0, p,
		     apr_psprintf(p,
                                 "pid file %s overwritten -- Unclean shutdown of previous Apache run?",
                     fname)
			       );
    }

    if ((rv = apr_file_open(&pid_file, fname, APR_WRITE | APR_CREATE | APR_TRUNCATE,
                      APR_UREAD | APR_UWRITE | APR_GREAD | APR_WREAD, p)) != APR_SUCCESS) {
        ap_log_error(APLOG_MARK, APLOG_ERR, rv, NULL, 
                     "could not create %s", fname);
        ap_log_error(APLOG_MARK, APLOG_NOERRNO|APLOG_ERR, 0, NULL, 
                     "%s: could not log pid to file %s",
		     ap_server_argv0, fname);
        exit(1);
    }
    apr_file_printf(pid_file, "%ld" APR_EOL_STR, (long)mypid);
    apr_file_close(pid_file);
    saved_pid = mypid;
}

AP_DECLARE(void) ap_log_assert(const char *szExp, const char *szFile, int nLine)
{
    char time_str[APR_CTIME_LEN];

    apr_ctime(time_str, apr_time_now());
    ap_log_error(APLOG_MARK, APLOG_STARTUP | APLOG_NOERRNO, 0, NULL,
                 "[%s] file %s, line %d, assertion \"%s\" failed",
	         time_str, szFile, nLine, szExp);
#ifndef WIN32
    /* unix assert does an abort leading to a core dump */
    abort();
#else
    exit(1);
#endif
}

/* piped log support */

#ifdef AP_HAVE_RELIABLE_PIPED_LOGS
/* forward declaration */
static void piped_log_maintenance(int reason, void *data, apr_wait_t status);

static int piped_log_spawn(piped_log *pl)
{
    int rc;
    apr_procattr_t *procattr;
    apr_proc_t *procnew = NULL;
    apr_status_t status;

#ifdef SIGHUP
    apr_signal(SIGHUP, SIG_IGN);
#endif
    if (((status = apr_procattr_create(&procattr, pl->p)) != APR_SUCCESS) ||
        ((status = apr_procattr_child_in_set(procattr, ap_piped_log_read_fd(pl), 
                                ap_piped_log_write_fd(pl)))  != APR_SUCCESS)) {
        char buf[120];
        /* Something bad happened, give up and go away. */
	ap_log_error(APLOG_MARK, APLOG_STARTUP | APLOG_NOERRNO, 0, NULL,
	    "piped_log_spawn: unable to setup child process '%s': %s",
	    pl->program, apr_strerror(status, buf, sizeof(buf)));
        rc = -1;
    }
    else {
        char **args;
        const char *pname;

        apr_tokenize_to_argv(pl->program, &args, pl->p);
        pname = apr_pstrdup(pl->p, args[0]);
        procnew = apr_pcalloc(pl->p, sizeof(apr_proc_t));
        rc = apr_proc_create(procnew, pname, (const char * const *) args,
                                NULL, procattr, pl->p);
    
        if (rc == APR_SUCCESS) {            
            /* pjr - This no longer happens inside the child, */
            /*   I am assuming that if apr_proc_create was  */
            /*   successful that the child is running.        */
            RAISE_SIGSTOP(PIPED_LOG_SPAWN); 
            pl->pid = procnew;
            ap_piped_log_write_fd(pl) = procnew->in;
            apr_proc_other_child_register(procnew, piped_log_maintenance, pl, 
                                    ap_piped_log_write_fd(pl), pl->p);
        }
    }
    
    return 0;
}


static void piped_log_maintenance(int reason, void *data, apr_wait_t status)
{
    piped_log *pl = data;
    apr_status_t stats;

    switch (reason) {
    case APR_OC_REASON_DEATH:
	pl->pid = NULL;
	apr_proc_other_child_unregister(pl);
	if (pl->program == NULL) {
	    /* during a restart */
	    break;
	}
        break;
    case APR_OC_REASON_LOST:
	pl->pid = NULL;
	apr_proc_other_child_unregister(pl);
	if (pl->program == NULL) {
	    /* during a restart */
	    break;
	}
	if ((stats = piped_log_spawn(pl)) != APR_SUCCESS) {
	    /* what can we do?  This could be the error log we're having
	     * problems opening up... */
            char buf[120];
	    ap_log_error(APLOG_MARK, APLOG_STARTUP | APLOG_NOERRNO, 0, NULL,
		"piped_log_maintenance: unable to respawn '%s': %s",
		pl->program, apr_strerror(stats, buf, sizeof(buf)));
	}
	break;
    
    case APR_OC_REASON_UNWRITABLE:
        /* We should not kill off the pipe here, since it may only be full.
         * If it really is locked, we should kill it off manually. */
	break;
    
    case APR_OC_REASON_RESTART:
	pl->program = NULL;
	if (pl->pid != NULL) {
	    apr_proc_kill(pl->pid, SIGTERM);
	}
	break;

    case APR_OC_REASON_UNREGISTER:
	break;
    }
}


static apr_status_t piped_log_cleanup(void *data)
{
    piped_log *pl = data;

    if (pl->pid != NULL) {
	apr_proc_kill(pl->pid, SIGTERM);
    }
    apr_proc_other_child_unregister(pl);
    apr_file_close(ap_piped_log_read_fd(pl));
    apr_file_close(ap_piped_log_write_fd(pl));
    return APR_SUCCESS;
}


static apr_status_t piped_log_cleanup_for_exec(void *data)
{
    piped_log *pl = data;

    apr_file_close(ap_piped_log_read_fd(pl));
    apr_file_close(ap_piped_log_write_fd(pl));
    return APR_SUCCESS;
}

AP_DECLARE(piped_log *) ap_open_piped_log(apr_pool_t *p, const char *program)
{
    piped_log *pl;

    pl = apr_palloc(p, sizeof (*pl));
    pl->p = p;
    pl->program = apr_pstrdup(p, program);
    pl->pid = NULL;
    if (apr_file_pipe_create(&ap_piped_log_read_fd(pl), &ap_piped_log_write_fd(pl), p) != APR_SUCCESS) {
	return NULL;
    }
    apr_pool_cleanup_register(p, pl, piped_log_cleanup, piped_log_cleanup_for_exec);
    if (piped_log_spawn(pl) == -1) {
	int save_errno = errno;
	apr_pool_cleanup_kill(p, pl, piped_log_cleanup);
	apr_file_close(ap_piped_log_read_fd(pl));
	apr_file_close(ap_piped_log_write_fd(pl));
	errno = save_errno;
	return NULL;
    }
    return pl;
}

#else

static apr_status_t piped_log_cleanup(void *data)
{
    piped_log *pl = data;

    apr_file_close(ap_piped_log_write_fd(pl));
    return APR_SUCCESS;
}

AP_DECLARE(piped_log *) ap_open_piped_log(apr_pool_t *p, const char *program)
{
    piped_log *pl;
    apr_file_t *dummy = NULL;
    int rc;

    rc = log_child(p, program, &dummy);
    if (rc != APR_SUCCESS) {
	ap_log_error(APLOG_MARK, APLOG_STARTUP, rc, NULL, 
                     "Couldn't start piped log process");
	exit (1);
    }

    pl = apr_palloc(p, sizeof (*pl));
    pl->p = p;
    ap_piped_log_read_fd(pl) = NULL;
    ap_piped_log_write_fd(pl) = dummy;
    apr_pool_cleanup_register(p, pl, piped_log_cleanup, piped_log_cleanup);

    return pl;
}

#endif

AP_DECLARE(void) ap_close_piped_log(piped_log *pl)
{
    apr_pool_cleanup_run(pl->p, pl, piped_log_cleanup);
}

