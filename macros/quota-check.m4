dnl $Id: quota-check.m4,v 1.1.12.1 2003-11-01 02:38:09 bfernhomberg Exp $
dnl Autoconf macro to check for quota support
dnl FIXME: This is in now way complete.

AC_DEFUN([AC_CHECK_QUOTA], [
	QUOTA_LIBS=
	AC_CHECK_LIB(rpcsvc, main, [QUOTA_LIBS=-lrpcsvc])
	AC_CHECK_HEADERS(rpc/rpc.h)
	AC_SUBST(QUOTA_LIBS)
])

