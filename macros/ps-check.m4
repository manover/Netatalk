dnl Autoconf macro to check for the existence of ps
dnl $Id: ps-check.m4,v 1.1.2.2 2002-03-12 16:23:11 srittau Exp $

AC_DEFUN([AC_PROG_PS], [
AC_REQUIRE([AC_EXEEXT])dnl
test x$PS = x && AC_PATH_PROG(PS, ps$EXEEXT, ps$EXEEXT)
test x$PS = x && AC_MSG_ERROR([no acceptable ps found in \$PATH])
])

AC_SUBST(PS)
