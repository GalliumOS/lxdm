#                                               -*- Autoconf -*-
# Process this file with autoconf to produce a configure script.

AC_PREREQ([2.63])
AC_INIT([lxdm], [0.5.3], [http://lxde.org/])
AM_INIT_AUTOMAKE([-Wall -Werror foreign subdir-objects no-dist-gzip dist-xz])

AC_CONFIG_SRCDIR([src/lxdm.c])
AC_CONFIG_HEADERS([config.h])

# Checks for programs.
AC_PROG_CC
AC_PROG_INSTALL
AM_PROG_CC_C_O

# Checks for libraries.
AC_CHECK_LIB([crypt], [crypt])

# Check for PAM support
AC_ARG_WITH(pam, AC_HELP_STRING([--with-pam],[Use PAM for authentication]),
	[USE_PAM=$withval], [USE_PAM=$use_pam_default])
if test "x$USE_PAM" != "xno" ; then
	AC_SEARCH_LIBS(pam_open_session,[pam])
	AC_CHECK_FUNC(pam_open_session, 
		[USE_PAM=1
			AC_DEFINE(USE_PAM,1,[Use PAM for authentication])],
		[if test "x$USE_PAM" != "xtry" ; then
			AC_MSG_ERROR(["PAM support requested, but pam_open_session not found."])
		 fi])
fi
AM_CONDITIONAL(HAVE_PAM, [test -n "$USE_PAM" -a "x$USE_PAM" != xno ])

# Checks for header files.
AC_PATH_X
AC_CHECK_HEADERS([shadow.h stdlib.h string.h unistd.h utmpx.h])

# Checks for typedefs, structures, and compiler characteristics.
AC_TYPE_PID_T

# Checks for library functions.
AC_FUNC_FORK
AC_CHECK_FUNCS([atexit endpwent getspnam getusershell memset putenv strstr])

PKG_CONFIG=pkg-config # FIXME: is this OK?
# Check for ISO Codes, this is taken from gdm
AC_MSG_CHECKING([whether iso-codes has iso-639 domain])
if $PKG_CONFIG --variable=domains iso-codes | grep 639 >/dev/null ; then
	AC_MSG_RESULT([yes])
else
	AC_MSG_RESULT([no])
fi
AC_DEFINE_UNQUOTED([ISO_CODES_PREFIX],["`$PKG_CONFIG --variable=prefix iso-codes`"],[ISO codes prefix])
ISO_CODES=iso-codes

IT_PROG_INTLTOOL([0.40.0])

PKG_CHECK_MODULES(XLIB,x11)
AC_SUBST(XLIB_CFLAGS)
AC_SUBST(XLIB_LIBS)

PKG_CHECK_MODULES(GLIB,glib-2.0)
AC_SUBST(GLIB_CFLAGS)
AC_SUBST(GLIB_LIBS)

AC_ARG_ENABLE(gtk3,
AC_HELP_STRING([--enable-gtk3],[enable to use gtk-3.0 instead of gtk-2.0]),
[case "${enableval}" in
  yes)  enable_gtk3=yes ;;
  no)   enable_gtk3=no ;;
  *) AC_MSG_ERROR([bad value "${enableval}" for --enable-gtk3, use "yes" (default) or "no".]) ;;
esac],[])

if test "x$enable_gtk3" = "xyes" ; then
  pkg_modules="gdk-3.0"
else
  pkg_modules="gdk-2.0"
fi
PKG_CHECK_MODULES(GDK,[$pkg_modules])
AC_SUBST(GDK_CFLAGS)
AC_SUBST(GDK_LIBS)

if test "x$enable_gtk3" = "xyes" ; then
  CFLAGS="$CFLAGS -DENABLE_GTK3"
  pkg_modules="gtk+-3.0 >= 3.0.0"
else
  pkg_modules="gtk+-2.0 >= 2.12.0"
fi
PKG_CHECK_MODULES(GTK, [$pkg_modules])
AC_SUBST(GTK_CFLAGS)
AC_SUBST(GTK_LIBS)


AC_ARG_ENABLE(consolekit,
	AC_HELP_STRING([--disable-consolekit],[disable ConsoleKit support]),
	[enable_consolekit=$enableval],
	[enable_consolekit=yes]
)

if test "x$enable_consolekit" = "xyes" ; then
  PKG_CHECK_MODULES(CONSOLEKIT, "ck-connector",[
	AC_SUBST(CONSOLEKIT_CFLAGS)
	AC_SUBST(CONSOLEKIT_LIBS)],[
	echo "ConsoleKit devel package not found"
	])
  AC_CHECK_LIB([ck-connector],[ck_connector_open_session])
fi


AC_ARG_ENABLE(password,
AC_HELP_STRING([--enable-password],[enable to load autologin password store at config file]),
[case "${enableval}" in
  yes)  enable_password=yes ;;
  no)   enable_password=no ;;
  *) AC_MSG_ERROR([bad value "${enableval}" for --enable-password, use "yes" (default) or "no".]) ;;
esac],[])

if test "x$enable_password" = "xyes" ; then
  CFLAGS="$CFLAGS -DENABLE_PASSWORD"
fi

AC_ARG_ENABLE(xauth,
	AC_HELP_STRING([--disable-xauth],[disable the xauth support ]),
	[enable_xauth=$enablevar],
	[enable_xauth=yes]
)

if test "x$enable_xauth" = "x$no" ; then
  CFLAGS="$CFLAGS -DDISABLE_XAUTH"
fi

AC_ARG_ENABLE(debug,
    AC_HELP_STRING([--enable-debug],[enable detailed debug]),
    [enable_debug=$enablevar],
    [enable_debug=yes]
)
if test "x$enable_debug" = "x$yes" ; then
	CFLAGS="$CFLAGS -DLXDM_DEBUG"
fi

AC_ARG_WITH(xconn,
	[AC_HELP_STRING([--with-xconn=@<:@xlib/xcb@:>@],[use xlib or xcb to use])],
	[if test "x$with_xconn" = "xxlib"; then
		PKG_CHECK_MODULES(XCONN,"x11")
		AC_SUBST(XCONN_CFLAGS)
		AC_SUBST(XCONN_LIBS)
		CFLAGS="$CFLAGS -DLXDM_XCONN_XLIB"

	 else
		PKG_CHECK_MODULES(XCONN,"xcb")
		AC_SUBST(XCONN_CFLAGS)
		AC_SUBST(XCONN_LIBS)
		CFLAGS="$CFLAGS -DLXDM_XCONN_XCB"
	 fi],
	[PKG_CHECK_MODULES(XCONN,"xcb")
	 AC_SUBST(XCONN_CFLAGS)
	 AC_SUBST(XCONN_LIBS)
	 CFLAGS="$CFLAGS -DLXDM_XCONN_XCB"
	]
)

AC_ARG_WITH([systemdsystemunitdir],
        AS_HELP_STRING([--with-systemdsystemunitdir=DIR], [Directory for systemd service files]),
        [], [with_systemdsystemunitdir=$($PKG_CONFIG --variable=systemdsystemunitdir systemd)])
if test "x$with_systemdsystemunitdir" != xno; then
        AC_SUBST([systemdsystemunitdir], [$with_systemdsystemunitdir])
fi
AM_CONDITIONAL(HAVE_SYSTEMD, [test -n "$with_systemdsystemunitdir" -a "x$with_systemdsystemunitdir" != xno ])

GETTEXT_PACKAGE=lxdm
AC_SUBST(GETTEXT_PACKAGE)
AC_DEFINE_UNQUOTED(GETTEXT_PACKAGE,"$GETTEXT_PACKAGE", [Gettext package.])

AC_DEFUN([AS_AC_EXPAND],[
	EXP_VAR=[$1]
	FROM_VAR=[$2]
	dnl first expand prefix and exec_prefix if necessary
	prefix_save=$prefix
	exec_prefix_save=$exec_prefix
	dnl if no prefix given, then use /usr/local, the default prefix
	if test "x$prefix" = "xNONE"; then
	prefix="$ac_default_prefix"
	fi
	dnl if no exec_prefix given, then use prefix
	if test "x$exec_prefix" = "xNONE"; then
	exec_prefix=$prefix
	fi
	full_var="$FROM_VAR"
	dnl loop until it doesn not change anymore
	while true; do
	new_full_var="`eval echo $full_var`"
	if test "x$new_full_var" = "x$full_var"; then break; fi
	full_var=$new_full_var
	done
	dnl clean up
	full_var=$new_full_var
	AC_SUBST([$1], "$full_var")
	dnl restore prefix and exec_prefix
	prefix=$prefix_save
	exec_prefix=$exec_prefix_save
])
AS_AC_EXPAND(FULL_LIBEXECDIR, $libexecdir)

AC_CONFIG_FILES([
	Makefile
	lxdm.spec
	src/Makefile
	po/Makefile.in
	pam/Makefile
	systemd/Makefile
	data/Makefile
	data/PostLogin
	data/PostLogout
	data/PreLogin
	data/PreReboot
	data/PreShutdown
	data/LoginReady
	data/lxdm.conf
])
AC_CONFIG_FILES([data/lxdm], [chmod +x data/lxdm])
AC_OUTPUT

dnl sanity checks
if test "$sysconfdir" != "/etc"; then
   AC_MSG_WARN(sysconfdir is not /etc.)
   AC_MSG_WARN(Please consider passing --sysconfdir=/etc to configure.)
fi
