/* 
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

#ifndef __BATTERY_PLUGIN_H__
#define __BATTERY_PLUGIN_H__

#include <glib.h>
#include <libxfce4panel/libxfce4panel.h>

G_BEGIN_DECLS
typedef struct _BatteryPluginClass BatteryPluginClass;
typedef struct _BatteryPlugin      BatteryPlugin;

#define TYPE_BATTERY_PLUGIN            (battery_plugin_get_type ())
#define BATTERY_PLUGIN(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), TYPE_BATTERY_PLUGIN, BatteryPlugin))
#define BATTERY_PLUGIN_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass),  TYPE_BATTERY_PLUGIN, BatteryPluginClass))
#define IS_BATTERY_PLUGIN(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), TYPE_BATTERY_PLUGIN))
#define IS_BATTERY_PLUGIN_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass),  TYPE_BATTERY_PLUGIN))
#define BATTERY_PLUGIN_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj),  TYPE_BATTERY_PLUGIN, BatteryPluginClass))

GType battery_plugin_get_type      (void) G_GNUC_CONST;

void  battery_plugin_register_type (XfcePanelTypeModule *type_module);

G_END_DECLS

#endif /* !__BATTERY_PLUGIN_H__ */
