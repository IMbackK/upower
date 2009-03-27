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
#include <math.h>

#include <glib.h>
#include <glib/gstdio.h>
#include <glib/gi18n-lib.h>
#include <glib-object.h>
#include <devkit-gobject/devkit-gobject.h>

#include "sysfs-utils.h"
#include "egg-debug.h"
#include "egg-string.h"

#include "dkp-enum.h"
#include "dkp-object.h"
#include "dkp-supply.h"

#define DKP_SUPPLY_REFRESH_TIMEOUT	10L

struct DkpSupplyPrivate
{
	guint			 poll_timer_id;
	gboolean		 has_coldplug_values;
	gdouble			 energy_old;
	GTimeVal		 energy_old_timespec;
};

static void	dkp_supply_class_init	(DkpSupplyClass	*klass);

G_DEFINE_TYPE (DkpSupply, dkp_supply, DKP_TYPE_DEVICE)
#define DKP_SUPPLY_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), DKP_TYPE_SUPPLY, DkpSupplyPrivate))

static gboolean		 dkp_supply_refresh	 	(DkpDevice *device);

/**
 * dkp_supply_refresh_line_power:
 **/
static gboolean
dkp_supply_refresh_line_power (DkpSupply *supply)
{
	DkpDevice *device = DKP_DEVICE (supply);
	DkpObject *obj = dkp_device_get_obj (device);

	/* force true */
	obj->power_supply = TRUE;

	/* get new AC value */
	obj->online = sysfs_get_int (obj->native_path, "online");

	return TRUE;
}

/**
 * dkp_supply_reset_values:
 **/
static void
dkp_supply_reset_values (DkpSupply *supply)
{
	gchar *native_path;
	DkpDeviceType type;
	DkpDevice *device = DKP_DEVICE (supply);
	DkpObject *obj = dkp_device_get_obj (device);

	/* some stuff we copy */
	type = obj->type;
	native_path = g_strdup (obj->native_path);

	supply->priv->has_coldplug_values = FALSE;
	supply->priv->energy_old = 0;
	supply->priv->energy_old_timespec.tv_sec = 0;
	dkp_object_clear (obj);

	/* restore the saved stuff */
	obj->type = type;
	obj->native_path = native_path;
}

/**
 * dkp_supply_get_on_battery:
 **/
static gboolean
dkp_supply_get_on_battery (DkpDevice *device, gboolean *on_battery)
{
	DkpSupply *supply = DKP_SUPPLY (device);
	DkpObject *obj = dkp_device_get_obj (device);

	g_return_val_if_fail (DKP_IS_SUPPLY (supply), FALSE);
	g_return_val_if_fail (on_battery != NULL, FALSE);

	if (obj->type != DKP_DEVICE_TYPE_BATTERY)
		return FALSE;
	if (!obj->is_present)
		return FALSE;

	*on_battery = (obj->state == DKP_DEVICE_STATE_DISCHARGING);
	return TRUE;
}

/**
 * dkp_supply_get_low_battery:
 **/
static gboolean
dkp_supply_get_low_battery (DkpDevice *device, gboolean *low_battery)
{
	gboolean ret;
	gboolean on_battery;
	DkpSupply *supply = DKP_SUPPLY (device);
	DkpObject *obj = dkp_device_get_obj (device);

	g_return_val_if_fail (DKP_IS_SUPPLY (supply), FALSE);
	g_return_val_if_fail (low_battery != NULL, FALSE);

	/* reuse the common checks */
	ret = dkp_supply_get_on_battery (device, &on_battery);
	if (!ret)
		return FALSE;

	/* shortcut */
	if (!on_battery) {
		*low_battery = FALSE;
		return TRUE;
	}

	*low_battery = (obj->percentage < 10);
	return TRUE;
}

/**
 * dkp_supply_calculate_rate:
 **/
static void
dkp_supply_calculate_rate (DkpSupply *supply)
{
	guint time;
	gdouble energy;
	GTimeVal now;
	DkpDevice *device = DKP_DEVICE (supply);
	DkpObject *obj = dkp_device_get_obj (device);

	if (obj->energy < 0)
		return;

	if (supply->priv->energy_old < 0)
		return;

	if (supply->priv->energy_old == obj->energy)
		return;

	/* get the time difference */
	g_get_current_time (&now);
	time = now.tv_sec - supply->priv->energy_old_timespec.tv_sec;

	if (time == 0)
		return;

	/* get the difference in charge */
	energy = supply->priv->energy_old - obj->energy;
	if (energy < 0.1)
		return;

	/* probably okay */
	obj->energy_rate = energy * 3600 / time;
}

/**
 * dkp_supply_convert_device_technology:
 **/
static DkpDeviceTechnology
dkp_supply_convert_device_technology (const gchar *type)
{
	if (type == NULL)
		return DKP_DEVICE_TECHNOLGY_UNKNOWN;
	/* every case combination of Li-Ion is commonly used.. */
	if (strcasecmp (type, "li-ion") == 0 ||
	    strcasecmp (type, "lion") == 0)
		return DKP_DEVICE_TECHNOLGY_LITHIUM_ION;
	if (strcasecmp (type, "pb") == 0 ||
	    strcasecmp (type, "pbac") == 0)
		return DKP_DEVICE_TECHNOLGY_LEAD_ACID;
	if (strcasecmp (type, "lip") == 0 ||
	    strcasecmp (type, "lipo") == 0)
		return DKP_DEVICE_TECHNOLGY_LITHIUM_POLYMER;
	if (strcasecmp (type, "nimh") == 0)
		return DKP_DEVICE_TECHNOLGY_NICKEL_METAL_HYDRIDE;
	if (strcasecmp (type, "lifo") == 0)
		return DKP_DEVICE_TECHNOLGY_LITHIUM_IRON_PHOSPHATE;
	return DKP_DEVICE_TECHNOLGY_UNKNOWN;
}

/**
 * dkp_supply_refresh_battery:
 *
 * Return value: TRUE if we changed
 **/
static gboolean
dkp_supply_refresh_battery (DkpSupply *supply)
{
	gchar *status = NULL;
	gchar *technology_native;
	gboolean ret = TRUE;
	gdouble voltage_design;
	DkpDeviceState state;
	DkpDevice *device = DKP_DEVICE (supply);
	DkpObject *obj = dkp_device_get_obj (device);

	/* have we just been removed? */
	obj->is_present = sysfs_get_bool (obj->native_path, "present");
	if (!obj->is_present) {
		dkp_supply_reset_values (supply);
		obj->type = DKP_DEVICE_TYPE_BATTERY;
		goto out;
	}

	/* get the currect charge */
	obj->energy = sysfs_get_double (obj->native_path, "energy_now") / 1000000.0;
	if (obj->energy == 0)
		obj->energy = sysfs_get_double (obj->native_path, "energy_avg") / 1000000.0;

	/* used to convert A to W later */
	voltage_design = sysfs_get_double (obj->native_path, "voltage_max_design") / 1000000.0;
	if (voltage_design < 1.00) {
		voltage_design = sysfs_get_double (obj->native_path, "voltage_min_design") / 1000000.0;
		if (voltage_design < 1.00) {
			egg_debug ("using present voltage as design voltage");
			voltage_design = sysfs_get_double (obj->native_path, "voltage_present") / 1000000.0;
		}
	}

	/* initial values */
	if (!supply->priv->has_coldplug_values) {

		/* when we add via sysfs power_supply class then we know this is true */
		obj->power_supply = TRUE;

		/* the ACPI spec is bad at defining battery type constants */
		technology_native = g_strstrip (sysfs_get_string (obj->native_path, "technology"));
		obj->technology = dkp_supply_convert_device_technology (technology_native);
		g_free (technology_native);

		obj->vendor = g_strstrip (sysfs_get_string (obj->native_path, "manufacturer"));
		obj->model = g_strstrip (sysfs_get_string (obj->native_path, "model_name"));
		obj->serial = g_strstrip (sysfs_get_string (obj->native_path, "serial_number"));

		/* assume true for laptops */
		obj->is_rechargeable = TRUE;
		obj->has_history = TRUE;
		obj->has_statistics = TRUE;

		/* these don't change at runtime */
		obj->energy_full = sysfs_get_double (obj->native_path, "energy_full") / 1000000.0;
		obj->energy_full_design = sysfs_get_double (obj->native_path, "energy_full_design") / 1000000.0;

		/* convert charge to energy */
		if (obj->energy == 0) {
			obj->energy_full = sysfs_get_double (obj->native_path, "charge_full") / 1000000.0;
			obj->energy_full_design = sysfs_get_double (obj->native_path, "charge_full_design") / 1000000.0;
			obj->energy_full *= voltage_design;
			obj->energy_full_design *= voltage_design;
		}

		/* the last full should not be bigger than the design */
		if (obj->energy_full > obj->energy_full_design)
			egg_warning ("energy_full (%f) is greater than energy_full_design (%f)",
				     obj->energy_full, obj->energy_full_design);

		/* some systems don't have this */
		if (obj->energy_full < 0.01) {
			egg_warning ("correcting energy_full (%f) using energy_full_design (%f)",
				     obj->energy_full, obj->energy_full_design);
			obj->energy_full = obj->energy_full_design;
		}

		/* calculate how broken our battery is */
		if (obj->energy_full > 0) {
			obj->capacity = (obj->energy_full / obj->energy_full_design) * 100.0f;
			if (obj->capacity < 0)
				obj->capacity = 0;
			if (obj->capacity > 100.0)
				obj->capacity = 100.0;
		}

		/* we only coldplug once, as these values will never change */
		supply->priv->has_coldplug_values = TRUE;
	}

	status = g_strstrip (sysfs_get_string (obj->native_path, "status"));
	if (strcasecmp (status, "charging") == 0)
		state = DKP_DEVICE_STATE_CHARGING;
	else if (strcasecmp (status, "discharging") == 0)
		state = DKP_DEVICE_STATE_DISCHARGING;
	else if (strcasecmp (status, "full") == 0)
		state = DKP_DEVICE_STATE_FULLY_CHARGED;
	else if (strcasecmp (status, "empty") == 0)
		state = DKP_DEVICE_STATE_EMPTY;
	else {
		egg_warning ("unknown status string: %s", status);
		state = DKP_DEVICE_STATE_UNKNOWN;
	}

	/* get rate; it seems odd as it's either in uVh or uWh */
	obj->energy_rate = fabs (sysfs_get_double (obj->native_path, "current_now") / 1000000.0);

	/* convert charge to energy */
	if (obj->energy == 0) {
		obj->energy = sysfs_get_double (obj->native_path, "charge_now") / 1000000.0;
		if (obj->energy == 0)
			obj->energy = sysfs_get_double (obj->native_path, "charge_avg") / 1000000.0;
		obj->energy *= voltage_design;
		obj->energy_rate *= voltage_design;
	}

	/* some batteries don't update last_full attribute */
	if (obj->energy > obj->energy_full)
		obj->energy_full = obj->energy;

	/* present voltage */
	obj->voltage = sysfs_get_double (obj->native_path, "voltage_now") / 1000000.0;
	if (obj->voltage == 0)
		obj->voltage = sysfs_get_double (obj->native_path, "voltage_avg") / 1000000.0;

	/* ACPI gives out the special 'Ones' value for rate when it's unable
	 * to calculate the true rate. We should set the rate zero, and wait
	 * for the BIOS to stabilise. */
	if (obj->energy_rate == 0xffff)
		obj->energy_rate = 0;

	/* sanity check to less than 100W */
	if (obj->energy_rate > 100*1000)
		obj->energy_rate = 0;

	/* the hardware reporting failed -- try to calculate this */
	if (obj->energy_rate < 0)
		dkp_supply_calculate_rate (supply);

	/* get a precise percentage */
	obj->percentage = 100.0 * obj->energy / obj->energy_full;
	if (obj->percentage < 0)
		obj->percentage = 0;
	if (obj->percentage > 100.0)
		obj->percentage = 100.0;

	/* calculate a quick and dirty time remaining value */
	obj->time_to_empty = 0;
	obj->time_to_full = 0;
	if (obj->energy_rate > 0) {
		if (state == DKP_DEVICE_STATE_DISCHARGING)
			obj->time_to_empty = 3600 * (obj->energy / obj->energy_rate);
		else if (state == DKP_DEVICE_STATE_CHARGING)
			obj->time_to_full = 3600 * ((obj->energy_full - obj->energy) / obj->energy_rate);
		/* TODO: need to factor in battery charge metrics */
	}

	/* check the remaining time is under a set limit, to deal with broken
	   primary batteries rate */
	if (obj->time_to_empty > (20 * 60 * 60))
		obj->time_to_empty = 0;
	if (obj->time_to_full > (20 * 60 * 60))
		obj->time_to_full = 0;

	/* set the old status */
	supply->priv->energy_old = obj->energy;
	g_get_current_time (&supply->priv->energy_old_timespec);

	/* we changed state */
	if (obj->state != state) {
		supply->priv->energy_old = 0;
		obj->state = state;
	}
out:
	g_free (status);
	return ret;
}

/**
 * dkp_supply_poll_battery:
 **/
static gboolean
dkp_supply_poll_battery (DkpSupply *supply)
{
	gboolean ret;
	DkpDevice *device = DKP_DEVICE (supply);
	DkpObject *obj = dkp_device_get_obj (device);

	egg_debug ("No updates on supply %s for 30 seconds; forcing update", obj->native_path);
	supply->priv->poll_timer_id = 0;
	ret = dkp_supply_refresh (device);
	if (ret)
		dkp_device_emit_changed (device);
	return FALSE;
}

/**
 * dkp_supply_coldplug:
 **/
static gboolean
dkp_supply_coldplug (DkpDevice *device)
{
	DkpSupply *supply = DKP_SUPPLY (device);
	DevkitDevice *d;
	const gchar *native_path;
	DkpObject *obj = dkp_device_get_obj (device);

	dkp_supply_reset_values (supply);

	/* detect what kind of device we are */
	d = dkp_device_get_d (device);
	if (d == NULL)
		egg_error ("could not get device");

	native_path = devkit_device_get_native_path (d);
	if (native_path == NULL)
		egg_error ("could not get native path");

	if (sysfs_file_exists (native_path, "online")) {
		obj->type = DKP_DEVICE_TYPE_LINE_POWER;
	} else {
		/* this is correct, UPS and CSR are not in the kernel */
		obj->type = DKP_DEVICE_TYPE_BATTERY;
	}

	/* coldplug values */
	dkp_supply_refresh (device);

	return TRUE;
}

/**
 * dkp_supply_refresh:
 **/
static gboolean
dkp_supply_refresh (DkpDevice *device)
{
	gboolean ret;
	GTimeVal time;
	DkpSupply *supply = DKP_SUPPLY (device);
	DkpObject *obj = dkp_device_get_obj (device);

	if (supply->priv->poll_timer_id > 0) {
		g_source_remove (supply->priv->poll_timer_id);
		supply->priv->poll_timer_id = 0;
	}

	g_get_current_time (&time);
	obj->update_time = time.tv_sec;

	switch (obj->type) {
	case DKP_DEVICE_TYPE_LINE_POWER:
		ret = dkp_supply_refresh_line_power (supply);
		break;
	case DKP_DEVICE_TYPE_BATTERY:
		ret = dkp_supply_refresh_battery (supply);
		/* Seems that we don't get change uevents from the
		 * kernel on some BIOS types; set up a timer to poll
		 * if we are charging or discharging */
		if (obj->state == DKP_DEVICE_STATE_CHARGING ||
		    obj->state == DKP_DEVICE_STATE_DISCHARGING)
			supply->priv->poll_timer_id =
				g_timeout_add_seconds (DKP_SUPPLY_REFRESH_TIMEOUT,
						       (GSourceFunc) dkp_supply_poll_battery, supply);
		break;
	default:
		g_assert_not_reached ();
		break;
	}
	return ret;
}

/**
 * dkp_supply_init:
 **/
static void
dkp_supply_init (DkpSupply *supply)
{
	supply->priv = DKP_SUPPLY_GET_PRIVATE (supply);
}

/**
 * dkp_supply_finalize:
 **/
static void
dkp_supply_finalize (GObject *object)
{
	DkpSupply *supply;

	g_return_if_fail (object != NULL);
	g_return_if_fail (DKP_IS_SUPPLY (object));

	supply = DKP_SUPPLY (object);
	g_return_if_fail (supply->priv != NULL);

	if (supply->priv->poll_timer_id > 0)
		g_source_remove (supply->priv->poll_timer_id);

	G_OBJECT_CLASS (dkp_supply_parent_class)->finalize (object);
}

/**
 * dkp_supply_class_init:
 **/
static void
dkp_supply_class_init (DkpSupplyClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	DkpDeviceClass *device_class = DKP_DEVICE_CLASS (klass);

	object_class->finalize = dkp_supply_finalize;
	device_class->get_on_battery = dkp_supply_get_on_battery;
	device_class->get_low_battery = dkp_supply_get_low_battery;
	device_class->coldplug = dkp_supply_coldplug;
	device_class->refresh = dkp_supply_refresh;

	g_type_class_add_private (klass, sizeof (DkpSupplyPrivate));
}

/**
 * dkp_supply_new:
 **/
DkpSupply *
dkp_supply_new (void)
{
	return g_object_new (DKP_TYPE_SUPPLY, NULL);
}

