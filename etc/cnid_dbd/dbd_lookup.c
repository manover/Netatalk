/*
 * $Id: dbd_lookup.c,v 1.1.4.2 2003-10-21 16:24:58 didg Exp $
 *
 * Copyright (C) Joerg Lenneis 2003
 * All Rights Reserved.  See COPYRIGHT.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif /* HAVE_CONFIG_H */


#include <stdio.h>
#include <string.h>
#include <sys/param.h>
#include <atalk/logger.h>
#include <errno.h>

#ifdef HAVE_DB4_DB_H
#include <db4/db.h>
#else
#include <db.h>
#endif
#include <netatalk/endian.h>
#include <atalk/cnid_dbd_private.h>

#include "pack.h"
#include "dbif.h"
#include "dbd.h"

/*
 *  This returns the CNID corresponding to a particular file.  It will also fix
 *  up the database if there's a problem.
 */

int dbd_lookup(struct cnid_dbd_rqst *rqst, struct cnid_dbd_rply *rply)
{
    char *buf;
    DBT key, devdata, diddata;
    dev_t  dev;
    ino_t  ino;
    int devino = 1, didname = 1; 
    int rc;
    cnid_t id_devino, id_didname;
    u_int32_t type_devino  = (unsigned)-1;
    u_int32_t type_didname = (unsigned)-1;
    int update = 0;
    
    
    memset(&key, 0, sizeof(key));
    memset(&diddata, 0, sizeof(diddata));
    memset(&devdata, 0, sizeof(devdata));

    rply->namelen = 0;
    rply->cnid = 0;
    
    buf = pack_cnid_data(rqst); 
    memcpy(&dev, buf + CNID_DEV_OFS, sizeof(dev));
    memcpy(&ino, buf + CNID_INO_OFS, sizeof(ino));

    /* Look for a CNID.  We have two options: dev/ino or did/name.  If we
       only get a match in one of them, that means a file has moved. */
    key.data = buf +CNID_DEVINO_OFS;
    key.size = CNID_DEVINO_LEN;

    if ((rc = dbif_get(DBIF_IDX_DEVINO, &key, &devdata, 0))  < 0) {
        LOG(log_error, logtype_cnid, "dbd_lookup: Unable to get CNID %u, name %s",
                ntohl(rqst->did), rqst->name);
        rply->result = CNID_DBD_RES_ERR_DB;
        return -1;
    }
    if (rc == 0) {
        devino = 0;
    }
    else {
        memcpy(&id_devino, devdata.data, sizeof(rply->cnid));
        memcpy(&type_devino, devdata.data +CNID_TYPE_OFS, sizeof(type_devino));
        type_devino = ntohl(type_devino);
    }
    
    buf = pack_cnid_data(rqst); 
    key.data = buf +CNID_DID_OFS;
    key.size = CNID_DID_LEN + rqst->namelen + 1;

    if ((rc = dbif_get(DBIF_IDX_DIDNAME, &key, &diddata, 0))  < 0) {
        LOG(log_error, logtype_cnid, "dbd_lookup: Unable to get CNID %u, name %s",
                ntohl(rqst->did), rqst->name);
        rply->result = CNID_DBD_RES_ERR_DB;
        return -1;
    }
    if (rc == 0) {
        didname = 0;
    }
    else {
        memcpy(&id_didname, diddata.data, sizeof(rply->cnid));
        memcpy(&type_didname, diddata.data +CNID_TYPE_OFS, sizeof(type_didname));
        type_didname = ntohl(type_didname);
    }
    
    if (!devino && !didname) {  
        /* not found */
        rply->result = CNID_DBD_RES_NOTFOUND;
        return 1;
    }

    if (devino && didname && id_devino == id_didname && type_devino == rqst->type) {
        /* the same */
        rply->cnid = id_didname;
        rply->result = CNID_DBD_RES_OK;
        return 1;
    }
    
    if (didname) {
        rqst->cnid = id_didname;
        /* we have a did:name 
         * if it's the same dev or not the same type
         * just delete it
        */
        if (!memcmp(&dev, (char *)diddata.data + CNID_DEV_OFS, sizeof(dev)) ||
                   type_didname != rqst->type) {
            if (dbd_delete(rqst, rply) < 0) {
                return -1;
            }
        }
        else {
            update = 1;
        }
    }

    if (devino) {
        rqst->cnid = id_devino;
        if (type_devino != rqst->type) {
            /* same dev:inode but not same type one is a folder the other 
             * is a file,it's an inode reused, delete the record
            */
            if (dbd_delete(rqst, rply) < 0) {
                return -1;
            }
        }
        else {
            update = 1;
        }
    }
    if (!update) {
        rply->result = CNID_DBD_RES_NOTFOUND;
        return 1;
    }
    /* Fix up the database. assume it was a file move and rename */
    rc = dbd_update(rqst, rply);
    if (rc >0) {
        rply->cnid = rqst->cnid;
    }
#ifdef DEBUG
    LOG(log_info, logtype_cnid, "cnid_lookup: Looked up did %u, name %s, as %u (needed update)", 
    ntohl(rqst->did), rqst->name, ntohl(rply->cnid));
#endif
    return rc;
}
