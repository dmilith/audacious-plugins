PLUGIN = ffaudio${PLUGIN_SUFFIX}

SRCS = ffaudio-core.cc ffaudio-io.cc

include ../../buildsys.mk
include ../../extra.mk

plugindir := ${plugindir}/${INPUT_PLUGIN_DIR}

LD = ${CXX}

CFLAGS += ${PLUGIN_CFLAGS}
CPPFLAGS += ${PLUGIN_CPPFLAGS} ${GTK_CFLAGS} ${GLIB_CFLAGS} ${FFMPEG_CFLAGS} -I../..
LIBS += ${GTK_LIBS} ${GLIB_LIBS} ${FFMPEG_LIBS} -laudtag
