## Makefile for distrib/initscripts/

SUFFIXES = .tmpl .

pkgconfdir = @PKGCONFDIR@

#
# Template Generation
#

.tmpl:
	sed -e s@:BINDIR:@${bindir}@ \
	    -e s@:SBINDIR:@${sbindir}@ \
	    -e s@:ETCDIR:@${pkgconfdir}@ \
	    <$< >$@

GENERATED_FILES = \
	rc.afpd.netbsd		\
	rc.atalk.redhat		\
	rc.atalk.tru64		\
	rc.atalk.bsd		\
	rc.atalkd.netbsd	\
	rc.atalk.suse		\
	rc.cnid_metad.netbsd	\
	rc.papd.netbsd		\
	rc.timelord.netbsd	\
	rc.atalk.sysv

TEMPLATES = \
	rc.afpd.netbsd.tmpl		\
	rc.atalk.redhat.tmpl		\
	rc.atalk.tru64.tmpl		\
	rc.atalk.bsd.tmpl		\
	rc.atalkd.netbsd.tmpl		\
	rc.atalk.suse.tmpl		\
	rc.cnid_metad.netbsd.tmpl	\
	rc.papd.netbsd.tmpl		\
	rc.timelord.netbsd.tmpl		\
	rc.atalk.sysv.tmpl

CLEANFILES = $(GENERATED_FILES) atalk afpd atalkd papd timelord
EXTRA_DIST = $(TEMPLATES) rc.atalk.cobalt


#
# checking for "redhat" style sysv scripts:
#

if USE_REDHAT

sysvdir	= /etc/rc.d/init.d
sysv_SCRIPTS = atalk

atalk: rc.atalk.redhat
	cp -f rc.atalk.redhat atalk
	chmod a+x atalk

install-data-hook:
	-chkconfig --add atalk

uninstall:
	-chkconfig --del atalk
	rm -f /etc/rc.d/init.d/atalk

endif

#
# checking for "SuSE" style sysv scripts:
#

if USE_SUSE

sysvdir	= /etc/init.d
sysv_SCRIPTS = atalk

atalk: rc.atalk.suse
	cp -f rc.atalk.suse atalk
	chmod a+x atalk

install-data-hook:
	-insserv atalk

uninstall:
	-insserv -d atalk
	rm -f /etc/init.d/atalk

endif

#
#
# checking for "cobalt" style sysv scripts:
#

if USE_COBALT

sysvdir	= /etc/rc.d/init.d
sysv_SCRIPTS = atalk

atalk: rc.atalk.cobalt
	cp -f rc.atalk.cobalt atalk
	chmod a+x atalk

endif

#
# checking for "tru64" style sysv scripts:
#

if USE_TRU64

sysvdir	= /sbin/init.d
sysv_SCRIPTS = atalk

atalk: rc.atalk.tru64
	cp -f rc.atalk.tru64 atalk
	chmod a+x atalk

endif

#
# checking for NetBSD init scripts
#

if USE_NETBSD

sysvdir = /etc/rc.d
sysv_SCRIPTS = afpd atalkd papd timelord

afpd: rc.afpd.netbsd
	cp -f $< $@

atalkd: rc.atalkd.netbsd
	cp -f $< $@
	chmod a+x $@

papd: rc.papd.netbsd
	cp -f $< $@
	chmod a+x $@

timelord: rc.timelord.netbsd
	cp -f $< $@
	chmod a+x $@

endif

#
# checking for Solaris init scripts
#

if USE_SOLARIS

sysvdir = /etc/init.d
sysv_SCRIPTS = atalk

atalk: rc.atalk.sysv
	cp -f rc.atalk.sysv $@
	chmod a+x $@

install-data-hook:
	rm -f /etc/rc2.d/S90atalk
	-ln -s ../init.d/atalk /etc/rc2.d/S90atalk
	rm -f /etc/rc0.d/K04atalk
	-ln -s ../init.d/atalk /etc/rc0.d/K04atalk

uninstall-hook:
	rm -f /etc/rc2.d/S90atalk /etc/rc0.d/K04atalk

endif
