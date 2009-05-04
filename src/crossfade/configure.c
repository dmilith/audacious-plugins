/* 
 *  XMMS Crossfade Plugin
 *  Copyright (C) 2000-2007  Peter Eisenlohr <peter@eisenlohr.org>
 *
 *  based on the original OSS Output Plugin
 *  Copyright (C) 1998-2000  Peter Alm, Mikael Alm, Olle Hallnas, Thomas Nilsson and 4Front Technologies
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
 *  Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307,
 *  USA.
 */

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include "crossfade.h"
#include "configure.h"
#include "cfgutil.h"

#include "interface-2.0.h"
#include "support-2.0.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>

#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/stat.h>

#ifdef HAVE_LIBSAMPLERATE
#  include <samplerate.h>
#endif


/* available rates for resampling */
gint sample_rates[] =
{
#if MAX_RATE > 48000
	192000,
	96000,
	88200,
	64000,
#endif
	48000,
	44100,
	32000,
	22050,
	16000,
	11025,
	8000,
	6000,
	0
};


#define HIDE(name)					\
{ if ((set_wgt = lookup_widget(config_win, name)))	\
    gtk_widget_hide(set_wgt); }

#define SHOW(name)					\
{ if ((set_wgt = lookup_widget(config_win, name)))	\
    gtk_widget_show(set_wgt); }


#define SETW_SENSITIVE(wgt, sensitive)		\
  gtk_widget_set_sensitive(wgt, sensitive)

#define SETW_TOGGLE(wgt, active)				\
  gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(wgt), active)

#define SETW_SPIN(wgt, value)					\
  gtk_spin_button_set_value(GTK_SPIN_BUTTON(wgt), value)


#define SET_SENSITIVE(name, sensitive)			\
{ if ((set_wgt = lookup_widget(config_win, name)))	\
    gtk_widget_set_sensitive(set_wgt, sensitive); }

#define SET_TOGGLE(name, active)					\
{ if ((set_wgt = lookup_widget(config_win, name)))			\
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(set_wgt), active); }

#define SET_SPIN(name, value)						\
{ if ((set_wgt = lookup_widget(config_win, name)))			\
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(set_wgt), value); }

#define SET_PAGE(name, index)					\
{ if ((set_wgt = lookup_widget(config_win, name)))		\
    gtk_notebook_set_page(GTK_NOTEBOOK(set_wgt), index); }

#define SET_HISTORY(name, index)					\
{ if((set_wgt = lookup_widget(config_win, name)))			\
    gtk_option_menu_set_history(GTK_OPTION_MENU(set_wgt), index); }


#define GET_SENSITIVE(name)			\
((get_wgt = lookup_widget(config_win, name))	\
  && GTK_WIDGET_SENSITIVE(get_wgt))		\

#define GET_TOGGLE(name)					\
((get_wgt = lookup_widget(config_win, name))			\
  && gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(get_wgt)))

#define GET_SPIN(name)							\
((get_wgt = lookup_widget(config_win, name))				\
  ? gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(get_wgt)) : 0)


static GtkWidget *config_win = NULL;
static GtkWidget *about_win = NULL;
static GtkWidget *set_wgt;
static GtkWidget *get_wgt;

/* defined in cfgutil.c */
extern config_t _xfg;
static config_t *xfg = &_xfg;

/* some helpers to keep track of the GUI's state */
static gboolean checking = FALSE;
static gint op_index;
static plugin_config_t op_config;

/* from crossfade.c */
extern MUTEX buffer_mutex;

/*** internal helpers ********************************************************/

typedef void (*activate_func_t)(GtkWidget *, gint index);

static void
add_menu_item(GtkWidget *menu, gchar *title, activate_func_t func, gint index, gint **imap)
{
	GtkWidget *item;
	if (!menu || !title || !func)
		return;
		
	item = gtk_menu_item_new_with_label(title);
	gtk_signal_connect(GTK_OBJECT(item), "activate", (GtkSignalFunc)func, (gpointer) index);
	gtk_widget_show(item);
	gtk_menu_append(GTK_MENU(menu), item);

	if (imap)
		*((*imap)++) = index;
}

static void
gtk2_spin_button_hack(GtkSpinButton *spin)
{
	static gboolean entered = FALSE;
	const gchar *text;
	
	if (entered) return;
	entered = TRUE;

	text = gtk_entry_get_text(GTK_ENTRY(spin));		
	if (text && *text)
	{
		gint value = atoi(text);
		if (value != gtk_spin_button_get_value_as_int(spin))
			gtk_spin_button_set_value(spin, value);
	}
	else
	{
		gtk_spin_button_set_value(spin, 0.0);
		gtk_entry_set_text(GTK_ENTRY(spin), "");
	}
	
	entered = FALSE;
}

/*** output method ***********************************************************/

/*-- callbacks --------------------------------------------------------------*/

static void
resampling_rate_cb(GtkWidget *widget, gint index)
{
	xfg->output_rate = index;
}

#ifdef HAVE_LIBSAMPLERATE
static void
resampling_quality_cb(GtkWidget *widget, gint index)
{
	xfg->output_quality = index;
}
#endif

/*** plugin output ***********************************************************/

static void config_plugin_cb(GtkWidget *widget, gint index);

static gint
scan_plugins(GtkWidget *option_menu, gchar *selected)
{
	GtkWidget *menu = gtk_menu_new();
	GList     *list = g_list_first(xfplayer_get_output_list());
	gint      index =  0;
	gint  sel_index = -1;
	gint  def_index = -1;

	/* sanity check */
	if (selected == NULL)
		selected = "";

	/* parse module list */
	while (list)
	{
		OutputPlugin *op = (OutputPlugin *) list->data;
		GtkWidget  *item = gtk_menu_item_new_with_label(op->description);

		if (op == get_crossfade_oplugin_info())  /* disable selecting ourselves */
			gtk_widget_set_sensitive(item, FALSE);
		else
		{
			if (def_index == -1)
				def_index = index;
				
			if (op->filename && strcmp(g_basename(op->filename), selected) == 0)
				sel_index = index;
		}

		/* create menu item */
		gtk_signal_connect(GTK_OBJECT(item), "activate", (GtkSignalFunc)config_plugin_cb, (gpointer) index++);
		gtk_widget_show(item);
		gtk_menu_append(GTK_MENU(menu), item);

		/* advance to next module */
		list = g_list_next(list);
	}

	/* attach menu */
	gtk_option_menu_set_menu(GTK_OPTION_MENU(option_menu), menu);

	if (sel_index == -1)
	{
		DEBUG(("[crossfade] scan_plugins: plugin not found (\"%s\")\n", selected));
		return def_index;  /* use default (first entry) */
	}
	return sel_index;
}

/*-- plugin output callbacks ------------------------------------------------*/

static void
config_plugin_cb(GtkWidget *widget, gint index)
{
	OutputPlugin *op = g_list_nth_data(xfplayer_get_output_list(), index);

	/* get plugin options from gui */
	op_config.throttle_enable  = GET_TOGGLE("op_throttle_check");
	op_config.max_write_enable = GET_TOGGLE("op_maxblock_check");
	op_config.max_write_len    = GET_SPIN  ("op_maxblock_spin");
	op_config.force_reopen     = GET_TOGGLE("op_forcereopen_check");

	/* config -> string */
	xfade_save_plugin_config(&xfg->op_config_string, xfg->op_name, &op_config);

	/* select new plugin */
	op_index = index;

	/* get new plugin's name */
	if (xfg->op_name) g_free(xfg->op_name);
	xfg->op_name = (op && op->filename) ? g_strdup(g_basename(op->filename)) : NULL;

	/* string -> config */
	xfade_load_plugin_config(xfg->op_config_string, xfg->op_name, &op_config);

	/* update gui */
	SET_SENSITIVE("op_configure_button",  op && (op->configure != NULL));
	SET_SENSITIVE("op_about_button",      op && (op->about != NULL));
	SET_TOGGLE   ("op_throttle_check",    op_config.throttle_enable);
	SET_TOGGLE   ("op_maxblock_check",    op_config.max_write_enable);
	SET_SPIN     ("op_maxblock_spin",     op_config.max_write_len);
	SET_SENSITIVE("op_maxblock_spin",     op_config.max_write_enable);
	SET_TOGGLE   ("op_forcereopen_check", op_config.force_reopen);
}

void
on_output_plugin_configure_button_clicked(GtkButton *button, gpointer user_data)
{
	OutputPlugin *op = g_list_nth_data(xfplayer_get_output_list(), op_index);
	if ((op == NULL) || (op->configure == NULL))
		return;
		
	op->configure();
}

void
on_output_plugin_about_button_clicked(GtkButton *button, gpointer user_data)
{
	OutputPlugin *op = g_list_nth_data(xfplayer_get_output_list(), op_index);
	if ((op == NULL) || (op->about == NULL))
		return;
		
	op->about();
}

void
on_op_throttle_check_toggled(GtkToggleButton *togglebutton, gpointer user_data)
{
	op_config.throttle_enable = gtk_toggle_button_get_active(togglebutton);
}

void
on_op_maxblock_check_toggled(GtkToggleButton *togglebutton, gpointer user_data)
{
	op_config.max_write_enable = gtk_toggle_button_get_active(togglebutton);
	SET_SENSITIVE("op_maxblock_spin", op_config.max_write_enable);
}

void
on_op_maxblock_spin_changed(GtkEditable *editable, gpointer user_data)
{
	gtk2_spin_button_hack(GTK_SPIN_BUTTON(editable));
	op_config.max_write_len = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(editable));
}

void
on_op_forcereopen_check_toggled(GtkToggleButton *togglebutton, gpointer user_data)
{
	op_config.max_write_enable = gtk_toggle_button_get_active(togglebutton);
}

/*** crossfader **************************************************************/

static void xf_config_cb(GtkWidget *widget, gint index);
static void xf_type_cb  (GtkWidget *widget, gint index);

/* crude hack to keep track of menu items */
static gint xf_config_index_map[MAX_FADE_CONFIGS];
static gint xf_type_index_map  [MAX_FADE_TYPES];

static void
create_crossfader_config_menu()
{
	GtkWidget *optionmenu, *menu;
	gint i, *imap;

	if ((optionmenu = lookup_widget(config_win, "xf_config_optionmenu")))
	{
		for (i = 0; i < MAX_FADE_CONFIGS; i++)
			xf_config_index_map[i] = -1;
			
		imap = xf_config_index_map;
		menu = gtk_menu_new();
		
		add_menu_item(menu, "Start of playback",    xf_config_cb, FADE_CONFIG_START, &imap);
		add_menu_item(menu, "Automatic songchange", xf_config_cb, FADE_CONFIG_XFADE, &imap);
#if 0
		/* this should be FADE_TYPE_NONE all the time, anyway,
		   so no need to make it configureable by the user */
		add_menu_item(menu, "Automatic (gapless)", xf_config_cb, FADE_CONFIG_ALBUM, &imap);
#endif
		add_menu_item(menu, "Manual songchange", xf_config_cb, FADE_CONFIG_MANUAL, &imap);
		add_menu_item(menu, "Manual stop",       xf_config_cb, FADE_CONFIG_STOP,   &imap);
		add_menu_item(menu, "End of playlist",   xf_config_cb, FADE_CONFIG_EOP,    &imap);
		add_menu_item(menu, "Seeking",           xf_config_cb, FADE_CONFIG_SEEK,   &imap);
		add_menu_item(menu, "Pause",             xf_config_cb, FADE_CONFIG_PAUSE,  &imap);
		gtk_option_menu_set_menu(GTK_OPTION_MENU(optionmenu), menu);
	}

}

static void
create_crossfader_type_menu()
{
	GtkWidget *optionmenu, *menu;
	gint i, *imap;
	guint32 mask;

	if ((optionmenu = lookup_widget(config_win, "xf_type_optionmenu")))
	{
		for (i = 0; i < MAX_FADE_TYPES; i++)
			xf_type_index_map[i] = -1;
			
		imap = xf_type_index_map;
		menu = gtk_menu_new();
		mask = xfg->fc[xfg->xf_index].type_mask;

		if (mask & (1 << FADE_TYPE_REOPEN))      add_menu_item(menu, "Reopen output device", xf_type_cb, FADE_TYPE_REOPEN,      &imap);
		if (mask & (1 << FADE_TYPE_FLUSH))       add_menu_item(menu, "Flush output device",  xf_type_cb, FADE_TYPE_FLUSH,       &imap);
		if (mask & (1 << FADE_TYPE_NONE))        add_menu_item(menu, "None (gapless/off)",   xf_type_cb, FADE_TYPE_NONE,        &imap);
		if (mask & (1 << FADE_TYPE_PAUSE))       add_menu_item(menu, "Pause",                xf_type_cb, FADE_TYPE_PAUSE,       &imap);
		if (mask & (1 << FADE_TYPE_SIMPLE_XF))   add_menu_item(menu, "Simple crossfade",     xf_type_cb, FADE_TYPE_SIMPLE_XF,   &imap);
		if (mask & (1 << FADE_TYPE_ADVANCED_XF)) add_menu_item(menu, "Advanced crossfade",   xf_type_cb, FADE_TYPE_ADVANCED_XF, &imap);
		if (mask & (1 << FADE_TYPE_FADEIN))      add_menu_item(menu, "Fadein",               xf_type_cb, FADE_TYPE_FADEIN,      &imap);
		if (mask & (1 << FADE_TYPE_FADEOUT))     add_menu_item(menu, "Fadeout",              xf_type_cb, FADE_TYPE_FADEOUT,     &imap);
		if (mask & (1 << FADE_TYPE_PAUSE_NONE))  add_menu_item(menu, "None",                 xf_type_cb, FADE_TYPE_PAUSE_NONE,  &imap);
		if (mask & (1 << FADE_TYPE_PAUSE_ADV))   add_menu_item(menu, "Fadeout/Fadein",       xf_type_cb, FADE_TYPE_PAUSE_ADV,   &imap);
		gtk_option_menu_set_menu(GTK_OPTION_MENU(optionmenu), menu);
	}
}

#define NONE             0x00000000L
#define XF_CONFIG        0x00000001L
#define XF_TYPE          0x00000002L
#define XF_MIX_SIZE      0x00000004L
#define XF_FADEOUT       0x00000008L
#define XF_OFFSET        0x00000010L
#define XF_FADEIN        0x00000020L
#define XF_PAGE          0x00000040L
#define XF_FLUSH         0x00000080L
#define ANY              0xffffffffL

static void
check_crossfader_dependencies(guint32 mask)
{
	fade_config_t *fc = &xfg->fc[xfg->xf_index];
	gint i;

	/* HACK: avoid endless recursion */
	if (checking) return;
	checking = TRUE;

	if (mask & XF_FLUSH)
	{
		SET_TOGGLE("xftfp_enable_check", fc->flush_pause_enable);
		SET_SENSITIVE("xftfp_length_label", fc->flush_pause_enable);
		SET_SENSITIVE("xftfp_length_spin", fc->flush_pause_enable);
		SET_TOGGLE("xftffi_enable_check", fc->flush_in_enable);
		SET_SENSITIVE("xftffi_length_label", fc->flush_in_enable);
		SET_SENSITIVE("xftffi_length_spin", fc->flush_in_enable);
		SET_SENSITIVE("xftffi_volume_label", fc->flush_in_enable);
		SET_SENSITIVE("xftffi_volume_spin", fc->flush_in_enable);
	}

	if (mask & XF_MIX_SIZE)
	{
		SET_TOGGLE("xf_autobuf_check", xfg->mix_size_auto);
		SET_SENSITIVE("xf_buffer_spin", !xfg->mix_size_auto);
		SET_SPIN("xf_buffer_spin", xfade_mix_size_ms(xfg));
	}

	if (mask & XF_CONFIG)
	{
		for (i = 0; i < MAX_FADE_CONFIGS && (xf_config_index_map[i] != xfg->xf_index); i++);
		if (i == MAX_FADE_CONFIGS)
			i = 0;
		SET_HISTORY("xf_config_optionmenu", i);
	}

	if (mask & XF_TYPE)
	{
		create_crossfader_type_menu();
		for (i = 0; i < MAX_FADE_TYPES && (xf_type_index_map[i] != fc->type); i++);
		if (i == MAX_FADE_TYPES)
		{
			fc->type = FADE_TYPE_NONE;
			for (i = 0; i < MAX_FADE_TYPES && (xf_type_index_map[i] != fc->type); i++);
			if (i == MAX_FADE_CONFIGS)
				i = 0;
		}
		SET_HISTORY("xf_type_optionmenu", i);
	}

	if (mask & XF_PAGE)
	{
		SET_PAGE("xf_type_notebook", fc->type);
		SET_SPIN("pause_length_spin", fc->pause_len_ms);
		SET_SPIN("simple_length_spin", fc->simple_len_ms);
		if (fc->config == FADE_CONFIG_SEEK)
		{
			HIDE("xftf_pause_frame");
			HIDE("xftf_fadein_frame");
		}
		else
		{
			SHOW("xftf_pause_frame");
			SHOW("xftf_fadein_frame");
		}
	}

	if (mask & XF_FADEOUT)
	{
		SET_TOGGLE("fadeout_enable_check", fc->out_enable);
		SET_SENSITIVE("fadeout_length_label", fc->out_enable);
		SET_SENSITIVE("fadeout_length_spin", fc->out_enable);
		SET_SPIN("fadeout_length_spin", fc->out_len_ms);
		SET_SENSITIVE("fadeout_volume_label", fc->out_enable);
		SET_SENSITIVE("fadeout_volume_spin", fc->out_enable);
		SET_SPIN("fadeout_volume_spin", fc->out_volume);
		SET_SPIN("xftfo_length_spin", fc->out_len_ms);
		SET_SPIN("xftfo_volume_spin", fc->out_volume);
		SET_SPIN("xftfoi_fadeout_spin", fc->out_len_ms);
	}

	if (mask & XF_FADEIN)
	{
		SET_TOGGLE("fadein_lock_check", fc->in_locked);
		SET_SENSITIVE("fadein_enable_check", !fc->in_locked);
		SET_TOGGLE("fadein_enable_check", fc->in_locked ? fc->out_enable : fc->in_enable);
		SET_SENSITIVE("fadein_length_label", !fc->in_locked && fc->in_enable);
		SET_SENSITIVE("fadein_length_spin", !fc->in_locked && fc->in_enable);
		SET_SPIN("fadein_length_spin", fc->in_locked ? fc->out_len_ms : fc->in_len_ms);
		SET_SENSITIVE("fadein_volume_label", !fc->in_locked && fc->in_enable);
		SET_SENSITIVE("fadein_volume_spin", !fc->in_locked && fc->in_enable);
		SET_SPIN("fadein_volume_spin", fc->in_locked ? fc->out_volume : fc->in_volume);
		SET_SPIN("xftfi_length_spin", fc->in_len_ms);
		SET_SPIN("xftfi_volume_spin", fc->in_volume);
		SET_SPIN("xftfoi_fadein_spin", fc->in_len_ms);
	}

	if (mask & XF_OFFSET)
	{
		if (fc->out_enable)
			SET_SENSITIVE("xfofs_lockout_radiobutton", TRUE);
		if (!fc->in_locked && fc->in_enable)
			SET_SENSITIVE("xfofs_lockin_radiobutton", TRUE);

		switch (fc->ofs_type)
		{
			case FC_OFFSET_LOCK_OUT:
				if (!fc->out_enable)
				{
					SET_TOGGLE("xfofs_none_radiobutton", TRUE);
					fc->ofs_type = FC_OFFSET_NONE;
				}
				break;

			case FC_OFFSET_LOCK_IN:
				if (!(!fc->in_locked && fc->in_enable))
				{
					if ((fc->in_locked && fc->out_enable))
					{
						SET_TOGGLE("xfofs_lockout_radiobutton", TRUE);
						fc->ofs_type = FC_OFFSET_LOCK_OUT;
					}
					else
					{
						SET_TOGGLE("xfofs_none_radiobutton", TRUE);
						fc->ofs_type = FC_OFFSET_NONE;
					}
				}
				break;
		}

		switch (fc->ofs_type_wanted)
		{
			case FC_OFFSET_NONE:
				SET_TOGGLE("xfofs_none_radiobutton", TRUE);
				fc->ofs_type = FC_OFFSET_NONE;
				break;

			case FC_OFFSET_LOCK_OUT:
				if (fc->out_enable)
				{
					SET_TOGGLE("xfofs_lockout_radiobutton", TRUE);
					fc->ofs_type = FC_OFFSET_LOCK_OUT;
				}
				break;

			case FC_OFFSET_LOCK_IN:
				if (!fc->in_locked && fc->in_enable)
				{
					SET_TOGGLE("xfofs_lockin_radiobutton", TRUE);
					fc->ofs_type = FC_OFFSET_LOCK_IN;
				}
				else if (fc->out_enable)
				{
					SET_TOGGLE("xfofs_lockout_radiobutton", TRUE);
					fc->ofs_type = FC_OFFSET_LOCK_OUT;
				}
				break;

			case FC_OFFSET_CUSTOM:
				SET_TOGGLE("xfofs_custom_radiobutton", TRUE);
				fc->ofs_type = FC_OFFSET_CUSTOM;
				break;
		}

		if (!fc->out_enable)
			SET_SENSITIVE("xfofs_lockout_radiobutton", FALSE);
		if (!(!fc->in_locked && fc->in_enable))
			SET_SENSITIVE("xfofs_lockin_radiobutton", FALSE);

		SET_SENSITIVE("xfofs_custom_spin", fc->ofs_type == FC_OFFSET_CUSTOM);
		SET_SPIN("xfofs_custom_spin", xfade_cfg_offset(fc));
		SET_SPIN("xftfo_silence_spin", xfade_cfg_offset(fc));
		SET_SPIN("xftfoi_silence_spin", xfade_cfg_offset(fc));
	}

	checking = FALSE;
}

/*-- crossfader callbacks ---------------------------------------------------*/

void
on_xf_buffer_spin_changed(GtkEditable *editable, gpointer user_data)
{
	if (checking) return;
	gtk2_spin_button_hack(GTK_SPIN_BUTTON(editable));
	xfg->mix_size_ms = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(editable));
	check_crossfader_dependencies(NONE);
}

void
on_xf_autobuf_check_toggled(GtkToggleButton *togglebutton, gpointer user_data)
{
	xfg->mix_size_auto = gtk_toggle_button_get_active(togglebutton);
	check_crossfader_dependencies(XF_MIX_SIZE);
}

/* - config/type  - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -*/

void
xf_config_cb(GtkWidget *widget, gint index)
{
	if (checking) return;
	xfg->xf_index = index;
	check_crossfader_dependencies(ANY & ~XF_CONFIG);
}

void
xf_type_cb(GtkWidget *widget, gint index)
{
	if (checking) return;
	xfg->fc[xfg->xf_index].type = index;
	check_crossfader_dependencies(ANY & ~XF_CONFIG);
}

/* - flush  - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -*/

void
on_xftfp_enable_check_toggled(GtkToggleButton *togglebutton, gpointer user_data)
{
	if (checking) return;
	xfg->fc[xfg->xf_index].flush_pause_enable = gtk_toggle_button_get_active(togglebutton);
	check_crossfader_dependencies(XF_FLUSH | XF_MIX_SIZE);
}

void
on_xftfp_length_spin_changed(GtkEditable *editable, gpointer user_data)
{
	if (checking) return;
	gtk2_spin_button_hack(GTK_SPIN_BUTTON(editable));
	xfg->fc[xfg->xf_index].flush_pause_len_ms = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(editable));
	check_crossfader_dependencies(XF_FLUSH);
}

void
on_xftffi_enable_check_toggled(GtkToggleButton *togglebutton, gpointer user_data)
{
	if (checking) return;
	xfg->fc[xfg->xf_index].flush_in_enable = gtk_toggle_button_get_active(togglebutton);
	check_crossfader_dependencies(XF_FLUSH | XF_OFFSET | XF_FADEOUT | XF_FADEIN);
}

void
on_xftffi_length_spin_changed(GtkEditable *editable, gpointer user_data)
{
	if (checking) return;
	gtk2_spin_button_hack(GTK_SPIN_BUTTON(editable));
	xfg->fc[xfg->xf_index].flush_in_len_ms = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(editable));
	check_crossfader_dependencies(XF_FLUSH);
}

void
on_xftffi_volume_spin_changed(GtkEditable *editable, gpointer user_data)
{
	if (checking) return;
	gtk2_spin_button_hack(GTK_SPIN_BUTTON(editable));
	xfg->fc[xfg->xf_index].flush_in_volume = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(editable));
	check_crossfader_dependencies(XF_FLUSH);
}

/* - pause  - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -*/

void
on_pause_length_spin_changed(GtkEditable *editable, gpointer user_data)
{
	if (checking) return;
	gtk2_spin_button_hack(GTK_SPIN_BUTTON(editable));
	xfg->fc[xfg->xf_index].pause_len_ms = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(editable));
	check_crossfader_dependencies(XF_MIX_SIZE);
}

/* - simple - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -*/

void
on_simple_length_spin_changed(GtkEditable *editable, gpointer user_data)
{
	if (checking) return;
	gtk2_spin_button_hack(GTK_SPIN_BUTTON(editable));
	xfg->fc[xfg->xf_index].simple_len_ms = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(editable));
	check_crossfader_dependencies(XF_MIX_SIZE);
}

/* - crossfade-fadeout  - - - - - - - - - - - - - - - - - - - - - - - - - - -*/

void
on_fadeout_enable_check_toggled(GtkToggleButton *togglebutton, gpointer user_data)
{
	if (checking) return;
	xfg->fc[xfg->xf_index].out_enable = gtk_toggle_button_get_active(togglebutton);
	check_crossfader_dependencies(XF_MIX_SIZE | XF_OFFSET | XF_FADEOUT | XF_FADEIN);
}

void
on_fadeout_length_spin_changed(GtkEditable *editable, gpointer user_data)
{
	if (checking) return;
	gtk2_spin_button_hack(GTK_SPIN_BUTTON(editable));
	xfg->fc[xfg->xf_index].out_len_ms = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(editable));
	check_crossfader_dependencies(XF_MIX_SIZE | XF_OFFSET | XF_FADEIN);
}

void
on_fadeout_volume_spin_changed(GtkEditable *editable, gpointer user_data)
{
	gtk2_spin_button_hack(GTK_SPIN_BUTTON(editable));
	xfg->fc[xfg->xf_index].out_volume = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(editable));
	check_crossfader_dependencies(XF_OFFSET | XF_FADEIN);
}

/* - crossfade-offset - - - - - - - - - - - - - - - - - - - - - - - - - - - -*/

void
on_xfofs_none_radiobutton_toggled(GtkToggleButton *togglebutton, gpointer user_data)
{
	if (checking || !gtk_toggle_button_get_active(togglebutton)) return;
	xfg->fc[xfg->xf_index].ofs_type = FC_OFFSET_NONE;
	xfg->fc[xfg->xf_index].ofs_type_wanted = FC_OFFSET_NONE;
	check_crossfader_dependencies(XF_MIX_SIZE | XF_OFFSET);
}

void
on_xfofs_none_radiobutton_clicked(GtkButton *button, gpointer user_data)
{
	if (checking) return;
	xfg->fc[xfg->xf_index].ofs_type_wanted = FC_OFFSET_NONE;
}

void
on_xfofs_lockout_radiobutton_toggled(GtkToggleButton *togglebutton, gpointer user_data)
{
	if (checking || !gtk_toggle_button_get_active(togglebutton)) return;
	xfg->fc[xfg->xf_index].ofs_type = FC_OFFSET_LOCK_OUT;
	xfg->fc[xfg->xf_index].ofs_type_wanted = FC_OFFSET_LOCK_OUT;
	check_crossfader_dependencies(XF_MIX_SIZE | XF_OFFSET);
}

void
on_xfofs_lockout_radiobutton_clicked(GtkButton *button, gpointer user_data)
{
	if (checking) return;
	xfg->fc[xfg->xf_index].ofs_type_wanted = FC_OFFSET_LOCK_OUT;
}

void
on_xfofs_lockin_radiobutton_toggled(GtkToggleButton *togglebutton, gpointer user_data)
{
	if (checking || !gtk_toggle_button_get_active(togglebutton)) return;
	xfg->fc[xfg->xf_index].ofs_type = FC_OFFSET_LOCK_IN;
	xfg->fc[xfg->xf_index].ofs_type_wanted = FC_OFFSET_LOCK_IN;
	check_crossfader_dependencies(XF_MIX_SIZE | XF_OFFSET);
}

void
on_xfofs_lockin_radiobutton_clicked(GtkButton *button, gpointer user_data)
{
	if (checking) return;
	xfg->fc[xfg->xf_index].ofs_type_wanted = FC_OFFSET_LOCK_IN;
}

void
on_xfofs_custom_radiobutton_toggled(GtkToggleButton *togglebutton, gpointer user_data)
{
	if (checking || !gtk_toggle_button_get_active(togglebutton)) return;
	xfg->fc[xfg->xf_index].ofs_type = FC_OFFSET_CUSTOM;
	xfg->fc[xfg->xf_index].ofs_type_wanted = FC_OFFSET_CUSTOM;
	check_crossfader_dependencies(XF_MIX_SIZE | XF_OFFSET);
}

void
on_xfofs_custom_radiobutton_clicked(GtkButton *button, gpointer user_data)
{
	if (checking) return;
	xfg->fc[xfg->xf_index].ofs_type_wanted = FC_OFFSET_CUSTOM;
}

void
on_xfofs_custom_spin_changed(GtkEditable *editable, gpointer user_data)
{
	if (checking) return;
	gtk2_spin_button_hack(GTK_SPIN_BUTTON(editable));
	xfg->fc[xfg->xf_index].ofs_custom_ms = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(editable));
	check_crossfader_dependencies(XF_MIX_SIZE);
}

/* - crossfade-fadein - - - - - - - - - - - - - - - - - - - - - - - - - - - -*/

void
on_fadein_lock_check_toggled(GtkToggleButton *togglebutton, gpointer user_data)
{
	if (checking) return;
	xfg->fc[xfg->xf_index].in_locked = gtk_toggle_button_get_active(togglebutton);
	check_crossfader_dependencies(XF_OFFSET | XF_FADEIN);
}

void
on_fadein_enable_check_toggled(GtkToggleButton *togglebutton, gpointer user_data)
{
	if (checking) return;
	xfg->fc[xfg->xf_index].in_enable = gtk_toggle_button_get_active(togglebutton);
	check_crossfader_dependencies(XF_OFFSET | XF_FADEIN);
}

void
on_fadein_length_spin_changed(GtkEditable *editable, gpointer user_data)
{
	if (checking) return;
	gtk2_spin_button_hack(GTK_SPIN_BUTTON(editable));
	xfg->fc[xfg->xf_index].in_len_ms = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(editable));
	check_crossfader_dependencies(XF_MIX_SIZE | XF_OFFSET);
}

void
on_fadein_volume_spin_changed(GtkEditable *editable, gpointer user_data)
{
	if (checking) return;
	gtk2_spin_button_hack(GTK_SPIN_BUTTON(editable));
	xfg->fc[xfg->xf_index].in_volume = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(editable));
	check_crossfader_dependencies(NONE);
}

/*-- fadein -----------------------------------------------------------------*/

/* signal set to on_fadein_length_spin_changed */
/* signal set to on_fadein_volume_spin_changed */

/*-- fadeout ----------------------------------------------------------------*/

/* signal set to on_fadeout_length_spin_changed */
/* signal set to on_fadeout_volume_spin_changed */

/*-- fadeout/fadein ---------------------------------------------------------*/

/* signal set to on_fadeout_length_spin_changed */
/* signal set to on_xfofs_custom_spin_changed */
/* signal set to on_fadeout_volume_spin_changed */

/*** gap killer **************************************************************/

void
check_gapkiller_dependencies()
{
	if (checking) return;
	checking = TRUE;

	SET_SENSITIVE("lgap_length_spin", xfg->gap_lead_enable);
	SET_SENSITIVE("lgap_level_spin", xfg->gap_lead_enable);
	SET_SENSITIVE("tgap_enable_check", !xfg->gap_trail_locked);
	SET_SENSITIVE("tgap_length_spin", !xfg->gap_trail_locked && xfg->gap_trail_enable);
	SET_SENSITIVE("tgap_level_spin", !xfg->gap_trail_locked && xfg->gap_trail_enable);

	if (xfg->gap_trail_locked)
	{
		SET_TOGGLE("tgap_enable_check", xfg->gap_lead_enable);
		SET_SPIN("tgap_length_spin", xfg->gap_lead_len_ms);
		SET_SPIN("tgap_level_spin", xfg->gap_lead_level);
	}
	else
	{
		SET_TOGGLE("tgap_enable_check", xfg->gap_trail_enable);
		SET_SPIN("tgap_length_spin", xfg->gap_trail_len_ms);
		SET_SPIN("tgap_level_spin", xfg->gap_trail_level);
	}

	if (xfg->mix_size_auto)
		SET_SPIN("xf_buffer_spin", xfade_mix_size_ms(xfg));

	checking = FALSE;
}

/*-- gapkiller callbacks ----------------------------------------------------*/

void
on_lgap_enable_check_toggled(GtkToggleButton *togglebutton, gpointer user_data)
{
	if (checking) return;
	xfg->gap_lead_enable = gtk_toggle_button_get_active(togglebutton);
	check_gapkiller_dependencies();
}

void
on_lgap_length_spin_changed(GtkEditable *editable, gpointer user_data)
{
	if (checking) return;
	gtk2_spin_button_hack(GTK_SPIN_BUTTON(editable));
	xfg->gap_lead_len_ms = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(editable));
	check_gapkiller_dependencies();
}

void
on_lgap_level_spin_changed(GtkEditable *editable, gpointer user_data)
{
	if (checking) return;
	gtk2_spin_button_hack(GTK_SPIN_BUTTON(editable));
	xfg->gap_lead_level = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(editable));
	check_gapkiller_dependencies();
}

void
on_tgap_lock_check_toggled(GtkToggleButton *togglebutton, gpointer user_data)
{
	if (checking) return;
	xfg->gap_trail_locked = gtk_toggle_button_get_active(togglebutton);
	check_gapkiller_dependencies();
}

void
on_tgap_enable_check_toggled(GtkToggleButton *togglebutton, gpointer user_data)
{
	if (checking) return;
	xfg->gap_trail_enable = gtk_toggle_button_get_active(togglebutton);
	check_gapkiller_dependencies();
}

void
on_tgap_length_spin_changed(GtkEditable *editable, gpointer user_data)
{
	if (checking) return;
	gtk2_spin_button_hack(GTK_SPIN_BUTTON(editable));
	xfg->gap_trail_len_ms = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(editable));
}

void
on_tgap_level_spin_changed(GtkEditable *editable, gpointer user_data)
{
	if (checking) return;
	gtk2_spin_button_hack(GTK_SPIN_BUTTON(editable));
	xfg->gap_trail_level = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(editable));
}

/*** misc ********************************************************************/

void
check_misc_dependencies()
{
	if (checking) return;
	checking = TRUE;

	if (xfg->mix_size_auto)
		SET_SPIN("xf_buffer_spin", xfade_mix_size_ms(xfg));

	SET_SENSITIVE("moth_opmaxused_spin", xfg->enable_op_max_used);

	checking = FALSE;
}

/*-- misc callbacks ---------------------------------------------------------*/

void
on_config_mixopt_enable_check_toggled(GtkToggleButton *togglebutton, gpointer user_data)
{
	SET_SENSITIVE("mixopt_reverse_check", gtk_toggle_button_get_active(togglebutton));
	SET_SENSITIVE("mixopt_software_check", gtk_toggle_button_get_active(togglebutton));
}

void
on_moth_songchange_spin_changed(GtkEditable *editable, gpointer user_data)
{
	if (checking) return;
	gtk2_spin_button_hack(GTK_SPIN_BUTTON(editable));
	xfg->songchange_timeout = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(editable));
	check_misc_dependencies();
}

void
on_moth_opmaxused_check_toggled(GtkToggleButton *togglebutton, gpointer user_data)
{
	if (checking) return;
	xfg->enable_op_max_used = gtk_toggle_button_get_active(togglebutton);
	check_misc_dependencies();
}

/*** main config *************************************************************/

void
on_config_apply_clicked(GtkButton *button, gpointer user_data)
{
	GtkWidget *widget;

	/* get current notebook page */
	if ((widget = lookup_widget(config_win, "config_notebook")))
		xfg->page = gtk_notebook_get_current_page(GTK_NOTEBOOK(widget));

	/* output method */

	/* sample rate */

	/* output method: builtin OSS */
	if ((widget = lookup_widget(config_win, "output_oss_notebook")))
		xfg->oss_page = gtk_notebook_get_current_page(GTK_NOTEBOOK(widget));

	xfg->oss_buffer_size_ms = GET_SPIN("ossbuf_buffer_spin");
	xfg->oss_preload_size_ms = GET_SPIN("ossbuf_preload_spin");

	xfg->oss_fragments = GET_SPIN("osshwb_fragments_spin");
	xfg->oss_fragment_size = GET_SPIN("osshwb_fragsize_spin");
	xfg->oss_maxbuf_enable = GET_TOGGLE("osshwb_maxbuf_check");

	xfg->oss_mixer_use_master = GET_TOGGLE("ossmixer_pcm_check");

	/* output method: plugin */
	op_config.throttle_enable = GET_TOGGLE("op_throttle_check");
	op_config.max_write_enable = GET_TOGGLE("op_maxblock_check");
	op_config.max_write_len = GET_SPIN("op_maxblock_spin");
	op_config.force_reopen = GET_TOGGLE("op_forcereopen_check");

	xfade_save_plugin_config(&xfg->op_config_string, xfg->op_name, &op_config);

	/* output method: none: */

	/* effects: pre-mixing effect plugin */

	/* effects: volume normalizer */

	/* crossfader */
	xfg->mix_size_auto = GET_TOGGLE("xf_autobuf_check");

	/* gap killer */
	xfg->gap_lead_enable = GET_TOGGLE("lgap_enable_check");
	xfg->gap_lead_len_ms = GET_SPIN("lgap_length_spin");
	xfg->gap_lead_level = GET_SPIN("lgap_level_spin");

	xfg->gap_trail_locked = GET_TOGGLE("tgap_lock_check");

	xfg->gap_crossing = GET_TOGGLE("gadv_crossing_check");

	/* misc */
	xfg->enable_debug = GET_TOGGLE("debug_stderr_check");
	xfg->enable_mixer = GET_TOGGLE("mixopt_enable_check");
	xfg->mixer_reverse = GET_TOGGLE("mixopt_reverse_check");
	xfg->mixer_software = GET_TOGGLE("mixopt_software_check");
	xfg->preload_size_ms = GET_SPIN("moth_preload_spin");
	xfg->album_detection = GET_TOGGLE("noxf_album_check");
	xfg->no_xfade_if_same_file = GET_TOGGLE("noxf_samefile_check");
	xfg->enable_http_workaround = GET_TOGGLE("moth_httpworkaround_check");
	xfg->op_max_used_ms = GET_SPIN("moth_opmaxused_spin");
	xfg->output_keep_opened = GET_TOGGLE("moth_outputkeepopened_check");

	/* presets */

	/* lock buffer */
	MUTEX_LOCK(&buffer_mutex);

	/* free existing strings */
	if (config->oss_alt_audio_device) g_free(config->oss_alt_audio_device);
	if (config->oss_alt_mixer_device) g_free(config->oss_alt_mixer_device);
	if (config->op_config_string)     g_free(config->op_config_string);
	if (config->op_name)              g_free(config->op_name);
	if (config->ep_name)              g_free(config->ep_name);

	/* copy current settings (dupping the strings) */
	*config = *xfg;
	config->oss_alt_audio_device = g_strdup(xfg->oss_alt_audio_device);
	config->oss_alt_mixer_device = g_strdup(xfg->oss_alt_mixer_device);
	config->op_config_string = g_strdup(xfg->op_config_string);
	config->op_name = g_strdup(xfg->op_name);
	config->ep_name = g_strdup(xfg->ep_name);

	/* tell the engine that the config has changed */
	xfade_realize_config();

	/* unlock buffer */
	MUTEX_UNLOCK(&buffer_mutex);

	/* save configuration */
	xfade_save_config();
}

void
on_config_ok_clicked(GtkButton *button, gpointer user_data)
{
	/* apply and save config */
	on_config_apply_clicked(button, user_data);

	/* close and destroy window */
	gtk_widget_destroy(config_win);
}

void
xfade_configure()
{
	GtkWidget *widget;

	if (!config_win)
	{
		/* create */
		if (!(config_win = create_config_win()))
		{
			DEBUG(("[crossfade] plugin_configure: error creating window!\n"));
			return;
		}

		/* update config_win when window is destroyed */
		gtk_signal_connect(GTK_OBJECT(config_win), "destroy", GTK_SIGNAL_FUNC(gtk_widget_destroyed), &config_win);

		/* free any strings that might be left in our local copy of the config */
		if (xfg->oss_alt_audio_device) g_free(xfg->oss_alt_audio_device);
		if (xfg->oss_alt_mixer_device) g_free(xfg->oss_alt_mixer_device);
		if (xfg->op_config_string)     g_free(xfg->op_config_string);
		if (xfg->op_name)              g_free(xfg->op_name);
		if (xfg->ep_name)              g_free(xfg->ep_name);

		/* copy current settings (dupping the strings) */
		*xfg = *config;
		xfg->oss_alt_audio_device = g_strdup(config->oss_alt_audio_device);
		xfg->oss_alt_mixer_device = g_strdup(config->oss_alt_mixer_device);
		xfg->op_config_string     = g_strdup(config->op_config_string);
		xfg->op_name              = g_strdup(config->op_name);
		xfg->ep_name              = g_strdup(config->ep_name);

		/* go to remembered notebook page */
		if ((widget = lookup_widget(config_win, "config_notebook")))
			gtk_notebook_set_page(GTK_NOTEBOOK(widget), config->page);

		/* output: method */
		if ((widget = lookup_widget(config_win, "output_notebook")))
			gtk_notebook_set_page(GTK_NOTEBOOK(widget), xfg->output_method);

		/* output: resampling rate */
		if ((widget = lookup_widget(config_win, "resampling_rate_optionmenu")))
		{
			GtkWidget *menu = gtk_menu_new();
			GtkWidget *item;
			gint index, *rate;
			char label[16];

			for (rate = &sample_rates[0]; *rate; rate++)
			{
				g_snprintf(label, sizeof(label), "%d Hz", *rate);
				item = gtk_menu_item_new_with_label(label);
				gtk_signal_connect(GTK_OBJECT(item), "activate", (GtkSignalFunc)resampling_rate_cb, (gpointer)*rate);
				gtk_widget_show(item);
				gtk_menu_append(GTK_MENU(menu), item);
			}
			gtk_option_menu_set_menu(GTK_OPTION_MENU(widget), menu);

			/* find list index for xfg->output_rate */
			for (rate = &sample_rates[0], index = 0; *rate && *rate != xfg->output_rate; rate++, index++);
			
			/* if the specified rate is not in the list of available rates, select default rate */
			if (!*rate)
			{
				DEBUG(("[crossfade] plugin_configure: WARNING: invalid output sample rate (%d)!\n", xfg->output_rate));
				DEBUG(("[crossfade] plugin_configure:          ... using default of 44100\n"));
				for (rate = &sample_rates[0], index = 0; *rate && *rate != 44100; rate++, index++);
			}
			
			/* finally, set the list selection */
			gtk_option_menu_set_history(GTK_OPTION_MENU(widget), index);
		}

		/* output: resampling quality (libsamplerate setting) */
#ifdef HAVE_LIBSAMPLERATE
		if ((widget = lookup_widget(config_win, "resampling_quality_optionmenu")))
		{
			GtkWidget *menu = gtk_menu_new();
			GtkWidget *item;

			GtkTooltips *tooltips = (GtkTooltips *) gtk_object_get_data(GTK_OBJECT(config_win), "tooltips");

			int converter_type;
			const char *name, *description;
			for (converter_type = 0; (name = src_get_name(converter_type)); converter_type++)
			{
				description = src_get_description(converter_type);

				item = gtk_menu_item_new_with_label(name);
				gtk_tooltips_set_tip(tooltips, item, description, NULL);

				gtk_signal_connect(GTK_OBJECT(item), "activate", (GtkSignalFunc)resampling_quality_cb, (gpointer) converter_type);
				gtk_widget_show(item);
				gtk_menu_append(GTK_MENU(menu), item);
			}

			gtk_option_menu_set_menu(GTK_OPTION_MENU(widget), menu);
			gtk_option_menu_set_history(GTK_OPTION_MENU(widget), xfg->output_quality);
		}
#else
		HIDE("resampling_quality_hbox");
		HIDE("resampling_quality_optionmenu");
#endif

		/* output method: plugin */
		xfade_load_plugin_config(xfg->op_config_string, xfg->op_name, &op_config);
		SET_TOGGLE   ("op_throttle_check",    op_config.throttle_enable);
		SET_TOGGLE   ("op_maxblock_check",    op_config.max_write_enable);
		SET_SPIN     ("op_maxblock_spin",     op_config.max_write_len);
		SET_SENSITIVE("op_maxblock_spin",     op_config.max_write_enable);
		SET_TOGGLE   ("op_forcereopen_check", op_config.force_reopen);

		if ((widget = lookup_widget(config_win, "op_plugin_optionmenu")))
		{
			OutputPlugin *op = NULL;
			if ((op_index = scan_plugins(widget, xfg->op_name)) >= 0)
			{
				gtk_option_menu_set_history(GTK_OPTION_MENU(widget), op_index);
				op = g_list_nth_data(xfplayer_get_output_list(), op_index);
			}
			SET_SENSITIVE("op_configure_button", op && (op->configure != NULL));
			SET_SENSITIVE("op_about_button",     op && (op->about     != NULL));
		}

		/* crossfader */
		create_crossfader_config_menu();

		if ((xfg->xf_index < 0) || (xfg->xf_index >= MAX_FADE_CONFIGS))
		{
			DEBUG(("[crossfade] plugin_configure: crossfade index out of range (%d)!\n", xfg->xf_index));
			xfg->xf_index = CLAMP(xfg->xf_index, 0, MAX_FADE_CONFIGS);
		}

		check_crossfader_dependencies(ANY);

		/* gap killer */
		SET_TOGGLE("lgap_enable_check",   xfg->gap_lead_enable);
		SET_SPIN  ("lgap_length_spin",    xfg->gap_lead_len_ms);
		SET_SPIN  ("lgap_level_spin",     xfg->gap_lead_level);
		SET_TOGGLE("tgap_lock_check",     xfg->gap_trail_locked);
		SET_TOGGLE("tgap_enable_check",   xfg->gap_trail_enable);
		SET_SPIN  ("tgap_length_spin",    xfg->gap_trail_len_ms);
		SET_SPIN  ("tgap_level_spin",     xfg->gap_trail_level);
		SET_TOGGLE("gadv_crossing_check", xfg->gap_crossing);

		check_gapkiller_dependencies();

		/* misc */
		SET_TOGGLE("debug_stderr_check",          xfg->enable_debug);
		SET_TOGGLE("mixopt_enable_check",         xfg->enable_mixer);
		SET_TOGGLE("mixopt_reverse_check",        xfg->mixer_reverse);
		SET_TOGGLE("mixopt_software_check",       xfg->mixer_software);
		SET_SPIN  ("moth_songchange_spin",        xfg->songchange_timeout);
		SET_SPIN  ("moth_preload_spin",           xfg->preload_size_ms);
		SET_TOGGLE("noxf_album_check",            xfg->album_detection);
		SET_TOGGLE("noxf_samefile_check",         xfg->album_detection);
		SET_TOGGLE("moth_httpworkaround_check",   xfg->enable_http_workaround);
		SET_TOGGLE("moth_opmaxused_check",        xfg->enable_op_max_used);
		SET_SPIN  ("moth_opmaxused_spin",         xfg->op_max_used_ms);
		SET_TOGGLE("moth_outputkeepopened_check", xfg->output_keep_opened);

		check_misc_dependencies();

		/* presets */
		if ((set_wgt = lookup_widget(config_win, "presets_list_list")))
		{
			GList *item;

			for (item = config->presets; item; item = g_list_next(item))
			{
				gchar *name = (gchar *) item->data;
				gchar *text[] = { name, "Default", "No" };
				gtk_clist_append(GTK_CLIST(set_wgt), text);
			}
		}

		/* show window near mouse pointer */
		gtk_window_set_position(GTK_WINDOW(config_win), GTK_WIN_POS_MOUSE);
		gtk_widget_show(config_win);
	}
	else
		/* bring window to front */
		gdk_window_raise(config_win->window);
}

void
xfade_about()
{
	if (!about_win)
	{
		gchar *about_text =
			"Audacious Crossfade Plugin\n"
			"Copyright © 2009 William Pitcock <nenolod@atheme.org>\n"
			"\n"
			"...based in part on XMMS-Crossfade:\n"
			"Copyright © 2000-2009 Peter Eisenlohr <peter@eisenlohr.org>\n"
			"\n"
			"based on the original OSS Output Plugin  Copyright (C) 1998-2000\n"
			"Peter Alm, Mikael Alm, Olle Hallnas, Thomas Nilsson and 4Front Technologies\n"
			"\n"
			"This program is free software; you can redistribute it and/or modify\n"
			"it under the terms of the GNU General Public License as published by\n"
			"the Free Software Foundation; either version 2 of the License, or\n"
			"(at your option) any later version.\n"
			"\n"
			"This program is distributed in the hope that it will be useful,\n"
			"but WITHOUT ANY WARRANTY; without even the implied warranty of\n"
			"MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the\n"
			"GNU General Public License for more details.\n"
			"\n"
			"You should have received a copy of the GNU General Public License\n"
			"along with this program; if not, write to the Free Software\n"
			"Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307,\n" "USA.";

		about_win = create_about_win();

		/* update about_win when window is destroyed */
		gtk_signal_connect(GTK_OBJECT(about_win), "destroy", GTK_SIGNAL_FUNC(gtk_widget_destroyed), &about_win);

		/* set about box text (this is done here and not in interface.c because
		   of the VERSION #define -- there is no way to do this with GLADE */
		if ((set_wgt = lookup_widget(about_win, "about_label")))
			gtk_label_set_text(GTK_LABEL(set_wgt), about_text);

		/* show near mouse pointer */
		gtk_window_set_position(GTK_WINDOW(about_win), GTK_WIN_POS_MOUSE);
		gtk_widget_show(about_win);
	}
	else
		gdk_window_raise(about_win->window);
}