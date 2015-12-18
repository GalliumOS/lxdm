NULL =

sbin_PROGRAMS = lxdm-binary

lxdm_binary_CFLAGS = \
	$(GLIB_CFLAGS) \
	-DCONFIG_FILE=\"@sysconfdir@/@PACKAGE@/@PACKAGE@.conf\" \
	-DXSESSIONS_DIR=\"@datarootdir@/xsessions\" \
	-DLXDM_DATA_DIR=@datadir@/@PACKAGE@ \
	-DLXDM_NUMLOCK_PATH=\"@libexecdir@/lxdm-numlock\" \
	-DLXDM_SESSION_PATH=\"@libexecdir@/lxdm-session\" \
	$(CONSOLEKIT_CFLAGS) \
	-Werror-implicit-function-declaration \
	-Wall \
	$(NULL)

lxdm_binary_LDADD = \
	$(GLIB_LIBS) \
	$(XCONN_LIBS) \
	$(CONSOLEKIT_LIBS) \
	$(NULL)

lxdm_binary_SOURCES = \
	lxdm.c lxdm.h ui.c lxcom.c lxcom.h xconn.c xconn.h auth.c auth.h lxcommon.h \
	$(NULL)

libexec_PROGRAMS = lxdm-greeter-gdk lxdm-numlock lxdm-greeter-gtk lxdm-session

lxdm_greeter_gtk_CFLAGS = \
	$(GTK_CFLAGS) \
	-DCONFIG_FILE=\"@sysconfdir@/@PACKAGE@/@PACKAGE@.conf\" \
	-DXSESSIONS_DIR=\"@datarootdir@/xsessions\" \
	-DLXDM_DATA_DIR=\"@datadir@/@PACKAGE@\" \
	-DPACKAGE_LOCALE_DIR=\"@localedir@\" \
	-DGNOMELOCALEDIR=\"@localedir@\" \
	-DLIBLOCALEDIR=\"@prefix@/lib/locale\" \
	-Werror-implicit-function-declaration \
	-Wall \
	$(NULL)

lxdm_greeter_gtk_LDADD = \
	$(XLIB_LIBS) \
	$(GTK_LIBS) \
	$(NULL)

lxdm_greeter_gtk_SOURCES = \
	greeter.c \
	lang.c lang.h \
	gdm/gdm-languages.c gdm/gdm-languages.h \
	gdm/locarchive.h \
	lxcom.c lxcom.h \
	greeter-utils.c greeter-utils.h \
	lxcommon.h \
	$(NULL)

lxdm_greeter_gdk_CFLAGS = \
	$(GTK_CFLAGS) \
	-DCONFIG_FILE=\"@sysconfdir@/@PACKAGE@/@PACKAGE@.conf\" \
	-DXSESSIONS_DIR=\"@datarootdir@/xsessions\" \
	-DLXDM_DATA_DIR=\"@datadir@/@PACKAGE@\" \
	-DPACKAGE_LOCALE_DIR=\"@localedir@\" \
	-DGNOMELOCALEDIR=\"@localedir@\" \
	-DLIBLOCALEDIR=\"@prefix@/lib/locale\" \
	-Werror-implicit-function-declaration \
	-Wall \
	$(NULL)

lxdm_greeter_gdk_LDADD = \
	$(XLIB_LIBS) \
	$(GDK_LIBS) \
	$(NULL)

lxdm_greeter_gdk_SOURCES = \
	greeter-gdk.c greeter-utils.c

lxdm_numlock_SOURCES = \
	numlock.c \
	$(NULL)

lxdm_numlock_CFLAGS = \
	$(XLIB_CFLAGS) \
	-DLXDM_NUMLOCK_PATH=\"@libexecdir@/lxdm-numlock\" \
	$(NULL)

lxdm_numlock_LDADD = \
	$(XLIB_LIBS) \
	$(NULL)

lxdm_session_SOURCES = \
	pam.c \
	$(NULL)

lxdm_session_CFLAGS = \
	$(GLIB_CFLAGS) \
	$(NULL)

lxdm_session_LDADD = \
	$(GLIB_LIBS) \
	$(NULL)

bin_PROGRAMS = lxdm-config

lxdm_config_CFLAGS = \
	$(GTK_CFLAGS) \
	-DCONFIG_FILE=\"@sysconfdir@/@PACKAGE@/@PACKAGE@.conf\" \
	-DLXDM_DATA_DIR=\"@datadir@/@PACKAGE@\" \
	$(NULL)

lxdm_config_LDADD = \
	$(GTK_LIBS) \
	$(NULL)

lxdm_config_SOURCES = \
	config.c \
	$(NULL)


