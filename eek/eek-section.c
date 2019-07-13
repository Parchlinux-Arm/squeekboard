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
 * SECTION:eek-section
 * @short_description: Base class of a section
 * @see_also: #EekKey
 *
 * The #EekSectionClass class represents a section, which consists
 * of one or more keys of the #EekKeyClass class.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif  /* HAVE_CONFIG_H */

#include <string.h>

#include "eek-keyboard.h"
#include "eek-section.h"
#include "eek-key.h"
#include "eek-symbol.h"

enum {
    PROP_0,
    PROP_ANGLE,
    PROP_LAST
};

enum {
    KEY_LOCKED,
    KEY_UNLOCKED,
    LAST_SIGNAL
};

static guint signals[LAST_SIGNAL] = { 0, };

struct _EekRow
{
    gint num_columns;
    EekOrientation orientation;
};

typedef struct _EekRow EekRow;

typedef struct _EekSectionPrivate
{
    gint angle;
    GSList *rows;
    EekModifierType modifiers;
} EekSectionPrivate;

G_DEFINE_TYPE_WITH_PRIVATE (EekSection, eek_section, EEK_TYPE_CONTAINER)

static gint
eek_section_real_get_n_rows (EekSection *self)
{
    EekSectionPrivate *priv = eek_section_get_instance_private (self);

    return g_slist_length (priv->rows);
}

static void
eek_section_real_add_row (EekSection    *self,
                          gint           num_columns,
                          EekOrientation orientation)
{
    EekSectionPrivate *priv = eek_section_get_instance_private (self);
    EekRow *row;

    row = g_slice_new (EekRow);
    row->num_columns = num_columns;
    row->orientation = orientation;
    priv->rows = g_slist_append (priv->rows, row);
}

static void
eek_section_real_get_row (EekSection     *self,
                          gint            index,
                          gint           *num_columns,
                          EekOrientation *orientation)
{
    EekSectionPrivate *priv = eek_section_get_instance_private (self);
    EekRow *row;

    row = g_slist_nth_data (priv->rows, index);
    g_return_if_fail (row);
    if (num_columns)
        *num_columns = row->num_columns;
    if (orientation)
        *orientation = row->orientation;
}

static void
on_locked (EekKey     *key,
           EekSection *section)
{
    g_signal_emit (section, signals[KEY_LOCKED], 0, key);
}

static void
on_unlocked (EekKey     *key,
             EekSection *section)
{
    g_signal_emit (section, signals[KEY_UNLOCKED], 0, key);
}

static EekKey *
eek_section_real_create_key (EekSection *self,
                             const gchar *name,
                             gint        keycode,
                             gint        column_index,
                             gint        row_index)
{
    EekKey *key;
    gint num_rows;
    EekRow *row;

    num_rows = eek_section_get_n_rows (self);
    g_return_val_if_fail (0 <= row_index && row_index < num_rows, NULL);

    EekSectionPrivate *priv = eek_section_get_instance_private (self);

    row = g_slist_nth_data (priv->rows, row_index);
    if (row->num_columns < column_index + 1)
        row->num_columns = column_index + 1;

    key = g_object_new (EEK_TYPE_KEY,
                        "name", name,
                        "keycode", keycode,
                        "column", column_index,
                        "row", row_index,
                        NULL);
    g_return_val_if_fail (key, NULL);

    EEK_CONTAINER_GET_CLASS(self)->add_child (EEK_CONTAINER(self),
                                              EEK_ELEMENT(key));

    return key;
}

static void
set_level_from_modifiers (EekSection *self)
{
    EekSectionPrivate *priv = eek_section_get_instance_private (self);
    EekKeyboard *keyboard;
    EekModifierType num_lock_mask;
    gint level = -1;

    keyboard = EEK_KEYBOARD(eek_element_get_parent (EEK_ELEMENT(self)));
    num_lock_mask = eek_keyboard_get_num_lock_mask (keyboard);
    if (priv->modifiers & num_lock_mask)
        level = 1;
    eek_element_set_level (EEK_ELEMENT(self), level);
}

static void
eek_section_real_key_pressed (EekSection *self, EekKey *key)
{
    EekSectionPrivate *priv = eek_section_get_instance_private (self);
    EekSymbol *symbol;
    EekKeyboard *keyboard;
    EekModifierBehavior behavior;
    EekModifierType modifier;

    symbol = eek_key_get_symbol_with_fallback (key, 0, 0);
    if (!symbol)
        return;

    keyboard = EEK_KEYBOARD(eek_element_get_parent (EEK_ELEMENT(self)));
    behavior = eek_keyboard_get_modifier_behavior (keyboard);
    modifier = eek_symbol_get_modifier_mask (symbol);
    if (behavior == EEK_MODIFIER_BEHAVIOR_NONE) {
        priv->modifiers |= modifier;
        set_level_from_modifiers (self);
    }
}

static void
eek_section_real_key_released (EekSection *self, EekKey *key)
{
    EekSectionPrivate *priv = eek_section_get_instance_private (self);
    EekSymbol *symbol;
    EekKeyboard *keyboard;
    EekModifierBehavior behavior;
    EekModifierType modifier;

    symbol = eek_key_get_symbol_with_fallback (key, 0, 0);
    if (!symbol)
        return;

    keyboard = EEK_KEYBOARD(eek_element_get_parent (EEK_ELEMENT(self)));
    behavior = eek_keyboard_get_modifier_behavior (keyboard);
    modifier = eek_symbol_get_modifier_mask (symbol);
    switch (behavior) {
    case EEK_MODIFIER_BEHAVIOR_NONE:
        priv->modifiers &= ~modifier;
        break;
    case EEK_MODIFIER_BEHAVIOR_LOCK:
        priv->modifiers ^= modifier;
        break;
    case EEK_MODIFIER_BEHAVIOR_LATCH:
        priv->modifiers = (priv->modifiers ^ modifier) & modifier;
        break;
    }
    set_level_from_modifiers (self);
}

static void
eek_section_finalize (GObject *object)
{
    EekSection        *self = EEK_SECTION (object);
    EekSectionPrivate *priv = eek_section_get_instance_private (self);
    GSList *head;

    for (head = priv->rows; head; head = g_slist_next (head))
        g_slice_free (EekRow, head->data);
    g_slist_free (priv->rows);

    G_OBJECT_CLASS (eek_section_parent_class)->finalize (object);
}

static void
eek_section_set_property (GObject      *object,
                          guint         prop_id,
                          const GValue *value,
                          GParamSpec   *pspec)
{
    switch (prop_id) {
    case PROP_ANGLE:
        eek_section_set_angle (EEK_SECTION(object),
                               g_value_get_int (value));
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
        break;
    }
}

static void
eek_section_get_property (GObject    *object,
                          guint       prop_id,
                          GValue     *value,
                          GParamSpec *pspec)
{
    switch (prop_id) {
    case PROP_ANGLE:
        g_value_set_int (value, eek_section_get_angle (EEK_SECTION(object)));
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
        break;
    }
}

static void
eek_section_real_child_added (EekContainer *self,
                              EekElement   *element)
{
    g_signal_connect (element, "locked", G_CALLBACK(on_locked), self);
    g_signal_connect (element, "unlocked", G_CALLBACK(on_unlocked), self);
}

static void
eek_section_real_child_removed (EekContainer *self,
                                EekElement   *element)
{
    g_signal_handlers_disconnect_by_func (element, on_locked, self);
    g_signal_handlers_disconnect_by_func (element, on_unlocked, self);
}

static void
eek_section_class_init (EekSectionClass *klass)
{
    EekContainerClass *container_class = EEK_CONTAINER_CLASS (klass);
    GObjectClass      *gobject_class = G_OBJECT_CLASS (klass);
    GParamSpec        *pspec;

    klass->get_n_rows = eek_section_real_get_n_rows;
    klass->add_row = eek_section_real_add_row;
    klass->get_row = eek_section_real_get_row;
    klass->create_key = eek_section_real_create_key;

    /* signals */
    klass->key_pressed = eek_section_real_key_pressed;
    klass->key_released = eek_section_real_key_released;

    container_class->child_added = eek_section_real_child_added;
    container_class->child_removed = eek_section_real_child_removed;

    gobject_class->set_property = eek_section_set_property;
    gobject_class->get_property = eek_section_get_property;
    gobject_class->finalize     = eek_section_finalize;

    /**
     * EekSection:angle:
     *
     * The rotation angle of #EekSection.
     */
    pspec = g_param_spec_int ("angle",
                              "Angle",
                              "Rotation angle of the section",
                              -360, 360, 0,
                              G_PARAM_READWRITE);
    g_object_class_install_property (gobject_class,
                                     PROP_ANGLE,
                                     pspec);

    /**
     * EekSection::key-locked:
     * @section: an #EekSection
     * @key: an #EekKey
     *
     * The ::key-locked signal is emitted each time a key in @section
     * is shifted to the locked state.
     */
    signals[KEY_LOCKED] =
        g_signal_new (I_("key-locked"),
                      G_TYPE_FROM_CLASS(gobject_class),
                      G_SIGNAL_RUN_LAST,
                      G_STRUCT_OFFSET(EekSectionClass, key_locked),
                      NULL,
                      NULL,
                      g_cclosure_marshal_VOID__OBJECT,
                      G_TYPE_NONE,
                      1,
                      EEK_TYPE_KEY);

    /**
     * EekSection::key-unlocked:
     * @section: an #EekSection
     * @key: an #EekKey
     *
     * The ::key-unlocked signal is emitted each time a key in @section
     * is shifted to the unlocked state.
     */
    signals[KEY_UNLOCKED] =
        g_signal_new (I_("key-unlocked"),
                      G_TYPE_FROM_CLASS(gobject_class),
                      G_SIGNAL_RUN_LAST,
                      G_STRUCT_OFFSET(EekSectionClass, key_unlocked),
                      NULL,
                      NULL,
                      g_cclosure_marshal_VOID__OBJECT,
                      G_TYPE_NONE,
                      1,
                      EEK_TYPE_KEY);
}

static void
eek_section_init (EekSection *self)
{
    /* void */
}

/**
 * eek_section_set_angle:
 * @section: an #EekSection
 * @angle: rotation angle
 *
 * Set rotation angle of @section to @angle.
 */
void
eek_section_set_angle (EekSection  *section,
                       gint         angle)
{
    g_return_if_fail (EEK_IS_SECTION(section));

    EekSectionPrivate *priv = eek_section_get_instance_private (section);

    if (priv->angle != angle) {
        priv->angle = angle;
        g_object_notify (G_OBJECT(section), "angle");
    }
}

/**
 * eek_section_get_angle:
 * @section: an #EekSection
 *
 * Get rotation angle of @section.
 */
gint
eek_section_get_angle (EekSection *section)
{
    g_return_val_if_fail (EEK_IS_SECTION(section), -1);

    EekSectionPrivate *priv = eek_section_get_instance_private (section);

    return priv->angle;
}

/**
 * eek_section_get_n_rows:
 * @section: an #EekSection
 *
 * Get the number of rows in @section.
 */
gint
eek_section_get_n_rows (EekSection *section)
{
    g_return_val_if_fail (EEK_IS_SECTION(section), -1);
    return EEK_SECTION_GET_CLASS(section)->get_n_rows (section);
}

/**
 * eek_section_add_row:
 * @section: an #EekSection
 * @num_columns: the number of column in the row
 * @orientation: #EekOrientation of the row
 *
 * Add a row which has @num_columns columns and whose orientation is
 * @orientation to @section.
 */
void
eek_section_add_row (EekSection    *section,
                     gint           num_columns,
                     EekOrientation orientation)
{
    g_return_if_fail (EEK_IS_SECTION(section));
    EEK_SECTION_GET_CLASS(section)->add_row (section,
                                             num_columns,
                                             orientation);
}

/**
 * eek_section_get_row:
 * @section: an #EekSection
 * @index: the index of row
 * @num_columns: pointer where the number of column in the row will be stored
 * @orientation: pointer where #EekOrientation of the row will be stored
 *
 * Get the information about the @index-th row in @section.
 */
void
eek_section_get_row (EekSection     *section,
                     gint            index,
                     gint           *num_columns,
                     EekOrientation *orientation)
{
    g_return_if_fail (EEK_IS_SECTION(section));
    EEK_SECTION_GET_CLASS(section)->get_row (section,
                                             index,
                                             num_columns,
                                             orientation);
}

/**
 * eek_section_create_key:
 * @section: an #EekSection
 * @name: a name
 * @keycode: a keycode
 * @column: the column index of the key
 * @row: the row index of the key
 *
 * Create an #EekKey instance and append it to @section.  This
 * function is rarely called by application but called by #EekLayout
 * implementation.
 */
EekKey *
eek_section_create_key (EekSection *section,
                        const gchar *name,
                        gint        keycode,
                        gint        column,
                        gint        row)
{
    g_return_val_if_fail (EEK_IS_SECTION(section), NULL);
    return EEK_SECTION_GET_CLASS(section)->create_key (section,
                                                       name,
                                                       keycode,
                                                       column,
                                                       row);
}

static void keysizer(EekElement *element, gpointer user_data) {
    EekKey *key = EEK_KEY(element);
    EekKeyboard *keyboard = EEK_KEYBOARD(user_data);
    uint oref = eek_key_get_oref (key);
    EekOutline *outline = eek_keyboard_get_outline (keyboard, oref);
    if (outline && outline->num_points > 0) {
        double minx = outline->points[0].x;
        double maxx = minx;
        double miny = outline->points[0].y;
        double maxy = miny;
        for (uint i = 1; i < outline->num_points; i++) {
            EekPoint p = outline->points[i];
            if (p.x < minx) {
                minx = p.x;
            } else if (p.x > maxx) {
                maxx = p.x;
            }

            if (p.y < miny) {
                miny = p.y;
            } else if (p.y > maxy) {
                maxy = p.y;
            }
        }
        EekBounds key_bounds = {0};
        eek_element_get_bounds(element, &key_bounds);
        key_bounds.height = maxy - miny;
        key_bounds.width = maxx - minx;
        eek_element_set_bounds(element, &key_bounds);
    }
}

struct keys_info {
    uint count;
    double total_width;
    double biggest_height;
};

static void keycounter (EekElement *element, gpointer user_data) {
    struct keys_info *data = user_data;
    data->count++;
    EekBounds key_bounds = {0};
    eek_element_get_bounds(element, &key_bounds);
    data->total_width += key_bounds.width;
    if (key_bounds.height > data->biggest_height) {
        data->biggest_height = key_bounds.height;
    }
}

const double keyspacing = 4.0;

static void keyplacer(EekElement *element, gpointer user_data) {
    double *current_offset = user_data;
    EekBounds key_bounds = {0};
    eek_element_get_bounds(element, &key_bounds);
    key_bounds.x = *current_offset;
    key_bounds.y = 0;
    eek_element_set_bounds(element, &key_bounds);
    *current_offset += key_bounds.width + keyspacing;
}

void eek_section_place_keys(EekSection *section, EekKeyboard *keyboard)
{
    eek_container_foreach_child(EEK_CONTAINER(section), keysizer, keyboard);

    struct keys_info keyinfo = {0};
    eek_container_foreach_child(EEK_CONTAINER(section), keycounter, &keyinfo);
    EekBounds section_bounds = {0};
    eek_element_get_bounds(EEK_ELEMENT(section), &section_bounds);

    double key_offset = (section_bounds.width - (keyinfo.total_width + (keyinfo.count - 1) * keyspacing)) / 2;
    eek_container_foreach_child(EEK_CONTAINER(section), keyplacer, &key_offset);

    section_bounds.height = keyinfo.biggest_height;
    eek_element_set_bounds(EEK_ELEMENT(section), &section_bounds);
}
