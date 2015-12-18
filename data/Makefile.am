FULL_LIBEXECDIR=$(libexecdir)

lxdm.conf: lxdm.conf.in
	cat lxdm.conf.in | sed 's,@FULL_LIBEXECDIR@,$(FULL_LIBEXECDIR),' > $@

NULL=

sbin_SCRIPTS = lxdm
CLEANFILES = $(sbin_SCRIPTS)

lxdmdir = $(datadir)/lxdm
lxdm_DATA = \
	lxdm.glade \
	config.ui \
	$(NULL)

lxdm_confdir = /etc/lxdm
lxdm_conf_DATA = \
	PostLogin \
	PostLogout \
	PreLogin \
	PreReboot \
	PreShutdown \
	LoginReady \
	lxdm.conf \
	Xsession \
	$(NULL)

install-data-hook:
	chmod +x $(DESTDIR)/etc/lxdm/Xsession \
		$(DESTDIR)/etc/lxdm/PostLogin \
		$(DESTDIR)/etc/lxdm/PostLogout \
		$(DESTDIR)/etc/lxdm/PreLogin \
		$(DESTDIR)/etc/lxdm/PreReboot \
		$(DESTDIR)/etc/lxdm/PreShutdown \
		$(DESTDIR)/etc/lxdm/LoginReady
	chmod 640 $(DESTDIR)/etc/lxdm/lxdm.conf

themedir=$(datadir)/lxdm/themes/Industrial
theme_DATA= \
	themes/Industrial/index.theme \
	themes/Industrial/gtkrc \
	themes/Industrial/wave.svg \
	themes/Industrial/login.png \
	themes/Industrial/shade.png \
	themes/Industrial/lang.png \
	themes/Industrial/session.png \
	themes/Industrial/greeter.ui \
	themes/Industrial/greeter-gtk3.ui \
	themes/Industrial/gtk.css \
	themes/Industrial/nobody.png \
	$(NULL)

EXTRA_DIST= \
	$(lxdm_DATA) \
	$(lxdm_conf_DATA) \
	$(theme_DATA) \
	lxdm.conf.in \
	$(NULL)

