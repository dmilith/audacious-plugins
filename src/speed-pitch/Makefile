PLUGIN = speed-pitch${PLUGIN_SUFFIX}

SRCS = speed-pitch.cc

include ../../buildsys.mk
include ../../extra.mk

plugindir := ${plugindir}/${EFFECT_PLUGIN_DIR}

CPPFLAGS += ${PLUGIN_CPPFLAGS} ${GLIB_CFLAGS} -I../..
CFLAGS += ${PLUGIN_CFLAGS}
LIBS += -lm ${GLIB_LIBS} -lsamplerate
