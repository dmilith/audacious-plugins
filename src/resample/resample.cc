/*
 * Sample Rate Converter Plugin for Audacious
 * Copyright 2010-2012 John Lindgren
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 *    this list of conditions, and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions, and the following disclaimer in the documentation
 *    provided with the distribution.
 *
 * This software is provided "as is" and without any warranty, express or
 * implied. In no event shall the authors be liable for any damages arising from
 * the use of this software.
 */

#include <stdio.h>
#include <stdlib.h>

#include <glib.h>

#include <samplerate.h>

#include <libaudcore/i18n.h>
#include <libaudcore/runtime.h>
#include <libaudcore/plugin.h>
#include <libaudcore/preferences.h>
#include <libaudcore/audstrings.h>

#define MIN_RATE 8000
#define MAX_RATE 192000
#define RATE_STEP 50

#define RESAMPLE_ERROR(e) fprintf (stderr, "resample: %s\n", src_strerror (e))

static const char * const resample_defaults[] = {
 "method", "2", /* SRC_SINC_FASTEST */
 "default-rate", "44100",
 "use-mappings", "FALSE",
 "8000", "48000",
 "16000", "48000",
 "22050", "44100",
 "32000", "48000",
 "44100", "44100",
 "48000", "48000",
 "88200", "44100",
 "96000", "96000",
 "176400", "44100",
 "192000", "96000",
 NULL};

static SRC_STATE * state;
static int stored_channels;
static double ratio;
static float * buffer;
static int buffer_samples;

bool_t resample_init (void)
{
    aud_config_set_defaults ("resample", resample_defaults);
    return TRUE;
}

void resample_cleanup (void)
{
    if (state)
    {
        src_delete (state);
        state = NULL;
    }

    g_free (buffer);
    buffer = NULL;
    buffer_samples = 0;
}

void resample_start (int * channels, int * rate)
{
    if (state)
    {
        src_delete (state);
        state = NULL;
    }

    int new_rate = 0;

    if (aud_get_bool ("resample", "use-mappings"))
        new_rate = aud_get_int ("resample", int_to_str (* rate));

    if (! new_rate)
        new_rate = aud_get_int ("resample", "default-rate");

    new_rate = CLAMP (new_rate, MIN_RATE, MAX_RATE);

    if (new_rate == * rate)
        return;

    int method = aud_get_int ("resample", "method");
    int error;

    if ((state = src_new (method, * channels, & error)) == NULL)
    {
        RESAMPLE_ERROR (error);
        return;
    }

    stored_channels = * channels;
    ratio = (double) new_rate / * rate;
    * rate = new_rate;
}

void do_resample (float * * data, int * samples, bool_t finish)
{
    if (! state || ! * samples)
        return;

    if (buffer_samples < (int) (* samples * ratio) + 256)
    {
        buffer_samples = (int) (* samples * ratio) + 256;
        buffer = g_renew (float, buffer, buffer_samples);
    }

    SRC_DATA d = {0};

    d.data_in = * data;
    d.input_frames = * samples / stored_channels;
    d.data_out = buffer;
    d.output_frames = buffer_samples / stored_channels;
    d.src_ratio = ratio;
    d.end_of_input = finish;

    int error;
    if ((error = src_process (state, & d)))
    {
        RESAMPLE_ERROR (error);
        return;
    }

    * data = buffer;
    * samples = stored_channels * d.output_frames_gen;
}

void resample_process (float * * data, int * samples)
{
    do_resample (data, samples, FALSE);
}

void resample_flush (void)
{
    int error;
    if (state && (error = src_reset (state)))
        RESAMPLE_ERROR (error);
}

void resample_finish (float * * data, int * samples)
{
    do_resample (data, samples, TRUE);
    resample_flush ();
}

static const char resample_about[] =
 N_("Sample Rate Converter Plugin for Audacious\n"
    "Copyright 2010-2012 John Lindgren");

static const ComboBoxElements method_list[] = {
 {"3", N_("Skip/repeat samples")}, /* SRC_ZERO_ORDER_HOLD */
 {"4", N_("Linear interpolation")}, /* SRC_LINEAR */
 {"2", N_("Fast sinc interpolation")}, /* SRC_SINC_FASTEST */
 {"1", N_("Medium sinc interpolation")}, /* SRC_SINC_MEDIUM_QUALITY */
 {"0", N_("Best sinc interpolation")}}; /* SRC_SINC_BEST_QUALITY */

static const PreferencesWidget resample_widgets[] = {
    WidgetLabel (N_("<b>Conversion</b>")),
    WidgetCombo (N_("Method:"),
        {VALUE_STRING, 0, "resample", "method"},
        {method_list, ARRAY_LEN (method_list)}),
    WidgetSpin (N_("Rate:"),
        {VALUE_INT, 0, "resample", "default-rate"},
        {MIN_RATE, MAX_RATE, RATE_STEP, N_("Hz")}),
    WidgetLabel (N_("<b>Rate Mappings</b>")),
    WidgetCheck (N_("Use rate mappings"),
        {VALUE_BOOLEAN, 0, "resample", "use-mappings"}),
    WidgetSpin (N_("8 kHz:"),
        {VALUE_INT, 0, "resample", "8000"},
        {MIN_RATE, MAX_RATE, RATE_STEP, N_("Hz")},
        WIDGET_CHILD),
    WidgetSpin (N_("16 kHz:"),
        {VALUE_INT, 0, "resample", "16000"},
        {MIN_RATE, MAX_RATE, RATE_STEP, N_("Hz")},
        WIDGET_CHILD),
    WidgetSpin (N_("22.05 kHz:"),
        {VALUE_INT, 0, "resample", "22050"},
        {MIN_RATE, MAX_RATE, RATE_STEP, N_("Hz")},
        WIDGET_CHILD),
    WidgetSpin (N_("32.0 kHz:"),
        {VALUE_INT, 0, "resample", "32000"},
        {MIN_RATE, MAX_RATE, RATE_STEP, N_("Hz")},
        WIDGET_CHILD),
    WidgetSpin (N_("44.1 kHz:"),
        {VALUE_INT, 0, "resample", "44100"},
        {MIN_RATE, MAX_RATE, RATE_STEP, N_("Hz")},
        WIDGET_CHILD),
    WidgetSpin (N_("48 kHz:"),
        {VALUE_INT, 0, "resample", "48000"},
        {MIN_RATE, MAX_RATE, RATE_STEP, N_("Hz")},
        WIDGET_CHILD),
    WidgetSpin (N_("88.2 kHz:"),
        {VALUE_INT, 0, "resample", "88200"},
        {MIN_RATE, MAX_RATE, RATE_STEP, N_("Hz")},
        WIDGET_CHILD),
    WidgetSpin (N_("96 kHz:"),
        {VALUE_INT, 0, "resample", "96000"},
        {MIN_RATE, MAX_RATE, RATE_STEP, N_("Hz")},
        WIDGET_CHILD),
    WidgetSpin (N_("176.4 kHz:"),
        {VALUE_INT, 0, "resample", "176400"},
        {MIN_RATE, MAX_RATE, RATE_STEP, N_("Hz")},
        WIDGET_CHILD),
    WidgetSpin (N_("192 kHz:"),
        {VALUE_INT, 0, "resample", "192000"},
        {MIN_RATE, MAX_RATE, RATE_STEP, N_("Hz")},
        WIDGET_CHILD)
};

static const PluginPreferences resample_prefs = {
    resample_widgets,
    ARRAY_LEN (resample_widgets)
};

#define AUD_PLUGIN_NAME        N_("Sample Rate Converter")
#define AUD_PLUGIN_ABOUT       resample_about
#define AUD_PLUGIN_PREFS       & resample_prefs
#define AUD_PLUGIN_INIT        resample_init
#define AUD_PLUGIN_CLEANUP     resample_cleanup
#define AUD_EFFECT_START       resample_start
#define AUD_EFFECT_PROCESS     resample_process
#define AUD_EFFECT_FLUSH       resample_flush
#define AUD_EFFECT_FINISH      resample_finish
#define AUD_EFFECT_ORDER       2  /* must be before crossfade */

#define AUD_DECLARE_EFFECT
#include <libaudcore/plugin-declare.h>
