AC_DEFUN([AC_CHECK_ICONV],
[

dnl	#################################################
dnl	# check for libiconv support
	AC_MSG_CHECKING(whether to use libiconv)
        savedcflags="$CFLAGS"
        savedldflags="$LDFLAGS"
	netatalk_cv_libiconv=no
	AC_ARG_WITH(libiconv,
	[  --with-libiconv=BASEDIR Use libiconv in BASEDIR/lib and BASEDIR/include [[default=auto]]],
	[ case "$withval" in
	  no)
	    AC_MSG_RESULT(no)
	    ;;
	  *)
	    AC_MSG_RESULT(yes)
	    CFLAGS="$CFLAGS -I$withval/include"
	    LDFLAGS="$LDFLAGS -L$withval/$atalk_libname"
	    AC_CHECK_LIB(iconv, iconv_open, [
			ICONV_CFLAGS="-I$withval/include"
			ICONV_LIBS="-L$withval/$atalk_libname -liconv"
			netatalk_cv_libiconv=yes
			AC_DEFINE_UNQUOTED(WITH_LIBICONV, "${withval}",[Path to iconv])
            		], [
			AC_MSG_ERROR([libiconv not found in specified path: $withval])
	    ])
	    ;;
	  esac ],
	  AC_MSG_RESULT(no)
	)

	CFLAGS_REMOVE_USR_INCLUDE(ICONV_CFLAGS)
	LIB_REMOVE_USR_LIB(ICONV_LIBS)
	AC_SUBST(ICONV_CFLAGS)
	AC_SUBST(ICONV_LIBS)

dnl	############
dnl	# check for iconv usability

	saved_CPPFLAGS="$CPPFLAGS"
	CPPFLAGS="$CFLAGS $ICONV_CFLAGS $ICONV_LIBS"
	AC_CACHE_CHECK([for working iconv],netatalk_cv_HAVE_USABLE_ICONV,[
		AC_TRY_RUN([\
#include <iconv.h>
main() {
       iconv_t cd = iconv_open("ASCII", "UTF-8");
       if (cd == 0 || cd == (iconv_t)-1) return -1;
       return 0;
}
], netatalk_cv_HAVE_USABLE_ICONV=yes,netatalk_cv_HAVE_USABLE_ICONV=no,netatalk_cv_HAVE_USABLE_ICONV=cross)])

	if test x"$netatalk_cv_HAVE_USABLE_ICONV" = x"yes"; then
	    AC_DEFINE(HAVE_USABLE_ICONV,1,[Whether to use native iconv])
	fi

dnl	###########
dnl	# check if iconv needs const
  	if test x"$netatalk_cv_HAVE_USABLE_ICONV" = x"yes"; then
    		AC_CACHE_VAL(am_cv_proto_iconv, [
      		AC_TRY_COMPILE([\
#include <stdlib.h>
#include <iconv.h>
extern
#ifdef __cplusplus
"C"
#endif
#if defined(__STDC__) || defined(__cplusplus)
size_t iconv (iconv_t cd, char * *inbuf, size_t *inbytesleft, char * *outbuf, size_t *outbytesleft);
#else
size_t iconv();
#endif
], [], am_cv_proto_iconv_arg1="", am_cv_proto_iconv_arg1="const")
	      	am_cv_proto_iconv="extern size_t iconv (iconv_t cd, $am_cv_proto_iconv_arg1 char * *inbuf, size_t *inbytesleft, char * *outbuf, size_t *outbytesleft);"])
    		AC_DEFINE_UNQUOTED(ICONV_CONST, $am_cv_proto_iconv_arg1,
      			[Define as const if the declaration of iconv() needs const.])
  	fi

dnl     ###########
dnl     # check if libiconv supports UCS-2-INTERNAL
	if test x"$netatalk_cv_libiconv" = x"yes"; then
	    AC_CACHE_CHECK([whether iconv supports UCS-2-INTERNAL],netatalk_cv_HAVE_UCS2INTERNAL,[
		AC_TRY_RUN([\
#include <iconv.h>
int main() {
       iconv_t cd = iconv_open("ASCII", "UCS-2-INTERNAL");
       if (cd == 0 || cd == (iconv_t)-1) return -1;
       return 0;
}
], netatalk_cv_HAVE_UCS2INTERNAL=yes,netatalk_cv_HAVE_UCS2INTERNAL=no,netatalk_cv_HAVEUCS2INTERNAL=cross)])

	if test x"$netatalk_cv_HAVE_UCS2INTERNAL" = x"yes"; then
		AC_DEFINE(HAVE_UCS2INTERNAL,1,[Whether UCS-2-INTERNAL is supported])
	fi
	fi
        CFLAGS="$savedcflags"
        LDFLAGS="$savedldflags"
	CPPFLAGS="$saved_CPPFLAGS"
	
])
