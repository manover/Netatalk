/* 
 * $Id: mangle.c,v 1.16.2.1.2.1 2003-09-09 16:42:20 didg Exp $ 
 *
 * Copyright (c) 2002. Joe Marcus Clarke (marcus@marcuscom.com)
 * All Rights Reserved.  See COPYRIGHT.
 *
 * mangle, demangle (filename):
 * mangle or demangle filenames if they are greater than the max allowed
 * characters for a given version of AFP.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif /* HAVE_CONFIG_H */

#include <stdio.h>
#include <ctype.h>
#include "mangle.h"

#define hextoint( c )   ( isdigit( c ) ? c - '0' : c + 10 - 'A' )
#define isuxdigit(x)    (isdigit(x) || (isupper(x) && isxdigit(x)))

char *
demangle(const struct vol *vol, char *mfilename) {
    char *t;
    char *u_name;
    u_int32_t id = 0;
    static char buffer[12 + MAXPATHLEN + 1];
    int len = 12 + MAXPATHLEN + 1;
    struct dir	*dir;


    t = strchr(mfilename, MANGLE_CHAR);
    if (t == NULL) {
	    return mfilename;
	}
    /* may be a mangled filename */
    t++;
    if (*t == '0') { /* can't start with a 0 */
        return mfilename;
	}
    while(isuxdigit(*t)) {
        id = (id *16) + hextoint(*t);
        t++;
    }
    if ((*t != 0 && *t != '.') || strlen(t) > MAX_EXT_LENGTH || id == 0) {
	    return mfilename;
	}

    id = htonl(id);
    /* is it a dir?*/
    if (NULL != (dir = dirsearch(vol, id))) {
        return dir->d_u_name;
	}
    if (NULL != (u_name = cnid_resolve(vol->v_cdb, &id, buffer, len)) ) {
        return u_name;
    }
    return mfilename;
}

/* -----------------------
   with utf8 filename not always round trip
   filename   mac filename too long or with unmatchable utf8 replaced with _
   uname      unix filename 
   id         file/folder ID or 0
   
*/
char *
mangle(const struct vol *vol, char *filename, char *uname, cnid_t id, int flags) {
    char *ext = NULL;
    char *m = NULL;
    static char mfilename[MAX_LENGTH + 1];
    char mangle_suffix[MANGLE_LENGTH + 1];
    size_t ext_len = 0;
    int k;

    /* Do we really need to mangle this filename? */
    if (!flags && strlen(filename) <= vol->max_filename) {
	return filename;
    }
    /* First, attempt to locate a file extension. */
    if (NULL != (ext = strrchr(uname, '.')) ) {
	ext_len = strlen(ext);
	if (ext_len > MAX_EXT_LENGTH) {
	    /* Do some bounds checking to prevent an extension overflow. */
	    ext_len = MAX_EXT_LENGTH;
	}
    }
	m = mfilename;
    memset(m, 0, MAX_LENGTH + 1);
    k = sprintf(mangle_suffix, "%c%X", MANGLE_CHAR, ntohl(id));

    strncpy(m, filename, MAX_LENGTH - k - ext_len);
    	strcat(m, mangle_suffix);
		strncat(m, ext, ext_len);

    return m;
}
