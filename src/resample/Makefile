PLUGIN = resample${PLUGIN_SUFFIX}

SRCS = resample.cc

include ../../buildsys.mk
include ../../extra.mk

plugindir := ${plugindir}/${EFFECT_PLUGIN_DIR}

LD = ${CXX}

CFLAGS += ${PLUGIN_CFLAGS}
CPPFLAGS += ${PLUGIN_CPPFLAGS} ${GLIB_CFLAGS} -I../..
LIBS += ${GLIB_LIBS} -lsamplerate
