/*
 * $Id: cnid_hash_lookup.c,v 1.1.2.1 2003-09-09 16:42:21 didg Exp $
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif /* HAVE_CONFIG_H */

#ifdef CNID_BACKEND_HASH

#include "cnid_hash.h"

cnid_t cnid_hash_lookup(struct _cnid_db *cdb, const struct stat *st, const cnid_t did, const char *name, const int len)
{
    return cnid_hash_add(cdb, st, did, name, len, 0  /*hint*/);
}

#endif /* CNID_BACKEND_HASH */
