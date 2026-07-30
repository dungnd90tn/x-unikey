moduledir = @GTK4_LIBDIR@/gtk-4.0/@GTK4_BINARY_VERSION@/immodules

module_LTLIBRARIES = libim-unikey.la

AM_CPPFLAGS = -Wall \
	-I../ukbridge -I../ukinterface -I../ukengine -I../vnconv -I../byteio

libim_unikey_la_CFLAGS  = @GTK4_CFLAGS@
libim_unikey_la_LDFLAGS = -rpath $(moduledir) -avoid-version -module
libim_unikey_la_LIBADD  = ../ukbridge/libukbridge.la ../ukinterface/libukint.la \
	../ukengine/libUnikey.la ../vnconv/libvnconv.la ../byteio/libbyteio.la \
	@GTK4_LIBS@

libim_unikey_la_SOURCES = \
	module.c \
	imcontext.c \
	imcontext.h \
	dummy.cpp
