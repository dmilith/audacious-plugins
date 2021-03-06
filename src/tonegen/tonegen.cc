/*
 *  Copyright 2000, 2001 Håvard Kvålen <havardk@sol.no>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 */

#include <libaudcore/i18n.h>
#include <libaudcore/input.h>
#include <libaudcore/plugin.h>

#include <glib.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#define MIN_FREQ        10
#define MAX_FREQ        20000
#define OUTPUT_FREQ     44100
#define BUF_SAMPLES     512
#define BUF_BYTES       (BUF_SAMPLES * sizeof(float))

#ifndef PI
#define PI              3.14159265358979323846
#endif

static gboolean tone_is_our_fd(const gchar *filename, VFSFile *fd)
{
    if (!strncmp(filename, "tone://", 7))
        return TRUE;
    return FALSE;
}

static GArray *tone_filename_parse(const gchar * filename)
{
    GArray *frequencies = g_array_new(FALSE, FALSE, sizeof(double));
    gchar **strings, **ptr;

    if (strncmp(filename, "tone://", 7))
        return NULL;

    strings = g_strsplit(filename + 7, ";", 100);

    for (ptr = strings; *ptr != NULL; ptr++)
    {
        gdouble freq = strtod(*ptr, NULL);
        if (freq >= MIN_FREQ && freq <= MAX_FREQ)
            g_array_append_val(frequencies, freq);
    }
    g_strfreev(strings);

    if (frequencies->len == 0)
    {
        g_array_free(frequencies, TRUE);
        frequencies = NULL;
    }

    return frequencies;
}

static gchar *tone_title(const gchar * filename)
{
    GArray *freqs;
    gchar *title;
    gsize i;

    freqs = tone_filename_parse(filename);
    if (freqs == NULL)
        return NULL;

    title = g_strdup_printf(_("%s %.1f Hz"), _("Tone Generator: "), g_array_index(freqs, double, 0));
    for (i = 1; i < freqs->len; i++)
    {
        gchar *old_title = title;
        title = g_strdup_printf("%s;%.1f Hz", old_title, g_array_index(freqs, double, i));
        g_free(old_title);
    }
    g_array_free(freqs, TRUE);

    return title;
}

struct tone_t
{
    double wd;
    unsigned period, t;
};

static gboolean tone_play(const gchar *filename, VFSFile *file)
{
    GArray *frequencies;
    gfloat data[BUF_SAMPLES];
    gsize i;
    gboolean error = FALSE;
    tone_t *tone = NULL;

    frequencies = tone_filename_parse(filename);
    if (frequencies == NULL)
        return FALSE;

    if (aud_input_open_audio(FMT_FLOAT, OUTPUT_FREQ, 1) == 0)
    {
        error = TRUE;
        goto error_exit;
    }

    aud_input_set_bitrate(16 * OUTPUT_FREQ);

    tone = g_new(tone_t, frequencies->len);
    for (i = 0; i < frequencies->len; i++)
    {
        gdouble f = g_array_index(frequencies, gdouble, i);
        tone[i].wd = 2 * PI * f / OUTPUT_FREQ;
        tone[i].period = (G_MAXINT * 2U / OUTPUT_FREQ) * (OUTPUT_FREQ / f);
        tone[i].t = 0;
    }

    while (!aud_input_check_stop())
    {
        for (i = 0; i < BUF_SAMPLES; i++)
        {
            gsize j;
            double sum_sines;

            for (sum_sines = 0, j = 0; j < frequencies->len; j++)
            {
                sum_sines += sin(tone[j].wd * tone[j].t);
                if (tone[j].t > tone[j].period)
                    tone[j].t -= tone[j].period;
                tone[j].t++;
            }
            /* dithering can cause a little bit of clipping */
            data[i] = (sum_sines * 0.999 / (gdouble) frequencies->len);
        }

        aud_input_write_audio(data, BUF_BYTES);
    }

error_exit:
    g_array_free(frequencies, TRUE);
    g_free(tone);

    return !error;
}

static Tuple tone_probe_for_tuple(const gchar *filename, VFSFile *fd)
{
    Tuple tuple;
    tuple.set_filename (filename);
    gchar *tmp;

    if ((tmp = tone_title(filename)) != NULL)
    {
        tuple.set_str (FIELD_TITLE, tmp);
        g_free(tmp);
    }

    return tuple;
}

static const char tone_about[] =
 N_("Sine tone generator by Håvard Kvålen <havardk@xmms.org>\n"
    "Modified by Daniel J. Peng <danielpeng@bigfoot.com>\n\n"
    "To use it, add a URL: tone://frequency1;frequency2;frequency3;...\n"
    "e.g. tone://2000;2005 to play a 2000 Hz tone and a 2005 Hz tone");

static const gchar * const schemes[] = {"tone", NULL};

#define AUD_PLUGIN_NAME        N_("Tone Generator")
#define AUD_PLUGIN_ABOUT       tone_about
#define AUD_INPUT_SCHEMES      schemes
#define AUD_INPUT_IS_OUR_FILE  tone_is_our_fd
#define AUD_INPUT_PLAY         tone_play
#define AUD_INPUT_READ_TUPLE   tone_probe_for_tuple

#define AUD_DECLARE_INPUT
#include <libaudcore/plugin-declare.h>
