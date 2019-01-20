#                                               -*- Autoconf -*-
# Process this file with autoconf to produce a configure script.

dnl AC_PREREQ([2.59])
AC_INIT([libsockets], [0.1.0], [jgraham@compukix.net])

AM_INIT_AUTOMAKE([foreign])
AC_CONFIG_HEADERS([config.h])
## AM_MAINTAINER_MODE turns off "rebuild rules" that contain dependencies
## for Makefiles, configure, src/H5config.h, etc.  If AM_MAINTAINER_MODE
## is *not* included here, these files will be rebuilt if out of date.
## This is a problem because if users try to build on a machine with
## the wrong versions of autoconf and automake, these files will be
## rebuilt with the wrong versions and bad things can happen.
## Also, CVS doesn't preserve dependencies between timestamps, so
## Makefiles will often think rebuilding needs to occur when it doesn't.
## Developers should './configure --enable-maintainer-mode' to turn on
## rebuild rules.
AM_MAINTAINER_MODE

AC_CONFIG_MACRO_DIR([m4])

# Checks for programs.
AC_PROG_MAKE_SET
AC_LANG(C)
AC_PROG_CC
AM_PROG_CC_C_O
AC_PROG_CPP
AC_PROG_INSTALL
AC_PROG_LN_S

dnl Initialize Libtool
LT_INIT([disable-static])

dnl Check if Libtool is present
dnl Libtool is used for building share libraries 
AC_PROG_LIBTOOL

# Checks for header files.
AC_CHECK_HEADERS([ \
	assert.h \
	pthread.h \
	stdatomic.h \
	stdbool.h \
	stdint.h \
	stdio.h \
	stdlib.h \
	string.h \
	unistd.h
])

# Checks for libraries
AC_CHECK_LIB([pthread], [pthread_create])

# Adjust prefix if --prefix not provided
AS_IF([test "x$prefix" = "xNONE"],
      [prefix=$ac_default_prefix],
      [])

AC_CONFIG_FILES([		\
Makefile			\
include/Makefile		\
include/libsockets/Makefile 	\
src/Makefile			\
])			

AC_OUTPUT
