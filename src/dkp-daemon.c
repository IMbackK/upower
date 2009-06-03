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
#include <glib/gi18n-lib.h>
#include <glib-object.h>
#include <dbus/dbus-glib.h>
#include <dbus/dbus-glib-lowlevel.h>
#include <devkit-gobject/devkit-gobject.h>

#include "egg-debug.h"

#include "dkp-polkit.h"
#include "dkp-daemon.h"
#include "dkp-device.h"
#include "dkp-supply.h"
#include "dkp-csr.h"
#include "dkp-wup.h"
#include "dkp-hid.h"
#include "dkp-input.h"
#include "dkp-device-list.h"

#include "dkp-daemon-glue.h"
#include "dkp-marshal.h"

enum
{
	PROP_0,
	PROP_DAEMON_VERSION,
	PROP_CAN_SUSPEND,
	PROP_CAN_HIBERNATE,
	PROP_ON_BATTERY,
	PROP_ON_LOW_BATTERY,
	PROP_LID_IS_CLOSED,
};

enum
{
	DEVICE_ADDED_SIGNAL,
	DEVICE_REMOVED_SIGNAL,
	DEVICE_CHANGED_SIGNAL,
	CHANGED_SIGNAL,
	LAST_SIGNAL,
};

static const gchar *subsystems[] = {"power_supply", "usb", "tty", "input", NULL};

static guint signals[LAST_SIGNAL] = { 0 };

struct DkpDaemonPrivate
{
	DBusGConnection		*connection;
	DBusGProxy		*proxy;
	DkpPolkit		*polkit;
	DkpDeviceList		*list;
	GPtrArray		*inputs;
	gboolean		 on_battery;
	gboolean		 low_battery;
	DevkitClient		*devkit_client;
	gboolean		 lid_is_closed;
};

static void	dkp_daemon_class_init		(DkpDaemonClass *klass);
static void	dkp_daemon_init			(DkpDaemon	*seat);
static void	dkp_daemon_finalize		(GObject	*object);
static gboolean	dkp_daemon_get_on_battery_local	(DkpDaemon	*daemon);
static gboolean	dkp_daemon_get_low_battery_local (DkpDaemon	*daemon);
static gboolean	dkp_daemon_get_on_ac_local 	(DkpDaemon	*daemon);

G_DEFINE_TYPE (DkpDaemon, dkp_daemon, G_TYPE_OBJECT)

#define DKP_DAEMON_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), DKP_TYPE_DAEMON, DkpDaemonPrivate))

/**
 * dkp_daemon_set_lid_is_closed:
 **/
gboolean
dkp_daemon_set_lid_is_closed (DkpDaemon *daemon, gboolean lid_is_closed)
{
	gboolean ret = FALSE;

	g_return_val_if_fail (DKP_IS_DAEMON (daemon), FALSE);

	egg_debug ("lid_is_closed=%i", lid_is_closed);
	if (daemon->priv->lid_is_closed == lid_is_closed) {
		egg_debug ("ignoring duplicate");
		goto out;
	}

	/* save */
	g_signal_emit (daemon, signals[CHANGED_SIGNAL], 0);
	daemon->priv->lid_is_closed = lid_is_closed;
	ret = TRUE;
out:
	return ret;
}

/**
 * dkp_daemon_error_quark:
 **/
GQuark
dkp_daemon_error_quark (void)
{
	static GQuark ret = 0;
	if (ret == 0)
		ret = g_quark_from_static_string ("dkp_daemon_error");
	return ret;
}

#define ENUM_ENTRY(NAME, DESC) { NAME, "" #NAME "", DESC }
/**
 * dkp_daemon_error_get_type:
 **/
GType
dkp_daemon_error_get_type (void)
{
	static GType etype = 0;

	if (etype == 0)
	{
		static const GEnumValue values[] =
			{
				ENUM_ENTRY (DKP_DAEMON_ERROR_GENERAL, "GeneralError"),
				ENUM_ENTRY (DKP_DAEMON_ERROR_NOT_SUPPORTED, "NotSupported"),
				ENUM_ENTRY (DKP_DAEMON_ERROR_NO_SUCH_DEVICE, "NoSuchDevice"),
				{ 0, 0, 0 }
			};
		g_assert (DKP_DAEMON_NUM_ERRORS == G_N_ELEMENTS (values) - 1);
		etype = g_enum_register_static ("DkpDaemonError", values);
	}
	return etype;
}

/**
 * dkp_daemon_constructor:
 **/
static GObject *
dkp_daemon_constructor (GType type, guint n_construct_properties, GObjectConstructParam *construct_properties)
{
	DkpDaemon *daemon;
	DkpDaemonClass *klass;

	klass = DKP_DAEMON_CLASS (g_type_class_peek (DKP_TYPE_DAEMON));
	daemon = DKP_DAEMON (G_OBJECT_CLASS (dkp_daemon_parent_class)->constructor (type, n_construct_properties, construct_properties));
	return G_OBJECT (daemon);
}

/**
 * dkp_daemon_get_property:
 **/
static void
dkp_daemon_get_property (GObject         *object,
			 guint            prop_id,
			 GValue          *value,
			 GParamSpec      *pspec)
{
	DkpDaemon *daemon;

	daemon = DKP_DAEMON (object);

	switch (prop_id) {

	case PROP_DAEMON_VERSION:
		g_value_set_string (value, PACKAGE_VERSION);
		break;

	case PROP_CAN_SUSPEND:
		/* TODO: for now assume we can always suspend */
		g_value_set_boolean (value, TRUE);
		break;

	case PROP_CAN_HIBERNATE:
		/* TODO for now assume we can always hibernate */
		g_value_set_boolean (value, TRUE);
		break;

	case PROP_ON_BATTERY:
		g_value_set_boolean (value, daemon->priv->on_battery);
		break;

	case PROP_ON_LOW_BATTERY:
		g_value_set_boolean (value, daemon->priv->on_battery && daemon->priv->low_battery);
		break;

	case PROP_LID_IS_CLOSED:
		g_value_set_boolean (value, daemon->priv->lid_is_closed);
		break;

	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

/**
 * dkp_daemon_class_init:
 **/
static void
dkp_daemon_class_init (DkpDaemonClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	object_class->constructor = dkp_daemon_constructor;
	object_class->finalize = dkp_daemon_finalize;
	object_class->get_property = dkp_daemon_get_property;

	g_type_class_add_private (klass, sizeof (DkpDaemonPrivate));

	signals[DEVICE_ADDED_SIGNAL] =
		g_signal_new ("device-added",
			      G_OBJECT_CLASS_TYPE (klass),
			      G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED,
			      0, NULL, NULL,
			      g_cclosure_marshal_VOID__STRING,
			      G_TYPE_NONE, 1, G_TYPE_STRING);

	signals[DEVICE_REMOVED_SIGNAL] =
		g_signal_new ("device-removed",
			      G_OBJECT_CLASS_TYPE (klass),
			      G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED,
			      0, NULL, NULL,
			      g_cclosure_marshal_VOID__STRING,
			      G_TYPE_NONE, 1, G_TYPE_STRING);

	signals[DEVICE_CHANGED_SIGNAL] =
		g_signal_new ("device-changed",
			      G_OBJECT_CLASS_TYPE (klass),
			      G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED,
			      0, NULL, NULL,
			      g_cclosure_marshal_VOID__STRING,
			      G_TYPE_NONE, 1, G_TYPE_STRING);

	signals[CHANGED_SIGNAL] =
		g_signal_new ("changed",
			      G_OBJECT_CLASS_TYPE (klass),
			      G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED,
			      0, NULL, NULL,
			      g_cclosure_marshal_VOID__BOOLEAN,
			      G_TYPE_NONE, 0);


	g_object_class_install_property (object_class,
					 PROP_DAEMON_VERSION,
					 g_param_spec_string ("daemon-version",
							      "Daemon Version",
							      "The version of the running daemon",
							      NULL,
							      G_PARAM_READABLE));

	g_object_class_install_property (object_class,
					 PROP_CAN_SUSPEND,
					 g_param_spec_boolean ("can-suspend",
							       "Can Suspend",
							       "Whether the system can suspend",
							       FALSE,
							       G_PARAM_READABLE));

	g_object_class_install_property (object_class,
					 PROP_CAN_SUSPEND,
					 g_param_spec_boolean ("can-hibernate",
							       "Can Hibernate",
							       "Whether the system can hibernate",
							       FALSE,
							       G_PARAM_READABLE));

	g_object_class_install_property (object_class,
					 PROP_ON_BATTERY,
					 g_param_spec_boolean ("on-battery",
							       "On Battery",
							       "Whether the system is running on battery",
							       FALSE,
							       G_PARAM_READABLE));

	g_object_class_install_property (object_class,
					 PROP_ON_LOW_BATTERY,
					 g_param_spec_boolean ("on-low-battery",
							       "On Low Battery",
							       "Whether the system is running on battery and if the battery is critically low",
							       FALSE,
							       G_PARAM_READABLE));

	g_object_class_install_property (object_class,
					 PROP_LID_IS_CLOSED,
					 g_param_spec_boolean ("lid-is-closed",
							       "Laptop lid is closed",
							       "If the laptop lid is closed",
							       FALSE,
							       G_PARAM_READABLE));

	dbus_g_object_type_install_info (DKP_TYPE_DAEMON, &dbus_glib_dkp_daemon_object_info);

	dbus_g_error_domain_register (DKP_DAEMON_ERROR, NULL, DKP_DAEMON_TYPE_ERROR);
}

/**
 * dkp_daemon_init:
 **/
static void
dkp_daemon_init (DkpDaemon *daemon)
{
	daemon->priv = DKP_DAEMON_GET_PRIVATE (daemon);
	daemon->priv->polkit = dkp_polkit_new ();
	daemon->priv->lid_is_closed = FALSE;
	daemon->priv->inputs = g_ptr_array_new ();
}

/**
 * dkp_daemon_finalize:
 **/
static void
dkp_daemon_finalize (GObject *object)
{
	DkpDaemon *daemon;

	g_return_if_fail (object != NULL);
	g_return_if_fail (DKP_IS_DAEMON (object));

	daemon = DKP_DAEMON (object);

	g_return_if_fail (daemon->priv != NULL);

	if (daemon->priv->proxy != NULL)
		g_object_unref (daemon->priv->proxy);
	if (daemon->priv->connection != NULL)
		dbus_g_connection_unref (daemon->priv->connection);
	if (daemon->priv->devkit_client != NULL)
		g_object_unref (daemon->priv->devkit_client);
	if (daemon->priv->list != NULL)
		g_object_unref (daemon->priv->list);
	g_object_unref (daemon->priv->polkit);

	/* unref inputs */
	g_ptr_array_foreach (daemon->priv->inputs, (GFunc) g_object_unref, NULL);
	g_ptr_array_free (daemon->priv->inputs, TRUE);

	G_OBJECT_CLASS (dkp_daemon_parent_class)->finalize (object);
}

static gboolean gpk_daemon_device_add (DkpDaemon *daemon, DevkitDevice *d, gboolean emit_event);
static void gpk_daemon_device_remove (DkpDaemon *daemon, DevkitDevice *d);

/**
 * dkp_daemon_get_on_battery_local:
 *
 * As soon as _any_ battery goes discharging, this is true
 **/
static gboolean
dkp_daemon_get_on_battery_local (DkpDaemon *daemon)
{
	guint i;
	gboolean ret;
	gboolean result = FALSE;
	gboolean on_battery;
	DkpDevice *device;
	const GPtrArray *array;

	/* ask each device */
	array = dkp_device_list_get_array (daemon->priv->list);
	for (i=0; i<array->len; i++) {
		device = (DkpDevice *) g_ptr_array_index (array, i);
		ret = dkp_device_get_on_battery (device, &on_battery);
		if (ret && on_battery) {
			result = TRUE;
			break;
		}
	}
	return result;
}

/**
 * dkp_daemon_get_low_battery_local:
 *
 * As soon as _all_ batteries are low, this is true
 **/
static gboolean
dkp_daemon_get_low_battery_local (DkpDaemon *daemon)
{
	guint i;
	gboolean ret;
	gboolean result = TRUE;
	gboolean low_battery;
	DkpDevice *device;
	const GPtrArray *array;

	/* ask each device */
	array = dkp_device_list_get_array (daemon->priv->list);
	for (i=0; i<array->len; i++) {
		device = (DkpDevice *) g_ptr_array_index (array, i);
		ret = dkp_device_get_low_battery (device, &low_battery);
		if (ret && !low_battery) {
			result = FALSE;
			break;
		}
	}
	return result;
}

/**
 * dkp_daemon_get_on_ac_local:
 *
 * As soon as _any_ ac supply goes online, this is true
 **/
static gboolean
dkp_daemon_get_on_ac_local (DkpDaemon *daemon)
{
	guint i;
	gboolean ret;
	gboolean result = FALSE;
	gboolean online;
	DkpDevice *device;
	const GPtrArray *array;

	/* ask each device */
	array = dkp_device_list_get_array (daemon->priv->list);
	for (i=0; i<array->len; i++) {
		device = (DkpDevice *) g_ptr_array_index (array, i);
		ret = dkp_device_get_online (device, &online);
		if (ret && online) {
			result = TRUE;
			break;
		}
	}
	return result;
}

/**
 * gpk_daemon_device_changed:
 **/
static void
gpk_daemon_device_changed (DkpDaemon *daemon, DevkitDevice *d, gboolean synthesized)
{
	DkpDevice *device;
	gboolean ret;

	/* first, change the device and add it if it doesn't exist */
	device = dkp_device_list_lookup (daemon->priv->list, d);
	if (device != NULL) {
		egg_debug ("changed %s", dkp_device_get_object_path (device));
		dkp_device_changed (device, d, synthesized);
	} else {
		egg_debug ("treating change event as add on %s", dkp_device_get_object_path (device));
		gpk_daemon_device_add (daemon, d, TRUE);
	}

	/* second, check if the on_battery and low_battery state has changed */
	ret = (dkp_daemon_get_on_battery_local (daemon) && !dkp_daemon_get_on_ac_local (daemon));
	if (ret != daemon->priv->on_battery) {
		daemon->priv->on_battery = ret;
		egg_debug ("now on_battery = %s", ret ? "yes" : "no");
		g_signal_emit (daemon, signals[CHANGED_SIGNAL], 0);
	}
	ret = dkp_daemon_get_low_battery_local (daemon);
	if (ret != daemon->priv->low_battery) {
		daemon->priv->low_battery = ret;
		egg_debug ("now low_battery = %s", ret ? "yes" : "no");
		g_signal_emit (daemon, signals[CHANGED_SIGNAL], 0);
	}
}

/**
 * gpk_daemon_device_went_away:
 **/
static void
gpk_daemon_device_went_away (gpointer user_data, GObject *_device)
{
	DkpDaemon *daemon = DKP_DAEMON (user_data);
	DkpDevice *device = DKP_DEVICE (_device);
	dkp_device_list_remove (daemon->priv->list, device);
}

/**
 * gpk_daemon_device_get:
 **/
static DkpDevice *
gpk_daemon_device_get (DkpDaemon *daemon, DevkitDevice *d)
{
	const gchar *subsys;
	const gchar *native_path;
	DkpDevice *device = NULL;
	DkpInput *input;
	gboolean ret;

	subsys = devkit_device_get_subsystem (d);
	if (g_strcmp0 (subsys, "power_supply") == 0) {

		/* are we a valid power supply */
		device = DKP_DEVICE (dkp_supply_new ());
		ret = dkp_device_coldplug (device, daemon, d);
		if (ret)
			goto out;
		g_object_unref (device);

		/* no valid power supply object */
		device = NULL;

	} else if (g_strcmp0 (subsys, "tty") == 0) {

		/* try to detect a Watts Up? Pro monitor */
		device = DKP_DEVICE (dkp_wup_new ());
		ret = dkp_device_coldplug (device, daemon, d);
		if (ret)
			goto out;
		g_object_unref (device);

		/* no valid TTY object ;-( */
		device = NULL;

	} else if (g_strcmp0 (subsys, "usb") == 0) {

		/* see if this is a CSR mouse or keyboard */
		device = DKP_DEVICE (dkp_csr_new ());
		ret = dkp_device_coldplug (device, daemon, d);
		if (ret)
			goto out;
		g_object_unref (device);

		/* try to detect a HID UPS */
		device = DKP_DEVICE (dkp_hid_new ());
		ret = dkp_device_coldplug (device, daemon, d);
		if (ret)
			goto out;
		g_object_unref (device);

		/* no valid USB object ;-( */
		device = NULL;

	} else if (g_strcmp0 (subsys, "input") == 0) {

		/* check input device */
		input = dkp_input_new ();
		ret = dkp_input_coldplug (input, daemon, d);
		if (!ret) {
			g_object_unref (input);
			goto out;
		}

		/* we can't use the device list as it's not a DkpDevice */
		g_ptr_array_add (daemon->priv->inputs, input);

		/* no valid input object */
		device = NULL;

	} else {
		native_path = devkit_device_get_native_path (d);
		egg_warning ("native path %s (%s) ignoring", native_path, subsys);
	}
out:
	return device;
}

/**
 * gpk_daemon_device_add:
 **/
static gboolean
gpk_daemon_device_add (DkpDaemon *daemon, DevkitDevice *d, gboolean emit_event)
{
	DkpDevice *device;
	gboolean ret = TRUE;

	/* does device exist in db? */
	device = dkp_device_list_lookup (daemon->priv->list, d);
	if (device != NULL) {
		/* we already have the device; treat as change event */
		egg_debug ("treating add event as change event on %s", dkp_device_get_object_path (device));
		gpk_daemon_device_changed (daemon, d, FALSE);
	} else {

		/* get the right sort of device */
		device = gpk_daemon_device_get (daemon, d);
		if (device == NULL) {
			egg_debug ("not adding device %s", devkit_device_get_native_path (d));
			ret = FALSE;
			goto out;
		}
		/* only take a weak ref; the device will stay on the bus until
		 * it's unreffed. So if we ref it, it'll never go away.
		 */
		g_object_weak_ref (G_OBJECT (device), gpk_daemon_device_went_away, daemon);
		dkp_device_list_insert (daemon->priv->list, d, device);
		if (emit_event) {
			g_signal_emit (daemon, signals[DEVICE_ADDED_SIGNAL], 0,
				       dkp_device_get_object_path (device));
		}
	}
out:
	return ret;
}

/**
 * gpk_daemon_device_remove:
 **/
static void
gpk_daemon_device_remove (DkpDaemon *daemon, DevkitDevice *d)
{
	DkpDevice *device;

	/* does device exist in db? */
	device = dkp_device_list_lookup (daemon->priv->list, d);
	if (device == NULL) {
		egg_debug ("ignoring remove event on %s", devkit_device_get_native_path (d));
	} else {
		dkp_device_removed (device);
		g_signal_emit (daemon, signals[DEVICE_REMOVED_SIGNAL], 0,
			       dkp_device_get_object_path (device));
		g_object_unref (device);
	}
}

/**
 * gpk_daemon_device_event_signal_handler:
 **/
static void
gpk_daemon_device_event_signal_handler (DevkitClient *client, const char *action,
					DevkitDevice *device, gpointer user_data)
{
	DkpDaemon *daemon = DKP_DAEMON (user_data);

	if (g_strcmp0 (action, "add") == 0) {
		egg_debug ("add %s", devkit_device_get_native_path (device));
		gpk_daemon_device_add (daemon, device, TRUE);
	} else if (g_strcmp0 (action, "remove") == 0) {
		egg_debug ("remove %s", devkit_device_get_native_path (device));
		gpk_daemon_device_remove (daemon, device);
	} else if (g_strcmp0 (action, "change") == 0) {
		egg_debug ("change %s", devkit_device_get_native_path (device));
		gpk_daemon_device_changed (daemon, device, FALSE);
	} else {
		egg_warning ("unhandled action '%s' on %s", action, devkit_device_get_native_path (device));
	}
}

#if 0
/**
 * gpk_daemon_throw_error:
 **/
static gboolean
gpk_daemon_throw_error (DBusGMethodInvocation *context, int error_code, const char *format, ...)
{
	GError *error;
	va_list args;
	gchar *message;

	va_start (args, format);
	message = g_strdup_vprintf (format, args);
	va_end (args);

	error = g_error_new (DKP_DAEMON_ERROR, error_code, message);
	dbus_g_method_return_error (context, error);
	g_error_free (error);
	g_free (message);
	return TRUE;
}
#endif

/* exported methods */

/**
 * dkp_daemon_enumerate_devices:
 **/
gboolean
dkp_daemon_enumerate_devices (DkpDaemon *daemon, DBusGMethodInvocation *context)
{
	guint i;
	const GPtrArray *array;
	GPtrArray *object_paths;
	DkpDevice *device;

	/* build a pointer array of the object paths */
	object_paths = g_ptr_array_new ();
	array = dkp_device_list_get_array (daemon->priv->list);
	for (i=0; i<array->len; i++) {
		device = (DkpDevice *) g_ptr_array_index (array, i);
		g_ptr_array_add (object_paths, g_strdup (dkp_device_get_object_path (device)));
	}

	/* return it on the bus */
	dbus_g_method_return (context, object_paths);

	/* free */
	g_ptr_array_foreach (object_paths, (GFunc) g_free, NULL);
	g_ptr_array_free (object_paths, TRUE);
	return TRUE;
}

/**
 * dkp_daemon_suspend:
 **/
gboolean
dkp_daemon_suspend (DkpDaemon *daemon, DBusGMethodInvocation *context)
{
	gboolean ret;
	GError *error;
	GError *error_local = NULL;
	PolKitCaller *caller;
	gchar *stdout = NULL;
	gchar *stderr = NULL;

	caller = dkp_polkit_get_caller (daemon->priv->polkit, context);
	if (caller == NULL)
		goto out;

	if (!dkp_polkit_check_auth (daemon->priv->polkit, caller, "org.freedesktop.devicekit.power.suspend", context))
		goto out;

	ret = g_spawn_command_line_sync ("/usr/sbin/pm-suspend", &stdout, &stderr, NULL, &error_local);
	if (!ret) {
		error = g_error_new (DKP_DAEMON_ERROR,
				     DKP_DAEMON_ERROR_GENERAL,
				     "Failed to spawn: %s, stdout:%s, stderr:%s", error_local->message, stdout, stderr);
		g_error_free (error_local);
		dbus_g_method_return_error (context, error);
		goto out;
	}
	dbus_g_method_return (context, NULL);
out:
	g_free (stdout);
	g_free (stderr);
	if (caller != NULL)
		polkit_caller_unref (caller);
	return TRUE;
}

/**
 * dkp_daemon_hibernate:
 **/
gboolean
dkp_daemon_hibernate (DkpDaemon *daemon, DBusGMethodInvocation *context)
{
	gboolean ret;
	GError *error;
	GError *error_local = NULL;
	PolKitCaller *caller;
	gchar *stdout = NULL;
	gchar *stderr = NULL;

	caller = dkp_polkit_get_caller (daemon->priv->polkit, context);
	if (caller == NULL)
		goto out;

	if (!dkp_polkit_check_auth (daemon->priv->polkit, caller, "org.freedesktop.devicekit.power.hibernate", context))
		goto out;

	ret = g_spawn_command_line_sync ("/usr/sbin/pm-hibernate", &stdout, &stderr, NULL, &error_local);
	if (!ret) {
		error = g_error_new (DKP_DAEMON_ERROR,
				     DKP_DAEMON_ERROR_GENERAL,
				     "Failed to spawn: %s, stdout:%s, stderr:%s", error_local->message, stdout, stderr);
		g_error_free (error_local);
		dbus_g_method_return_error (context, error);
		goto out;
	}
	dbus_g_method_return (context, NULL);
out:
	g_free (stdout);
	g_free (stderr);
	if (caller != NULL)
		polkit_caller_unref (caller);
	return TRUE;
}

/**
 * gpk_daemon_register_power_daemon:
 **/
static gboolean
gpk_daemon_register_power_daemon (DkpDaemon *daemon)
{
	DBusConnection *connection;
	DBusError dbus_error;
	GError *error = NULL;
	guint i;

	error = NULL;
	daemon->priv->connection = dbus_g_bus_get (DBUS_BUS_SYSTEM, &error);
	if (daemon->priv->connection == NULL) {
		if (error != NULL) {
			g_critical ("error getting system bus: %s", error->message);
			g_error_free (error);
		}
		goto error;
	}
	connection = dbus_g_connection_get_connection (daemon->priv->connection);

	dbus_g_connection_register_g_object (daemon->priv->connection,
					     "/org/freedesktop/DeviceKit/Power",
					     G_OBJECT (daemon));

	daemon->priv->proxy = dbus_g_proxy_new_for_name (daemon->priv->connection,
						      DBUS_SERVICE_DBUS,
						      DBUS_PATH_DBUS,
						      DBUS_INTERFACE_DBUS);
	dbus_error_init (&dbus_error);
	if (dbus_error_is_set (&dbus_error)) {
		egg_warning ("Cannot add match rule: %s: %s", dbus_error.name, dbus_error.message);
		dbus_error_free (&dbus_error);
		goto error;
	}

	/* connect to the DeviceKit daemon */
	for (i=0; subsystems[i] != NULL; i++)
		egg_debug ("registering subsystem : %s", subsystems[i]);

	daemon->priv->devkit_client = devkit_client_new (subsystems);
	if (!devkit_client_connect (daemon->priv->devkit_client, &error)) {
		egg_warning ("Couldn't open connection to DeviceKit daemon: %s", error->message);
		g_error_free (error);
		goto error;
	}
	g_signal_connect (daemon->priv->devkit_client, "device-event",
			  G_CALLBACK (gpk_daemon_device_event_signal_handler), daemon);

	return TRUE;
error:
	return FALSE;
}

/**
 * dkp_daemon_new:
 **/
DkpDaemon *
dkp_daemon_new (void)
{
	DkpDaemon *daemon;
	GError *error = NULL;
	GList *devices;
	GList *l;

	daemon = DKP_DAEMON (g_object_new (DKP_TYPE_DAEMON, NULL));

	daemon->priv->list = dkp_device_list_new ();
	if (!gpk_daemon_register_power_daemon (DKP_DAEMON (daemon))) {
		g_object_unref (daemon);
		return NULL;
	}

	devices = devkit_client_enumerate_by_subsystem (daemon->priv->devkit_client, subsystems, &error);
	if (error != NULL) {
		egg_warning ("Cannot enumerate devices: %s", error->message);
		g_error_free (error);
		g_object_unref (daemon);
		return NULL;
	}

	for (l = devices; l != NULL; l = l->next) {
		DevkitDevice *device = l->data;
		gpk_daemon_device_add (daemon, device, FALSE);
	}
	g_list_foreach (devices, (GFunc) g_object_unref, NULL);
	g_list_free (devices);

	daemon->priv->on_battery = (dkp_daemon_get_on_battery_local (daemon) &&
				    !dkp_daemon_get_on_ac_local (daemon));
	daemon->priv->low_battery = dkp_daemon_get_low_battery_local (daemon);

	return daemon;
}

