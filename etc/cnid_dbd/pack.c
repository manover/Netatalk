/*
 * $Id: pack.c,v 1.1.4.3 2003-10-30 10:03:19 bfernhomberg Exp $
 *
 * Copyright (C) Joerg Lenneis 2003
 * All Rights Reserved.  See COPYRIGHT.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif /* HAVE_CONFIG_H */

#include <string.h>
#ifdef HAVE_SYS_TYPES_H
#include <sys/types.h>
#endif /* HAVE_SYS_TYPES_H */
#include <sys/param.h>
#include <sys/cdefs.h>
#include <db.h>

#include <atalk/cnid_dbd_private.h>
#include <netatalk/endian.h>
#include "pack.h"

/* --------------- */
int didname(dbp, pkey, pdata, skey)
DB *dbp;
const DBT *pkey, *pdata;
DBT *skey;
{
int len;
 
    memset(skey, 0, sizeof(DBT));
    skey->data = pdata->data + CNID_DID_OFS;
    len = strlen(skey->data + CNID_DID_LEN);
    skey->size = CNID_DID_LEN + len + 1;
    return (0);
}
 
/* --------------- */
int devino(dbp, pkey, pdata, skey)
DB *dbp;
const DBT *pkey, *pdata;
DBT *skey;
{
    memset(skey, 0, sizeof(DBT));
    skey->data = pdata->data + CNID_DEVINO_OFS;
    skey->size = CNID_DEVINO_LEN;
    return (0);
}

/* The equivalent to make_cnid_data in the cnid library. Non re-entrant. We
   differ from make_cnid_data in that we never return NULL, rqst->name cannot
   ever cause start[] to overflow because name length is checked in libatalk. */

char *pack_cnid_data(struct cnid_dbd_rqst *rqst)
{
    static char start[CNID_HEADER_LEN + MAXPATHLEN + 1];
    char *buf = start +CNID_LEN;
    u_int32_t i;

    memcpy(buf, &rqst->dev, sizeof(rqst->dev));
    buf += sizeof(rqst->dev);

    memcpy(buf, &rqst->ino, sizeof(rqst->ino));
    buf += sizeof(rqst->ino);

    i = htonl(rqst->type);
    memcpy(buf, &i, sizeof(i));
    buf += sizeof(i);

    /* did is already in network byte order */
    buf = memcpy(buf, &rqst->did, sizeof(rqst->did));
    buf += sizeof(rqst->did);
    buf = memcpy(buf, rqst->name, rqst->namelen);
    *(buf + rqst->namelen) = '\0';

    return start;
}
