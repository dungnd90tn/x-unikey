AM_CPPFLAGS = -Wall -I../ukinterface -I../ukengine -I../vnconv -I../byteio

noinst_LTLIBRARIES = libukbridge.la

# Khong _LIBADD libukint o day: cac target cuoi (module toolkit) tu link
# thang tat ca thu vien tien ich, tranh loi trung ten object cua libtool.

# ukopt.c/optparse.c: bo doc ~/.unikey/options. Truoc day nam trong src/xim,
# chuyen ve day khi tang XIM bi go bo -- gio moi front-end deu dung chung.
libukbridge_la_SOURCES = \
	ukbridge.c \
	ukbridge.h \
	ukkeys.c \
	ukkeys.h \
	ukopt.c ukopt.h \
	optparse.c optparse.h

check_PROGRAMS = test-bridge
TESTS = test-bridge

test_bridge_SOURCES = test-bridge.c dummy.cpp
test_bridge_LDADD = libukbridge.la ../ukinterface/libukint.la \
	../ukengine/libUnikey.la ../vnconv/libvnconv.la ../byteio/libbyteio.la
