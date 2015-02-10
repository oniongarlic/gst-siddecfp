plugin_LTLIBRARIES = libgstsidfp.la

libgstsidfp_la_SOURCES = gstsiddecfp.cpp
libgstsidfp_la_CXXFLAGS = $(GST_CFLAGS) $(GST_CXXFLAGS) $(SIDPLAY_CFLAGS)
libgstsidfp_la_LIBADD = $(GST_LIBS) $(GST_LIBS) $(SIDPLAY_LIBS) -lgstaudio-1.0
libgstsidfp_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgstsidfp_la_LIBTOOLFLAGS = --tag=disable-static

noinst_HEADERS = gstsiddecfp.h
