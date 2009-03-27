/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2008 David Zeuthen <davidz@redhat.com>
 * Copyright (C) 2008 Richard Hughes <richard@hughsie.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 *
 */

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include <string.h>

#include <glib.h>
#include <glib/gstdio.h>
#include <glib/gi18n-lib.h>
#include <glib-object.h>
#include <dbus/dbus-glib.h>
#include <dbus/dbus-glib-lowlevel.h>
#include <devkit-gobject/devkit-gobject.h>
#include <polkit-dbus/polkit-dbus.h>

#include "sysfs-utils.h"
#include "egg-debug.h"
#include "egg-string.h"
#include "egg-obj-list.h"

#include "dkp-supply.h"
#include "dkp-device.h"
#include "dkp-history.h"
#include "dkp-history-obj.h"
#include "dkp-stats-obj.h"
#include "dkp-marshal.h"
#include "dkp-device-glue.h"

struct DkpDevicePrivate
{
	gchar			*object_path;
	DBusGConnection		*system_bus_connection;
	DBusGProxy		*system_bus_proxy;
	DkpDaemon		*daemon;
	DkpHistory		*history;
	DevkitDevice		*d;
	DkpObject		*obj;
	gboolean		 has_ever_refresh;
};

static void     dkp_device_class_init		(DkpDeviceClass *klass);
static void     dkp_device_init			(DkpDevice *device);
static gboolean	dkp_device_register_device	(DkpDevice *device);
static gboolean	dkp_device_refresh_internal	(DkpDevice *device);

enum
{
	PROP_0,
	PROP_NATIVE_PATH,
	PROP_VENDOR,
	PROP_MODEL,
	PROP_SERIAL,
	PROP_UPDATE_TIME,
	PROP_TYPE,
	PROP_LINE_POWER_ONLINE,
	PROP_POWER_SUPPLY,
	PROP_BATTERY_CAPACITY,
	PROP_BATTERY_IS_PRESENT,
	PROP_BATTERY_IS_RECHARGEABLE,
	PROP_BATTERY_HAS_HISTORY,
	PROP_BATTERY_HAS_STATISTICS,
	PROP_BATTERY_STATE,
	PROP_BATTERY_ENERGY,
	PROP_BATTERY_ENERGY_EMPTY,
	PROP_BATTERY_ENERGY_FULL,
	PROP_BATTERY_ENERGY_FULL_DESIGN,
	PROP_BATTERY_ENERGY_RATE,
	PROP_BATTERY_VOLTAGE,
	PROP_BATTERY_TIME_TO_EMPTY,
	PROP_BATTERY_TIME_TO_FULL,
	PROP_BATTERY_PERCENTAGE,
	PROP_BATTERY_TECHNOLOGY,
};

enum
{
	CHANGED_SIGNAL,
	LAST_SIGNAL,
};

static guint signals[LAST_SIGNAL] = { 0 };

G_DEFINE_TYPE (DkpDevice, dkp_device, G_TYPE_OBJECT)
#define DKP_DEVICE_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), DKP_TYPE_DEVICE, DkpDevicePrivate))
#define DKP_DBUS_STRUCT_UINT_DOUBLE_STRING (dbus_g_type_get_struct ("GValueArray", \
	G_TYPE_UINT, G_TYPE_DOUBLE, G_TYPE_STRING, G_TYPE_INVALID))
#define DKP_DBUS_STRUCT_DOUBLE_DOUBLE (dbus_g_type_get_struct ("GValueArray", \
	G_TYPE_DOUBLE, G_TYPE_DOUBLE, G_TYPE_INVALID))

/**
 * dkp_device_error_quark:
 **/
GQuark
dkp_device_error_quark (void)
{
	static GQuark ret = 0;

	if (ret == 0) {
		ret = g_quark_from_static_string ("dkp_device_error");
	}

	return ret;
}

#define ENUM_ENTRY(NAME, DESC) { NAME, "" #NAME "", DESC }

/**
 * dkp_device_error_get_type:
 **/
GType
dkp_device_error_get_type (void)
{
	static GType etype = 0;

	if (etype == 0)
	{
		static const GEnumValue values[] =
			{
				ENUM_ENTRY (DKP_DEVICE_ERROR_GENERAL, "GeneralError"),
				{ 0, 0, 0 }
			};
		g_assert (DKP_DEVICE_NUM_ERRORS == G_N_ELEMENTS (values) - 1);
		etype = g_enum_register_static ("DkpDeviceError", values);
	}
	return etype;
}

/**
 * dkp_device_get_property:
 **/
static void
dkp_device_get_property (GObject *object, guint prop_id, GValue *value, GParamSpec *pspec)
{
	DkpDevice *device = DKP_DEVICE (object);
	DkpObject *obj = device->priv->obj;

	switch (prop_id) {
	case PROP_NATIVE_PATH:
		g_value_set_string (value, obj->native_path);
		break;
	case PROP_VENDOR:
		g_value_set_string (value, obj->vendor);
		break;
	case PROP_MODEL:
		g_value_set_string (value, obj->model);
		break;
	case PROP_SERIAL:
		g_value_set_string (value, obj->serial);
		break;
	case PROP_UPDATE_TIME:
		g_value_set_uint64 (value, obj->update_time);
		break;
	case PROP_TYPE:
		g_value_set_string (value, dkp_device_type_to_text (obj->type));
		break;
	case PROP_POWER_SUPPLY:
		g_value_set_boolean (value, obj->power_supply);
		break;
	case PROP_LINE_POWER_ONLINE:
		g_value_set_boolean (value, obj->online);
		break;
	case PROP_BATTERY_IS_PRESENT:
		g_value_set_boolean (value, obj->is_present);
		break;
	case PROP_BATTERY_IS_RECHARGEABLE:
		g_value_set_boolean (value, obj->is_rechargeable);
		break;
	case PROP_BATTERY_HAS_HISTORY:
		g_value_set_boolean (value, obj->has_history);
		break;
	case PROP_BATTERY_HAS_STATISTICS:
		g_value_set_boolean (value, obj->has_statistics);
		break;
	case PROP_BATTERY_STATE:
		g_value_set_string (value, dkp_device_state_to_text (obj->state));
		break;
	case PROP_BATTERY_CAPACITY:
		g_value_set_double (value, obj->capacity);
		break;
	case PROP_BATTERY_ENERGY:
		g_value_set_double (value, obj->energy);
		break;
	case PROP_BATTERY_ENERGY_EMPTY:
		g_value_set_double (value, obj->energy_empty);
		break;
	case PROP_BATTERY_ENERGY_FULL:
		g_value_set_double (value, obj->energy_full);
		break;
	case PROP_BATTERY_ENERGY_FULL_DESIGN:
		g_value_set_double (value, obj->energy_full_design);
		break;
	case PROP_BATTERY_ENERGY_RATE:
		g_value_set_double (value, obj->energy_rate);
		break;
	case PROP_BATTERY_VOLTAGE:
		g_value_set_double (value, obj->voltage);
		break;
	case PROP_BATTERY_TIME_TO_EMPTY:
		g_value_set_int64 (value, obj->time_to_empty);
		break;
	case PROP_BATTERY_TIME_TO_FULL:
		g_value_set_int64 (value, obj->time_to_full);
		break;
	case PROP_BATTERY_PERCENTAGE:
		g_value_set_double (value, obj->percentage);
		break;
	case PROP_BATTERY_TECHNOLOGY:
		g_value_set_string (value, dkp_device_technology_to_text (obj->technology));
		break;

	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

/**
 * dkp_device_removed:
 **/
void
dkp_device_removed (DkpDevice *device)
{
	//DkpDeviceClass *klass = DKP_DEVICE_GET_CLASS (device);
	//klass->removed (device);
	g_return_if_fail (DKP_IS_DEVICE (device));
	egg_warning ("do something here?");
}

/**
 * dkp_device_get_on_battery:
 *
 * Note: Only implement for system devices, i.e. ones supplying the system
 **/
gboolean
dkp_device_get_on_battery (DkpDevice *device, gboolean *on_battery)
{
	DkpDeviceClass *klass = DKP_DEVICE_GET_CLASS (device);

	g_return_val_if_fail (DKP_IS_DEVICE (device), FALSE);

	/* no support */
	if (klass->get_on_battery == NULL)
		return FALSE;

	return klass->get_on_battery (device, on_battery);
}

/**
 * dkp_device_get_low_battery:
 *
 * Note: Only implement for system devices, i.e. ones supplying the system
 **/
gboolean
dkp_device_get_low_battery (DkpDevice *device, gboolean *low_battery)
{
	DkpDeviceClass *klass = DKP_DEVICE_GET_CLASS (device);

	g_return_val_if_fail (DKP_IS_DEVICE (device), FALSE);

	/* no support */
	if (klass->get_low_battery == NULL)
		return FALSE;

	return klass->get_low_battery (device, low_battery);
}

/**
 * dkp_device_coldplug:
 **/
gboolean
dkp_device_coldplug (DkpDevice *device, DkpDaemon *daemon, DevkitDevice *d)
{
	gboolean ret;
	const gchar *native_path;
	DkpDeviceClass *klass = DKP_DEVICE_GET_CLASS (device);
	gchar *id;

	g_return_val_if_fail (DKP_IS_DEVICE (device), FALSE);

	/* save */
	device->priv->d = g_object_ref (d);
	device->priv->daemon = g_object_ref (daemon);

	native_path = devkit_device_get_native_path (d);
	device->priv->obj->native_path = g_strdup (native_path);

	/* coldplug source */
	ret = klass->coldplug (device);
	if (!ret)
		goto out;

	/* only put on the bus if we succeeded */
	ret = dkp_device_register_device (device);
	if (!ret)
		goto out;

	/* force a refresh */
	ret = dkp_device_refresh_internal (device);
	if (!ret)
		goto out;

	/* get the id so we can load the old history */
	id = dkp_object_get_id (device->priv->obj);
	if (id != NULL)
		dkp_history_set_id (device->priv->history, id);
	g_free (id);

out:
	return ret;
}

/**
 * dkp_device_get_statistics:
 **/
gboolean
dkp_device_get_statistics (DkpDevice *device, const gchar *type, DBusGMethodInvocation *context)
{
	GError *error;
	EggObjList *array = NULL;
	GPtrArray *complex;
	const DkpStatsObj *obj;
	GValue *value;
	guint i;

	g_return_val_if_fail (DKP_IS_DEVICE (device), FALSE);
	g_return_val_if_fail (type != NULL, FALSE);

	/* doesn't even try to support this */
	if (!device->priv->obj->has_statistics) {
		error = g_error_new (DKP_DAEMON_ERROR, DKP_DAEMON_ERROR_GENERAL, "device does not support getting stats");
		dbus_g_method_return_error (context, error);
		goto out;
	}

	/* get the correct data */
	if (egg_strequal (type, "charging"))
		array = dkp_history_get_profile_data (device->priv->history, TRUE);
	else if (egg_strequal (type, "discharging"))
		array = dkp_history_get_profile_data (device->priv->history, FALSE);

	/* maybe the device doesn't support histories */
	if (array == NULL) {
		error = g_error_new (DKP_DAEMON_ERROR, DKP_DAEMON_ERROR_GENERAL, "device has no statistics");
		dbus_g_method_return_error (context, error);
		goto out;
	}

	/* always 101 items of data */
	if (array->len != 101) {
		error = g_error_new (DKP_DAEMON_ERROR, DKP_DAEMON_ERROR_GENERAL, "statistics invalid as have %i items", array->len);
		dbus_g_method_return_error (context, error);
		goto out;
	}

	/* copy data to dbus struct */
	complex = g_ptr_array_sized_new (array->len);
	for (i=0; i<array->len; i++) {
		obj = (const DkpStatsObj *) egg_obj_list_index (array, i);
		value = g_new0 (GValue, 1);
		g_value_init (value, DKP_DBUS_STRUCT_DOUBLE_DOUBLE);
		g_value_take_boxed (value, dbus_g_type_specialized_construct (DKP_DBUS_STRUCT_DOUBLE_DOUBLE));
		dbus_g_type_struct_set (value, 0, obj->value, 1, obj->accuracy, -1);
		g_ptr_array_add (complex, g_value_get_boxed (value));
		g_free (value);
	}

	dbus_g_method_return (context, complex);
out:
	if (array != NULL)
		g_object_unref (array);
	return TRUE;
}

/**
 * dkp_device_get_history:
 **/
gboolean
dkp_device_get_history (DkpDevice *device, const gchar *type_string, guint timespan, guint resolution, DBusGMethodInvocation *context)
{
	GError *error;
	EggObjList *array = NULL;
	GPtrArray *complex;
	const DkpHistoryObj *obj;
	GValue *value;
	guint i;
	DkpHistoryType type = DKP_HISTORY_TYPE_UNKNOWN;

	g_return_val_if_fail (DKP_IS_DEVICE (device), FALSE);
	g_return_val_if_fail (type_string != NULL, FALSE);

	/* doesn't even try to support this */
	if (!device->priv->obj->has_history) {
		error = g_error_new (DKP_DAEMON_ERROR, DKP_DAEMON_ERROR_GENERAL, "device does not support getting history");
		dbus_g_method_return_error (context, error);
		goto out;
	}

	/* get the correct data */
	if (egg_strequal (type_string, "rate"))
		type = DKP_HISTORY_TYPE_RATE;
	else if (egg_strequal (type_string, "charge"))
		type = DKP_HISTORY_TYPE_CHARGE;
	else if (egg_strequal (type_string, "time-full"))
		type = DKP_HISTORY_TYPE_TIME_FULL;
	else if (egg_strequal (type_string, "time-empty"))
		type = DKP_HISTORY_TYPE_TIME_EMPTY;

	/* something recognised */
	if (type != DKP_HISTORY_TYPE_UNKNOWN)
		array = dkp_history_get_data (device->priv->history, type, timespan, resolution);

	/* maybe the device doesn't have any history */
	if (array == NULL) {
		error = g_error_new (DKP_DAEMON_ERROR, DKP_DAEMON_ERROR_GENERAL, "device has no history");
		dbus_g_method_return_error (context, error);
		goto out;
	}

	/* copy data to dbus struct */
	complex = g_ptr_array_sized_new (array->len);
	for (i=0; i<array->len; i++) {
		obj = (const DkpHistoryObj *) egg_obj_list_index (array, i);
		value = g_new0 (GValue, 1);
		g_value_init (value, DKP_DBUS_STRUCT_UINT_DOUBLE_STRING);
		g_value_take_boxed (value, dbus_g_type_specialized_construct (DKP_DBUS_STRUCT_UINT_DOUBLE_STRING));
		dbus_g_type_struct_set (value, 0, obj->time, 1, obj->value, 2, dkp_device_state_to_text (obj->state), -1);
		g_ptr_array_add (complex, g_value_get_boxed (value));
		g_free (value);
	}

	dbus_g_method_return (context, complex);
out:
	if (array != NULL)
		g_object_unref (array);
	return TRUE;
}

/**
 * dkp_device_refresh_internal:
 **/
static gboolean
dkp_device_refresh_internal (DkpDevice *device)
{
	DkpObject *obj = dkp_device_get_obj (device);
	DkpObject *obj_old;
	gboolean ret;
	gboolean success;
	DkpDeviceClass *klass = DKP_DEVICE_GET_CLASS (device);

	/* make a copy so we can see if anything changed */
	obj_old = dkp_object_copy (obj);

	/* do the refresh */
	success = klass->refresh (device);
	if (!success) {
		egg_warning ("failed to reresh");
		goto out;
	}

	/* the first time, print all properties */
	if (!device->priv->has_ever_refresh) {
		egg_debug ("iniital fillup");
		dkp_object_print (obj);
		device->priv->has_ever_refresh = TRUE;
		goto out;
	}

	/* print difference */
	ret = !dkp_object_equal (obj, obj_old);
	if (ret)
		dkp_object_diff (obj_old, obj);

out:
	dkp_object_free (obj_old);
	return success;
}

/**
 * dkp_device_refresh:
 **/
gboolean
dkp_device_refresh (DkpDevice *device, DBusGMethodInvocation *context)
{
	gboolean ret;

	g_return_val_if_fail (DKP_IS_DEVICE (device), FALSE);

	ret = dkp_device_refresh_internal (device);
	dbus_g_method_return (context);
	return ret;
}


/**
 * dkp_device_changed:
 **/
gboolean
dkp_device_changed (DkpDevice *device, DevkitDevice *d, gboolean synthesized)
{
	gboolean changed;

	g_return_val_if_fail (DKP_IS_DEVICE (device), FALSE);

	g_object_unref (device->priv->d);
	device->priv->d = g_object_ref (d);

	changed = dkp_device_refresh_internal (device);

	/* this 'change' event might prompt us to remove the supply */
	if (!changed)
		goto out;

	/* no, it's good .. keep it */
	dkp_device_emit_changed (device);

out:
	return changed;
}

/**
 * dkp_device_get_object_path:
 **/
const gchar *
dkp_device_get_object_path (DkpDevice *device)
{
	g_return_val_if_fail (DKP_IS_DEVICE (device), NULL);
	return device->priv->object_path;
}

DkpObject *
dkp_device_get_obj (DkpDevice *device)
{
	g_return_val_if_fail (DKP_IS_DEVICE (device), NULL);
	return device->priv->obj;
}

DevkitDevice *
dkp_device_get_d (DkpDevice *device)
{
	g_return_val_if_fail (DKP_IS_DEVICE (device), NULL);
	return device->priv->d;
}

/**
 * dkp_device_emit_changed:
 **/
void
dkp_device_emit_changed (DkpDevice *device)
{
	DkpObject *obj = dkp_device_get_obj (device);

	g_return_if_fail (DKP_IS_DEVICE (device));

	/* save new history */
	dkp_history_set_state (device->priv->history, obj->state);
	dkp_history_set_charge_data (device->priv->history, obj->percentage);
	dkp_history_set_rate_data (device->priv->history, obj->energy_rate);
	dkp_history_set_time_full_data (device->priv->history, obj->time_to_full);
	dkp_history_set_time_empty_data (device->priv->history, obj->time_to_empty);

	egg_debug ("emitting changed on %s", device->priv->obj->native_path);

	/*  The order here matters; we want Device::Changed() before
	 *  the DeviceChanged() signal on the main object; otherwise
	 *  clients that only listens on DeviceChanged() won't be
	 *  fully caught up...
	 */
	g_signal_emit (device, signals[CHANGED_SIGNAL], 0);
	g_signal_emit_by_name (device->priv->daemon, "device-changed",
			       device->priv->object_path, NULL);
}

/**
 * dkp_device_compute_object_path:
 **/
static gchar *
dkp_device_compute_object_path (DkpDevice *device)
{
	gchar *basename;
	gchar *id;
	gchar *object_path;
	const gchar *native_path;
	const gchar *type;
	guint i;

	type = dkp_device_type_to_text (device->priv->obj->type);
	native_path = device->priv->obj->native_path;
	basename = g_path_get_basename (native_path);
	id = g_strjoin ("_", type, basename, NULL);

	/* make DBUS valid path */
	for (i=0; id[i] != '\0'; i++) {
		if (id[i] == '-')
			id[i] = '_';
		if (id[i] == '.')
			id[i] = 'x';
		if (id[i] == ':')
			id[i] = 'o';
	}
	object_path = g_build_filename ("/org/freedesktop/DeviceKit/Power/devices", id, NULL);

	g_free (basename);
	g_free (id);

	return object_path;
}

/**
 * dkp_device_register_device:
 **/
static gboolean
dkp_device_register_device (DkpDevice *device)
{
	gboolean ret = TRUE;

	device->priv->object_path = dkp_device_compute_object_path (device);
	egg_debug ("object path = %s", device->priv->object_path);
	dbus_g_connection_register_g_object (device->priv->system_bus_connection,
					     device->priv->object_path, G_OBJECT (device));
	device->priv->system_bus_proxy = dbus_g_proxy_new_for_name (device->priv->system_bus_connection,
								    DBUS_SERVICE_DBUS, DBUS_PATH_DBUS, DBUS_INTERFACE_DBUS);
	if (device->priv->system_bus_proxy == NULL) {
		egg_warning ("proxy invalid");
		ret = FALSE;
	}
	return ret;
}

/**
 * dkp_device_init:
 **/
static void
dkp_device_init (DkpDevice *device)
{
	GError *error = NULL;

	device->priv = DKP_DEVICE_GET_PRIVATE (device);
	device->priv->object_path = NULL;
	device->priv->system_bus_connection = NULL;
	device->priv->system_bus_proxy = NULL;
	device->priv->daemon = NULL;
	device->priv->d = NULL;
	device->priv->has_ever_refresh = FALSE;
	device->priv->obj = dkp_object_new ();
	device->priv->history = dkp_history_new ();

	device->priv->system_bus_connection = dbus_g_bus_get (DBUS_BUS_SYSTEM, &error);
	if (device->priv->system_bus_connection == NULL) {
		egg_error ("error getting system bus: %s", error->message);
		g_error_free (error);
	}
}

/**
 * dkp_device_finalize:
 **/
static void
dkp_device_finalize (GObject *object)
{
	DkpDevice *device;

	g_return_if_fail (object != NULL);
	g_return_if_fail (DKP_IS_DEVICE (object));

	device = DKP_DEVICE (object);
	g_return_if_fail (device->priv != NULL);
	g_object_unref (device->priv->d);
	g_object_unref (device->priv->daemon);
	g_object_unref (device->priv->history);
	dkp_object_free (device->priv->obj);

	G_OBJECT_CLASS (dkp_device_parent_class)->finalize (object);
}

/**
 * dkp_device_class_init:
 **/
static void
dkp_device_class_init (DkpDeviceClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	object_class->get_property = dkp_device_get_property;
	object_class->finalize = dkp_device_finalize;

	g_type_class_add_private (klass, sizeof (DkpDevicePrivate));

	signals[CHANGED_SIGNAL] =
		g_signal_new ("changed",
			      G_OBJECT_CLASS_TYPE (klass),
			      G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED,
			      0, NULL, NULL,
			      g_cclosure_marshal_VOID__VOID,
			      G_TYPE_NONE, 0);

	dbus_g_object_type_install_info (DKP_TYPE_DEVICE, &dbus_glib_dkp_device_object_info);

	g_object_class_install_property (
		object_class,
		PROP_NATIVE_PATH,
		g_param_spec_string ("native-path", NULL, NULL, NULL, G_PARAM_READABLE));
	g_object_class_install_property (
		object_class,
		PROP_VENDOR,
		g_param_spec_string ("vendor", NULL, NULL, NULL, G_PARAM_READABLE));
	g_object_class_install_property (
		object_class,
		PROP_MODEL,
		g_param_spec_string ("model", NULL, NULL, NULL, G_PARAM_READABLE));
	g_object_class_install_property (
		object_class,
		PROP_SERIAL,
		g_param_spec_string ("serial", NULL, NULL, NULL, G_PARAM_READABLE));
	g_object_class_install_property (
		object_class,
		PROP_UPDATE_TIME,
		g_param_spec_uint64 ("update-time", NULL, NULL, 0, G_MAXUINT64, 0, G_PARAM_READABLE));
	g_object_class_install_property (
		object_class,
		PROP_TYPE,
		g_param_spec_string ("type", NULL, NULL, NULL, G_PARAM_READABLE));
	g_object_class_install_property (
		object_class,
		PROP_POWER_SUPPLY,
		g_param_spec_boolean ("power-supply", NULL, NULL, FALSE, G_PARAM_READABLE));
	g_object_class_install_property (
		object_class,
		PROP_LINE_POWER_ONLINE,
		g_param_spec_boolean ("online", NULL, NULL, FALSE, G_PARAM_READABLE));
	g_object_class_install_property (
		object_class,
		PROP_BATTERY_ENERGY,
		g_param_spec_double ("energy", NULL, NULL, 0, G_MAXDOUBLE, 0, G_PARAM_READABLE));
	g_object_class_install_property (
		object_class,
		PROP_BATTERY_IS_PRESENT,
		g_param_spec_boolean ("is-present", NULL, NULL, FALSE, G_PARAM_READABLE));
	g_object_class_install_property (
		object_class,
		PROP_BATTERY_IS_RECHARGEABLE,
		g_param_spec_boolean ("is-rechargeable", NULL, NULL, FALSE, G_PARAM_READABLE));
	g_object_class_install_property (
		object_class,
		PROP_BATTERY_HAS_HISTORY,
		g_param_spec_boolean ("has-history", NULL, NULL, FALSE, G_PARAM_READABLE));
	g_object_class_install_property (
		object_class,
		PROP_BATTERY_HAS_STATISTICS,
		g_param_spec_boolean ("has-statistics", NULL, NULL, FALSE, G_PARAM_READABLE));
	g_object_class_install_property (
		object_class,
		PROP_BATTERY_STATE,
		g_param_spec_string ("state", NULL, NULL, NULL, G_PARAM_READABLE));
	g_object_class_install_property (
		object_class,
		PROP_BATTERY_CAPACITY,
		g_param_spec_double ("capacity", NULL, NULL, 0, G_MAXDOUBLE, 0, G_PARAM_READABLE));
	g_object_class_install_property (
		object_class,
		PROP_BATTERY_ENERGY_EMPTY,
		g_param_spec_double ("energy-empty", NULL, NULL, 0, G_MAXDOUBLE, 0, G_PARAM_READABLE));
	g_object_class_install_property (
		object_class,
		PROP_BATTERY_ENERGY_FULL,
		g_param_spec_double ("energy-full", NULL, NULL, 0, G_MAXDOUBLE, 0, G_PARAM_READABLE));
	g_object_class_install_property (
		object_class,
		PROP_BATTERY_ENERGY_FULL_DESIGN,
		g_param_spec_double ("energy-full-design", NULL, NULL, 0, G_MAXDOUBLE, 0, G_PARAM_READABLE));
	g_object_class_install_property (
		object_class,
		PROP_BATTERY_ENERGY_RATE,
		g_param_spec_double ("energy-rate", NULL, NULL, -G_MAXDOUBLE, G_MAXDOUBLE, 0, G_PARAM_READABLE));
	g_object_class_install_property (
		object_class,
		PROP_BATTERY_VOLTAGE,
		g_param_spec_double ("voltage", NULL, NULL, -G_MAXDOUBLE, G_MAXDOUBLE, 0, G_PARAM_READABLE));
	g_object_class_install_property (
		object_class,
		PROP_BATTERY_TIME_TO_EMPTY,
		g_param_spec_int64 ("time-to-empty", NULL, NULL, 0, G_MAXINT64, 0, G_PARAM_READABLE));
	g_object_class_install_property (
		object_class,
		PROP_BATTERY_TIME_TO_FULL,
		g_param_spec_int64 ("time-to-full", NULL, NULL, 0, G_MAXINT64, 0, G_PARAM_READABLE));
	g_object_class_install_property (
		object_class,
		PROP_BATTERY_PERCENTAGE,
		g_param_spec_double ("percentage", NULL, NULL, 0, 100, 0, G_PARAM_READABLE));
	g_object_class_install_property (
		object_class,
		PROP_BATTERY_TECHNOLOGY,
		g_param_spec_string ("technology", NULL, NULL, NULL, G_PARAM_READABLE));

	dbus_g_error_domain_register (DKP_DEVICE_ERROR, NULL, DKP_DEVICE_TYPE_ERROR);
}

