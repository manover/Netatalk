/*
 * $Id: comm.c,v 1.1.4.2 2003-09-20 02:47:21 bfernhomberg Exp $
 *
 * Copyright (C) Joerg Lenneis 2003
 * All Rights Reserved.  See COPYRIGHT.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif /* HAVE_CONFIG_H */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif /* HAVE_UNISTD_H */
#include <sys/param.h>
#include <sys/socket.h>
#ifdef HAVE_SYS_TYPES_H
#include <sys/types.h>
#endif /* HAVE_SYS_TYPES_H */
#ifdef HAVE_SYS_TIME_H
#include <sys/time.h>
#endif /* HAVE_SYS_TIME_H */
#ifdef HAVE_SYS_UIO_H
#include <sys/uio.h>
#endif /* HAVE_SYS_UIO_H */


#include <assert.h>
#include <time.h>

#include <atalk/logger.h>
#include <atalk/cnid_dbd_private.h>

#include "db_param.h"
#include "usockfd.h"
#include "comm.h"

struct connection {
    time_t tm;                    /* When respawned last */
    int    fd;
};

static int   usock_fd;
static int   cur_fd;
static struct connection *fd_table;
static int  fd_table_size;
static int  fds_in_use = 0;


static void invalidate_fd(int fd)
{
    int i;

    if (fd == usock_fd)
        return;
    for (i = 0; i != fds_in_use; i++) 
	if (fd_table[i].fd == fd)
	    break;
    
    assert(i < fds_in_use);

    fds_in_use--;
    fd_table[i] = fd_table[fds_in_use];
    fd_table[fds_in_use].fd = -1;
    close(fd);    
    return;
}

static int recv_cred(int fd)
{
int ret;
struct msghdr msgh; 
struct iovec iov[1];
struct cmsghdr *cmsgp = NULL;
char buf[CMSG_SPACE(sizeof(int))];
char dbuf[80];

    memset(&msgh,0,sizeof(msgh));
    memset(buf,0,sizeof(buf));

    msgh.msg_name = NULL;
    msgh.msg_namelen = 0;

    msgh.msg_iov = iov;
    msgh.msg_iovlen = 1;

    iov[0].iov_base = dbuf;
    iov[0].iov_len = sizeof(dbuf);

    msgh.msg_control = buf;
    msgh.msg_controllen = sizeof(buf);

    do  {
          ret = recvmsg(fd ,&msgh,0);
    } while ( ret == -1 && errno == EINTR );

    if ( ret == -1 ) {
        return -1;
    }
      
    for ( cmsgp = CMSG_FIRSTHDR(&msgh); cmsgp != NULL; cmsgp = CMSG_NXTHDR(&msgh,cmsgp) ) {
        if ( cmsgp->cmsg_level == SOL_SOCKET && cmsgp->cmsg_type == SCM_RIGHTS ) {
              return *(int *) CMSG_DATA(cmsgp);
        }
    }

    if ( ret == sizeof (int) )
       errno = *(int *)dbuf; /* Rcvd errno */
    else
       errno = ENOENT;    /* Default errno */
   
    return -1;
}

/*
 *  Check for client requests. We keep up to fd_table_size open descriptors in
 *  fd_table. If the table is full and we get a request for a new descriptor via
 *  usock_fd, we close a random decriptor in the table to make space. The
 *  affected client will automatically reconnect. For an EOF (descriptor is
 *  closed by the client, so a read here returns 0) comm_rcv will take care of
 *  things and clean up fd_table. The same happens for any read/write errors.
 */

static int check_fd()
{
    int fd;
    fd_set readfds;
    struct timeval tv;
    int ret;
    int i;
    int maxfd = usock_fd;
    time_t t;
    
    FD_ZERO(&readfds);
    FD_SET(usock_fd, &readfds);
    
    for (i = 0; i != fds_in_use; i++) {
	FD_SET(fd_table[i].fd, &readfds);
	if (maxfd < fd_table[i].fd)
	    maxfd = fd_table[i].fd;
    }

    tv.tv_usec = 0;
    tv.tv_sec  = 1;
    if ((ret = select(maxfd + 1, &readfds, NULL, NULL, &tv)) < 0) {
        if (errno == EINTR)
            return 0;
        LOG(log_error, logtype_cnid, "error in select: %s",strerror(errno));
        return -1;
    }

    if (!ret)
	return 0;

    time(&t);

    if (FD_ISSET(usock_fd, &readfds)) {
	int    l = 0;
	
        fd = recv_cred(usock_fd);
        if (fd < 0) {
            return -1;
        }
	if (fds_in_use < fd_table_size) {
	    fd_table[fds_in_use].fd = fd;
	    fd_table[fds_in_use].tm = t;
	    fds_in_use++;
	} else {
	    time_t older = t;
	     
	    for (i = 0; i != fds_in_use; i++) {
	        if (older <= fd_table[i].tm) {
	            older = fd_table[i].tm;
	            l = i;
	        }
	    }
	    close(fd_table[l].fd);
	    fd_table[l].fd = fd;
	    fd_table[l].tm = t;
	}
	return 0;
    }

    for (i = 0; i != fds_in_use; i++) {
	if (FD_ISSET(fd_table[i].fd, &readfds)) {
	    fd_table[i].tm = t;
	    return fd_table[i].fd;
	}
    }	    
    /* We should never get here */
    return 0;
}

int comm_init(struct db_param *dbp)
{
    int i;

    fds_in_use = 0;
    fd_table_size = dbp->fd_table_size;
    
    if ((fd_table = malloc(fd_table_size * sizeof(struct connection))) == NULL) {
        LOG(log_error, logtype_cnid, "Out of memory");
	return -1;
    }
    for (i = 0; i != fd_table_size; i++)
	fd_table[i].fd = -1;
    /* from dup2 */
    usock_fd = 0;
#if 0
    int b = 1;
    /* this one dump core in recvmsg, great */
    if ( setsockopt(usock_fd, SOL_SOCKET, SO_PASSCRED, &b, sizeof (b)) < 0) {
        LOG(log_error, logtype_cnid, "setsockopt SO_PASSCRED %s",  strerror(errno));
	return -1;
    }
#endif
    /* push the first from dup2 */
    fd_table[fds_in_use].fd = 1;
    fds_in_use++;
    
    return 0;
}

/* ------------ 
   nbe of clients
*/
int comm_nbe(void)
{
    return fds_in_use;
}

/* ------------ */
int comm_rcv(struct cnid_dbd_rqst *rqst)
{
    char *nametmp;
    int b;

    if ((cur_fd = check_fd()) < 0)
        return -1;

    if (!cur_fd)
        return 0;
    nametmp = rqst->name;
    if ((b = read(cur_fd, rqst, sizeof(struct cnid_dbd_rqst))) != sizeof(struct cnid_dbd_rqst)) {
	if (b)
	    LOG(log_error, logtype_cnid, "error reading message header: %s", strerror(errno));
        invalidate_fd(cur_fd);
        rqst->name = nametmp;
        return 0;
    }
    rqst->name = nametmp;
    if (rqst->namelen && read(cur_fd, rqst->name, rqst->namelen) != rqst->namelen) {
        LOG(log_error, logtype_cnid, "error reading message name: %s", strerror(errno));
        invalidate_fd(cur_fd);
        return 0;
    }
    /* We set this to make life easier for logging. None of the other stuff
       needs zero terminated strings. */
    rqst->name[rqst->namelen] = '\0';
        
    return 1;
}


int comm_snd(struct cnid_dbd_rply *rply)
{
    if (write(cur_fd, rply, sizeof(struct cnid_dbd_rply)) != sizeof(struct cnid_dbd_rply)) {
        LOG(log_error, logtype_cnid, "error writing message header: %s", strerror(errno));
        invalidate_fd(cur_fd);
        return 0;
    }
    if (rply->namelen && write(cur_fd, rply->name, rply->namelen) != rply->namelen) {
        LOG(log_error, logtype_cnid, "error writing message name: %s", strerror(errno));
        invalidate_fd(cur_fd);
        return 0;
    }
    return 1;
}


