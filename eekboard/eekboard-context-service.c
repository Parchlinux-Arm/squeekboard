/*
 * Copyright (C) 2010-2011 Daiki Ueno <ueno@unixuser.org>
 * Copyright (C) 2010-2011 Red Hat, Inc.
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
 * SECTION:eekboard-context-service
 * @short_description: base server implementation of eekboard input
 * context service
 *
 * The #EekboardService class provides a base server side
 * implementation of eekboard input context service.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif  /* HAVE_CONFIG_H */

#include "eekboard/eekboard-context-service.h"

#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/random.h> // TODO: this is Linux-specific
#include <xkbcommon/xkbcommon.h>

#include "eekboard/key-emitter.h"
#include "wayland.h"
//#include "eekboard/eekboard-xklutil.h"
//#include "eek/eek-xkl.h"

#define CSW 640
#define CSH 480

enum {
    PROP_0, // Magic: without this, keyboard is not useable in g_object_notify
    PROP_KEYBOARD,
    PROP_VISIBLE,
    PROP_FULLSCREEN,
    PROP_LAST
};

enum {
    ENABLED,
    DISABLED,
    DESTROYED,
    LAST_SIGNAL
};

static guint signals[LAST_SIGNAL] = { 0, };

#define EEKBOARD_CONTEXT_SERVICE_GET_PRIVATE(obj)                       \
    (G_TYPE_INSTANCE_GET_PRIVATE ((obj), EEKBOARD_TYPE_CONTEXT_SERVICE, EekboardContextServicePrivate))

struct _EekboardContextServicePrivate {
    gboolean enabled;
    gboolean visible;
    gboolean fullscreen;

    EekKeyboard *keyboard; // currently used keyboard
    GHashTable *keyboard_hash; // a table of available keyboards, per layout

    // TODO: make use of repeating buttons
    EekKey *repeat_key;
    guint repeat_timeout_id;
    gboolean repeat_triggered;

    GSettings *settings;
};

G_DEFINE_TYPE (EekboardContextService, eekboard_context_service, G_TYPE_OBJECT);

#if 0  // ===== REMOVAL NOTICE ==== expires by: 2019-08-20 -Wunused-variable =====
static Display *display = NULL;
#endif // ===== REMOVAL NOTICE ==== expires by: 2019-08-20 -Wunused-variable =====

static EekKeyboard *
eekboard_context_service_real_create_keyboard (EekboardContextService *self,
                                               const gchar            *keyboard_type)
{
    EekKeyboard *keyboard;
    EekLayout *layout;
    GError *error;

    if (g_str_has_prefix (keyboard_type, "xkb:")) {
        /* TODO: Depends on xklavier
        XklConfigRec *rec =
            eekboard_xkl_config_rec_from_string (&keyboard_type[4]);

        if (display == NULL)
            //display = XOpenDisplay (NULL);
            return NULL; // FIXME: replace with wl display

        error = NULL;
        layout = eek_xkl_layout_new (display, &error);
        if (layout == NULL) {
            g_warning ("can't create keyboard %s: %s",
                       keyboard_type, error->message);
            g_error_free (error);
            return NULL;
        }

        if (!eek_xkl_layout_set_config (EEK_XKL_LAYOUT(layout), rec)) {
            g_object_unref (layout);
            return NULL;
        }
        */
        return NULL;
    } else {
        error = NULL;
        layout = eek_xml_layout_new (keyboard_type, &error);
        if (layout == NULL) {
            g_warning ("can't create keyboard %s: %s",
                       keyboard_type, error->message);
            g_error_free (error);
            keyboard_type = "us";
            error = NULL;
            layout = eek_xml_layout_new (keyboard_type, &error);
            if (layout == NULL) {
                g_error ("failed to create fallback layout: %s", error->message);
                g_error_free (error);
                return NULL;
            }
        }
    }
    keyboard = eek_keyboard_new (self, layout, CSW, CSH);
    if (!keyboard) {
        g_error("Failed to create a keyboard");
    }
    g_object_unref (layout);

    struct xkb_context *context = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
    if (!context) {
        g_error("No context created");
    }

    struct xkb_rule_names rules = { 0 };
    rules.layout = strdup(keyboard_type);
    struct xkb_keymap *keymap = xkb_keymap_new_from_names(context, &rules,
        XKB_KEYMAP_COMPILE_NO_FLAGS);
    if (!keymap) {
        g_error("Bad keymap");
    }
    keyboard->keymap = keymap;
    char *keymap_str = xkb_keymap_get_as_string(keymap, XKB_KEYMAP_FORMAT_TEXT_V1);
    keyboard->keymap_len = strlen(keymap_str) + 1;
    char *path = strdup("/eek_keymap-XXXXXX");
    char *r = &path[strlen(path) - 6];
    getrandom(r, 6, GRND_NONBLOCK);
    for (uint i = 0; i < 6; i++) {
        r[i] = (r[i] & 0b1111111) | 0b1000000; // A-z
        r[i] = r[i] > 'z' ? '?' : r[i]; // The randomizer doesn't need to be good...
    }
    int keymap_fd = shm_open(path, O_RDWR | O_CREAT | O_EXCL, 0600);
    if (keymap_fd < 0) {
        g_error("Failed to set up keymap fd");
    }
    keyboard->keymap_fd = keymap_fd;
    shm_unlink(path);
    if (ftruncate(keymap_fd, (off_t)keyboard->keymap_len)) {
        g_error("Failed to increase keymap fd size");
    }
    char *ptr = mmap(NULL, keyboard->keymap_len, PROT_WRITE, MAP_SHARED,
        keymap_fd, 0);
    if ((void*)ptr == (void*)-1) {
        g_error("Failed to set up mmap");
    }
    strcpy(ptr, keymap_str);
    munmap(ptr, keyboard->keymap_len);
    free(keymap_str);

    return keyboard;
}

static void
eekboard_context_service_real_show_keyboard (EekboardContextService *self)
{
    self->priv->visible = TRUE;
}

static void
eekboard_context_service_real_hide_keyboard (EekboardContextService *self)
{
    self->priv->visible = FALSE;
}

static void
eekboard_context_service_set_property (GObject      *object,
                                       guint         prop_id,
                                       const GValue *value,
                                       GParamSpec   *pspec)
{
    EekboardContextService *context = EEKBOARD_CONTEXT_SERVICE(object);

    switch (prop_id) {
    case PROP_KEYBOARD:
        if (context->priv->keyboard)
            g_object_unref (context->priv->keyboard);
        context->priv->keyboard = g_value_get_object (value);
        break;
    case PROP_VISIBLE:
        /*if (context->priv->keyboard) {
            if (g_value_get_boolean (value))
                eekboard_context_service_show_keyboard (context);
            else
                eekboard_context_service_hide_keyboard (context);
        }*/
        break;
    case PROP_FULLSCREEN:
        context->priv->fullscreen = g_value_get_boolean (value);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
        break;
    }
}

static void
eekboard_context_service_get_property (GObject    *object,
                                       guint       prop_id,
                                       GValue     *value,
                                       GParamSpec *pspec)
{
    EekboardContextService *context = EEKBOARD_CONTEXT_SERVICE(object);

    switch (prop_id) {
    case PROP_KEYBOARD:
        g_value_set_object (value, context->priv->keyboard);
        break;
    case PROP_VISIBLE:
        g_value_set_boolean (value, context->priv->visible);
        break;
    case PROP_FULLSCREEN:
        g_value_set_boolean (value, context->priv->fullscreen);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
        break;
    }
}

static void
eekboard_context_service_dispose (GObject *object)
{
    EekboardContextService *context = EEKBOARD_CONTEXT_SERVICE(object);

    if (context->priv->keyboard_hash) {
        g_hash_table_destroy (context->priv->keyboard_hash);
        context->priv->keyboard_hash = NULL;
    }

    G_OBJECT_CLASS (eekboard_context_service_parent_class)->
        dispose (object);
}

static void
settings_get_layout(GSettings *settings, char **type, char **layout)
{
    GVariant *inputs = g_settings_get_value(settings, "sources");
    guint32 index;
    g_settings_get(settings, "current", "u", &index);

    GVariantIter *iter;
    g_variant_get(inputs, "a(ss)", &iter);

    for (unsigned i = 0;
         g_variant_iter_loop(iter, "(ss)", type, layout);
         i++) {
        if (i == index) {
            //printf("Found layout %s %s\n", *type, *layout);
            break;
        }
    }
    g_variant_iter_free(iter);
}

static void
settings_update_layout(EekboardContextService *context) {
    g_autofree gchar *keyboard_type = NULL;
    g_autofree gchar *keyboard_layout = NULL;
    settings_get_layout(context->priv->settings, &keyboard_type, &keyboard_layout);

    if (!keyboard_type) {
        keyboard_type = g_strdup("us");
    }
    if (!keyboard_layout) {
        keyboard_layout = g_strdup("undefined");
    }

    // generic part follows
    static guint keyboard_id = 0;
    EekKeyboard *keyboard = g_hash_table_lookup(context->priv->keyboard_hash,
                                                GUINT_TO_POINTER(keyboard_id));
    // create a keyboard
    if (!keyboard) {
        EekboardContextServiceClass *klass = EEKBOARD_CONTEXT_SERVICE_GET_CLASS(context);
        keyboard = klass->create_keyboard (context, keyboard_layout);
        eek_keyboard_set_modifier_behavior (keyboard,
                                            EEK_MODIFIER_BEHAVIOR_LATCH);

        g_hash_table_insert (context->priv->keyboard_hash,
                             GUINT_TO_POINTER(keyboard_id),
                             keyboard);
        g_object_set_data (G_OBJECT(keyboard),
                           "keyboard-id",
                           GUINT_TO_POINTER(keyboard_id));
        keyboard_id++;
    }
    // set as current
    context->priv->keyboard = keyboard;
    // TODO: this used to save the group, why?
    //group = eek_element_get_group (EEK_ELEMENT(context->priv->keyboard));
    g_object_notify (G_OBJECT(context), "keyboard");
}

static gboolean
settings_handle_layout_changed(GSettings *s,
                               gpointer keys, gint n_keys,
                               gpointer user_data) {
    (void)s;
    (void)keys;
    (void)n_keys;
    EekboardContextService *context = user_data;
    settings_update_layout(context);
    return TRUE;
}

static void
eekboard_context_service_constructed (GObject *object)
{
    EekboardContextService *context = EEKBOARD_CONTEXT_SERVICE (object);
    context->virtual_keyboard = zwp_virtual_keyboard_manager_v1_create_virtual_keyboard(
                squeek_wayland->virtual_keyboard_manager,
                squeek_wayland->seat);
    if (!context->virtual_keyboard) {
        g_error("Programmer error: Failed to receive a virtual keyboard instance");
    }
    settings_update_layout(context);
}

static void
eekboard_context_service_class_init (EekboardContextServiceClass *klass)
{
    GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
    GParamSpec *pspec;

    g_type_class_add_private (gobject_class,
                              sizeof (EekboardContextServicePrivate));

    klass->create_keyboard = eekboard_context_service_real_create_keyboard;
    klass->show_keyboard = eekboard_context_service_real_show_keyboard;
    klass->hide_keyboard = eekboard_context_service_real_hide_keyboard;

    gobject_class->constructed = eekboard_context_service_constructed;
    gobject_class->set_property = eekboard_context_service_set_property;
    gobject_class->get_property = eekboard_context_service_get_property;
    gobject_class->dispose = eekboard_context_service_dispose;

    /**
     * EekboardContextService::enabled:
     * @context: an #EekboardContextService
     *
     * Emitted when @context is enabled.
     */
    signals[ENABLED] =
        g_signal_new (I_("enabled"),
                      G_TYPE_FROM_CLASS(gobject_class),
                      G_SIGNAL_RUN_LAST,
                      G_STRUCT_OFFSET(EekboardContextServiceClass, enabled),
                      NULL,
                      NULL,
                      g_cclosure_marshal_VOID__VOID,
                      G_TYPE_NONE,
                      0);

    /**
     * EekboardContextService::disabled:
     * @context: an #EekboardContextService
     *
     * Emitted when @context is enabled.
     */
    signals[DISABLED] =
        g_signal_new (I_("disabled"),
                      G_TYPE_FROM_CLASS(gobject_class),
                      G_SIGNAL_RUN_LAST,
                      G_STRUCT_OFFSET(EekboardContextServiceClass, disabled),
                      NULL,
                      NULL,
                      g_cclosure_marshal_VOID__VOID,
                      G_TYPE_NONE,
                      0);

    /**
     * EekboardContextService::destroyed:
     * @context: an #EekboardContextService
     *
     * Emitted when @context is destroyed.
     */
    signals[DESTROYED] =
        g_signal_new (I_("destroyed"),
                      G_TYPE_FROM_CLASS(gobject_class),
                      G_SIGNAL_RUN_LAST,
                      G_STRUCT_OFFSET(EekboardContextServiceClass, destroyed),
                      NULL,
                      NULL,
                      g_cclosure_marshal_VOID__VOID,
                      G_TYPE_NONE,
                      0);

    /**
     * EekboardContextService:keyboard:
     *
     * An #EekKeyboard currently active in this context.
     */
    pspec = g_param_spec_object ("keyboard",
                                 "Keyboard",
                                 "Keyboard",
                                 EEK_TYPE_KEYBOARD,
                                 G_PARAM_READWRITE);
    g_object_class_install_property (gobject_class,
                                     PROP_KEYBOARD,
                                     pspec);

    /**
     * EekboardContextService:visible:
     *
     * Flag to indicate if keyboard is visible or not.
     */
    pspec = g_param_spec_boolean ("visible",
                                  "Visible",
                                  "Visible",
                                  FALSE,
                                  G_PARAM_READWRITE);
    g_object_class_install_property (gobject_class,
                                     PROP_VISIBLE,
                                     pspec);

    /**
     * EekboardContextService:fullscreen:
     *
     * Flag to indicate if keyboard is rendered in fullscreen mode.
     */
    pspec = g_param_spec_boolean ("fullscreen",
                                  "Fullscreen",
                                  "Fullscreen",
                                  FALSE,
                                  G_PARAM_READWRITE);
    g_object_class_install_property (gobject_class,
                                     PROP_FULLSCREEN,
                                     pspec);
}

static void
eekboard_context_service_init (EekboardContextService *self)
{
    self->priv = EEKBOARD_CONTEXT_SERVICE_GET_PRIVATE(self);

    self->priv->keyboard_hash =
        g_hash_table_new_full (g_direct_hash,
                               g_direct_equal,
                               NULL,
                               (GDestroyNotify)g_object_unref);

    self->priv->settings = g_settings_new ("org.gnome.desktop.input-sources");
    gulong conn_id = g_signal_connect(self->priv->settings, "change-event",
                                      G_CALLBACK(settings_handle_layout_changed),
                                      self);
    if (conn_id == 0) {
        g_warning ("Could not connect to gsettings updates, layout"
                   " changing unavailable");
    }
}

#if 0  // ===== REMOVAL NOTICE ==== expires by: 2019-08-20 -Wunused-function =====
static gboolean on_repeat_timeout (EekboardContextService *context);

static gboolean
on_repeat_timeout (EekboardContextService *context)
{
    guint delay = 500; // ms

    // hardcoding; needs to connect to yet another settings path because
    // org.gnome.desktop.input-sources doesn't control repeating
    //g_settings_get (context->priv->settings, "repeat-interval", "u", &delay);

    context->priv->repeat_timeout_id =
        g_timeout_add (delay,
                       (GSourceFunc)on_repeat_timeout,
                       context);

    return FALSE;
}
#endif // ===== REMOVAL NOTICE ==== expires by: 2019-08-20 -Wunused-function =====

#if 0  // ===== REMOVAL NOTICE ==== expires by: 2019-08-20 -Wunused-function =====
static gboolean
on_repeat_timeout_init (EekboardContextService *context)
{
    /* FIXME: clear modifiers for further key repeat; better not
       depend on modifier behavior is LATCH */
    eek_keyboard_set_modifiers (context->priv->keyboard, 0);

    /* reschedule repeat timeout only when "repeat" option is set */
    /* TODO: org.gnome.desktop.input-sources doesn't have repeat info.
     * In addition, repeat is only useful when the keyboard is not in text
     * input mode */
    /*
    if (g_settings_get_boolean (context->priv->settings, "repeat")) {
        guint delay;

        g_settings_get (context->priv->settings, "repeat-interval", "u", &delay);
        context->priv->repeat_timeout_id =
            g_timeout_add (delay,
                           (GSourceFunc)on_repeat_timeout,
                           context);
    } else */
        context->priv->repeat_timeout_id = 0;

    return FALSE;
}
#endif // ===== REMOVAL NOTICE ==== expires by: 2019-08-20 -Wunused-function =====

/**
 * eekboard_context_service_enable:
 * @context: an #EekboardContextService
 *
 * Enable @context.  This function is called when @context is pushed
 * by eekboard_service_push_context().
 */
void
eekboard_context_service_enable (EekboardContextService *context)
{
    g_return_if_fail (EEKBOARD_IS_CONTEXT_SERVICE(context));

    if (!context->priv->enabled) {
        context->priv->enabled = TRUE;
        g_signal_emit (context, signals[ENABLED], 0);
    }
}

/**
 * eekboard_context_service_disable:
 * @context: an #EekboardContextService
 *
 * Disable @context.  This function is called when @context is pushed
 * by eekboard_service_pop_context().
 */
void
eekboard_context_service_disable (EekboardContextService *context)
{
    g_return_if_fail (EEKBOARD_IS_CONTEXT_SERVICE(context));

    if (context->priv->enabled) {
        context->priv->enabled = FALSE;
        g_signal_emit (context, signals[DISABLED], 0);
    }
}

void
eekboard_context_service_show_keyboard (EekboardContextService *context)
{
    g_return_if_fail (EEKBOARD_IS_CONTEXT_SERVICE(context));

    if (!context->priv->visible) {
        EEKBOARD_CONTEXT_SERVICE_GET_CLASS(context)->show_keyboard (context);
    }
}

void
eekboard_context_service_hide_keyboard (EekboardContextService *context)
{
    g_return_if_fail (EEKBOARD_IS_CONTEXT_SERVICE(context));

    if (context->priv->visible) {
        EEKBOARD_CONTEXT_SERVICE_GET_CLASS(context)->hide_keyboard (context);
    }
}

/**
 * eekboard_context_service_destroy:
 * @context: an #EekboardContextService
 *
 * Destroy @context.
 */
void
eekboard_context_service_destroy (EekboardContextService *context)
{
    g_return_if_fail (EEKBOARD_IS_CONTEXT_SERVICE(context));

    if (context->priv->enabled) {
        eekboard_context_service_disable (context);
    }
    g_signal_emit (context, signals[DESTROYED], 0);
}

/**
 * eekboard_context_service_get_keyboard:
 * @context: an #EekboardContextService
 *
 * Get keyboard currently active in @context.
 * Returns: (transfer none): an #EekKeyboard
 */
EekKeyboard *
eekboard_context_service_get_keyboard (EekboardContextService *context)
{
    return context->priv->keyboard;
}

/**
 * eekboard_context_service_get_fullscreen:
 * @context: an #EekboardContextService
 *
 * Check if keyboard is rendered in fullscreen mode in @context.
 * Returns: %TRUE or %FALSE
 */
gboolean
eekboard_context_service_get_fullscreen (EekboardContextService *context)
{
    return context->priv->fullscreen;
}

void eekboard_context_service_set_keymap(EekboardContextService *context,
                                         const EekKeyboard *keyboard)
{
    zwp_virtual_keyboard_v1_keymap(context->virtual_keyboard,
        WL_KEYBOARD_KEYMAP_FORMAT_XKB_V1,
        keyboard->keymap_fd, keyboard->keymap_len);
}
