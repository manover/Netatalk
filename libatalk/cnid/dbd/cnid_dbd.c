/*
 * $Id: cnid_dbd.c,v 1.1.4.3 2003-09-22 07:21:34 bfernhomberg Exp $
 *
 * Copyright (C) Joerg Lenneis 2003
 * All Rights Reserved.  See COPYRIGHT.
 */

/*
 *  Replacement functions for cnid_xxx if we use the cnid_dbd database
 *  daemon. Basically we just check parameters for obvious errors and then pass
 *  the request to the daemon for further processing.
 *  
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif /* HAVE_CONFIG_H */

#ifdef CNID_BACKEND_DBD

#include <stdlib.h>
#ifdef HAVE_SYS_STAT_H
#include <sys/stat.h>
#endif /* HAVE_SYS_STAT_H */
#ifdef HAVE_SYS_UIO_H
#include <sys/uio.h>
#endif /* HAVE_SYS_UIO_H */
#include <sys/time.h>
#include <sys/un.h>
#include <sys/socket.h>
#include <sys/param.h>
#include <errno.h>
#include <netinet/in.h>
#include <net/if.h>
#include <netinet/tcp.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <errno.h>
#include <netdb.h>

#include <netatalk/endian.h>
#include <atalk/logger.h>
#include <atalk/adouble.h>
#include <atalk/cnid.h>
#include "cnid_dbd.h"
#include <atalk/cnid_dbd_private.h>

#define RQST_RESET(r) do { (r).namelen = 0; } while (0)

/* ----------- */
extern char             *Cnid_srv;
extern int              Cnid_port;

static int tsock_getfd(char *host, int port, int silent)
{
int sock;
struct sockaddr_in server;
struct hostent* hp;
int attr;
 
    server.sin_family=AF_INET;
    server.sin_port=htons((unsigned short)port);
    if (!host) {
        LOG(log_error, logtype_cnid, "transmit: -cnidserver not defined");
        return -1;
    }
    
    hp=gethostbyname(host);
    if (!hp) {
        unsigned long int addr=inet_addr(host);
        if (addr!= (unsigned)-1)
            hp=gethostbyaddr((char*)addr,sizeof(addr),AF_INET);
 
    	if (!hp) {
            if (!silent)
                LOG(log_error, logtype_cnid, "transmit: get_fd %s: %s", host, strerror(errno));
    	    return(-1);
    	}
    }
    memcpy((char*)&server.sin_addr,(char*)hp->h_addr,sizeof(server.sin_addr));
    sock=socket(PF_INET,SOCK_STREAM,0);
    if (sock==-1) {
        if (!silent)
            LOG(log_error, logtype_cnid, "transmit: socket %s: %s", host, strerror(errno));
    	return(-1);
    }
    attr = 1;
    setsockopt(sock, SOL_TCP, TCP_NODELAY, &attr, sizeof(attr));
    if(connect(sock ,(struct sockaddr*)&server,sizeof(server))==-1) {
        struct timeval tv;
        switch (errno) {
        case ENETUNREACH:
        case ECONNREFUSED: 
            
            tv.tv_usec = 0;
            tv.tv_sec  = 5;
            select(0, NULL, NULL, NULL, &tv);
            break;
        }
    	close(sock);
    	sock=-1;
        if (!silent)
            LOG(log_error, logtype_cnid, "transmit: connect %s: %s", host, strerror(errno));
    }
    return(sock);
}

/* --------------------- */
static int init_tsockfn(CNID_private *db)
{
    int fd;
    int len;
    
    if ((fd = tsock_getfd(Cnid_srv, Cnid_port, 0)) < 0) 
        return -1;
    len = strlen(db->db_dir);
    if (write(fd, &len, sizeof(int)) != sizeof(int)) {
        LOG(log_error, logtype_cnid, "get_usock: Error/short write: %s", strerror(errno));
        close(fd);
        return -1;
    }
    if (write(fd, db->db_dir, len) != len) {
        LOG(log_error, logtype_cnid, "get_usock: Error/short write dir: %s", strerror(errno));
        close(fd);
        return -1;
    }
    return fd;
}

/* --------------------- */
static int send_packet(CNID_private *db, struct cnid_dbd_rqst *rqst)
{
    struct iovec iov[2];
    size_t towrite;
    ssize_t len;
  
    iov[0].iov_base = rqst;
    iov[0].iov_len  = sizeof(struct cnid_dbd_rqst);
    
    if (!rqst->namelen) {
        if (write(db->fd, rqst, sizeof(struct cnid_dbd_rqst)) != sizeof(struct cnid_dbd_rqst)) {
            return -1;
        }
        return 0;
    }
    iov[1].iov_base = rqst->name;
    iov[1].iov_len  = rqst->namelen;

    towrite = sizeof(struct cnid_dbd_rqst) +rqst->namelen;
    while (towrite > 0) {
        if (((len = writev(db->fd, iov, 2)) == -1 && errno == EINTR) || !len)
            continue;
 
        if (len == towrite) /* wrote everything out */
            break;
        else if (len < 0) { /* error */
            return -1;
        }
 
        towrite -= len;
        if (towrite > rqst->namelen) { /* skip part of header */
            iov[0].iov_base = (char *) iov[0].iov_base + len;
            iov[0].iov_len -= len;
        } else { /* skip to data */
            if (iov[0].iov_len) {
                len -= iov[0].iov_len;
                iov[0].iov_len = 0;
            }
            iov[1].iov_base = (char *) iov[1].iov_base + len;
            iov[1].iov_len -= len;
        }
    }
    return 0;
}

/* --------------------- */
static int transmit(CNID_private *db, struct cnid_dbd_rqst *rqst, struct cnid_dbd_rply *rply)
{
    char *nametmp;
    int  ret;
    
    while (1) {

        if (db->fd == -1 && (db->fd = init_tsockfn(db)) < 0) {
	    goto transmit_fail;
        }
        if (send_packet(db, rqst) < 0) {
            goto transmit_fail;
        }
        nametmp = rply->name;
        if ((ret = read(db->fd, rply, sizeof(struct cnid_dbd_rply))) != sizeof(struct cnid_dbd_rply)) {
            LOG(log_error, logtype_cnid, "transmit: Error reading header from fd for usock %s: %s",
                db->usock_file, ret == -1?strerror(errno):"closed");
            rply->name = nametmp;
            goto transmit_fail;
        }
        rply->name = nametmp;
        if (rply->namelen && (ret = read(db->fd, rply->name, rply->namelen)) != rply->namelen) {
            LOG(log_error, logtype_cnid, "transmit: Error reading name from fd for usock %s: %s",
                db->usock_file, ret == -1?strerror(errno):"closed");
	    goto transmit_fail;
        }
        return 0;
 
 transmit_fail:
        if (db->fd != -1) {
            close(db->fd);
        }
        db->fd = -1;
    }
    return -1;
}

static struct _cnid_db *cnid_dbd_new(const char *volpath)
{
    struct _cnid_db *cdb;
    
    if ((cdb = (struct _cnid_db *)calloc(1, sizeof(struct _cnid_db))) == NULL)
	return NULL;
	
    if ((cdb->volpath = strdup(volpath)) == NULL) {
        free(cdb);
	return NULL;
    }
    
    cdb->flags = CNID_FLAG_PERSISTENT;
    
    cdb->cnid_add = cnid_dbd_add;
    cdb->cnid_delete = cnid_dbd_delete;
    cdb->cnid_get = cnid_dbd_get;
    cdb->cnid_lookup = cnid_dbd_lookup;
    cdb->cnid_nextid = NULL;
    cdb->cnid_resolve = cnid_dbd_resolve;
    cdb->cnid_getstamp = cnid_dbd_getstamp;
    cdb->cnid_update = cnid_dbd_update;
    cdb->cnid_close = cnid_dbd_close;
    
    return cdb;
}

/* ---------------------- */
struct _cnid_db *cnid_dbd_open(const char *dir, mode_t mask)
{
    CNID_private *db = NULL;
    struct _cnid_db *cdb = NULL;

    if (!dir) {
         return NULL;
    }
    
    if ((cdb = cnid_dbd_new(dir)) == NULL) {
        LOG(log_error, logtype_default, "cnid_open: Unable to allocate memory for database");
	return NULL;
    }
        
    if ((db = (CNID_private *)calloc(1, sizeof(CNID_private))) == NULL) {
        LOG(log_error, logtype_cnid, "cnid_open: Unable to allocate memory for database");
        goto cnid_dbd_open_fail;
    }
    
    cdb->_private = db;

    /* We keep a copy of the directory in the db structure so that we can
       transparently reconnect later. */
    strcpy(db->db_dir, dir);
    db->usock_file[0] = '\0';
    db->magic = CNID_DB_MAGIC;
    db->fd = -1;
#ifdef DEBUG
    LOG(log_info, logtype_cnid, "opening database connection to %s", db->db_dir); 
#endif
    return cdb;

cnid_dbd_open_fail:
    if (cdb != NULL) {
	if (cdb->volpath != NULL) {
	    free(cdb->volpath);
	}
	free(cdb);
    }
    if (db != NULL)
	free(db);
	
    return NULL;
}

/* ---------------------- */
void cnid_dbd_close(struct _cnid_db *cdb)
{
    CNID_private *db;

    if (!cdb) {
        LOG(log_error, logtype_afpd, "cnid_close called with NULL argument !");
	return;
    }

    if ((db = cdb->_private) != NULL) {
#ifdef DEBUG 
        LOG(log_info, logtype_cnid, "closing database connection to %s", db->db_dir);
#endif  
	if (db->fd >= 0)
	    close(db->fd);
        free(db);
    }
    
    free(cdb->volpath);
    free(cdb);
    
    return;
}

/* ---------------------- */
cnid_t cnid_dbd_add(struct _cnid_db *cdb, const struct stat *st,
                const cnid_t did, const char *name, const int len,
                cnid_t hint)
{
    CNID_private *db;
    struct cnid_dbd_rqst rqst;
    struct cnid_dbd_rply rply;
    cnid_t id;

    if (!cdb || !(db = cdb->_private) || !st || !name) {
        LOG(log_error, logtype_cnid, "cnid_add: Parameter error");
        errno = CNID_ERR_PARAM;
        return CNID_INVALID;
    }

    if (len > MAXPATHLEN) {
        LOG(log_error, logtype_cnid, "cnid_add: Path name is too long");
        errno = CNID_ERR_PATH;
        return CNID_INVALID;
    }

    RQST_RESET(rqst);
    rqst.op = CNID_DBD_OP_ADD;
    rqst.dev = st->st_dev;
    rqst.ino = st->st_ino;
    rqst.type = S_ISDIR(st->st_mode)?1:0;
    rqst.did = did;
    rqst.name = name;
    rqst.namelen = len;

    if (transmit(db, &rqst, &rply) < 0) {
        errno = CNID_ERR_DB;
        return CNID_INVALID;
    }
    
    switch(rply.result) {
    case CNID_DBD_RES_OK:
        id = rply.cnid;
        break;
    case CNID_DBD_RES_ERR_MAX:
        errno = CNID_ERR_MAX;
        id = CNID_INVALID;
        break;
    case CNID_DBD_RES_ERR_DB:
    case CNID_DBD_RES_ERR_DUPLCNID:
        errno = CNID_ERR_DB;
        id = CNID_INVALID;
        break;
    default:
        abort();
    }
    return id;
}

/* ---------------------- */
cnid_t cnid_dbd_get(struct _cnid_db *cdb, const cnid_t did, const char *name,
                const int len)
{
    CNID_private *db;
    struct cnid_dbd_rqst rqst;
    struct cnid_dbd_rply rply;
    cnid_t id;


    if (!cdb || !(db = cdb->_private) || !name) {
        LOG(log_error, logtype_cnid, "cnid_get: Parameter error");
        errno = CNID_ERR_PARAM;        
        return CNID_INVALID;
    }

    if (len > MAXPATHLEN) {
        LOG(log_error, logtype_cnid, "cnid_add: Path name is too long");
        errno = CNID_ERR_PATH;
        return CNID_INVALID;
    }

    RQST_RESET(rqst);
    rqst.op = CNID_DBD_OP_GET;
    rqst.did = did;
    rqst.name = name;
    rqst.namelen = len;

    if (transmit(db, &rqst, &rply) < 0) {
        errno = CNID_ERR_DB;
        return CNID_INVALID;
    }
    
    switch(rply.result) {
    case CNID_DBD_RES_OK:
        id = rply.cnid;
        break;
    case CNID_DBD_RES_NOTFOUND:
        id = CNID_INVALID;
        break;
    case CNID_DBD_RES_ERR_DB:
        id = CNID_INVALID;
        errno = CNID_ERR_DB;
        break;
    default: 
        abort();
    }

    return id;
}

/* ---------------------- */
char *cnid_dbd_resolve(struct _cnid_db *cdb, cnid_t *id, void *buffer, u_int32_t len)
{
    CNID_private *db;
    struct cnid_dbd_rqst rqst;
    struct cnid_dbd_rply rply;
    char *name;

    if (!cdb || !(db = cdb->_private) || !id || !(*id)) {
        LOG(log_error, logtype_cnid, "cnid_resolve: Parameter error");
        errno = CNID_ERR_PARAM;                
        return NULL;
    }

    /* TODO: We should maybe also check len. At the moment we rely on the caller
       to provide a buffer that is large enough for MAXPATHLEN plus
       CNID_HEADER_LEN plus 1 byte, which is large enough for the maximum that
       can come from the database. */

    RQST_RESET(rqst);
    rqst.op = CNID_DBD_OP_RESOLVE;
    rqst.cnid = *id;

    /* This mimicks the behaviour of the "regular" cnid_resolve. So far,
       nobody uses the content of buffer. It only provides space for the
       name in the caller. */
    rply.name = buffer + CNID_HEADER_LEN;

    if (transmit(db, &rqst, &rply) < 0) {
        errno = CNID_ERR_DB;
        *id = CNID_INVALID;
        return NULL;
    }

    switch (rply.result) {
    case CNID_DBD_RES_OK:
        *id = rply.did;
        name = rply.name;
        break;
    case CNID_DBD_RES_NOTFOUND:
        *id = CNID_INVALID;
        name = NULL;
        break;
    case CNID_DBD_RES_ERR_DB:
        errno = CNID_ERR_DB;
        *id = CNID_INVALID;
        name = NULL;
        break;
    default:
        abort();
    }

    return name;
}

/* ---------------------- */
int cnid_dbd_getstamp(struct _cnid_db *cdb, void *buffer, u_int32_t len)
{
    CNID_private *db;
    struct cnid_dbd_rqst rqst;
    struct cnid_dbd_rply rply;
    cnid_t id = 0;
    char temp[12 + MAXPATHLEN + 1];

    if (!cdb || !(db = cdb->_private)) {
        LOG(log_error, logtype_cnid, "cnid_getstamp: Parameter error");
        errno = CNID_ERR_PARAM;                
        return -1;
    }

    /* TODO: We should maybe also check len. At the moment we rely on the caller
       to provide a buffer that is large enough for MAXPATHLEN plus
       CNID_HEADER_LEN plus 1 byte, which is large enough for the maximum that
       can come from the database. */

    RQST_RESET(rqst);
    rqst.op = CNID_DBD_OP_RESOLVE;
    rqst.cnid = id;

    memset(buffer, 0, len);

    /* This mimicks the behaviour of the "regular" cnid_resolve. So far,
       nobody uses the content of buffer. It only provides space for the
       name in the caller. */
    rply.name = temp + CNID_HEADER_LEN;

    if (transmit(db, &rqst, &rply) < 0) {
        errno = CNID_ERR_DB;
        return -1;
    }

    switch (rply.result) {
    case CNID_DBD_RES_OK:
        memcpy(buffer, &rply.did, sizeof(cnid_t));
        break;
    case CNID_DBD_RES_NOTFOUND:
        return -1;
    case CNID_DBD_RES_ERR_DB:
        errno = CNID_ERR_DB;
        return -1;
    default:
        abort();
    }

    return 0;
}

/* ---------------------- */
cnid_t cnid_dbd_lookup(struct _cnid_db *cdb, const struct stat *st, const cnid_t did,
                   const char *name, const int len)
{
    CNID_private *db;
    struct cnid_dbd_rqst rqst;
    struct cnid_dbd_rply rply;
    cnid_t id;

    if (!cdb || !(db = cdb->_private) || !st || !name) {
        LOG(log_error, logtype_cnid, "cnid_lookup: Parameter error");
        errno = CNID_ERR_PARAM;        
        return CNID_INVALID;
    }

    if (len > MAXPATHLEN) {
        LOG(log_error, logtype_cnid, "cnid_lookup: Path name is too long");
        errno = CNID_ERR_PATH;
        return CNID_INVALID;
    }

    RQST_RESET(rqst);
    rqst.op = CNID_DBD_OP_LOOKUP;
    rqst.dev = st->st_dev;
    rqst.ino = st->st_ino;
    rqst.type = S_ISDIR(st->st_mode)?1:0;
    rqst.did = did;
    rqst.name = name;
    rqst.namelen = len;

    if (transmit(db, &rqst, &rply) < 0) {
        errno = CNID_ERR_DB;
        return CNID_INVALID;
    }

    switch (rply.result) {
    case CNID_DBD_RES_OK:
        id = rply.cnid;
        break;
    case CNID_DBD_RES_NOTFOUND:
        id = CNID_INVALID;
        break;
    case CNID_DBD_RES_ERR_DB:
        errno = CNID_ERR_DB;
        id = CNID_INVALID;
        break;
    default:
        abort();
    }

    return id;
}

/* ---------------------- */
int cnid_dbd_update(struct _cnid_db *cdb, const cnid_t id, const struct stat *st,
                const cnid_t did, const char *name, const int len)
{
    CNID_private *db;
    struct cnid_dbd_rqst rqst;
    struct cnid_dbd_rply rply;

    
    if (!cdb || !(db = cdb->_private) || !id || !st || !name) {
        LOG(log_error, logtype_cnid, "cnid_update: Parameter error");
        errno = CNID_ERR_PARAM;        
        return -1;
    }

    if (len > MAXPATHLEN) {
        LOG(log_error, logtype_cnid, "cnid_update: Path name is too long");
        errno = CNID_ERR_PATH;
        return -1;
    }

    RQST_RESET(rqst);
    rqst.op = CNID_DBD_OP_UPDATE;
    rqst.cnid = id;
    rqst.dev = st->st_dev;
    rqst.ino = st->st_ino;
    rqst.type = S_ISDIR(st->st_mode)?1:0;
    rqst.did = did;
    rqst.name = name;
    rqst.namelen = len;

    if (transmit(db, &rqst, &rply) < 0) {
        errno = CNID_ERR_DB;
        return -1;
    }

    switch (rply.result) {
    case CNID_DBD_RES_OK:
    case CNID_DBD_RES_NOTFOUND:
        return 0;
    case CNID_DBD_RES_ERR_DB:
        errno = CNID_ERR_DB;
        return -1;
    default:
        abort();
    }
}

/* ---------------------- */
int cnid_dbd_delete(struct _cnid_db *cdb, const cnid_t id) 
{
    CNID_private *db;
    struct cnid_dbd_rqst rqst;
    struct cnid_dbd_rply rply;


    if (!cdb || !(db = cdb->_private) || !id) {
        LOG(log_error, logtype_cnid, "cnid_delete: Parameter error");
        errno = CNID_ERR_PARAM;        
        return -1;
    }

    RQST_RESET(rqst);
    rqst.op = CNID_DBD_OP_DELETE;
    rqst.cnid = id;

    if (transmit(db, &rqst, &rply) < 0) {
        errno = CNID_ERR_DB;
        return -1;
    }

    switch (rply.result) {
    case CNID_DBD_RES_OK:
    case CNID_DBD_RES_NOTFOUND:
        return 0;
    case CNID_DBD_RES_ERR_DB:
        errno = CNID_ERR_DB;
        return -1;
    default:
        abort();
    }
}

struct _cnid_module cnid_dbd_module = {
    "dbd",
    {NULL, NULL},
    cnid_dbd_open,
};

#endif /* CNID_DBD */

