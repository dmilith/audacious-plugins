/*
 * settings.c
 * Copyright 2013 John Lindgren
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

#include "gtkui.h"

#include <libaudcore/i18n.h>
#include <libaudcore/preferences.h>

#include "ui_playlist_notebook.h"
#include "ui_playlist_widget.h"

static void redisplay_playlists (void)
{
    ui_playlist_notebook_empty ();
    ui_playlist_notebook_populate ();
}

static const PreferencesWidget gtkui_widgets[] = {
    WidgetLabel (N_("<b>Playlist Tabs</b>")),
    WidgetCheck (N_("Always show tabs"),
        {VALUE_BOOLEAN, 0, "gtkui", "playlist_tabs_visible", show_hide_playlist_tabs}),
    WidgetCheck (N_("Show entry counts"),
        {VALUE_BOOLEAN, 0, "gtkui", "entry_count_visible", redisplay_playlists}),
    WidgetCheck (N_("Show close buttons"),
        {VALUE_BOOLEAN, 0, "gtkui", "close_button_visible", redisplay_playlists}),
    WidgetLabel (N_("<b>Playlist Columns</b>")),
    WidgetCustom (pw_col_create_chooser),
    WidgetCheck (N_("Show column headers"),
        {VALUE_BOOLEAN, 0, "gtkui", "playlist_headers", redisplay_playlists}),
    WidgetLabel (N_("<b>Miscellaneous</b>")),
    WidgetSpin (N_("Arrow keys seek by:"),
        {VALUE_FLOAT, 0, "gtkui", "step_size", update_step_size},
        {0.1, 60, 0.1, N_("seconds")}),
    WidgetCheck (N_("Scroll on song change"),
        {VALUE_BOOLEAN, 0, "gtkui", "autoscroll"})
};

const PluginPreferences gtkui_prefs = {
    gtkui_widgets,
    ARRAY_LEN (gtkui_widgets)
};

