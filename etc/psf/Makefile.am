# Makefile.am for etc/psf/

sbin_PROGRAMS = psf psa
sbin_SCRIPTS = etc2ps.sh

pkgdata_DATA = pagecount.ps

psf_SOURCES = psf.c
psa_SOURCES = psa.c

INCLUDES = -I$(top_srcdir)/include -I$(top_srcdir)/sys \
	-DZEROWIDTH \
	-D_PATH_PAP=\"$(bindir)/pap\" \
	-D_PATH_PSORDER=\"$(bindir)/psorder\" \
	-D_PATH_PSA=\"$(sbindir)/psa\" \
	-D_PATH_PSFILTER=\"$(sbindir)/etc2ps.sh\" \
	-D_PATH_PAGECOUNT=\"$(pkgdatadir)/pagecount.ps\"

EXTRA_DIST = etc2ps.sh pagecount.ps

# declare links
psf_LINKS = ofpap ifpap tfpap ifpaprev tfpaprev ofwpap ifwpap \
	 tfwpap ifwpaprev tfwpaprev ofmpap ifmpap tfmpap ifmpaprev \
	 tfmpaprev ofwmpap ifwmpap tfwmpap ifwmpaprev tfwmpaprev


#
# install sections for links
#

install-exec-hook:
	@list='$(psf_LINKS)'; for l in $$list; do \
		rm $(DESTDIR)$(sbindir)/$$l 2>/dev/null || true; \
		(cd $(DESTDIR)$(sbindir) && $(LN_S) psf $$l);  \
	done

#
# cleanup
#

CLEANFILES = $(psf_LINKS)
