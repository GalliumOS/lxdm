NULL =

SUBDIRS= \
	src \
	po \
	data \
	$(NULL)

if HAVE_SYSTEMD
SUBDIRS += systemd
endif

if HAVE_PAM
SUBDIRS += pam
endif

rpm: dist @PACKAGE_NAME@.spec
	rpmbuild -bb \
	--define "_sourcedir `pwd`" \
	--define "_builddir `pwd`" \
	--define "_srcrpmdir `pwd`" \
	--define "_rpmdir `pwd`" \
	--define "_specdir `pwd`" \
	@PACKAGE_NAME@.spec

EXTRA_DIST= \
	ChangeLog \
	AUTHORS \
	README \
	NEWS \
	INSTALL \
	COPYING \
	gpl-2.0.txt \
	lgpl-2.1.txt \
	$(NULL)

