#                                               -*- Autoconf -*-
# Process this file with autoconf to produce a configure script.
AC_PREREQ(2.50)
AC_INIT([Unikey X Input Method], [1.0.4], [unikey@gmail.com], [x-unikey])
AC_CONFIG_SRCDIR([src/ukbridge/ukbridge.c])
AM_INIT_AUTOMAKE([dist-bzip2])
AC_CONFIG_HEADER([config.h])
AC_PROG_LIBTOOL

# Checks for programs.
AC_PROG_CXX
AC_PROG_CC
AC_PROG_CPP
AC_PROG_RANLIB
AC_PROG_INSTALL
AC_PROG_LN_S
AC_PROG_MAKE_SET
#AC_PROG_RANLIB

#### customization

AC_MSG_CHECKING([for some Win32 platform])
case "$host" in
  *-*-mingw*|*-*-cygwin*)
    platform_win32=yes
    ;;
  *)
    platform_win32=no
    ;;
esac
AC_MSG_RESULT([$platform_win32])
AM_CONDITIONAL(PLATFORM_WIN32, test "$platform_win32" = "yes")
####

# Checks for header files.
AC_HEADER_STDC
AC_HEADER_STDBOOL
AC_HEADER_SYS_WAIT
AC_CHECK_HEADERS([fcntl.h libintl.h locale.h malloc.h memory.h stddef.h stdlib.h string.h sys/param.h unistd.h time.h])

# Checks for typedefs, structures, and compiler characteristics.
AC_C_CONST
AC_C_INLINE
AC_TYPE_PID_T
AC_TYPE_SIZE_T

# Checks for library functions.
AC_FUNC_FORK
AC_FUNC_MALLOC
AC_FUNC_SETVBUF_REVERSED
AC_TYPE_SIGNAL
AC_CHECK_FUNCS([gethostname memmove memset setlocale strcasecmp strchr strdup strrchr strtol strtoul mkdir strstr])

# Phai goi tuong minh o day: cac PKG_CHECK_MODULES ben duoi deu nam trong
# khoi "if", neu de autoconf tu chen thi $PKG_CONFIG chi duoc gan khi khoi do
# chay -- gtk2 mac dinh tat nen gtk3 se khong bao gio tim thay.
PKG_PROG_PKG_CONFIG

AC_ARG_WITH(libdir, 
	AC_HELP_STRING([--with-libdir=PATH],
		[System lib dir to install gtk module. [[automatic]]]), 
	sys_libdir=$with_libdir, [sys_libdir=""])
AC_SUBST(sys_libdir)

####################################################################
# GTK3 input method module (standalone: khong can ibus/fcitx5)
# Mac dinh: build neu tim thay gtk+-3.0
####################################################################
AC_ARG_WITH(gtk3,
	AC_HELP_STRING([--without-gtk3],
		[Do not build the GTK3 input method module (default: build if gtk+-3.0 is found)]),
	[unikey_gtk3=$withval], [unikey_gtk3=check])

if test "X$unikey_gtk3" != "Xno" ; then
	PKG_CHECK_MODULES(GTK3, gtk+-3.0 >= 3.22,
		[unikey_gtk3=yes],
		[if test "X$unikey_gtk3" = "Xyes" ; then
			AC_MSG_ERROR([--with-gtk3 was given but gtk+-3.0 >= 3.22 was not found])
		 else
			unikey_gtk3=no
			AC_MSG_WARN([gtk+-3.0 not found, GTK3 module will not be built])
		 fi])
fi

if test "X$unikey_gtk3" = "Xyes" ; then
	AC_MSG_CHECKING([gtk3 binary version])
		GTK3_BINARY_VERSION=`pkg-config gtk+-3.0 --variable=gtk_binary_version`
		AC_MSG_RESULT([$GTK3_BINARY_VERSION])
		AC_SUBST(GTK3_BINARY_VERSION)

	AC_MSG_CHECKING([gtk3 libdir])
		if test "X$sys_libdir" = "X" ; then
			GTK3_LIBDIR=`pkg-config gtk+-3.0 --variable=libdir`
		else
			GTK3_LIBDIR=$sys_libdir
		fi
		AC_MSG_RESULT([$GTK3_LIBDIR])
		AC_SUBST(GTK3_LIBDIR)
fi

AM_CONDITIONAL([COND_UNIKEY_GTK3], [test "X$unikey_gtk3" = "Xyes"])

####################################################################
# GTK4 input method module
####################################################################
AC_ARG_WITH(gtk4,
	AC_HELP_STRING([--without-gtk4],
		[Do not build the GTK4 input method module (default: build if gtk4 is found)]),
	[unikey_gtk4=$withval], [unikey_gtk4=check])

if test "X$unikey_gtk4" != "Xno" ; then
	PKG_CHECK_MODULES(GTK4, gtk4 >= 4.6,
		[unikey_gtk4=yes],
		[if test "X$unikey_gtk4" = "Xyes" ; then
			AC_MSG_ERROR([--with-gtk4 was given but gtk4 >= 4.6 was not found])
		 else
			unikey_gtk4=no
			AC_MSG_WARN([gtk4 not found, GTK4 module will not be built])
		 fi])
fi

if test "X$unikey_gtk4" = "Xyes" ; then
	AC_MSG_CHECKING([gtk4 binary version])
		GTK4_BINARY_VERSION=`pkg-config gtk4 --variable=gtk_binary_version`
		AC_MSG_RESULT([$GTK4_BINARY_VERSION])
		AC_SUBST(GTK4_BINARY_VERSION)

	AC_MSG_CHECKING([gtk4 libdir])
		if test "X$sys_libdir" = "X" ; then
			GTK4_LIBDIR=`pkg-config gtk4 --variable=libdir`
		else
			GTK4_LIBDIR=$sys_libdir
		fi
		AC_MSG_RESULT([$GTK4_LIBDIR])
		AC_SUBST(GTK4_LIBDIR)
fi

AM_CONDITIONAL([COND_UNIKEY_GTK4], [test "X$unikey_gtk4" = "Xyes"])

####################################################################
# Qt6 platform input context plugin
# Can ca header private (QPlatformInputContext la API private cua Qt)
####################################################################
AC_ARG_WITH(qt6,
	AC_HELP_STRING([--without-qt6],
		[Do not build the Qt6 input context plugin (default: build if Qt6Gui is found)]),
	[unikey_qt6=$withval], [unikey_qt6=check])

if test "X$unikey_qt6" != "Xno" ; then
	PKG_CHECK_MODULES(QT6, Qt6Gui >= 6.2,
		[unikey_qt6=yes],
		[if test "X$unikey_qt6" = "Xyes" ; then
			AC_MSG_ERROR([--with-qt6 was given but Qt6Gui >= 6.2 was not found])
		 else
			unikey_qt6=no
			AC_MSG_WARN([Qt6Gui not found, Qt6 plugin will not be built])
		 fi])
fi

if test "X$unikey_qt6" = "Xyes" ; then
	QT6_VERSION=`pkg-config Qt6Gui --modversion`
	QT6_INCDIR=`pkg-config Qt6Gui --variable=includedir`
	QT6_LIBEXEC=`pkg-config Qt6Core --variable=libexecdir`

	AC_MSG_CHECKING([for Qt6 private headers])
	if test -f "$QT6_INCDIR/QtGui/$QT6_VERSION/QtGui/qpa/qplatforminputcontext.h" ; then
		AC_MSG_RESULT([$QT6_INCDIR/QtGui/$QT6_VERSION])
		QT6_CFLAGS="$QT6_CFLAGS -I$QT6_INCDIR/QtGui/$QT6_VERSION -I$QT6_INCDIR/QtCore/$QT6_VERSION"
	else
		AC_MSG_RESULT([khong tim thay])
		unikey_qt6=no
		AC_MSG_WARN([Qt6 private headers missing (cai qt6-base-private-dev), Qt6 plugin will not be built])
	fi
fi

if test "X$unikey_qt6" = "Xyes" ; then
	AC_MSG_CHECKING([for moc])
	if test -x "$QT6_LIBEXEC/moc" ; then
		QT6_MOC="$QT6_LIBEXEC/moc"
		AC_MSG_RESULT([$QT6_MOC])
	else
		AC_PATH_PROGS([QT6_MOC], [moc-qt6 moc], [no])
		if test "X$QT6_MOC" = "Xno" ; then
			unikey_qt6=no
			AC_MSG_WARN([moc not found, Qt6 plugin will not be built])
		fi
	fi
	AC_SUBST(QT6_MOC)

	QT6_PLUGINDIR=`pkg-config Qt6Gui --variable=libdir`/qt6/plugins
	AC_SUBST(QT6_PLUGINDIR)

	# moc chi hieu -I va -D, khong hieu co compiler (-Wall, -f...). No van can
	# duong dan header Qt de giai macro IID trong Q_PLUGIN_METADATA.
	QT6_MOCFLAGS=`echo $QT6_CFLAGS | tr ' ' '\n' | grep '^-I' | tr '\n' ' '`
	AC_SUBST(QT6_MOCFLAGS)
fi

AM_CONDITIONAL([COND_UNIKEY_QT6], [test "X$unikey_qt6" = "Xyes"])

####################################################################
# IBus engine
#
# Day la front-end duy nhat co the hien trong GNOME Settings -> Input Sources:
# `gsettings describe org.gnome.desktop.input-sources sources` cho biet chi co
# hai loai nguon la "xkb" va "ibus". Module toolkit khong bao gio vao duoc do.
####################################################################
AC_ARG_WITH(ibus,
	AC_HELP_STRING([--without-ibus],
		[Do not build the IBus engine (default: build if ibus-1.0 is found)]),
	[unikey_ibus=$withval], [unikey_ibus=check])

if test "X$unikey_ibus" != "Xno" ; then
	PKG_CHECK_MODULES(IBUS, ibus-1.0 >= 1.5,
		[unikey_ibus=yes],
		[if test "X$unikey_ibus" = "Xyes" ; then
			AC_MSG_ERROR([--with-ibus was given but ibus-1.0 >= 1.5 was not found])
		 else
			unikey_ibus=no
			AC_MSG_WARN([ibus-1.0 not found (cai libibus-1.0-dev), IBus engine will not be built])
		 fi])
fi

if test "X$unikey_ibus" = "Xyes" ; then
	# Phai lay datadir CUA IBUS, khong phai $datadir cua goi nay: ibus chi quet
	# thu muc component nam trong datadir cua chinh no. Cai vao
	# /usr/local/share/ibus/component thi ibus khong bao gio thay -> engine
	# khong hien trong Settings ma cung khong bao loi gi.
	AC_MSG_CHECKING([ibus component dir])
		IBUS_COMPONENTDIR=`pkg-config ibus-1.0 --variable=datadir`/ibus/component
		AC_MSG_RESULT([$IBUS_COMPONENTDIR])
		AC_SUBST(IBUS_COMPONENTDIR)
fi

AM_CONDITIONAL([COND_UNIKEY_IBUS], [test "X$unikey_ibus" = "Xyes"])

AH_VERBATIM([_GNU_SOURCE],
[/* Enable GNU extensions on systems that have them.  */
#ifndef _GNU_SOURCE
# define _GNU_SOURCE
#endif])

AC_CONFIG_FILES([Makefile
  src/Makefile
  src/ukengine/Makefile
  src/ukbridge/Makefile
  src/unikey-gtk3/Makefile
  src/unikey-gtk4/Makefile
  src/unikey-qt/Makefile
  src/unikey-ibus/Makefile
  src/byteio/Makefile
  src/ukinterface/Makefile
  src/vnconv/Makefile
])
AC_OUTPUT
