/*
 *  Copyright (C) 2014 Eric Koegel <eric@xfce.org>
 *  Copyright (C) 2015-2017 Gooroom <gooroom@gooroom.kr>
 *
 *  This program is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU General Public License
 *  as published by the Free Software Foundation; either version 2
 *  of the License, or (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */


#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "xfpm-power-common.h"
#include "battery-plugin.h"

#include <gtk/gtk.h>
#include <gdk/gdkkeysyms.h>

#include <glib-object.h>
#include <libxfce4util/libxfce4util.h>
#include <libxfce4panel/xfce-panel-plugin.h>

#include <upower.h>

#include <xfconf/xfconf.h>


#define PANEL_TRAY_ICON_SIZE        (22)
#define SET_BRIGHTNESS_TIMEOUT      (50)
#define PANEL_DEFAULT_ICON          ("battery-full-charged")
#define PANEL_DEFAULT_ICON_SYMBOLIC ("battery-full-charged-symbolic")



struct _BatteryPluginClass
{
  XfcePanelPluginClass __parent__;
};

/* plugin structure */
struct _BatteryPlugin
{
	XfcePanelPlugin      __parent__;

	GtkWidget       *button;
	GtkWidget       *box_devices;
	GtkWidget       *popup_window;
	GtkWidget       *scl_brightness;

    /* The actual panel icon image */
    GtkWidget       *img_tray;

	XfconfChannel   *channel;

	UpClient        *upower;

    /* A list of BatteryDevices  */
	GList           *devices;

    /* Upower 0.99 has a display device that can be used for the
     * panel image and tooltip description */
	UpDevice        *display_device;

    /* Keep track of icon name to redisplay during size changes */
	gchar           *tray_icon_name;

	guint            set_brightness_timeout;
};

typedef struct
{
	GdkPixbuf   *pix;               /* Icon */
	gchar       *details;           /* Description of the device + state */
	gchar       *object_path;       /* UpDevice object path */
	UpDevice    *device;            /* Pointer to the UpDevice */
	gulong       changed_signal_id; /* device changed callback id */

	GtkWidget   *item_detail;       /* The device's item on the menu (if shown) */
	GtkWidget   *label_detail;      /* The device's item on the menu (if shown) */
	GtkWidget   *icon_detail;       /* The device's item on the menu (if shown) */
	GtkWidget   *separator;         /* The device's item on the menu (if shown) */
} BatteryDevice;


/* define the plugin */
XFCE_PANEL_DEFINE_PLUGIN (BatteryPlugin, battery_plugin)


static gboolean popup_window_add_device (BatteryDevice *battery_device, BatteryPlugin *plugin);





static gboolean
xfpm_brightness_helper_set_level (gint32 level)
{
	gboolean ret = FALSE;
	gint exit_status = 0;
	gchar *pkexec, *cmdline = NULL;

	pkexec = g_find_program_in_path ("pkexec");
	cmdline = g_strdup_printf ("%s %s/xfpm-power-backlight-helper --set-brightness %i", pkexec, SBINDIR, level);
	if (!g_spawn_command_line_sync (cmdline, NULL, NULL, &exit_status, NULL)) {
		goto out;
	}

	ret = (exit_status == 0);

out:
	g_free (cmdline);

	return ret;
}

static gint
xfpm_brightness_helper_get_level (const gchar *argument)
{
	gint value = -1;
	gint exit_status = 0;
	gchar *cmdline, *output = NULL;

	cmdline = g_strdup_printf (SBINDIR "/xfpm-power-backlight-helper --%s", argument);
	if (!g_spawn_command_line_sync (cmdline, &output, NULL, &exit_status, NULL)) {
		goto out;
	}

	if (exit_status != 0)
		goto out;

	if (output[0] == 'N') {
		value = 0;
	} else if (output[0] == 'Y') {
		value = 1;
	} else {
		value = atoi (output);
	}

out:
	g_free (cmdline);
	g_free (output);

	return value;
}

static GList*
find_device_in_list (GList *devices, const gchar *object_path)
{
	GList *item = NULL;

	g_return_val_if_fail (devices != NULL, NULL);

	for (item = g_list_first (devices); item != NULL; item = g_list_next (item))
	{
		BatteryDevice *battery_device = item->data;
		if (battery_device == NULL)
		{
			continue;
		}

		if (g_strcmp0 (battery_device->object_path, object_path) == 0)
			return item;
	}

	return NULL;
}

static BatteryDevice*
get_display_device (BatteryPlugin *plugin)
{
	GList *item = NULL;
	gdouble highest_percentage = 0;
	BatteryDevice *display_device = NULL;

	if (plugin->display_device)
	{
		item = find_device_in_list (plugin->devices, up_device_get_object_path (plugin->display_device));
		if (item)
		{
			return item->data;
		}
	}

	/* We want to find the battery or ups device with the highest percentage
	 * and use that to get our tooltip from */
	for (item = g_list_first (plugin->devices); item != NULL; item = g_list_next (item))
	{
		BatteryDevice *battery_device = item->data;
		guint type = 0;
		gdouble percentage;

		if (!battery_device->device || !UP_IS_DEVICE (battery_device->device))
		{
			continue;
		}

		g_object_get (battery_device->device,
				"kind", &type,
				"percentage", &percentage,
				NULL);

		if (type == UP_DEVICE_KIND_BATTERY || type == UP_DEVICE_KIND_UPS)
		{
			if (highest_percentage < percentage)
			{
				display_device = battery_device;
				highest_percentage = percentage;
			}
		}
	}

	return display_device;
}

/* This function unrefs the pix and img from the battery device and
 * disconnects the expose-event callback on the img.
 */
static void
battery_device_remove_pix (BatteryDevice *battery_device)
{
	if (battery_device == NULL)
		return;

	if (G_IS_OBJECT (battery_device->pix))
	{
		g_object_unref (battery_device->pix);
		battery_device->pix = NULL;
	}
}

static void
remove_battery_device (BatteryDevice *battery_device, BatteryPlugin *plugin)
{
	g_return_if_fail (battery_device != NULL);

	/* If it is being shown in the popup window, remove it */
	if (plugin->popup_window && battery_device->item_detail) {
		gtk_container_remove (GTK_CONTAINER (plugin->box_devices), battery_device->item_detail);
		gtk_container_remove (GTK_CONTAINER (plugin->box_devices), battery_device->separator);
	}

	g_free (battery_device->details);
	g_free (battery_device->object_path);

	battery_device_remove_pix (battery_device);

	if (battery_device->device != NULL && UP_IS_DEVICE(battery_device->device))
	{
		/* disconnect the signal handler if we were using it */
		if (battery_device->changed_signal_id != 0)
			g_signal_handler_disconnect (battery_device->device, battery_device->changed_signal_id);
		battery_device->changed_signal_id = 0;

		g_object_unref (battery_device->device);
		battery_device->device = NULL;
	}

	g_free (battery_device);
}

static void
update_tray_icon (BatteryPlugin *plugin)
{
	GdkPixbuf *pix = NULL;
	pix = gtk_icon_theme_load_icon (gtk_icon_theme_get_default (),
                                    plugin->tray_icon_name,
                                    PANEL_TRAY_ICON_SIZE,
                                    GTK_ICON_LOOKUP_FORCE_SIZE,
                                    NULL);

	if (pix) {
		gtk_image_set_from_pixbuf (GTK_IMAGE (plugin->img_tray), pix);
		g_object_unref (pix);
	}
}


static void
update_device_icon_and_details (UpDevice *device, BatteryPlugin *plugin)
{
	GList          *item;
	BatteryDevice  *battery_device;
	BatteryDevice  *display_device;
	const gchar    *object_path = up_device_get_object_path (device);
	gchar          *details;
	gchar          *icon_name;
	GdkPixbuf      *pix = NULL;

	item = find_device_in_list (plugin->devices, object_path);

	if (item == NULL)
		return;

	battery_device = item->data;

	icon_name = get_device_icon_name (plugin->upower, device);
	details = get_device_description (plugin->upower, device);

	/* If UPower doesn't give us an icon, just use the default */
	if (g_strcmp0 (icon_name, "") == 0)
	{
		/* ignore empty icon names */
		g_free (icon_name);
		icon_name = NULL;
	}

	if (icon_name == NULL)
		icon_name = g_strdup (PANEL_DEFAULT_ICON);

	pix = gtk_icon_theme_load_icon (gtk_icon_theme_get_default (),
                                    icon_name,
                                    32,
                                    GTK_ICON_LOOKUP_FORCE_SIZE,
                                    NULL);

	if (battery_device->details)
		g_free (battery_device->details);
	battery_device->details = details;

	/* If we had an image before, remove it and the callback */
	battery_device_remove_pix (battery_device);
	battery_device->pix = pix;

	/* Get the display device, which may now be this one */
	display_device = get_display_device (plugin);

	if (battery_device == display_device)
	{
		/* update the icon */
		g_free (plugin->tray_icon_name);

		plugin->tray_icon_name = g_strdup_printf ("%s-%s", icon_name, "symbolic");

		update_tray_icon (plugin);
	}
	g_free (icon_name);

	/* If the popup window is being displayed, update it */
	if (plugin->popup_window && battery_device->item_detail)
	{
		gtk_label_set_markup (GTK_LABEL (battery_device->label_detail), details);
		gtk_image_set_from_pixbuf (GTK_IMAGE (battery_device->icon_detail), battery_device->pix);
	}
}

static gboolean
set_brightness_level_with_timeout (gpointer data)
{
	BatteryPlugin *plugin = BATTERY_PLUGIN (data);

	gint32 value;

	value = (gint32) gtk_range_get_value (GTK_RANGE (plugin->scl_brightness));

	xfpm_brightness_helper_set_level (value);

	if (plugin->set_brightness_timeout) {
		g_source_remove (plugin->set_brightness_timeout);
		plugin->set_brightness_timeout = 0;
	}

	return FALSE;
}

static void
on_brightness_changed_cb (GtkWidget *widget, gpointer data)
{
	BatteryPlugin *plugin = BATTERY_PLUGIN (data);

	if (plugin->set_brightness_timeout)
		return;

	plugin->set_brightness_timeout =
		g_timeout_add (SET_BRIGHTNESS_TIMEOUT,
		(GSourceFunc) set_brightness_level_with_timeout, plugin);
}

static void
device_changed_cb (UpDevice *device, GParamSpec *pspec, BatteryPlugin *plugin)
{
	update_device_icon_and_details (device, plugin);
}

static void
add_device (UpDevice *device, BatteryPlugin *plugin)
{
	BatteryDevice *battery_device;
	guint type = 0;
	const gchar *object_path = up_device_get_object_path (device);
	gulong signal_id;
	gboolean is_present = FALSE;

	/* don't add the same device twice */
	if (find_device_in_list (plugin->devices, object_path) != NULL)
		return;

	battery_device = g_new0 (BatteryDevice, 1);

	/* hack, this depends on XFPM_DEVICE_TYPE_* being in sync with UP_DEVICE_KIND_* */
	g_object_get (device,
                  "kind", &type,
                  NULL);

	signal_id = g_signal_connect (device, "notify", G_CALLBACK (device_changed_cb), plugin);

	/* populate the struct */
	battery_device->object_path = g_strdup (object_path);
	battery_device->changed_signal_id = signal_id;
	battery_device->device = g_object_ref (device);

	/* add it to the list */
	plugin->devices = g_list_append (plugin->devices, battery_device);

	/* Add the icon and description for the device */
	update_device_icon_and_details (device, plugin);

	/* If the menu is being shown, add this new device to it */
	if (plugin->popup_window)
	{
		popup_window_add_device (battery_device, plugin);
	}
}

static void
remove_device (const gchar *object_path, BatteryPlugin *plugin)
{
	GList *item;
	BatteryDevice *battery_device;

	item = find_device_in_list (plugin->devices, object_path);

	if (item == NULL)
		return;

	battery_device = item->data;

	/* Remove its resources */
	remove_battery_device (battery_device, plugin);

	/* remove it item and free the battery device */
	plugin->devices = g_list_delete_link (plugin->devices, item);
}

static void
add_all_devices (BatteryPlugin *plugin)
{
	GPtrArray *array = NULL;
	guint i;

	plugin->display_device = up_client_get_display_device (plugin->upower);

	add_device (plugin->display_device, plugin);

	array = up_client_get_devices (plugin->upower);

	if (array)
	{
		for (i = 0; i < array->len; i++)
		{
			UpDevice *device = g_ptr_array_index (array, i);

			add_device (device, plugin);
		}
		g_ptr_array_free (array, TRUE);
	}
}

static void
remove_all_devices (BatteryPlugin *plugin)
{
	GList *item = NULL;

	for (item = g_list_first (plugin->devices); item != NULL; item = g_list_next (item))
	{
		BatteryDevice *battery_device = item->data;
		if (battery_device == NULL)
		{
			continue;
		}

		/* Remove its resources */
		remove_battery_device (battery_device, plugin);
	}
}

static void
device_added_cb (UpClient *upower, UpDevice *device, BatteryPlugin *plugin)
{
	add_device (device, plugin);
}

static void
device_removed_cb (UpClient *upower, const gchar *object_path, BatteryPlugin *plugin)
{
	remove_device (object_path, plugin);
}

static void
setup_brightness (BatteryPlugin *plugin)
{
	gint32 step, range;
	gint32 max_brightness, min_brightness, cur_brightness;

	max_brightness = (gint32)xfpm_brightness_helper_get_level ("get-max-brightness");
	cur_brightness = (gint32)xfpm_brightness_helper_get_level ("get-brightness");

	if (max_brightness < 0 || cur_brightness < 0) {
		gtk_widget_set_sensitive (plugin->scl_brightness, FALSE);
		return;
	}

	min_brightness = (max_brightness * 1) / 10;
	range = max_brightness - min_brightness;
	step = (range <= 100) ? 1 : (range / 100);

	gtk_range_set_range (GTK_RANGE (plugin->scl_brightness), min_brightness, max_brightness);
	gtk_range_set_increments (GTK_RANGE (plugin->scl_brightness), step, step);  
	gtk_range_set_value (GTK_RANGE (plugin->scl_brightness), cur_brightness);

	g_signal_connect (G_OBJECT (plugin->scl_brightness), "value-changed", G_CALLBACK (on_brightness_changed_cb), plugin);
}

static void
popup_window_destroy_device (GtkWidget *object, gpointer data)
{
	BatteryPlugin *plugin = BATTERY_PLUGIN (data);

	GList *item = NULL;
	for (item = g_list_first (plugin->devices); item != NULL; item = g_list_next (item))
	{
		BatteryDevice *battery_device = item->data;

		if (battery_device->item_detail == object)
		{
			battery_device->label_detail = NULL;
			battery_device->icon_detail = NULL;
			battery_device->separator = NULL;
			battery_device->item_detail = NULL;
			return;
		}
	}
}

static gboolean
popup_window_add_device (BatteryDevice *battery_device, BatteryPlugin *plugin)
{
	guint type = 0;
	GtkWidget *label, *icon, *hbox, *separator;

	if (UP_IS_DEVICE (battery_device->device))
	{
		g_object_get (battery_device->device,
				"kind", &type,
				NULL);

		/* Don't add the display device or line power to the menu */
		if (type == UP_DEVICE_KIND_LINE_POWER || battery_device->device == plugin->display_device)
		{
			return FALSE;
		}
	}

	hbox = gtk_hbox_new (FALSE, 9);
	gtk_container_set_border_width (GTK_CONTAINER (hbox), 7);
	gtk_box_pack_start (GTK_BOX (plugin->box_devices), hbox, TRUE, TRUE, 0);
	gtk_widget_show (hbox);

	icon = gtk_image_new_from_pixbuf (battery_device->pix);
	gtk_image_set_pixel_size (GTK_IMAGE (icon), 32);
	gtk_box_pack_start (GTK_BOX (hbox), icon, FALSE, FALSE, 9);
	gtk_widget_show (icon);

	label = gtk_label_new (NULL);
	gtk_label_set_justify (GTK_LABEL (label), GTK_JUSTIFY_LEFT);
	gtk_label_set_use_markup (GTK_LABEL (label), TRUE);
	gtk_label_set_markup (GTK_LABEL (label), battery_device->details);
	gtk_box_pack_start (GTK_BOX (hbox), label, TRUE, FALSE, 9);
	gtk_widget_show (label);

	separator = gtk_hseparator_new ();
	gtk_box_pack_start (GTK_BOX (plugin->box_devices), separator, TRUE, FALSE, 0);
	gtk_widget_show (separator);

	/* keep track of the item in the battery_device so we can update it */
	battery_device->item_detail= hbox;
	battery_device->icon_detail = icon;
	battery_device->label_detail = label;
	battery_device->separator = separator;

	g_signal_connect(G_OBJECT (hbox), "destroy", G_CALLBACK (popup_window_destroy_device), plugin);

	return TRUE;
}

static gboolean
on_popup_window_closed (gpointer data)
{
	BatteryPlugin *plugin = BATTERY_PLUGIN (data);

	if (plugin->popup_window != NULL) {
		gtk_widget_destroy (plugin->popup_window);
		plugin->popup_window = NULL;
    }

	xfce_panel_plugin_block_autohide (XFCE_PANEL_PLUGIN (plugin), FALSE);
	gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (plugin->button), FALSE);

	return TRUE;
}

static gboolean
on_popup_key_press_event (GtkWidget *widget, GdkEventKey *event, gpointer data)
{
	if (event->type == GDK_KEY_PRESS && event->keyval == GDK_Escape) {
		on_popup_window_closed (data);
		return TRUE;
	}

	return FALSE;
}

static void
on_popup_window_realized (GtkWidget *widget, gpointer data)
{
	gint x, y;
	BatteryPlugin *plugin = BATTERY_PLUGIN (data);

	xfce_panel_plugin_position_widget (XFCE_PANEL_PLUGIN (plugin), widget, plugin->button, &x, &y);
	gtk_window_move (GTK_WINDOW (widget), x, y);
}

static GtkWidget *
popup_window_new (BatteryPlugin *plugin, GdkEventButton *event)
{
	GtkWidget *window;

	window = gtk_window_new (GTK_WINDOW_TOPLEVEL);
	gtk_window_set_type_hint (GTK_WINDOW (window), GDK_WINDOW_TYPE_HINT_UTILITY);
	gtk_window_set_decorated (GTK_WINDOW (window), FALSE);
	gtk_window_set_resizable (GTK_WINDOW (window), FALSE);
	gtk_window_set_skip_taskbar_hint (GTK_WINDOW (window), TRUE);
	gtk_window_set_skip_pager_hint(GTK_WINDOW (window), TRUE);
	gtk_window_set_keep_above (GTK_WINDOW (window), TRUE);
	gtk_window_stick(GTK_WINDOW (window));
	gtk_window_set_screen (GTK_WINDOW (window), gtk_widget_get_screen (GTK_WIDGET (plugin)));

	GtkWidget *main_vbox = gtk_vbox_new (FALSE, 0);
	gtk_container_add (GTK_CONTAINER (window), main_vbox);

	GtkWidget *ebox = gtk_event_box_new ();
	gtk_widget_set_name (ebox, "panel-popup-window-frame");
	gtk_box_pack_start (GTK_BOX (main_vbox), ebox, FALSE, FALSE, 0);

	GtkWidget *alignment = gtk_alignment_new (0.0, 0.5, 0.0, 0.0);
	gtk_alignment_set_padding (GTK_ALIGNMENT (alignment), 7, 7, 7, 7);
	gtk_container_add (GTK_CONTAINER (ebox), alignment);

	GtkWidget *title = gtk_label_new (NULL);
	gtk_label_set_markup (GTK_LABEL (title), _("<big><b>Power Management</b></big>"));
	gtk_label_set_justify (GTK_LABEL (title), GTK_JUSTIFY_LEFT);
	gtk_container_add (GTK_CONTAINER (alignment), title);

	plugin->box_devices = gtk_vbox_new (FALSE, 0);
	gtk_box_pack_start (GTK_BOX (main_vbox), plugin->box_devices, FALSE, FALSE, 0);

	GList *item = NULL;
	for (item = g_list_first (plugin->devices); item != NULL; item = g_list_next (item)) {
		BatteryDevice *battery_device = item->data;

		popup_window_add_device (battery_device, plugin);
	}

	GtkWidget *hbox = gtk_hbox_new (FALSE, 9);
	gtk_container_set_border_width (GTK_CONTAINER (hbox), 7);
	gtk_box_pack_start (GTK_BOX (main_vbox), hbox, FALSE, FALSE, 0);

	GtkWidget *icon = gtk_image_new_from_icon_name ("display-brightness-symbolic", GTK_ICON_SIZE_DND);
	gtk_image_set_pixel_size (GTK_IMAGE (icon), 32);
	gtk_box_pack_start (GTK_BOX (hbox), icon, FALSE, FALSE, 9);


	plugin->scl_brightness = gtk_hscale_new_with_range (0.0, 100.0, 1.0);
	gtk_widget_set_can_focus (plugin->scl_brightness, FALSE);
	gtk_widget_set_size_request (plugin->scl_brightness, 224, -1);
	gtk_range_set_inverted (GTK_RANGE (plugin->scl_brightness), FALSE);
	gtk_scale_set_draw_value (GTK_SCALE (plugin->scl_brightness), FALSE);
	gtk_range_set_round_digits (GTK_RANGE (plugin->scl_brightness), 0);
	gtk_box_pack_end (GTK_BOX (hbox), plugin->scl_brightness, TRUE, FALSE, 0);

	setup_brightness (plugin);

	if (plugin->channel) {
		GtkWidget *separator = gtk_hseparator_new ();
		gtk_box_pack_start (GTK_BOX (main_vbox), separator, TRUE, FALSE, 0);

		alignment = gtk_alignment_new (0.0, 0.5, 0.0, 0.0);
		gtk_alignment_set_padding (GTK_ALIGNMENT (alignment), 3, 3, 7, 7);
		gtk_box_pack_start (GTK_BOX (main_vbox), alignment, FALSE, FALSE, 0);

		GtkWidget *chk_button = gtk_check_button_new_with_label (_("Presentation mode"));
		gtk_widget_set_can_focus (chk_button, FALSE);
		gtk_container_add (GTK_CONTAINER (alignment), chk_button);

		xfconf_g_property_bind (plugin->channel,
			"/xfce4-power-manager/presentation-mode",
			G_TYPE_BOOLEAN, G_OBJECT (chk_button), "active");

		gchar *xfpm = g_find_program_in_path ("xfce4-power-manager");
		if (!xfpm) {
			gtk_widget_set_sensitive (chk_button, FALSE);
		}
		g_free (xfpm);
	}

	g_signal_connect (G_OBJECT (window), "realize", G_CALLBACK (on_popup_window_realized), plugin);
	g_signal_connect_swapped (G_OBJECT (window), "delete-event", G_CALLBACK (on_popup_window_closed), plugin);
	g_signal_connect (G_OBJECT (window), "key-press-event", G_CALLBACK (on_popup_key_press_event), plugin);
	g_signal_connect_swapped (G_OBJECT (window), "focus-out-event", G_CALLBACK (on_popup_window_closed), plugin);

	gtk_widget_show_all (window);

	xfce_panel_plugin_block_autohide (XFCE_PANEL_PLUGIN (plugin), TRUE);
	gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (plugin->button), TRUE);

	return window;
}

static gboolean
on_plugin_button_pressed (GtkWidget *widget, GdkEventButton *event, gpointer data)
{
	BatteryPlugin *plugin = BATTERY_PLUGIN (data);

	if (event->button == 1 || event->button == 2) {
		if (event->type == GDK_BUTTON_PRESS) {
			if (plugin->popup_window != NULL) {
				on_popup_window_closed (plugin);
			} else {
				plugin->popup_window = popup_window_new (plugin, event);
			}

			return TRUE;
		}
	}

	/* bypass GTK_TOGGLE_BUTTON's handler and go directly to the plugin's one */
	return (*GTK_WIDGET_CLASS (battery_plugin_parent_class)->button_press_event) (GTK_WIDGET (plugin), event);
}

static gboolean
scan_battery (void)
{
	GDir *dir;
	gboolean ret = FALSE;

	dir = g_dir_open ("/sys/class/power_supply", 0, NULL);

	if (dir) {
		const gchar *entry;
		while ((entry = g_dir_read_name (dir))) {
			if (g_str_has_prefix (entry, "BAT")) {
				ret = TRUE;
				break;
			}
		}
	}

	g_dir_close(dir);

	return ret;
	
#if 0
	gboolean ret = FALSE;
	gchar *upower, *cmdline, *output = NULL;

	upower = g_find_program_in_path ("upower");
	if (upower) {
		gchar *output = NULL;
		gchar *cmdline = g_strdup_printf ("%s -e", upower);

		if (g_spawn_command_line_sync (cmdline, &output, NULL, NULL, NULL)) {
			if (output) {
				guint i = 0;
				gchar **lines = g_strsplit (output, "\n", -1);
				for (i = 0; lines[i] != NULL; i++) {
					if (g_strrstr (lines[i], "battery_BAT") != NULL) {
						ret = TRUE;
						break;
					}
				}
				g_strfreev (lines);
			}
		}

		g_free (output);
		g_free (cmdline);
	}

	g_free (upower);

	return ret;
#endif
}

static gboolean
update_ui (gpointer data)
{
	BatteryPlugin *plugin = BATTERY_PLUGIN (data);

	if (!scan_battery ())
		return FALSE;

	gtk_widget_show_all (plugin->button);
		
	plugin->upower = up_client_new ();

	if (xfconf_init (NULL)) {
		plugin->channel = xfconf_channel_get ("xfce4-power-manager");
	}

    /* Add all the devcies currently attached to the system */
	add_all_devices (plugin);

	g_signal_connect (G_OBJECT (plugin->button), "button-press-event", G_CALLBACK (on_plugin_button_pressed), plugin);

	g_signal_connect (plugin->upower, "device-added", G_CALLBACK (device_added_cb), plugin);
	g_signal_connect (plugin->upower, "device-removed", G_CALLBACK (device_removed_cb), plugin);

	return FALSE;
}

static void
battery_plugin_free_data (XfcePanelPlugin *panel_plugin)
{
    BatteryPlugin *plugin = BATTERY_PLUGIN (panel_plugin);

	if (plugin->set_brightness_timeout) {
		g_source_remove (plugin->set_brightness_timeout);
		plugin->set_brightness_timeout = 0;
	}

    if (plugin->popup_window != NULL)
        on_popup_window_closed (plugin);

	g_free (plugin->tray_icon_name);

	g_signal_handlers_disconnect_by_data (plugin->upower, plugin);

    remove_all_devices (plugin);
}

static gboolean
battery_plugin_size_changed (XfcePanelPlugin *panel_plugin, gint size)
{
	BatteryPlugin *plugin = BATTERY_PLUGIN (panel_plugin);

	if (xfce_panel_plugin_get_mode (panel_plugin) == XFCE_PANEL_PLUGIN_MODE_HORIZONTAL) {
		gtk_widget_set_size_request (GTK_WIDGET (panel_plugin), -1, size);
	} else {
		gtk_widget_set_size_request (GTK_WIDGET (panel_plugin), size, -1);
	}

	return TRUE;
}

static void
battery_plugin_mode_changed (XfcePanelPlugin *plugin, XfcePanelPluginMode mode)
{
	battery_plugin_size_changed (plugin, xfce_panel_plugin_get_size (plugin));
}

static void
battery_plugin_init (BatteryPlugin *plugin)
{
	plugin->button         = NULL;
	plugin->devices        = NULL;
	plugin->box_devices    = NULL;
	plugin->popup_window   = NULL;
	plugin->scl_brightness = NULL;
	plugin->set_brightness_timeout = 0;

	xfce_textdomain (GETTEXT_PACKAGE, PACKAGE_LOCALE_DIR, "UTF-8");

	plugin->button = xfce_panel_create_toggle_button ();
	xfce_panel_plugin_add_action_widget (XFCE_PANEL_PLUGIN (plugin), plugin->button);
	gtk_container_add (GTK_CONTAINER (plugin), plugin->button);

	GdkPixbuf *pix;
	pix = gtk_icon_theme_load_icon (gtk_icon_theme_get_default (),
                                    PANEL_DEFAULT_ICON_SYMBOLIC,
                                    PANEL_TRAY_ICON_SIZE,
                                    GTK_ICON_LOOKUP_FORCE_SIZE, NULL);

	if (pix) {
		plugin->img_tray = gtk_image_new_from_pixbuf (pix);
		gtk_image_set_pixel_size (GTK_IMAGE (plugin->img_tray), PANEL_TRAY_ICON_SIZE);
		gtk_container_add (GTK_CONTAINER (plugin->button), plugin->img_tray);
		g_object_unref (G_OBJECT (pix));
	}

	g_timeout_add (500, (GSourceFunc) update_ui, plugin);
}

static void
battery_plugin_class_init (BatteryPluginClass *klass)
{
	XfcePanelPluginClass *plugin_class;

	plugin_class = XFCE_PANEL_PLUGIN_CLASS (klass);
	plugin_class->free_data = battery_plugin_free_data;
	plugin_class->size_changed = battery_plugin_size_changed;
	plugin_class->mode_changed = battery_plugin_mode_changed;
}
