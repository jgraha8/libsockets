## Makefile.am -- Process this file with automake to produce Makefile.in
ACLOCAL_AMFLAGS = -I m4

EXTRA_DIST = COPYING INSTALL README.md
SUBDIRS = include src
SUBLIBS = src/libsockets_src.la

lib_LTLIBRARIES = libsockets.la
libsockets_la_SOURCES =

#
# The rules of thumb, when dealing with these values are:
#
#    Always increase the revision value.
#
#    Increase the current value whenever an interface has been added, removed or changed.
#
#    Increase the age value only if the changes made to the ABI are backward compatible.
#
# See: https://autotools.io/libtool/version.html
#
# version-info current:revision:age
#
libsockets_la_LDFLAGS=-rpath '$(libdir)' -version-info 1:1:1
libsockets_la_LIBADD = $(SUBLIBS)

pkgincludedir = ${includedir}
