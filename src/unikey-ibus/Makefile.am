AM_CPPFLAGS = -Wall \
	-I../ukbridge -I../ukinterface -I../ukengine -I../vnconv -I../byteio

# ibus-daemon goi engine, khong phai nguoi dung -> libexecdir chu khong phai bindir.
libexec_PROGRAMS = ibus-engine-unikey

ibus_engine_unikey_CFLAGS = @IBUS_CFLAGS@
ibus_engine_unikey_LDADD  = ../ukbridge/libukbridge.la ../ukinterface/libukint.la \
	../ukengine/libUnikey.la ../vnconv/libvnconv.la ../byteio/libbyteio.la \
	@IBUS_LIBS@

ibus_engine_unikey_SOURCES = \
	engine.c \
	ibus-policy.h \
	dummy.cpp

# ibus quet thu muc nay luc khoi dong de biet co nhung engine nao.
componentdir = @IBUS_COMPONENTDIR@
component_DATA = unikey.xml

# Duong dan phai chot luc build (libexecdir chi biet o thoi diem nay).
unikey.xml: $(srcdir)/unikey.xml.in Makefile
	$(AM_V_GEN)sed -e 's|@LIBEXECDIR[@]|$(libexecdir)|g' \
	    -e 's|@VERSION[@]|$(VERSION)|g' \
	    $(srcdir)/unikey.xml.in > $@

CLEANFILES = unikey.xml
EXTRA_DIST = unikey.xml.in vte-smoke.py
