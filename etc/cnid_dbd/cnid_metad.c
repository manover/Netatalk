/*
 * $Id: cnid_metad.c,v 1.1.4.2 2003-09-20 02:47:21 bfernhomberg Exp $
 *
 * Copyright (C) Joerg Lenneis 2003
 * All Rights Reserved.  See COPYRIGHT.
 *
 */

/* cnid_dbd metadaemon to start up cnid_dbd upon request from afpd */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif /* HAVE_CONFIG_H */

#include <stdlib.h>

#ifdef HAVE_UNISTD_H
#define __USE_GNU
#include <unistd.h>
#undef __USE_GNU
#endif /* HAVE_UNISTD_H */
#include <sys/param.h>
#include <errno.h>
#include <string.h>
#include <signal.h>
#ifdef HAVE_SYS_TYPES_H
#include <sys/types.h>
#endif
#ifdef HAVE_SYS_TIME_H
#include <sys/time.h>
#endif
#ifdef HAVE_SYS_WAIT_H
#include <sys/wait.h>
#endif
#ifdef HAVE_SYS_UIO_H
#include <sys/uio.h>
#endif
#include <sys/un.h>
#include <sys/socket.h>
#include <stdio.h>
#include <time.h>

#ifndef WEXITSTATUS 
#define WEXITSTATUS(stat_val) ((unsigned)(stat_val) >> 8)
#endif /* ! WEXITSTATUS */
#ifndef WIFEXITED
#define WIFEXITED(stat_val) (((stat_val) & 255) == 0)
#endif /* ! WIFEXITED */
#ifndef WIFSTOPPED
#define WIFSTOPPED(status) (((status) & 0xff) == 0x7f)
#endif

#ifndef WIFSIGNALED
#define WIFSIGNALED(status) (!WIFSTOPPED(status) && !WIFEXITED(status))
#endif
#ifndef WTERMSIG
#define WTERMSIG(status)      ((status) & 0x7f)
#endif

#ifdef ATACC
#define fork aTaC_fork
#endif

/* functions for username and group */
#include <pwd.h>
#include <grp.h>

#include <atalk/logger.h>
#include <atalk/cnid_dbd_private.h>

#include "db_param.h"
#include "usockfd.h"

#define DBHOME        ".AppleDB"
#define DBHOMELEN    8      

static int srvfd;
static int rqstfd;

#define MAXSRV 20

#define MAXSPAWN   3                   /* Max times respawned in.. */
#define TESTTIME   20                  /* this much seconds */

struct server {
    char  *name;
    pid_t pid;
    time_t tm;                    /* When respawned last */
    int count;                    /* Times respawned in the last TESTTIME secondes */
    int   sv[2];
};

static struct server srv[MAXSRV +1];

static struct server *test_usockfn(char *dir, char *fn)
{
int i;
    for (i = 1; i <= MAXSRV; i++) {
        if (srv[i].name && !strcmp(srv[i].name, dir)) {
            return &srv[i];
        }
    }
    return NULL;
}

/* -------------------- */
static int send_cred(int socket, int fd)
{
   int ret;
   struct msghdr msgh; 
   struct iovec iov[1];
   struct cmsghdr *cmsgp = NULL;
   char buf[CMSG_SPACE(sizeof fd)];
   int er=0;

   memset(&msgh,0,sizeof (msgh));
   memset(buf,0,sizeof (buf));

   msgh.msg_name = NULL;
   msgh.msg_namelen = 0;

   msgh.msg_iov = iov;
   msgh.msg_iovlen = 1;

   iov[0].iov_base = &er;
   iov[0].iov_len = sizeof(er);

   msgh.msg_control = buf;
   msgh.msg_controllen = sizeof(buf);

   cmsgp = CMSG_FIRSTHDR(&msgh);
   cmsgp->cmsg_level = SOL_SOCKET;
   cmsgp->cmsg_type = SCM_RIGHTS;
   cmsgp->cmsg_len = CMSG_LEN(sizeof(fd));

   *((int *)CMSG_DATA(cmsgp)) = fd;
   msgh.msg_controllen = cmsgp->cmsg_len;

   do  {
       ret = sendmsg(socket,&msgh, 0);
   } while ( ret == -1 && errno == EINTR );
   if (ret == -1) {
       LOG(log_error, logtype_cnid, "error in sendmsg: %s", strerror(errno));
       return -1;
   }
   return 0;
}

/* -------------------- */
static int maybe_start_dbd(char *dbdpn, char *dbdir, char *usockfn)
{
    pid_t pid;
    struct server *up;
    int i;
    time_t t;

    up = test_usockfn(dbdir, usockfn);
    if (up && up->pid) {
       /* we already have a process, send our fd */
       if (send_cred(up->sv[0], rqstfd) < 0) {
           /* FIXME */
           return -1;
       }
       return 0;
    }

    time(&t);
    if (!up) {
        /* find an empty slot */
        for (i = 1; i <= MAXSRV; i++) {
            if (!srv[i].pid && srv[i].tm + TESTTIME < t) {
                up = &srv[i];
                free(up->name);
                up->tm = t;
                up->count = 0;
                /* copy name */
                up->name = strdup(dbdir);
                break;
            }
        }
        if (!up) {
	    LOG(log_error, logtype_cnid, "no free slot");
	    return -1;
        }
    }
    else {
        /* we have a slot but no process, check for respawn too fast */
        if (up->tm + TESTTIME > t) {
            up->count++;
        } else {
            up->count = 0;
            up->tm = t;
        }
        if (up->count >= MAXSPAWN) {
            up->tm = t;
	    LOG(log_error, logtype_cnid, "respawn too fast %s", up->name);
	    /* FIXME should we sleep a little ? */
	    return -1;
        }
        
    }
    /* create socketpair for comm between parent and child 
     * FIXME Do we really need a permanent pipe between them ?
     */
    if (socketpair(PF_UNIX, SOCK_STREAM, 0, up->sv) < 0) {
	LOG(log_error, logtype_cnid, "error in fork: %s", strerror(errno));
	return -1;
    }
        
    if ((pid = fork()) < 0) {
	LOG(log_error, logtype_cnid, "error in fork: %s", strerror(errno));
	return -1;
    }    
    if (pid == 0) {
	/*
	 *  Child. Close descriptors and start the daemon. If it fails
	 *  just log it. The client process will fail connecting
	 *  afterwards anyway.
	 */
	close(0);
	close(1);
	close(srvfd);
	dup2(up->sv[1], 0);
	dup2(rqstfd, 1);

	close(up->sv[0]);
	close(up->sv[1]);
	close(rqstfd);
	if (execlp(dbdpn, dbdpn, dbdir, NULL) < 0) {
	    LOG(log_error, logtype_cnid, "Fatal error in exec: %s", strerror(errno));
	    exit(0);
	}
    }
    /*
     *  Parent.
     */
    up->pid = pid;
    return 0;
}

/* ------------------ */
static int set_dbdir(char *dbdir, int len)
{
   struct stat st;

    if (!len)
        return -1;

    if (stat(dbdir, &st) < 0 && mkdir(dbdir, 0755) < 0) {
        LOG(log_error, logtype_cnid, "set_dbdir: mkdir failed for %s", dbdir);
        return -1;
    }

    if (dbdir[len - 1] != '/') {
         strcat(dbdir, "/");
         len++;
    }
    strcpy(dbdir + len, DBHOME);
    if (stat(dbdir, &st) < 0 && mkdir(dbdir, 0755 ) < 0) {
        LOG(log_error, logtype_cnid, "set_dbdir: mkdir failed for %s", dbdir);
        return -1;
    }
    return 0;   
}

/* ------------------ */
uid_t user_to_uid ( username )
char    *username;
{
    struct passwd *this_passwd;
 
    /* check for anything */
    if ( !username || strlen ( username ) < 1 ) return 0;
 
    /* grab the /etc/passwd record relating to username */
    this_passwd = getpwnam ( username );
 
    /* return false if there is no structure returned */
    if (this_passwd == NULL) return 0;
 
    /* return proper uid */
    return this_passwd->pw_uid;
 
} 

/* ------------------ */
gid_t group_to_gid ( group )
char    *group;
{
    struct group *this_group;
 
    /* check for anything */
    if ( !group || strlen ( group ) < 1 ) return 0;
 
    /* grab the /etc/groups record relating to group */
    this_group = getgrnam ( group );
 
    /* return false if there is no structure returned */
    if (this_group == NULL) return 0;
 
    /* return proper gid */
    return this_group->gr_gid;
 
}

/* ------------------ */
int main(int argc, char *argv[])
{
    char  dbdir[MAXPATHLEN + 1];
    int   len;
    pid_t pid;
    int   status;
    char  *dbdpn = NULL;
    char  *host = NULL;
    int   port = 0;
    struct db_param *dbp;
    int    i;
    int    cc;
    uid_t  uid = 0;
    gid_t  gid = 0;
    int    err = 0;
    int    debug = 0;
    
    while (( cc = getopt( argc, argv, "ds:p:h:u:g:")) != -1 ) {
        switch (cc) {
        case 'd':
            debug = 1;
            break;
        case 'h':
            host = strdup(optarg);  
            break;
        case 'u':
            uid = user_to_uid (optarg);
            if (!uid) {
                LOG(log_error, logtype_cnid, "main: bad user %s", optarg);
                err++;
            }
            break;
        case 'g':
            gid =group_to_gid (optarg);
            if (!gid) {
                LOG(log_error, logtype_cnid, "main: bad group %s", optarg);
                err++;
            }
            break;
        case 'p':
            port = atoi(optarg);
            break;
        case 's':
            dbdpn = strdup(optarg);
            break;
        default:
            err++;
            break;
        }
    }
    
    if (err || !host || !port || !dbdpn) {
        LOG(log_error, logtype_cnid, "main: bad arguments");
        exit(1);
    }
    
    if (!debug) {
 
        switch (fork()) {
        case 0 :
            fclose(stdin);
            fclose(stdout);
            fclose(stderr);

#ifdef TIOCNOTTY
            {
    	        int i;
                if (( i = open( "/dev/tty", O_RDWR )) >= 0 ) {
                    (void)ioctl( i, TIOCNOTTY, 0 );
                    setpgid( 0, getpid());
                    (void) close(i);
                }
            }
#else
            setpgid( 0, getpid());
#endif
           break;
        case -1 :  /* error */
            LOG(log_error, logtype_cnid, "detach from terminal: %s", strerror(errno));
            exit(1);
        default :  /* server */
            exit(0);
        }
    }

    if ((srvfd = tsockfd_create(host, port, 10)) < 0)
        exit(1);
    /* switch uid/gid */
    if (uid || gid) {

        LOG(log_info, logtype_cnid, "Setting uid/gid to %i/%i", uid, gid);
        if (gid) {
            if (setresgid(gid,gid,gid) < 0 || setgid(gid) < 0) {
                LOG(log_info, logtype_cnid, "unable to switch to group %d", gid);
                exit(1);
            }
        }
        if (uid) {
            if (setresuid(uid,uid,uid) < 0 || setuid(uid) < 0) {
                LOG(log_info, logtype_cnid, "unable to switch to user %d", uid);
                exit(1);
            }
        }
    }

    signal(SIGPIPE, SIG_IGN);

    while (1) {
	/* Collect zombie processes and log what happened to them */       
        while ((pid = waitpid(-1, &status, WNOHANG)) > 0) {
           for (i = 1; i <= MAXSRV; i++) {
               if (srv[i].pid == pid) {
                   srv[i].pid = 0;
#if 0                   
                   free(srv[i].name);
#endif                   
                   close(srv[i].sv[0]);
                   close(srv[i].sv[1]);
                   break;
               }
            }
	    if (WIFEXITED(status)) {
		LOG(log_info, logtype_cnid, "cnid_dbd pid %i exited with exit code %i", 
		    pid, WEXITSTATUS(status));
	    }
	    else if (WIFSIGNALED(status)) {
		LOG(log_info, logtype_cnid, "cnid_dbd pid %i exited with signal %i", 
		    pid, WTERMSIG(status));
	    }
	    /* FIXME should */
	    
	}
        if ((rqstfd = usockfd_check(srvfd, 10000000)) <= 0)
            continue;
        /* TODO: Check out read errors, broken pipe etc. in libatalk. Is
           SIGIPE ignored there? Answer: Ignored for dsi, but not for asp ... */
        if (read(rqstfd, &len, sizeof(int)) != sizeof(int)) {
            LOG(log_error, logtype_cnid, "error/short read: %s", strerror(errno));
            goto loop_end;
        }
        /*
         *  checks for buffer overruns. The client libatalk side does it too 
         *  before handing the dir path over but who trusts clients?
         */
        if (!len || len +DBHOMELEN +2 > MAXPATHLEN) {
            LOG(log_error, logtype_cnid, "wrong len parameter: %d", len);
            goto loop_end;
        }
        if (read(rqstfd, dbdir, len) != len) {
            LOG(log_error, logtype_cnid, "error/short read (dir): %s", strerror(errno));
            goto loop_end;
        }
        dbdir[len] = '\0';
        
        if (set_dbdir(dbdir, len) < 0) {
            goto loop_end;
        }
        
        if ((dbp = db_param_read(dbdir)) == NULL) {
            LOG(log_error, logtype_cnid, "Error reading config file");
            goto loop_end;
        }
	maybe_start_dbd(dbdpn, dbdir, dbp->usock_file);

    loop_end:
        close(rqstfd);
    }
}
