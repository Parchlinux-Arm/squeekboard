/* 
 * Copyright (C) 2010-2011 Daiki Ueno <ueno@unixuser.org>
 * Copyright (C) 2010-2011 Red Hat, Inc.
 * 
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public License
 * as published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 * 
 * This library is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 * 
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301 USA
 */

/**
 * SECTION:eek-keyboard
 * @short_description: Base class of a keyboard
 * @see_also: #EekSection
 *
 * The #EekKeyboardClass class represents a keyboard, which consists
 * of one or more sections of the #EekSectionClass class.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif  /* HAVE_CONFIG_H */

#include "eek-keyboard.h"
#include "eek-marshalers.h"
#include "eek-section.h"
#include "eek-key.h"
#include "eek-symbol.h"
#include "eek-enumtypes.h"
#include "eekboard/key-emitter.h"

enum {
    PROP_0,
    PROP_LAYOUT,
    PROP_MODIFIER_BEHAVIOR,
    PROP_LAST
};

enum {
    KEY_RELEASED,
    KEY_LOCKED,
    KEY_UNLOCKED,
    LAST_SIGNAL
};

enum {
    VIEW_LETTERS_LOWER,
    VIEW_LETTERS_UPPER,
    VIEW_NUMBERS,
    VIEW_SYMBOLS
};

static guint signals[LAST_SIGNAL] = { 0, };

#define EEK_KEYBOARD_GET_PRIVATE(obj)                                  \
    (G_TYPE_INSTANCE_GET_PRIVATE ((obj), EEK_TYPE_KEYBOARD, EekKeyboardPrivate))

struct _EekKeyboardPrivate
{
    EekLayout *layout;
    EekModifierBehavior modifier_behavior;
    EekModifierType modifiers;
    unsigned int old_level;
    GList *pressed_keys;
    GList *locked_keys;
    GArray *outline_array;
    GHashTable *names;

    /* modifiers dynamically assigned at run time */
    EekModifierType num_lock_mask;
    EekModifierType alt_gr_mask;
};

G_DEFINE_TYPE_WITH_PRIVATE (EekKeyboard, eek_keyboard, EEK_TYPE_CONTAINER);

G_DEFINE_BOXED_TYPE(EekModifierKey, eek_modifier_key,
                    eek_modifier_key_copy, eek_modifier_key_free);

EekModifierKey *
eek_modifier_key_copy (EekModifierKey *modkey)
{
    return g_slice_dup (EekModifierKey, modkey);
}

void
eek_modifier_key_free (EekModifierKey *modkey)
{
    g_object_unref (modkey->key);
    g_slice_free (EekModifierKey, modkey);
}

static void
on_key_locked (EekSection  *section,
                EekKey      *key,
                EekKeyboard *keyboard)
{
    g_signal_emit (keyboard, signals[KEY_LOCKED], 0, key);
}

static void
on_key_unlocked (EekSection  *section,
                 EekKey      *key,
                 EekKeyboard *keyboard)
{
    g_signal_emit (keyboard, signals[KEY_UNLOCKED], 0, key);
}

static void
on_symbol_index_changed (EekSection *section,
                         gint group,
                         gint level,
                         EekKeyboard *keyboard)
{
    g_signal_emit_by_name (keyboard, "symbol-index-changed", group, level);
}

static void
section_child_added_cb (EekContainer *container,
                        EekElement   *element,
                        EekKeyboard  *keyboard)
{
    const gchar *name = eek_element_get_name(element);
    g_hash_table_insert (keyboard->priv->names,
                         (gpointer)name,
                         element);
}

static void
section_child_removed_cb (EekContainer *container,
                          EekElement   *element,
                          EekKeyboard  *keyboard)
{
    const gchar *name = eek_element_get_name(element);
    g_hash_table_remove (keyboard->priv->names,
                         name);
}

static EekSection *
eek_keyboard_real_create_section (EekKeyboard *self)
{
    EekSection *section;

    section = g_object_new (EEK_TYPE_SECTION, NULL);
    g_return_val_if_fail (section, NULL);

    g_signal_connect (G_OBJECT(section), "child-added",
                      G_CALLBACK(section_child_added_cb), self);

    g_signal_connect (G_OBJECT(section), "child-removed",
                      G_CALLBACK(section_child_removed_cb), self);

    EEK_CONTAINER_GET_CLASS(self)->add_child (EEK_CONTAINER(self),
                                              EEK_ELEMENT(section));
    return section;
}

static void
eek_keyboard_set_property (GObject      *object,
                           guint         prop_id,
                           const GValue *value,
                           GParamSpec   *pspec)
{
    EekKeyboardPrivate *priv = EEK_KEYBOARD_GET_PRIVATE(object);

    switch (prop_id) {
    case PROP_LAYOUT:
        priv->layout = g_value_get_object (value);
        if (priv->layout)
            g_object_ref (priv->layout);
        break;
    case PROP_MODIFIER_BEHAVIOR:
        eek_keyboard_set_modifier_behavior (EEK_KEYBOARD(object),
                                            g_value_get_enum (value));
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
        break;
    }
}

static void
eek_keyboard_get_property (GObject    *object,
                           guint       prop_id,
                           GValue     *value,
                           GParamSpec *pspec)
{
    EekKeyboardPrivate *priv = EEK_KEYBOARD_GET_PRIVATE(object);

    switch (prop_id) {
    case PROP_LAYOUT:
        g_value_set_object (value, priv->layout);
        break;
    case PROP_MODIFIER_BEHAVIOR:
        g_value_set_enum (value,
                          eek_keyboard_get_modifier_behavior (EEK_KEYBOARD(object)));
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
        break;
    }
}

static void
set_level_from_modifiers (EekKeyboard *self, EekKey *key)
{
    EekKeyboardPrivate *priv = EEK_KEYBOARD_GET_PRIVATE(self);

    /* The levels are: 0 Letters, 1 Upper case letters, 2 Numbers, 3 Symbols */

    /* Use the numbers/letters bit from the old level */
    gint level = priv->old_level & 2;

    /* Handle non-emitting keys */
    if (key) {
        const gchar *name = eek_element_get_name(EEK_ELEMENT(key));
        if (g_strcmp0(name, "ABC123") == 0)
            level ^= 2;
    }

    level |= ((priv->modifiers & EEK_SHIFT_MASK) ? 1 : 0);

    switch (priv->old_level) {
    case VIEW_LETTERS_UPPER:
    {
        /* Redirect upper case letters to numbers instead of symbols, clearing
           the shift modifier to keep the modifiers in sync with the level */
        if (level == VIEW_SYMBOLS) {
            level = VIEW_NUMBERS;
            priv->modifiers &= ~EEK_SHIFT_MASK;
        }
        break;
    }
    case VIEW_SYMBOLS:
    {
        /* Redirect symbols to lower case letters instead of upper case,
           clearing the shift modifier to keep the modifiers in sync with the
           level */
        if (level == VIEW_LETTERS_UPPER) {
            level = VIEW_LETTERS_LOWER;
            priv->modifiers &= ~EEK_SHIFT_MASK;
        }
        break;
    }
    case VIEW_LETTERS_LOWER:    /* Direct transitions between views */
    case VIEW_NUMBERS:
    default:
        break;
    }

    if (level == VIEW_NUMBERS || level == VIEW_SYMBOLS)
        priv->modifier_behavior = EEK_MODIFIER_BEHAVIOR_LOCK;
    else
        priv->modifier_behavior = EEK_MODIFIER_BEHAVIOR_LATCH;

    priv->old_level = level;
    eek_element_set_level (EEK_ELEMENT(self), level);
}

static void
set_modifiers_with_key (EekKeyboard    *self,
                        EekKey         *key,
                        EekModifierType modifiers)
{
    EekKeyboardPrivate *priv = EEK_KEYBOARD_GET_PRIVATE(self);
    EekModifierType enabled = (priv->modifiers ^ modifiers) & modifiers;
    EekModifierType disabled = (priv->modifiers ^ modifiers) & priv->modifiers;

    if (enabled != 0) {
        if (priv->modifier_behavior != EEK_MODIFIER_BEHAVIOR_NONE) {
            EekModifierKey *modifier_key = g_slice_new (EekModifierKey);
            modifier_key->modifiers = enabled;
            modifier_key->key = g_object_ref (key);
            priv->locked_keys =
                g_list_prepend (priv->locked_keys, modifier_key);
            g_signal_emit_by_name (modifier_key->key, "locked");
        }
    } else {
        if (priv->modifier_behavior != EEK_MODIFIER_BEHAVIOR_NONE) {
            GList *head;
            for (head = priv->locked_keys; head; ) {
                EekModifierKey *modifier_key = head->data;
                if (modifier_key->modifiers & disabled) {
                    GList *next = g_list_next (head);
                    priv->locked_keys =
                        g_list_remove_link (priv->locked_keys, head);
                    g_signal_emit_by_name (modifier_key->key, "unlocked");
                    g_list_free1 (head);
                    head = next;
                } else
                    head = g_list_next (head);
            }
        }
    }

    priv->modifiers = modifiers;
}

void eek_keyboard_press_key(EekKeyboard *keyboard, EekKey *key, guint32 timestamp) {
    EekKeyboardPrivate *priv = EEK_KEYBOARD_GET_PRIVATE(keyboard);

    eek_key_set_pressed(key, TRUE);
    priv->pressed_keys = g_list_prepend (priv->pressed_keys, key);

    EekSymbol *symbol = eek_key_get_symbol_with_fallback (key, 0, 0);
    if (!symbol)
        return;

    EekModifierType modifier = eek_symbol_get_modifier_mask (symbol);
    if (priv->modifier_behavior == EEK_MODIFIER_BEHAVIOR_NONE) {
        set_modifiers_with_key (keyboard, key, priv->modifiers | modifier);
        set_level_from_modifiers (keyboard, key);
    }

    // "Borrowed" from eek-context-service; doesn't influence the state but forwards the event

    guint keycode = eek_key_get_keycode (key);
    EekModifierType modifiers = eek_keyboard_get_modifiers (keyboard);

    emit_key_activated(keyboard->manager, keyboard, keycode, symbol, modifiers, TRUE, timestamp);
}

void eek_keyboard_release_key( EekKeyboard *keyboard,
                               EekKey      *key,
                               guint32      timestamp) {
    EekKeyboardPrivate *priv = EEK_KEYBOARD_GET_PRIVATE(keyboard);

    for (GList *head = priv->pressed_keys; head; head = g_list_next (head)) {
        if (head->data == key) {
            priv->pressed_keys = g_list_remove_link (priv->pressed_keys, head);
            g_list_free1 (head);
            break;
        }
    }

    EekSymbol *symbol = eek_key_get_symbol_with_fallback (key, 0, 0);
    if (!symbol)
        return;

    EekModifierType modifier = eek_symbol_get_modifier_mask (symbol);

    if (!symbol)
        return;

    modifier = eek_symbol_get_modifier_mask (symbol);
    switch (priv->modifier_behavior) {
    case EEK_MODIFIER_BEHAVIOR_NONE:
        set_modifiers_with_key (keyboard, key, priv->modifiers & ~modifier);
        break;
    case EEK_MODIFIER_BEHAVIOR_LOCK:
        priv->modifiers ^= modifier;
        break;
    case EEK_MODIFIER_BEHAVIOR_LATCH:
        if (modifier)
            set_modifiers_with_key (keyboard, key, priv->modifiers ^ modifier);
        else
            set_modifiers_with_key (keyboard, key,
                                    (priv->modifiers ^ modifier) & modifier);
        break;
    }
    set_level_from_modifiers (keyboard, key);

    // "Borrowed" from eek-context-service; doesn't influence the state but forwards the event

    guint keycode = eek_key_get_keycode (key);
    guint modifiers = eek_keyboard_get_modifiers (keyboard);

    emit_key_activated(keyboard->manager, keyboard, keycode, symbol, modifiers, FALSE, timestamp);
}

static void
eek_keyboard_dispose (GObject *object)
{
    EekKeyboardPrivate *priv = EEK_KEYBOARD_GET_PRIVATE(object);

    if (priv->layout) {
        g_object_unref (priv->layout);
        priv->layout = NULL;
    }

    G_OBJECT_CLASS (eek_keyboard_parent_class)->dispose (object);
}

static void
eek_keyboard_finalize (GObject *object)
{
    EekKeyboardPrivate *priv = EEK_KEYBOARD_GET_PRIVATE(object);
    guint i;

    g_list_free (priv->pressed_keys);
    g_list_free_full (priv->locked_keys,
                      (GDestroyNotify) eek_modifier_key_free);

    g_hash_table_destroy (priv->names);

    for (i = 0; i < priv->outline_array->len; i++) {
        EekOutline *outline = &g_array_index (priv->outline_array,
                                              EekOutline,
                                              i);
        g_slice_free1 (sizeof (EekPoint) * outline->num_points,
                       outline->points);
    }
    g_array_free (priv->outline_array, TRUE);
        
    G_OBJECT_CLASS (eek_keyboard_parent_class)->finalize (object);
}

static void
eek_keyboard_real_child_added (EekContainer *self,
                               EekElement   *element)
{
    g_signal_connect (element, "key-locked",
                      G_CALLBACK(on_key_locked), self);
    g_signal_connect (element, "key-unlocked",
                      G_CALLBACK(on_key_unlocked), self);
    g_signal_connect (element, "symbol-index-changed",
                      G_CALLBACK(on_symbol_index_changed), self);
}

static void
eek_keyboard_real_child_removed (EekContainer *self,
                                 EekElement   *element)
{
    g_signal_handlers_disconnect_by_func (element, on_key_locked, self);
    g_signal_handlers_disconnect_by_func (element, on_key_unlocked, self);
}

static void
eek_keyboard_class_init (EekKeyboardClass *klass)
{
    EekContainerClass *container_class = EEK_CONTAINER_CLASS (klass);
    GObjectClass      *gobject_class = G_OBJECT_CLASS (klass);
    GParamSpec        *pspec;

    klass->create_section = eek_keyboard_real_create_section;

    /* signals */
    container_class->child_added = eek_keyboard_real_child_added;
    container_class->child_removed = eek_keyboard_real_child_removed;

    gobject_class->get_property = eek_keyboard_get_property;
    gobject_class->set_property = eek_keyboard_set_property;
    gobject_class->dispose = eek_keyboard_dispose;
    gobject_class->finalize = eek_keyboard_finalize;

    /**
     * EekKeyboard:layout:
     *
     * The layout used to create this #EekKeyboard.
     */
    pspec = g_param_spec_object ("layout",
                                 "Layout",
                                 "Layout used to create the keyboard",
                                 EEK_TYPE_LAYOUT,
                                 G_PARAM_CONSTRUCT_ONLY | G_PARAM_READWRITE);
    g_object_class_install_property (gobject_class,
                                     PROP_LAYOUT,
                                     pspec);

    /**
     * EekKeyboard:modifier-behavior:
     *
     * The modifier handling mode of #EekKeyboard.
     */
    pspec = g_param_spec_enum ("modifier-behavior",
                               "Modifier behavior",
                               "Modifier handling mode of the keyboard",
                               EEK_TYPE_MODIFIER_BEHAVIOR,
                               EEK_MODIFIER_BEHAVIOR_NONE,
                               G_PARAM_READWRITE);
    g_object_class_install_property (gobject_class,
                                     PROP_MODIFIER_BEHAVIOR,
                                     pspec);

    /**
     * EekKeyboard::key-locked:
     * @keyboard: an #EekKeyboard
     * @key: an #EekKey
     *
     * The ::key-locked signal is emitted each time a key in @keyboard
     * is shifted to the locked state.
     */
    signals[KEY_LOCKED] =
        g_signal_new (I_("key-locked"),
                      G_TYPE_FROM_CLASS(gobject_class),
                      G_SIGNAL_RUN_LAST,
                      G_STRUCT_OFFSET(EekKeyboardClass, key_locked),
                      NULL,
                      NULL,
                      g_cclosure_marshal_VOID__OBJECT,
                      G_TYPE_NONE,
                      1,
                      EEK_TYPE_KEY);

    /**
     * EekKeyboard::key-unlocked:
     * @keyboard: an #EekKeyboard
     * @key: an #EekKey
     *
     * The ::key-unlocked signal is emitted each time a key in @keyboard
     * is shifted to the unlocked state.
     */
    signals[KEY_UNLOCKED] =
        g_signal_new (I_("key-unlocked"),
                      G_TYPE_FROM_CLASS(gobject_class),
                      G_SIGNAL_RUN_LAST,
                      G_STRUCT_OFFSET(EekKeyboardClass, key_unlocked),
                      NULL,
                      NULL,
                      g_cclosure_marshal_VOID__OBJECT,
                      G_TYPE_NONE,
                      1,
                      EEK_TYPE_KEY);
}

static void
eek_keyboard_init (EekKeyboard *self)
{
    self->priv = EEK_KEYBOARD_GET_PRIVATE(self);
    self->priv->modifier_behavior = EEK_MODIFIER_BEHAVIOR_NONE;
    self->priv->outline_array = g_array_new (FALSE, TRUE, sizeof (EekOutline));
    self->priv->names = g_hash_table_new (g_str_hash, g_str_equal);
    eek_element_set_symbol_index (EEK_ELEMENT(self), 0, 0);
}

/**
 * eek_keyboard_create_section:
 * @keyboard: an #EekKeyboard
 *
 * Create an #EekSection instance and append it to @keyboard.  This
 * function is rarely called by application but called by #EekLayout
 * implementation.
 */
EekSection *
eek_keyboard_create_section (EekKeyboard *keyboard)
{
    g_return_val_if_fail (EEK_IS_KEYBOARD(keyboard), NULL);
    return EEK_KEYBOARD_GET_CLASS(keyboard)->create_section (keyboard);
}

/**
 * eek_keyboard_find_key_by_name:
 * @keyboard: an #EekKeyboard
 * @name: a key name
 *
 * Find an #EekKey whose name is @name.
 * Return value: (transfer none): #EekKey whose name is @name
 */
EekKey *
eek_keyboard_find_key_by_name (EekKeyboard *keyboard,
                               const gchar *name)
{
    g_return_val_if_fail (EEK_IS_KEYBOARD(keyboard), NULL);
    return g_hash_table_lookup (keyboard->priv->names,
                                name);
}

/**
 * eek_keyboard_get_layout:
 * @keyboard: an #EekKeyboard
 *
 * Get the layout used to create @keyboard.
 * Returns: an #EekLayout
 */
EekLayout *
eek_keyboard_get_layout (EekKeyboard *keyboard)
{
    g_return_val_if_fail (EEK_IS_KEYBOARD(keyboard), NULL);
    return keyboard->priv->layout;
}

/**
 * eek_keyboard_get_size:
 * @keyboard: an #EekKeyboard
 * @width: width of @keyboard
 * @height: height of @keyboard
 *
 * Get the size of @keyboard.
 */
void
eek_keyboard_get_size (EekKeyboard *keyboard,
                       gdouble     *width,
                       gdouble     *height)
{
    EekBounds bounds;

    eek_element_get_bounds (EEK_ELEMENT(keyboard), &bounds);
    *width = bounds.width;
    *height = bounds.height;
}

/**
 * eek_keyboard_set_modifier_behavior:
 * @keyboard: an #EekKeyboard
 * @modifier_behavior: modifier behavior of @keyboard
 *
 * Set the modifier handling mode of @keyboard.
 */
void
eek_keyboard_set_modifier_behavior (EekKeyboard        *keyboard,
                                    EekModifierBehavior modifier_behavior)
{
    g_return_if_fail (EEK_IS_KEYBOARD(keyboard));
    keyboard->priv->modifier_behavior = modifier_behavior;
}

/**
 * eek_keyboard_get_modifier_behavior:
 * @keyboard: an #EekKeyboard
 *
 * Get the modifier handling mode of @keyboard.
 * Returns: #EekModifierBehavior
 */
EekModifierBehavior
eek_keyboard_get_modifier_behavior (EekKeyboard *keyboard)
{
    g_return_val_if_fail (EEK_IS_KEYBOARD(keyboard), 0);
    return keyboard->priv->modifier_behavior;
}

void
eek_keyboard_set_modifiers (EekKeyboard    *keyboard,
                            EekModifierType modifiers)
{
    g_return_if_fail (EEK_IS_KEYBOARD(keyboard));
    keyboard->priv->modifiers = modifiers;
    set_level_from_modifiers (keyboard, NULL);
}

/**
 * eek_keyboard_get_modifiers:
 * @keyboard: an #EekKeyboard
 *
 * Get the current modifier status of @keyboard.
 * Returns: #EekModifierType
 */
EekModifierType
eek_keyboard_get_modifiers (EekKeyboard *keyboard)
{
    g_return_val_if_fail (EEK_IS_KEYBOARD(keyboard), 0);
    return keyboard->priv->modifiers;
}

/**
 * eek_keyboard_add_outline:
 * @keyboard: an #EekKeyboard
 * @outline: an #EekOutline
 *
 * Register an outline of @keyboard.
 * Returns: an unsigned integer ID of the registered outline, for
 * later reference
 */
guint
eek_keyboard_add_outline (EekKeyboard *keyboard,
                          EekOutline  *outline)
{
    EekOutline *_outline;

    g_return_val_if_fail (EEK_IS_KEYBOARD(keyboard), 0);

    _outline = eek_outline_copy (outline);
    g_array_append_val (keyboard->priv->outline_array, *_outline);
    /* don't use eek_outline_free here, so as to keep _outline->points */
    g_slice_free (EekOutline, _outline);
    return keyboard->priv->outline_array->len - 1;
}

/**
 * eek_keyboard_get_outline:
 * @keyboard: an #EekKeyboard
 * @oref: ID of the outline
 *
 * Get an outline associated with @oref in @keyboard.
 * Returns: an #EekOutline, which should not be released
 */
EekOutline *
eek_keyboard_get_outline (EekKeyboard *keyboard,
                          guint        oref)
{
    g_return_val_if_fail (EEK_IS_KEYBOARD(keyboard), NULL);

    if (oref > keyboard->priv->outline_array->len)
        return NULL;

    return &g_array_index (keyboard->priv->outline_array, EekOutline, oref);
}

/**
 * eek_keyboard_get_n_outlines:
 * @keyboard: an #EekKeyboard
 *
 * Get the number of outlines defined in @keyboard.
 * Returns: integer
 */
gsize
eek_keyboard_get_n_outlines (EekKeyboard *keyboard)
{
    g_return_val_if_fail (EEK_IS_KEYBOARD(keyboard), 0);
    return keyboard->priv->outline_array->len;
}

/**
 * eek_keyboard_set_num_lock_mask:
 * @keyboard: an #EekKeyboard
 * @num_lock_mask: an #EekModifierType
 *
 * Set modifier mask used as Num_Lock.
 */
void
eek_keyboard_set_num_lock_mask (EekKeyboard    *keyboard,
                                EekModifierType num_lock_mask)
{
    g_return_if_fail (EEK_IS_KEYBOARD(keyboard));
    keyboard->priv->num_lock_mask = num_lock_mask;
}

/**
 * eek_keyboard_get_num_lock_mask:
 * @keyboard: an #EekKeyboard
 *
 * Get modifier mask used as Num_Lock.
 * Returns: an #EekModifierType
 */
EekModifierType
eek_keyboard_get_num_lock_mask (EekKeyboard *keyboard)
{
    g_return_val_if_fail (EEK_IS_KEYBOARD(keyboard), 0);
    return keyboard->priv->num_lock_mask;
}

/**
 * eek_keyboard_set_alt_gr_mask:
 * @keyboard: an #EekKeyboard
 * @alt_gr_mask: an #EekModifierType
 *
 * Set modifier mask used as Alt_Gr.
 */
void
eek_keyboard_set_alt_gr_mask (EekKeyboard    *keyboard,
                              EekModifierType alt_gr_mask)
{
    g_return_if_fail (EEK_IS_KEYBOARD(keyboard));
    keyboard->priv->alt_gr_mask = alt_gr_mask;
}

/**
 * eek_keyboard_get_alt_gr_mask:
 * @keyboard: an #EekKeyboard
 *
 * Get modifier mask used as Alt_Gr.
 * Returns: an #EekModifierType
 */
EekModifierType
eek_keyboard_get_alt_gr_mask (EekKeyboard *keyboard)
{
    g_return_val_if_fail (EEK_IS_KEYBOARD(keyboard), 0);
    return keyboard->priv->alt_gr_mask;
}

/**
 * eek_keyboard_get_pressed_keys:
 * @keyboard: an #EekKeyboard
 *
 * Get pressed keys.
 * Returns: (transfer container) (element-type EekKey): A list of
 * pressed keys.
 */
GList *
eek_keyboard_get_pressed_keys (EekKeyboard *keyboard)
{
    g_return_val_if_fail (EEK_IS_KEYBOARD(keyboard), NULL);
    return g_list_copy (keyboard->priv->pressed_keys);
}

/**
 * eek_keyboard_get_locked_keys:
 * @keyboard: an #EekKeyboard
 *
 * Get locked keys.
 * Returns: (transfer container) (element-type Eek.ModifierKey): A list
 * of locked keys.
 */
GList *
eek_keyboard_get_locked_keys (EekKeyboard *keyboard)
{
    g_return_val_if_fail (EEK_IS_KEYBOARD(keyboard), NULL);
    return g_list_copy (keyboard->priv->locked_keys);
}
