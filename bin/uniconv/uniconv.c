/*
   uniconv - convert volume encodings
   Copyright (C) Bjoern Fernhomberg 2002,2003

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
*/


#ifdef HAVE_CONFIG_H
#include "config.h"
#endif /* HAVE_CONFIG_H */
#include <errno.h>
#include <stdlib.h>
#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <fcntl.h>
#include <time.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <ctype.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <pwd.h>
#include <dirent.h>
#include <atalk/afp.h>
#include <atalk/unicode.h>

#include "atalk/cnid.h"
#define MAXPATHLEN 4096

static struct _cnid_db *cdb;
static char curpath[MAXPATHLEN];
static cnid_t cdir_id;
static char db_stamp[ADEDLEN_PRIVSYN];

static charset_t ch_from;
char* from_charset;
static charset_t ch_to;
char* to_charset;
static charset_t ch_mac;
char* mac_charset;
static int usedots = 0;
static u_int16_t conv_flags = 0;
static int dry_run = 0;
static int verbose=0;
char *cnid_type;

char             Cnid_srv[256] = "localhost";
int              Cnid_port = 4700;

extern  struct charset_functions charset_iso8859_adapted;

#ifndef MAX
#define MAX(a,b) (((a)>(b))?(a):(b))
#endif


#define VETO "./../.AppleDB/.AppleDouble/.AppleDesktop/.Parent/"

static int veto(const char *path)
{
    int i,j;
    char *veto_str = VETO;
    

    if ((path == NULL))
        return 0;

    for(i=0, j=0; veto_str[i] != '\0'; i++) {
        if (veto_str[i] == '/') {
            if ((j>0) && (path[j] == '\0'))
                return 1;
            j = 0;
        } else {
            if (veto_str[i] != path[j]) {
                while ((veto_str[i] != '/')
                        && (veto_str[i] != '\0'))
                    i++;
                j = 0;
                continue;
            }
            j++;
        }
    }
    return 0;
}


static int do_rename( char* src, char *dst, struct stat *st)
{
    char        adsrc[ MAXPATHLEN + 1];
    struct      stat tmp_st;

    if (!stat(dst, &tmp_st)) {
	fprintf (stderr, "error: cannot rename %s to %s, destination exists\n", src, dst);
	return -1;
    }

    if ( rename( src, dst ) < 0 ) {
	fprintf (stderr, "error: cannot rename %s to %s, %s\n", src, dst, strerror(errno));
	return -1;
    }

    if (S_ISDIR(st->st_mode))
	return 0;

    strcpy( adsrc, ad_path( src, 0 ));

    if (rename( adsrc, ad_path( dst, 0 )) < 0 ) {
        struct stat ad_st;

        if (errno == ENOENT) {
            if (stat(adsrc, &ad_st)) /* source has no ressource fork, */
		return 0;
	}
	else {
		fprintf (stderr, "failed to rename resource fork, error: %s\n", strerror(errno));
                return -1;
 	}	

    }
    return 0;
}


static char *convert_name(char *name, struct stat *st, cnid_t cur_did)
{
	static char buffer[MAXPATHLEN];
	size_t len = 0;
	size_t outlen = 0;
	unsigned char *p,*q;
	int require_conversion = 0;
        u_int16_t    flags = conv_flags;
	cnid_t id;

	p = name;
	q = buffer;

	/* optimize for ascii case */
	while (*p != 0) {
		if ( *p >= 0x80 || *p == ':') {
			require_conversion = 1;
			break;
		}
		p++;
	}

	if (!require_conversion) {
		if (verbose > 1)
		    fprintf(stdout, "no conversion required\n");
		return name;
	}

	/* convert charsets */
	q=buffer;
	p=name;

	outlen = convert_charset(ch_from, ch_to, ch_mac, p, strlen(p), q, sizeof(buffer)-len, &flags);
	if ((size_t)-1 == outlen) {
        	fprintf(stderr, "conversion from '%s' to '%s' for '%s' in DID %u failed, ignoring\n", 
                        from_charset, to_charset, name, ntohl(cur_did));
		return name;
	}
	buffer[len+outlen] = 0;
	if (strcmp (name, buffer)) {
	    if (dry_run) {
    		fprintf(stdout, "dry_run: would rename %s to %s.\n", name, buffer);
	    }	
	    else if (!do_rename(name, buffer, st)) {
		if (CNID_INVALID != (id = cnid_add(cdb, st, cur_did, buffer, strlen(buffer), 0))) 
       	    		fprintf(stdout, "converted '%s' to '%s' (ID %u, DID %u).\n", 
                                name, buffer, ntohl(id), ntohl(cur_did));
	    }
	}
	else if (verbose > 1)
	    fprintf(stdout, "no conversion required\n");
	
	return (buffer);
}

static int check_dirent(char** name, cnid_t cur_did)
{
	struct stat st;
	int ret = 0;

	if (veto(*name))
		return 0;

        if (stat(*name, &st) != 0) {
        	switch (errno) {
                    case ELOOP:
                    case ENOENT:
			return 0;
                    default:
			return (-1);
                }
        }

	if (S_ISDIR(st.st_mode)){
		ret = 1;
	} 

	if (verbose > 1)
	    fprintf(stdout, "Checking: '%s' - ", *name);

	*name = convert_name(*name, &st, cur_did);

	return ret;
}

static int check_adouble(DIR *curdir, char * path)
{
	DIR *adouble;
	struct dirent* entry;
	struct dirent* ad_entry;
	int found = 0;

	strncat(curpath, "/", MAXPATHLEN);
	strncat(curpath, ".AppleDouble", MAXPATHLEN);

	if (NULL == (adouble = opendir(curpath))) {
		return(-1);
	}

	while (NULL != (ad_entry=readdir(adouble)) ) {
		if (veto(ad_entry->d_name))
			break;
		found = 0;
		rewinddir(curdir);
		while (NULL != (entry=readdir(curdir)) ) {
			if (!strcmp(ad_entry->d_name, entry->d_name)) {
				found = 1;
				break;
			}
		}
		if (!found) {
			fprintf (stderr, "found orphaned resource file %s", ad_entry->d_name);
		}
	}
			
	rewinddir(curdir);
	closedir(adouble);
	return (0);
}
		
static cnid_t add_dir_db(char *name, cnid_t cur_did)
{
	cnid_t id, did;
	struct stat st;

	if (CNID_INVALID != ( id = cnid_get(cdb, cur_did, name, strlen(name))) )
	    return id;

	if (dry_run) {
        	fprintf( stderr, "dry_run: dir '%s' not in db and cannot add (dry run), skipping\n", name);
		return 0;
	}

        did = cur_did;
	if (stat(name, &st)) {
             fprintf( stderr, "dir '%s' cannot be stat'ed, error %u\n", name, errno);
	     return 0;
        }

        id = cnid_add(cdb, &st, did, name, strlen(name), 0);
	
	fprintf (stderr, "added '%s' to DID %u as %u\n", name, ntohl(did), ntohl(id));
	return id;
}

static int getdir(DIR *curdir, char ***names)
{
	struct dirent* entry;
	char **tmp = NULL, **new=NULL;
	char *name;
	int i=0;

        while ((entry=readdir(curdir)) != NULL) {
		new = (char **) realloc (tmp, (i+1) * sizeof(char*));
		if (new == NULL) {
			fprintf(stderr, "out of memory");
			exit (-1);
		}
		tmp = new;
		name = strdup(entry->d_name);
		if (name == NULL) {
			fprintf(stderr, "out of memory");
			exit (-1);
		}
		tmp[i]= (char*) name;
		i++;
        };

	*names = tmp;
	return i;
}

static int checkdir(DIR *curdir, char *path, cnid_t cur_did)
{
	DIR* cdir;
	int ret = 0;
	cnid_t id;
	char *name;
        int n;
	size_t len=strlen(curpath);

	char **names;

	chdir(path);

	check_adouble(curdir, path);
	curpath[len] = 0;

	if (verbose)
	    fprintf( stdout, "\nchecking DIR '%s' with ID %u\n", path, ntohl(cur_did));

	n = getdir(curdir, &names);

	while (n--) {
		name = names[n];
                ret = check_dirent(&name, cur_did);
                if (ret==1) {
                    id = add_dir_db(name, cur_did);
                    if ( id == 0)
                        continue;  /* skip, no ID */
                    strncat(curpath, "/", MAXPATHLEN);
                    strncat(curpath, name, MAXPATHLEN);
                    cdir = opendir(curpath);
                    checkdir(cdir, curpath, id);
                    closedir(cdir);
                    curpath[len] = 0;
                    chdir(path);
		    if (verbose)
	    	 	fprintf( stdout, "returned to DIR '%s' with ID %u\n", path, ntohl(cur_did));
                }
                free(names[n]);
        }
	free(names);
	if (verbose)
	    fprintf( stderr, "leaving DIR '%s' with ID %u\n\n", path, ntohl(cur_did));

	return 0;
}

static int init(char* path)
{
	DIR* startdir;

        if (NULL == (cdb = cnid_open (path, 0, cnid_type, 0)) ) {
                fprintf (stderr, "ERROR: cannot open CNID database in '%s'\n", path);
                fprintf (stderr, "ERROR: check the logs for reasons, aborting\n");
		return -1;
        }
        cnid_getstamp(cdb, db_stamp, sizeof(db_stamp));
	cdir_id = htonl(2);

	startdir = opendir(path);
	strncpy(curpath, path, MAXPATHLEN);
	checkdir (startdir, path, cdir_id);
	closedir(startdir);

	cnid_close(cdb);

	return (0);
}



int main(int argc, char *argv[])
{
        char path[MAXPATHLEN];
        int  c;

	path[0]= 0;
        conv_flags = CONV_UNESCAPEHEX | CONV_ESCAPEHEX | CONV_ESCAPEDOTS;

        while ((c = getopt (argc, argv, "f:m:p:t:nc:v")) != -1)
        switch (c)
        {
	case 'f':
		from_charset = strdup(optarg);
		break;
	case 't':
		to_charset = strdup(optarg);
		break;
        case 'p':
		strncpy(path, optarg, MAXPATHLEN);
                break;
	case 'm':
		mac_charset = strdup(optarg);
		break;
	case 'd':
		conv_flags &= !CONV_ESCAPEDOTS;
		usedots = 1;
		break;
	case 'n':
		fprintf (stderr, "doing dry run, volume will *not* be changed\n");
		dry_run = 1;
		break;
	case 'c':
		cnid_type = strdup(optarg);
		fprintf (stderr, "CNID backend set to: %s\n", cnid_type);
		break;
	case 'v':
		verbose++;
		break;
        case 'h':
                break;
        default:
		break;
        }

	if ( from_charset == NULL || to_charset == NULL) {
		fprintf (stderr, "charsets not specified\n");
		exit(-1);
	}

	if ( mac_charset == NULL )
		mac_charset = "MAC_ROMAN";

	if ( cnid_type == NULL)
		cnid_type = DEFAULT_CNID_SCHEME;
	

	if (path[0] == 0) {
                if (NULL == (getcwd(path, MAXPATHLEN)) ) {
                        fprintf (stderr, "ERROR: path == NULL\n");
                        return (-1);
                }
	}

	atalk_register_charset(&charset_iso8859_adapted);

    	if ( (charset_t) -1 == ( ch_from = add_charset(from_charset)) ) {
        	fprintf( stderr, "Setting codepage %s as source codepage failed", from_charset);
		exit (-1);
    	}

    	if ( (charset_t) -1 == ( ch_to = add_charset(to_charset)) ) {
        	fprintf( stderr, "Setting codepage %s as destination codepage failed", to_charset);
		exit (-1);
    	}

    	if ( (charset_t) -1 == ( ch_mac = add_charset(mac_charset)) ) {
        	fprintf( stderr, "Setting codepage %s as mac codepage failed", mac_charset);
		exit (-1);
    	}

	cnid_init();
	init(path);

	return (0);
}
