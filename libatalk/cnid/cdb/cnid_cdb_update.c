/*
 * $Id: cnid_cdb_update.c,v 1.1.4.1 2003-09-09 16:42:21 didg Exp $
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif /* HAVE_CONFIG_H */

#ifdef CNID_BACKEND_CDB

#include <stdio.h>
#include <string.h>
#include <sys/param.h>
#include <sys/stat.h>
#include <errno.h>
#include <atalk/logger.h>

#include <db.h>
#include <netatalk/endian.h>
#include <atalk/adouble.h>
#include "cnid_cdb.h"

#include "cnid_cdb_private.h"

#define tid    NULL

/* cnid_update: takes the given cnid and updates the metadata.  To
 * handle the did/name data, there are a bunch of functions to get
 * and set the various fields. */
int cnid_cdb_update(struct _cnid_db *cdb, const cnid_t id, const struct stat *st,
                const cnid_t did, const char *name, const int len
                /*, const char *info, const int infolen*/)
{
    char *buf;
    CNID_private *db;
    DBT key, data;
    int rc;
    int notfound = 0;

    if (!cdb || !(db = cdb->_private) || !id || !st || !name || (db->flags & CNIDFLAG_DB_RO)) {
        return -1;
    }

    memset(&key, 0, sizeof(key));

    buf = make_cnid_data(st, did, name, len);

    key.data = buf +CNID_DEVINO_OFS;
    key.size = CNID_DEVINO_LEN;

    if (0 != (rc = db->db_devino->del(db->db_devino, tid, &key, 0)) ) {
        if (rc != DB_NOTFOUND && rc != DB_SECONDARY_BAD) {
           LOG(log_error, logtype_default, "cnid_update: Unable to del devino CNID %u, name %s: %s",
               ntohl(did), name, db_strerror(rc));
           goto fin;
        }
        notfound = 1;
    }

    buf = make_cnid_data(st, did, name, len);
    key.data = buf + CNID_DID_OFS;
    key.size = CNID_DID_LEN + len + 1;

    if (0 != (rc = db->db_didname->del(db->db_didname, tid, &key, 0)) ) {
        if (rc != DB_NOTFOUND && rc != DB_SECONDARY_BAD) {
           LOG(log_error, logtype_default, "cnid_update: Unable to del didname CNID %u, name %s: %s",
               ntohl(did), name, db_strerror(rc));
           goto fin;
        }
        notfound |= 2;
    }

    memset(&key, 0, sizeof(key));
    key.data = (cnid_t *)&id;
    key.size = sizeof(id);

    memset(&data, 0, sizeof(data));
    /* Make a new entry. */
    buf = make_cnid_data(st, did, name, len);
    data.data = buf;
    memcpy(data.data, &id, sizeof(id));
    data.size = CNID_HEADER_LEN + len + 1;

    /* Update the old CNID with the new info. */
    if ((rc = db->db_cnid->put(db->db_cnid, tid, &key, &data, 0))) {
        LOG(log_error, logtype_default, "cnid_update: (%d) Unable to update CNID %u:%s: %s",
            notfound, ntohl(id), name, db_strerror(rc));
        goto fin;
    }

    return 0;
fin:
    return -1;
 
}

#endif
