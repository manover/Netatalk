/*
 * $Id: dbd_resolve.c,v 1.1.4.5 2004-01-09 21:05:50 lenneis Exp $
 *
 * Copyright (C) Joerg Lenneis 2003
 * All Rights Reserved.  See COPYING.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif /* HAVE_CONFIG_H */

#include <string.h>
#include <atalk/logger.h>
#include <errno.h>
#include <netatalk/endian.h>
#include <atalk/cnid_dbd_private.h>

#include "dbif.h"
#include "dbd.h"
#include "pack.h"

/* Return the did/name pair corresponding to a CNID. */

int dbd_resolve(struct cnid_dbd_rqst *rqst, struct cnid_dbd_rply *rply)
{
    DBT key, data;
    int rc;

    memset(&key, 0, sizeof(key));
    memset(&data, 0, sizeof(data));

    rply->namelen = 0;

    key.data = (void *) &rqst->cnid;
    key.size = sizeof(cnid_t);

    if ((rc = dbif_get(DBIF_IDX_CNID, &key, &data, 0)) < 0) {
        LOG(log_error, logtype_cnid, "dbd_resolve: Unable to get did/name %u/%s", ntohl(rqst->did), rqst->name);
        rply->result = CNID_DBD_RES_ERR_DB;
        return -1;
    }
     
    if (rc == 0) {
        rply->result = CNID_DBD_RES_NOTFOUND;
        return 1;
    }

    memcpy(&rply->did, (char *) data.data + CNID_DID_OFS, sizeof(cnid_t));

    rply->namelen = data.size - CNID_NAME_OFS;
    rply->name = data.data + CNID_NAME_OFS;

#ifdef DEBUG
    LOG(log_info, logtype_cnid, "cnid_resolve: Returning id = %u, did/name = %s",
        ntohl(rply->did), (char *)data.data + CNID_NAME_OFS);
#endif
    rply->result = CNID_DBD_RES_OK;
    return 1;
}
