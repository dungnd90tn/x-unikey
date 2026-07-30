moduledir = @GTK3_LIBDIR@/gtk-3.0/@GTK3_BINARY_VERSION@/immodules

module_LTLIBRARIES = im-unikey.la

AM_CPPFLAGS = -Wall \
	-I../ukbridge -I../ukinterface -I../ukengine -I../vnconv -I../byteio

im_unikey_la_CFLAGS  = @GTK3_CFLAGS@
im_unikey_la_LDFLAGS = -rpath $(moduledir) -avoid-version -module
im_unikey_la_LIBADD  = ../ukbridge/libukbridge.la ../ukinterface/libukint.la \
	../ukengine/libUnikey.la ../vnconv/libvnconv.la ../byteio/libbyteio.la \
	@GTK3_LIBS@

im_unikey_la_SOURCES = \
	module.c \
	imcontext.c \
	imcontext.h \
	dummy.cpp
