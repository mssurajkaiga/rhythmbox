NULL =

plugindir = $(PLUGINDIR)/cd-recorder
plugin_LTLIBRARIES = libcd-recorder.la

libcd_recorder_la_SOURCES =					\
	rb-disc-recorder-plugin.c				\
	$(NULL)

libcd_recorder_la_LDFLAGS = $(PLUGIN_LIBTOOL_FLAGS)
libcd_recorder_la_LIBTOOLFLAGS = --tag=disable-static
libcd_recorder_la_LIBADD = 				\
	$(top_builddir)/shell/librhythmbox-core.la	\
	$(LIBBRASERO_MEDIA_LIBS)			\
	$(NULL)

AM_CPPFLAGS = 						\
        -DGNOMELOCALEDIR=\""$(datadir)/locale"\"        \
	-DG_LOG_DOMAIN=\"Rhythmbox\"		 	\
	-I$(top_srcdir) 				\
	-I$(top_srcdir)/lib                        	\
	-I$(top_builddir)/lib				\
	-I$(top_srcdir)/rhythmdb                       	\
	-I$(top_srcdir)/widgets                    	\
	-I$(top_srcdir)/sources                    	\
	-I$(top_srcdir)/plugins				\
	-I$(top_srcdir)/shell				\
	-DPIXMAP_DIR=\""$(datadir)/pixmaps"\"		\
	-DSHARE_DIR=\"$(pkgdatadir)\"                   \
	-DDATADIR=\""$(datadir)"\"			\
	$(RHYTHMBOX_CFLAGS)				\
	$(LIBBRASERO_MEDIA_CFLAGS)			\
	-D_BSD_SOURCE			\
	$(NULL)

plugin_in_files = cd-recorder.plugin.in

%.plugin: %.plugin.in $(INTLTOOL_MERGE) $(wildcard $(top_srcdir)/po/*po) ; $(INTLTOOL_MERGE) $(top_srcdir)/po $< $@ -d -u -c $(top_builddir)/po/.intltool-merge-cache

BUILT_SOURCES =						\
	$(plugin_in_files:.plugin.in=.plugin) 		\
	$(NULL)

plugin_DATA = 			\
	$(BUILT_SOURCES)	\
	$(NULL)

EXTRA_DIST = 			\
	$(plugin_in_files)	\
	$(NULL)

CLEANFILES = 			\
	$(BUILT_SOURCES)	\
	$(NULL)

DISTCLEANFILES =		\
	$(BUILT_SOURCES)	\
	$(NULL)
