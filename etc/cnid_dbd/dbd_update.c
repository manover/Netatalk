/*
 * $Id: dbd_update.c,v 1.1.4.1 2003-09-09 16:42:20 didg Exp $
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

#include <db.h>
#include <netatalk/endian.h>
#include <atalk/cnid_dbd_private.h>


#include "pack.h"
#include "dbif.h"
#include "dbd.h"


/* cnid_update: takes the given cnid and updates the metadata. */

int dbd_update(struct cnid_dbd_rqst *rqst, struct cnid_dbd_rply *rply)
{
    DBT key, data;
    int rc;
    char *buf;                                                                                               
    int notfound = 0;
    
    memset(&key, 0, sizeof(key));

    rply->namelen = 0;

    buf = pack_cnid_data(rqst);
    key.data = buf +CNID_DEVINO_OFS;
    key.size = CNID_DEVINO_LEN;

    if ((rc = dbif_del(DBIF_IDX_DEVINO, &key, 0) < 0) ) {
        goto err_db;
    }
    if (!rc) {
       notfound = 1;
    }

    buf = pack_cnid_data(rqst);
    key.data = buf + CNID_DID_OFS;
    key.size = CNID_DID_LEN + rqst->namelen +1;

    if ((rc = dbif_del(DBIF_IDX_DIDNAME, &key, 0) < 0)) {
        goto err_db;
    }
    if (!rc) {
       notfound |= 2;
    }

    memset(&key, 0, sizeof(key));
    key.data = (cnid_t *) &rqst->cnid;
    key.size = sizeof(rqst->cnid);

    memset(&data, 0, sizeof(data));
    /* Make a new entry. */
    data.data = pack_cnid_data(rqst);
    memcpy(data.data, &rqst->cnid, sizeof(rqst->cnid));
    data.size = CNID_HEADER_LEN + rqst->namelen + 1;

    if (dbif_put(DBIF_IDX_CNID, &key, &data, 0) < 0)
        goto err_db;

    rply->result = CNID_DBD_RES_OK;
    return 1;

err_db:
    LOG(log_error, logtype_cnid, "dbd_update: Unable to update CNID %u",
        ntohl(rqst->cnid));
    rply->result = CNID_DBD_RES_ERR_DB;
    return -1;
}
