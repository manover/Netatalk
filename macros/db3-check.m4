dnl $Id: db3-check.m4,v 1.11.6.3 2004-01-03 01:49:54 bfernhomberg Exp $
dnl Autoconf macro to check for the Berkeley DB library

AC_DEFUN([AC_PATH_BDB], 
[
	trybdbdir=""
	dobdbsearch=yes
	bdb_search_dirs="/usr/local/include/db4 /usr/local/include /usr/include/db4 /usr/include"


	AC_ARG_WITH(bdb,
		[  --with-bdb=PATH         specify path to Berkeley DB installation[[auto]]],
		if test "x$withval" = "xno"; then
			dobdbsearch=no
		elif test "x$withval" = "xyes"; then
			dobdbsearch=yes
		else
			bdb_search_dirs="$withval/include/db4 $withval/include $withval"
		fi
	)

	bdbfound=no
	if test "x$dobdbsearch" = "xyes"; then
	    for bdbdir in $bdb_search_dirs; do
		AC_MSG_CHECKING([for Berkeley DB headers in $bdbdir])
		if test -f "$bdbdir/db.h" ; then
			AC_MSG_RESULT([yes])
			bdblibdir="`echo $bdbdir | sed 's/include\/db4$/lib/'`"
			bdblibdir="`echo $bdblibdir | sed 's/include$/lib/'`"
			bdbbindir="`echo $bdbdir | sed 's/include\/db4$/bin/'`"
			bdbbindir="`echo $bdbbindir | sed 's/include$/bin/'`"

			savedcflags="$CFLAGS"
			savedldflags="$LDFLAGS"
			savedcppflags="$CPPFLAGS"
			savedlibs="$LIBS"
			CPPFLAGS="$CFLAGS -I$bdbdir"
			CFLAGS=""
			LDFLAGS="-L$bdblibdir $LDFLAGS"

dnl This check breaks if bdb was configured with --with-uniquename, removed for now
dnl			dnl db_create is BDB >3 specific 
dnl			AC_CHECK_LIB(db, db_create, [
dnl				bdbfound=yes
dnl				LIBS="$LIBS -ldb"
dnl				BDB_LIBS="-ldb"], [
dnl			    AC_CHECK_LIB(db4, db_create, [
dnl					bdbfound=yes
dnl					LIBS="$LIBS -ldb4"
dnl					BDB_LIBS="-ldb4"])
dnl                        ])
			
			bdbfound=yes
			LIBS="$LIBS -ldb"
			BDB_LIBS="-ldb"

			dnl check for header ... should only fail if the header cannot be compiled
			dnl it does exist, otherwise we wouldn't be here

			if test "x$bdbfound" = "xyes"; then
			    AC_CHECK_HEADERS(db.h, bdbfound=yes, bdbfound=no)
			fi

			if test "x$bdbfound" = "xno"; then
				AC_MSG_WARN([Berkeley DB libraries found, but required header files cannot be used!!!])
			fi

			dnl check we have the correct bdb version
		  	AC_MSG_CHECKING([Berkeley DB version >= 4.0])
 			AC_TRY_RUN([ 
#if STDC_HEADERS
#include <stdlib.h>
#endif
#include <db.h>

#define DB_MAJOR_REQ	4
#define DB_MINOR_REQ	0
#define DB_PATCH_REQ	0


int main(void) {
	int major, minor, patch;
	char *version_str;

	version_str = db_version(&major, &minor, &patch);

	/* check header version */
	if (DB_VERSION_MAJOR < DB_MAJOR_REQ || DB_VERSION_MINOR < DB_MINOR_REQ ||
	    DB_VERSION_PATCH < DB_PATCH_REQ )
		exit (1);
		
	/* check library version */
	if (major < DB_MAJOR_REQ || minor < DB_MINOR_REQ || patch < DB_PATCH_REQ)
		exit (2);

	/* check header and library match */
	if ( major != DB_VERSION_MAJOR || minor != DB_VERSION_MINOR || patch != DB_VERSION_PATCH)
		exit(3);

	exit (0);
}
], atalk_cv_bdbversion="yes", atalk_cv_bdbversion="no", atalk_cv_bdbversion="cross")


			if test ${atalk_cv_bdbversion} = "yes"; then
   				AC_MSG_RESULT(yes)
			else
   				AC_MSG_RESULT(no)
				bdbfound=no
			fi

			if test "x$bdbfound" = "xyes"; then
				if test "$bdbdir" != "/usr/include"; then
				    BDB_CFLAGS="-I$bdbdir"
				fi
				if test "$bdblibdir" != "/usr/lib"; then
				    BDB_LIBS="-L$bdblibdir $BDB_LIBS"
				fi
				BDB_BIN=$bdbbindir
				BDB_PATH="`echo $bdbdir | sed 's,include\/db4$,,'`"
				BDB_PATH="`echo $BDB_PATH | sed 's,include$,,'`"
			fi
			CFLAGS="$savedcflags"
			LDFLAGS="$savedldflags"
			CPPFLAGS="$savedcppflags"
			LIBS="$savedlibs"
			break;
		else
			AC_MSG_RESULT([no])
		fi
	    done
	fi

	if test "x$bdbfound" = "xyes"; then
		ifelse([$1], , :, [$1])
	else
		ifelse([$2], , :, [$2])     
	fi

	AC_SUBST(BDB_CFLAGS)
	AC_SUBST(BDB_LIBS)
	AC_SUBST(BDB_BIN)
	AC_SUBST(BDB_PATH)
])


