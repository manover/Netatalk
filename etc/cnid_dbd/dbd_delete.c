/*
 * $Id: dbd_delete.c,v 1.1.4.2 2003-10-21 16:24:58 didg Exp $
 *
 * Copyright (C) Joerg Lenneis 2003
 * All Rights Reserved.  See COPYRIGHT.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif /* HAVE_CONFIG_H */

#include <string.h>
#include <errno.h>
#include <atalk/logger.h>

#ifdef HAVE_DB4_DB_H
#include <db4/db.h>
#else
#include <db.h>
#endif
#include <netatalk/endian.h>
#include <atalk/cnid_dbd_private.h>

#include "dbif.h"
#include "dbd.h"
#include "pack.h"

int dbd_delete(struct cnid_dbd_rqst *rqst, struct cnid_dbd_rply *rply)
{
    DBT key;
    int rc;

    memset(&key, 0, sizeof(key));

    rply->namelen = 0;

    key.data = (void *) &rqst->cnid;
    key.size = sizeof(rqst->cnid);

    if ((rc = dbif_del(DBIF_IDX_CNID, &key, 0)) < 0) {
        LOG(log_error, logtype_cnid, "dbd_delete: Unable to delete entry for CNID %u", ntohl(rqst->cnid));
        rply->result = CNID_DBD_RES_ERR_DB;
        return -1;
    }
    else {
#ifdef DEBUG
        LOG(log_info, logtype_cnid, "cnid_delete: CNID %u not in database",
            ntohl(rqst->cnid));
#endif
        rply->result = CNID_DBD_RES_NOTFOUND;
    }
    return 1;
}
