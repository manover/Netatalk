/*
 * $Id: cnid_cdb_lookup.c,v 1.1.4.1 2003-09-09 16:42:21 didg Exp $
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif /* HAVE_CONFIG_H */

#ifdef CNID_BACKEND_CDB

#include <stdio.h>
#include <string.h>
#include <sys/param.h>
#include <sys/stat.h>
#include <atalk/logger.h>
#include <errno.h>

#include <db.h>
#include <netatalk/endian.h>
#include <atalk/adouble.h>
#include "cnid_cdb.h"

#include "cnid_cdb_private.h"

#define LOGFILEMAX    100  /* kbytes */
#define CHECKTIMEMAX   30  /* minutes */

/* This returns the CNID corresponding to a particular file.  It will
 * also fix up the various databases if there's a problem. */
cnid_t cnid_cdb_lookup(struct _cnid_db *cdb, const struct stat *st, const cnid_t did,
                   const char *name, const int len)
{
    char *buf;
    CNID_private *db;
    DBT key, diddata;
    dev_t dev;
    ino_t ino;  
    cnid_t id = 0;
    int rc;

    if (!cdb || !(db = cdb->_private) || !st || !name) {
        return 0;
    }
    
    if ((buf = make_cnid_data(st, did, name, len)) == NULL) {
        LOG(log_error, logtype_default, "cnid_lookup: Pathname is too long");
        return 0;
    }

    memset(&key, 0, sizeof(key));
    memset(&diddata, 0, sizeof(diddata));

    /* Look for a CNID for our did/name */
    key.data = buf +CNID_DID_OFS;
    key.size = CNID_DID_LEN + len + 1;

    memcpy(&dev, buf + CNID_DEV_OFS, sizeof(dev));
    memcpy(&ino, buf + CNID_INO_OFS, sizeof(ino));

    if (0 != (rc = db->db_didname->get(db->db_didname, NULL, &key, &diddata, 0 )) ) {
        if (rc != DB_NOTFOUND) {
           LOG(log_error, logtype_default, "cnid_lookup: Unable to get CNID did 0x%x, name %s: %s",
               did, name, db_strerror(rc));
        }
        return 0;
    }
 
    memcpy(&id, diddata.data, sizeof(id));
    /* if dev:ino the same */
    if (!memcmp(&dev, (char *)diddata.data + CNID_DEV_OFS, sizeof(dev)) &&
        !memcmp(&ino, (char *)diddata.data + CNID_INO_OFS, sizeof(ino))
        /* FIXME and type are the same */
       )
    {
        /* then it's what we are looking for */
        return id;
    }
 
    /* with have a did:name but it's not one the same dev:inode
     * it should be always a different file, but in moveandrename with cross/dev
     * we don't update the db.
    */
    if (!memcmp(&dev, (char *)diddata.data + CNID_DEV_OFS, sizeof(dev)))
       /* FIXME or diff type */
    {
       /* dev are the same so it's a copy */
        return 0;
    }
    /* Fix up the database. */
    cnid_cdb_update(cdb, id, st, did, name, len);

#ifdef DEBUG
    LOG(log_info, logtype_default, "cnid_lookup: Looked up did %u, name %s, as %u (needed update)", ntohl(did), name, ntohl(id));
#endif
    return id;
}

#endif /* CNID_BACKEND_CDB */
