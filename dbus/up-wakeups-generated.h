/*
 * Generated by gdbus-codegen 2.58.3 from org.freedesktop.UPower.Wakeups.xml. DO NOT EDIT.
 *
 * The license of this code is the same as for the D-Bus interface description
 * it was derived from.
 */

#ifndef __UP_WAKEUPS_GENERATED_H__
#define __UP_WAKEUPS_GENERATED_H__

#include <gio/gio.h>

G_BEGIN_DECLS


/* ------------------------------------------------------------------------ */
/* Declarations for org.freedesktop.UPower.Wakeups */

#define UP_TYPE_EXPORTED_WAKEUPS (up_exported_wakeups_get_type ())
#define UP_EXPORTED_WAKEUPS(o) (G_TYPE_CHECK_INSTANCE_CAST ((o), UP_TYPE_EXPORTED_WAKEUPS, UpExportedWakeups))
#define UP_IS_EXPORTED_WAKEUPS(o) (G_TYPE_CHECK_INSTANCE_TYPE ((o), UP_TYPE_EXPORTED_WAKEUPS))
#define UP_EXPORTED_WAKEUPS_GET_IFACE(o) (G_TYPE_INSTANCE_GET_INTERFACE ((o), UP_TYPE_EXPORTED_WAKEUPS, UpExportedWakeupsIface))

struct _UpExportedWakeups;
typedef struct _UpExportedWakeups UpExportedWakeups;
typedef struct _UpExportedWakeupsIface UpExportedWakeupsIface;

struct _UpExportedWakeupsIface
{
  GTypeInterface parent_iface;



  gboolean (*handle_get_data) (
    UpExportedWakeups *object,
    GDBusMethodInvocation *invocation);

  gboolean (*handle_get_total) (
    UpExportedWakeups *object,
    GDBusMethodInvocation *invocation);

  gboolean  (*get_has_capability) (UpExportedWakeups *object);

  void (*data_changed) (
    UpExportedWakeups *object);

  void (*total_changed) (
    UpExportedWakeups *object,
    guint arg_value);

};

GType up_exported_wakeups_get_type (void) G_GNUC_CONST;

GDBusInterfaceInfo *up_exported_wakeups_interface_info (void);
guint up_exported_wakeups_override_properties (GObjectClass *klass, guint property_id_begin);


/* D-Bus method call completion functions: */
void up_exported_wakeups_complete_get_total (
    UpExportedWakeups *object,
    GDBusMethodInvocation *invocation,
    guint value);

void up_exported_wakeups_complete_get_data (
    UpExportedWakeups *object,
    GDBusMethodInvocation *invocation,
    GVariant *data);



/* D-Bus signal emissions functions: */
void up_exported_wakeups_emit_total_changed (
    UpExportedWakeups *object,
    guint arg_value);

void up_exported_wakeups_emit_data_changed (
    UpExportedWakeups *object);



/* D-Bus method calls: */
void up_exported_wakeups_call_get_total (
    UpExportedWakeups *proxy,
    GCancellable *cancellable,
    GAsyncReadyCallback callback,
    gpointer user_data);

gboolean up_exported_wakeups_call_get_total_finish (
    UpExportedWakeups *proxy,
    guint *out_value,
    GAsyncResult *res,
    GError **error);

gboolean up_exported_wakeups_call_get_total_sync (
    UpExportedWakeups *proxy,
    guint *out_value,
    GCancellable *cancellable,
    GError **error);

void up_exported_wakeups_call_get_data (
    UpExportedWakeups *proxy,
    GCancellable *cancellable,
    GAsyncReadyCallback callback,
    gpointer user_data);

gboolean up_exported_wakeups_call_get_data_finish (
    UpExportedWakeups *proxy,
    GVariant **out_data,
    GAsyncResult *res,
    GError **error);

gboolean up_exported_wakeups_call_get_data_sync (
    UpExportedWakeups *proxy,
    GVariant **out_data,
    GCancellable *cancellable,
    GError **error);



/* D-Bus property accessors: */
gboolean up_exported_wakeups_get_has_capability (UpExportedWakeups *object);
void up_exported_wakeups_set_has_capability (UpExportedWakeups *object, gboolean value);


/* ---- */

#define UP_TYPE_EXPORTED_WAKEUPS_PROXY (up_exported_wakeups_proxy_get_type ())
#define UP_EXPORTED_WAKEUPS_PROXY(o) (G_TYPE_CHECK_INSTANCE_CAST ((o), UP_TYPE_EXPORTED_WAKEUPS_PROXY, UpExportedWakeupsProxy))
#define UP_EXPORTED_WAKEUPS_PROXY_CLASS(k) (G_TYPE_CHECK_CLASS_CAST ((k), UP_TYPE_EXPORTED_WAKEUPS_PROXY, UpExportedWakeupsProxyClass))
#define UP_EXPORTED_WAKEUPS_PROXY_GET_CLASS(o) (G_TYPE_INSTANCE_GET_CLASS ((o), UP_TYPE_EXPORTED_WAKEUPS_PROXY, UpExportedWakeupsProxyClass))
#define UP_IS_EXPORTED_WAKEUPS_PROXY(o) (G_TYPE_CHECK_INSTANCE_TYPE ((o), UP_TYPE_EXPORTED_WAKEUPS_PROXY))
#define UP_IS_EXPORTED_WAKEUPS_PROXY_CLASS(k) (G_TYPE_CHECK_CLASS_TYPE ((k), UP_TYPE_EXPORTED_WAKEUPS_PROXY))

typedef struct _UpExportedWakeupsProxy UpExportedWakeupsProxy;
typedef struct _UpExportedWakeupsProxyClass UpExportedWakeupsProxyClass;
typedef struct _UpExportedWakeupsProxyPrivate UpExportedWakeupsProxyPrivate;

struct _UpExportedWakeupsProxy
{
  /*< private >*/
  GDBusProxy parent_instance;
  UpExportedWakeupsProxyPrivate *priv;
};

struct _UpExportedWakeupsProxyClass
{
  GDBusProxyClass parent_class;
};

GType up_exported_wakeups_proxy_get_type (void) G_GNUC_CONST;

#if GLIB_CHECK_VERSION(2, 44, 0)
G_DEFINE_AUTOPTR_CLEANUP_FUNC (UpExportedWakeupsProxy, g_object_unref)
#endif

void up_exported_wakeups_proxy_new (
    GDBusConnection     *connection,
    GDBusProxyFlags      flags,
    const gchar         *name,
    const gchar         *object_path,
    GCancellable        *cancellable,
    GAsyncReadyCallback  callback,
    gpointer             user_data);
UpExportedWakeups *up_exported_wakeups_proxy_new_finish (
    GAsyncResult        *res,
    GError             **error);
UpExportedWakeups *up_exported_wakeups_proxy_new_sync (
    GDBusConnection     *connection,
    GDBusProxyFlags      flags,
    const gchar         *name,
    const gchar         *object_path,
    GCancellable        *cancellable,
    GError             **error);

void up_exported_wakeups_proxy_new_for_bus (
    GBusType             bus_type,
    GDBusProxyFlags      flags,
    const gchar         *name,
    const gchar         *object_path,
    GCancellable        *cancellable,
    GAsyncReadyCallback  callback,
    gpointer             user_data);
UpExportedWakeups *up_exported_wakeups_proxy_new_for_bus_finish (
    GAsyncResult        *res,
    GError             **error);
UpExportedWakeups *up_exported_wakeups_proxy_new_for_bus_sync (
    GBusType             bus_type,
    GDBusProxyFlags      flags,
    const gchar         *name,
    const gchar         *object_path,
    GCancellable        *cancellable,
    GError             **error);


/* ---- */

#define UP_TYPE_EXPORTED_WAKEUPS_SKELETON (up_exported_wakeups_skeleton_get_type ())
#define UP_EXPORTED_WAKEUPS_SKELETON(o) (G_TYPE_CHECK_INSTANCE_CAST ((o), UP_TYPE_EXPORTED_WAKEUPS_SKELETON, UpExportedWakeupsSkeleton))
#define UP_EXPORTED_WAKEUPS_SKELETON_CLASS(k) (G_TYPE_CHECK_CLASS_CAST ((k), UP_TYPE_EXPORTED_WAKEUPS_SKELETON, UpExportedWakeupsSkeletonClass))
#define UP_EXPORTED_WAKEUPS_SKELETON_GET_CLASS(o) (G_TYPE_INSTANCE_GET_CLASS ((o), UP_TYPE_EXPORTED_WAKEUPS_SKELETON, UpExportedWakeupsSkeletonClass))
#define UP_IS_EXPORTED_WAKEUPS_SKELETON(o) (G_TYPE_CHECK_INSTANCE_TYPE ((o), UP_TYPE_EXPORTED_WAKEUPS_SKELETON))
#define UP_IS_EXPORTED_WAKEUPS_SKELETON_CLASS(k) (G_TYPE_CHECK_CLASS_TYPE ((k), UP_TYPE_EXPORTED_WAKEUPS_SKELETON))

typedef struct _UpExportedWakeupsSkeleton UpExportedWakeupsSkeleton;
typedef struct _UpExportedWakeupsSkeletonClass UpExportedWakeupsSkeletonClass;
typedef struct _UpExportedWakeupsSkeletonPrivate UpExportedWakeupsSkeletonPrivate;

struct _UpExportedWakeupsSkeleton
{
  /*< private >*/
  GDBusInterfaceSkeleton parent_instance;
  UpExportedWakeupsSkeletonPrivate *priv;
};

struct _UpExportedWakeupsSkeletonClass
{
  GDBusInterfaceSkeletonClass parent_class;
};

GType up_exported_wakeups_skeleton_get_type (void) G_GNUC_CONST;

#if GLIB_CHECK_VERSION(2, 44, 0)
G_DEFINE_AUTOPTR_CLEANUP_FUNC (UpExportedWakeupsSkeleton, g_object_unref)
#endif

UpExportedWakeups *up_exported_wakeups_skeleton_new (void);


G_END_DECLS

#endif /* __UP_WAKEUPS_GENERATED_H__ */
