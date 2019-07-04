/* 
 * Copyright (C) 2011 Daiki Ueno <ueno@unixuser.org>
 * Copyright (C) 2011 Red Hat, Inc.
 * 
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

/**
 * SECTION:eekboard-client
 * @short_description: client interface of eekboard service
 *
 * The #EekboardClient class provides a client side access to eekboard-server.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif  /* HAVE_CONFIG_H */

#include "eekboard/eekboard-client.h"

enum {
    DESTROYED,
    LAST_SIGNAL
};

static guint signals[LAST_SIGNAL] = { 0, };

typedef struct _EekboardClientPrivate
{
    GHashTable *context_hash;
} EekboardClientPrivate;

G_DEFINE_TYPE_WITH_PRIVATE (EekboardClient, eekboard_client, G_TYPE_DBUS_PROXY)

static void send_destroy_context (EekboardClient  *client,
                                  EekboardContext *context,
                                  GCancellable    *cancellable);

static void
eekboard_client_real_destroyed (EekboardClient *self)
{
    EekboardClientPrivate *priv = eekboard_client_get_instance_private (self);

    // g_debug ("eekboard_client_real_destroyed");
    g_hash_table_remove_all (priv->context_hash);
}

static void
eekboard_client_dispose (GObject *object)
{
    EekboardClient *client = EEKBOARD_CLIENT(object);
    EekboardClientPrivate *priv = eekboard_client_get_instance_private (client);

    if (priv->context_hash) {
        GHashTableIter iter;
        gpointer key, value;

        g_hash_table_iter_init (&iter, priv->context_hash);
        while (g_hash_table_iter_next (&iter, &key, &value)) {
            send_destroy_context (client, (EekboardContext *)value, NULL);
            g_hash_table_iter_remove (&iter);
        }
        g_hash_table_destroy (priv->context_hash);
        priv->context_hash = NULL;
    }

    G_OBJECT_CLASS (eekboard_client_parent_class)->dispose (object);
}

static void
eekboard_client_class_init (EekboardClientClass *klass)
{
    GObjectClass *gobject_class = G_OBJECT_CLASS (klass);

    klass->destroyed = eekboard_client_real_destroyed;

    gobject_class->dispose = eekboard_client_dispose;

    /**
     * EekboardClient::destroyed:
     * @eekboard: an #EekboardClient
     *
     * The ::destroyed signal is emitted each time the name of remote
     * end is vanished.
     */
    signals[DESTROYED] =
        g_signal_new (I_("destroyed"),
                      G_TYPE_FROM_CLASS(gobject_class),
                      G_SIGNAL_RUN_LAST,
                      G_STRUCT_OFFSET(EekboardClientClass, destroyed),
                      NULL,
                      NULL,
                      g_cclosure_marshal_VOID__VOID,
                      G_TYPE_NONE,
                      0);
}

static void
eekboard_client_init (EekboardClient *self)
{
    EekboardClientPrivate *priv = eekboard_client_get_instance_private (self);

    priv->context_hash =
        g_hash_table_new_full (g_str_hash,
                               g_str_equal,
                               (GDestroyNotify)g_free,
                               (GDestroyNotify)g_object_unref);
}

static void
eekboard_name_vanished_callback (GDBusConnection *connection,
                                 const gchar     *name,
                                 gpointer         user_data)
{
    EekboardClient *client = user_data;
    g_signal_emit_by_name (client, "destroyed", NULL);
}

/**
 * eekboard_client_new:
 * @connection: a #GDBusConnection
 * @cancellable: a #GCancellable
 *
 * Create a client.
 */
EekboardClient *
eekboard_client_new (GDBusConnection *connection,
                     GCancellable    *cancellable)
{
    GInitable *initable;
    GError *error;

    g_assert (G_IS_DBUS_CONNECTION(connection));

    error = NULL;
    initable =
        g_initable_new (EEKBOARD_TYPE_CLIENT,
                        cancellable,
                        &error,
                        "g-connection", connection,
                        "g-name", "org.fedorahosted.Eekboard",
                        "g-interface-name", "org.fedorahosted.Eekboard",
                        "g-object-path", "/org/fedorahosted/Eekboard",
                        NULL);
    if (initable != NULL) {
        EekboardClient *client = EEKBOARD_CLIENT (initable);
        gchar *name_owner = g_dbus_proxy_get_name_owner (G_DBUS_PROXY(client));
        if (name_owner == NULL) {
            g_object_unref (client);
            return NULL;
        }

        /* the vanished callback is called when the server is disconnected */
        g_bus_watch_name_on_connection (connection,
                                        name_owner,
                                        G_BUS_NAME_WATCHER_FLAGS_NONE,
                                        NULL,
                                        eekboard_name_vanished_callback,
                                        client,
                                        NULL);
        g_free (name_owner);

        return client;
    }

    g_warning ("can't create client: %s", error->message);
    g_error_free (error);
    return NULL;
}

static void
on_context_destroyed (EekboardContext *context,
                      gpointer         user_data)
{
    EekboardClient *client = user_data;
    EekboardClientPrivate *priv = eekboard_client_get_instance_private (client);

    g_hash_table_remove (priv->context_hash,
                         g_dbus_proxy_get_object_path (G_DBUS_PROXY(context)));
}

/**
 * eekboard_client_create_context:
 * @eekboard: an #EekboardClient
 * @client_name: name of the client
 * @cancellable: a #GCancellable
 *
 * Create a new input context.
 *
 * Return value: (transfer full): a newly created #EekboardContext.
 */
EekboardContext *
eekboard_client_create_context (EekboardClient *client,
                                const gchar    *client_name,
                                GCancellable   *cancellable)
{
    GVariant *variant;
    const gchar *object_path;
    EekboardContext *context;
    GError *error;
    GDBusConnection *connection;

    g_assert (EEKBOARD_IS_CLIENT(client));
    g_assert (client_name);

    error = NULL;
    variant = g_dbus_proxy_call_sync (G_DBUS_PROXY(client),
                                      "CreateContext",
                                      g_variant_new ("(s)", client_name),
                                      G_DBUS_CALL_FLAGS_NONE,
                                      -1,
                                      cancellable,
                                      &error);
    if (!variant) {
        g_warning ("failed to call CreateContext: %s", error->message);
        g_error_free (error);
        return NULL;
    }

    g_variant_get (variant, "(&s)", &object_path);
    connection = g_dbus_proxy_get_connection (G_DBUS_PROXY(client));
    context = eekboard_context_new (connection, object_path, cancellable);
    if (!context) {
        g_variant_unref (variant);
        return NULL;
    }

    EekboardClientPrivate *priv = eekboard_client_get_instance_private (client);

    g_hash_table_insert (priv->context_hash,
                         g_strdup (object_path),
                         g_object_ref (context));
    g_signal_connect (context, "destroyed",
                      G_CALLBACK(on_context_destroyed), client);
    return context;
}

static void
eekboard_async_ready_callback (GObject      *source_object,
                               GAsyncResult *res,
                               gpointer      user_data)
{
    GError *error = NULL;
    GVariant *result;

    result = g_dbus_proxy_call_finish (G_DBUS_PROXY(source_object),
                                       res,
                                       &error);
    if (result)
        g_variant_unref (result);
    else {
        g_warning ("error in D-Bus proxy call: %s", error->message);
        g_error_free (error);
    }
}

/**
 * eekboard_client_push_context:
 * @eekboard: an #EekboardClient
 * @context: an #EekboardContext
 * @cancellable: a #GCancellable
 *
 * Enable the input context @context and disable the others.
 */
void
eekboard_client_push_context (EekboardClient  *client,
                              EekboardContext *context,
                              GCancellable    *cancellable)
{
    const gchar *object_path;

    g_return_if_fail (EEKBOARD_IS_CLIENT(client));
    g_return_if_fail (EEKBOARD_IS_CONTEXT(context));

    EekboardClientPrivate *priv = eekboard_client_get_instance_private (client);

    object_path = g_dbus_proxy_get_object_path (G_DBUS_PROXY(context));

    context = g_hash_table_lookup (priv->context_hash,
                                   object_path);
    if (!context)
        return;

    eekboard_context_set_enabled (context, TRUE);
    g_dbus_proxy_call (G_DBUS_PROXY(client),
                       "PushContext",
                       g_variant_new ("(s)", object_path),
                       G_DBUS_CALL_FLAGS_NONE,
                       -1,
                       cancellable,
                       eekboard_async_ready_callback,
                       NULL);
}

/**
 * eekboard_client_pop_context:
 * @eekboard: an #EekboardClient
 * @cancellable: a #GCancellable
 *
 * Disable the current input context and enable the previous one.
 */
void
eekboard_client_pop_context (EekboardClient *client,
                             GCancellable   *cancellable)
{
    g_return_if_fail (EEKBOARD_IS_CLIENT(client));

    g_dbus_proxy_call (G_DBUS_PROXY(client),
                       "PopContext",
                       NULL,
                       G_DBUS_CALL_FLAGS_NONE,
                       -1,
                       cancellable,
                       eekboard_async_ready_callback,
                       NULL);
}

void
eekboard_client_show_keyboard (EekboardClient  *client,
                               GCancellable    *cancellable)
{
    g_return_if_fail (EEKBOARD_IS_CLIENT(client));

    g_dbus_proxy_call (G_DBUS_PROXY(client),
                       "ShowKeyboard",
                       NULL,
                       G_DBUS_CALL_FLAGS_NONE,
                       -1,
                       cancellable,
                       eekboard_async_ready_callback,
                       NULL);
}

void
eekboard_client_hide_keyboard (EekboardClient *client,
                               GCancellable   *cancellable)
{
    g_return_if_fail (EEKBOARD_IS_CLIENT(client));

    g_dbus_proxy_call (G_DBUS_PROXY(client),
                       "HideKeyboard",
                       NULL,
                       G_DBUS_CALL_FLAGS_NONE,
                       -1,
                       cancellable,
                       eekboard_async_ready_callback,
                       NULL);
}

static void
send_destroy_context (EekboardClient  *client,
                      EekboardContext *context,
                      GCancellable    *cancellable)
{
    const gchar *object_path;

    object_path = g_dbus_proxy_get_object_path (G_DBUS_PROXY(context));

    g_dbus_proxy_call (G_DBUS_PROXY(client),
                       "DestroyContext",
                       g_variant_new ("(s)", object_path),
                       G_DBUS_CALL_FLAGS_NONE,
                       -1,
                       cancellable,
                       eekboard_async_ready_callback,
                       NULL);
}

/**
 * eekboard_client_destroy_context:
 * @eekboard: an #EekboardClient
 * @context: an #EekboardContext
 * @cancellable: a #GCancellable
 *
 * Remove @context from @eekboard.
 */
void
eekboard_client_destroy_context (EekboardClient  *client,
                                 EekboardContext *context,
                                 GCancellable    *cancellable)
{
    const gchar *object_path;

    g_return_if_fail (EEKBOARD_IS_CLIENT(client));
    g_return_if_fail (EEKBOARD_IS_CONTEXT(context));

    EekboardClientPrivate *priv = eekboard_client_get_instance_private (client);

    object_path = g_dbus_proxy_get_object_path (G_DBUS_PROXY(context));
    g_hash_table_remove (priv->context_hash, object_path);

    send_destroy_context (client, context, cancellable);
}
