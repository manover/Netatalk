dnl $Id: quota-check.m4,v 1.1.2.1 2001-12-05 08:43:39 srittau Exp $
dnl Autoconf macro to check for quota support
dnl FIXME: This is in now way complete.

AC_DEFUN([AC_CHECK_QUOTA], [
	QUOTA_LIBS=
	AC_CHECK_LIB(rpcsvc, main, [QUOTA_LIBS=-lrpcsvc])
	AC_SUBST(QUOTA_LIBS)
])

