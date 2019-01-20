#######################################
# The list of libraries we are building seperated by spaces.
# The 'lib_' indicates that these build products will be installed
# in the $(libdir) directory. For example /usr/lib
noinst_LTLIBRARIES = libsockets_src.la

#AM_CPPFLAGS = -I${top_srcdir}

libsockets_src_la_SOURCES = sockets.c

# Compiler options. Here we are adding the include directory
# to be searched for headers included in the source code.
libsockets_src_la_CPPFLAGS = -I$(top_srcdir)/include
