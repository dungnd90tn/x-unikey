plugindir = @QT6_PLUGINDIR@/platforminputcontexts

plugin_LTLIBRARIES = libunikeyplatforminputcontextplugin.la

AM_CPPFLAGS = -Wall \
	-I../ukbridge -I../ukinterface -I../ukengine -I../vnconv -I../byteio

libunikeyplatforminputcontextplugin_la_CXXFLAGS = @QT6_CFLAGS@ -fPIC
libunikeyplatforminputcontextplugin_la_LDFLAGS  = -rpath $(plugindir) -avoid-version -module
libunikeyplatforminputcontextplugin_la_LIBADD   = \
	../ukbridge/libukbridge.la ../ukinterface/libukint.la \
	../ukengine/libUnikey.la ../vnconv/libvnconv.la ../byteio/libbyteio.la \
	@QT6_LIBS@

libunikeyplatforminputcontextplugin_la_SOURCES = \
	main.cpp \
	ukinputcontext.cpp \
	ukinputcontext.h

# main.cpp ket thuc bang #include "main.moc" nen phai chay moc truoc khi bien dich.
BUILT_SOURCES = main.moc
CLEANFILES = main.moc

# moc chi nhan duong dan include, khong nhan co compiler nhu -Wall, nen o day
# chi truyen -I: no can tim unikey.json cua Q_PLUGIN_METADATA va cac header Qt.
main.moc: $(srcdir)/main.cpp $(srcdir)/unikey.json
	@QT6_MOC@ @QT6_MOCFLAGS@ -I$(srcdir) -o $@ $(srcdir)/main.cpp

check_PROGRAMS = test-plugin
TESTS = test-plugin

test_plugin_SOURCES  = test-plugin.cpp
test_plugin_CXXFLAGS = @QT6_CFLAGS@
test_plugin_LDADD    = @QT6_LIBS@
# Test nap plugin luc chay bang QPluginLoader nen phai co no truoc.
check_PROGRAMS: $(plugin_LTLIBRARIES)

EXTRA_DIST = unikey.json
