AM_CPPFLAGS = -Wall \
	-I../ukbridge -I../ukinterface -I../ukengine -I../vnconv -I../byteio

# ibus-daemon goi engine, khong phai nguoi dung -> libexecdir chu khong phai bindir.
libexec_PROGRAMS = ibus-engine-unikey
applicationsdir = $(datadir)/applications

if COND_UNIKEY_GTK3
libexec_PROGRAMS += ibus-setup-unikey

ibus_setup_unikey_CFLAGS = @GTK3_CFLAGS@
ibus_setup_unikey_LDADD = ../ukbridge/libukbridge.la \
	../ukinterface/libukint.la ../ukengine/libUnikey.la \
	../vnconv/libvnconv.la ../byteio/libbyteio.la @GTK3_LIBS@
ibus_setup_unikey_SOURCES = setup.c dummy.cpp

applications_DATA = ibus-setup-unikey.desktop

SETUP_ELEMENT = <setup>$(libexecdir)/ibus-setup-unikey</setup>
SETUP_CPPFLAG = -DHAVE_UNIKEY_SETUP=1
else
SETUP_ELEMENT =
SETUP_CPPFLAG =
applications_DATA =
endif

ibus_engine_unikey_CFLAGS = @IBUS_CFLAGS@ $(SETUP_CPPFLAG)
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
	    -e 's|@SETUP_ELEMENT[@]|$(SETUP_ELEMENT)|g' \
	    $(srcdir)/unikey.xml.in > $@

ibus-setup-unikey.desktop: $(srcdir)/ibus-setup-unikey.desktop.in Makefile
	$(AM_V_GEN)sed -e 's|@LIBEXECDIR[@]|$(libexecdir)|g' \
	    $(srcdir)/ibus-setup-unikey.desktop.in > $@

CLEANFILES = unikey.xml ibus-setup-unikey.desktop
EXTRA_DIST = unikey.xml.in ibus-setup-unikey.desktop.in vte-smoke.py
