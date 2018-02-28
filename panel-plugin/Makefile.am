AM_CPPFLAGS = \
	-I$(top_srcdir) \
	-DPACKAGE_LOCALE_DIR=\"$(localedir)\" \
	-DSBINDIR="\"$(sbindir)\""	\
	$(PLATFORM_CPPFLAGS)

plugindir = $(libdir)/xfce4/panel/plugins

plugin_LTLIBRARIES = \
	libbattery-plugin.la

libbattery_plugin_la_SOURCES = \
	xfpm-icons.h	\
	xfpm-power-common.h	\
	xfpm-power-common.c	\
	battery-plugin.h \
	battery-plugin.c \
	$(NULL)

libbattery_plugin_la_CFLAGS = \
	$(GLIB_CFLAGS) \
	$(GTK_CFLAGS) \
	$(UPOWER_CFLAGS) \
	$(XFCONF_CFLAGS) \
	$(LIBXFCE4PANEL_CFLAGS) \
	$(LIBXFCE4UTIL_CFLAGS) \
	$(PLATFORM_CFLAGS)

libbattery_plugin_la_LIBADD = \
	$(GLIB_LIBS) \
	$(GTK_LIBS) \
	$(UPOWER_LIBS) \
	$(XFCONF_LIBS) \
	$(LIBXFCE4PANEL_LIBS) \
	$(LIBXFCE4UTIL_LIBS)

libbattery_plugin_la_LDFLAGS = \
	-avoid-version \
	-module \
	-no-undefined \
	-export-symbols-regex '^xfce_panel_module_(preinit|init|construct)' \
	$(PLATFORM_LDFLAGS)


#
# Desktop file
#
desktopdir = \
	$(datadir)/xfce4/panel/plugins

desktop_in_files = \
	battery-plugin.desktop.in.in

desktop_DATA = $(desktop_in_files:.desktop.in.in=.desktop)

@INTLTOOL_DESKTOP_RULE@

EXTRA_DIST = $(desktop_in_files)

DISTCLEANFILES = $(desktop_DATA)
