/*
 * $Id: cnid_hash_get.c,v 1.1.2.1 2003-09-09 16:42:21 didg Exp $
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif /* HAVE_CONFIG_H */

#ifdef CNID_BACKEND_HASH

#include "cnid_hash.h"

/* Return CNID for a given did/name. */
cnid_t cnid_hash_get(struct _cnid_db *cdb, const cnid_t did, const char *name, const int len)
{
    return CNID_INVALID;
}

#endif /* CNID_BACKEND_HASH */
