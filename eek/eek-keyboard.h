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

#if !defined(__EEK_H_INSIDE__) && !defined(EEK_COMPILATION)
#error "Only <eek/eek.h> can be included directly."
#endif

#ifndef EEK_KEYBOARD_H
#define EEK_KEYBOARD_H 1

#include <glib-object.h>
#include <xkbcommon/xkbcommon.h>
#include "eek-container.h"
#include "eek-types.h"
#include "eek-layout.h"

G_BEGIN_DECLS

#define EEK_TYPE_KEYBOARD (eek_keyboard_get_type())
#define EEK_KEYBOARD(obj) (G_TYPE_CHECK_INSTANCE_CAST ((obj), EEK_TYPE_KEYBOARD, EekKeyboard))
#define EEK_KEYBOARD_CLASS(klass) (G_TYPE_CHECK_CLASS_CAST ((klass), EEK_TYPE_KEYBOARD, EekKeyboardClass))
#define EEK_IS_KEYBOARD(obj) (G_TYPE_CHECK_INSTANCE_TYPE ((obj), EEK_TYPE_KEYBOARD))
#define EEK_IS_KEYBOARD_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass), EEK_TYPE_KEYBOARD))
#define EEK_KEYBOARD_GET_CLASS(obj) (G_TYPE_INSTANCE_GET_CLASS ((obj), EEK_TYPE_KEYBOARD, EekKeyboardClass))

typedef struct _EekKeyboardClass EekKeyboardClass;
typedef struct _EekKeyboardPrivate EekKeyboardPrivate;

/**
 * EekKeyboard:
 *
 * Contains the state of the physical keyboard.
 *
 * Is also a graphical element...
 *
 * The #EekKeyboard structure contains only private data and should
 * only be accessed using the provided API.
 */
struct _EekKeyboard
{
    /*< private >*/
    EekContainer parent;

    EekKeyboardPrivate *priv;
    struct xkb_keymap *keymap;
    int keymap_fd; // keymap formatted as XKB string
    size_t keymap_len; // length of the data inside keymap_fd

    EekboardContextService *manager; // unowned reference
};

/**
 * EekKeyboardClass:
 * @create_section: virtual function for creating a section
 * @find_key_by_name: virtual function for finding a key in the
 * keyboard by name
 * @key_pressed: class handler for #EekKeyboard::key-pressed signal
 * @key_released: class handler for #EekKeyboard::key-released signal
 * @key_locked: class handler for #EekKeyboard::key-locked signal
 * @key_unlocked: class handler for #EekKeyboard::key-unlocked signal
 * @key_cancelled: class handler for #EekKeyboard::key-cancelled signal
 */
struct _EekKeyboardClass
{
    /*< private >*/
    EekContainerClass parent_class;

    /* obsolete members moved to EekElement */
    gpointer set_symbol_index;
    gpointer get_symbol_index;

    /*< public >*/
    EekSection *(* create_section)      (EekKeyboard *self);

    EekKey     *(* find_key_by_name)    (EekKeyboard *self,
                                         const gchar *name);

    /*< private >*/
    /* obsolete members moved to EekElement */
    gpointer symbol_index_changed;

    /*< public >*/
    /* signals */
    void        (* key_locked)          (EekKeyboard *self,
                                         EekKey      *key);
    void        (* key_unlocked)        (EekKeyboard *self,
                                         EekKey      *key);

    /*< private >*/
    /* padding */
    gpointer pdummy[21];
};

/**
 * EekModifierKey:
 * @modifiers: an #EekModifierType which @key triggers
 * @key: an #EekKey
 *
 * Entry which associates modifier mask to a key.  This is returned by
 * eek_keyboard_get_locked_keys().
 */
struct _EekModifierKey {
    /*< public >*/
    EekModifierType modifiers;
    EekKey *key;
};
typedef struct _EekModifierKey EekModifierKey;


EekKeyboard        *eek_keyboard_new (EekboardContextService *manager,
                                      EekLayout          *layout,
                                      gdouble             initial_width,
                                      gdouble             initial_height);
GType               eek_keyboard_get_type
                                     (void) G_GNUC_CONST;
EekLayout          *eek_keyboard_get_layout
                                     (EekKeyboard        *keyboard);
void                eek_keyboard_get_size
                                     (EekKeyboard        *keyboard,
                                      gdouble            *width,
                                      gdouble            *height);
void                eek_keyboard_set_size
                                     (EekKeyboard        *keyboard,
                                      gdouble             width,
                                      gdouble             height);

void                eek_keyboard_set_modifier_behavior
                                     (EekKeyboard        *keyboard,
                                      EekModifierBehavior modifier_behavior);
EekModifierBehavior eek_keyboard_get_modifier_behavior
                                     (EekKeyboard        *keyboard);
void                eek_keyboard_set_modifiers
                                     (EekKeyboard        *keyboard,
                                      EekModifierType     modifiers);
EekModifierType     eek_keyboard_get_modifiers
                                     (EekKeyboard        *keyboard);

EekSection         *eek_keyboard_create_section
                                     (EekKeyboard        *keyboard);

EekKey             *eek_keyboard_find_key_by_name
                                     (EekKeyboard        *keyboard,
                                      const gchar        *name);

guint               eek_keyboard_add_outline
                                     (EekKeyboard        *keyboard,
                                      EekOutline         *outline);

EekOutline         *eek_keyboard_get_outline
                                     (EekKeyboard        *keyboard,
                                      guint               oref);
gsize               eek_keyboard_get_n_outlines
                                     (EekKeyboard        *keyboard);

void                eek_keyboard_set_num_lock_mask
                                     (EekKeyboard        *keyboard,
                                      EekModifierType     num_lock_mask);
EekModifierType     eek_keyboard_get_num_lock_mask
                                     (EekKeyboard        *keyboard);

void                eek_keyboard_set_alt_gr_mask
                                     (EekKeyboard        *keyboard,
                                      EekModifierType     alt_gr_mask);
EekModifierType     eek_keyboard_get_alt_gr_mask
                                     (EekKeyboard        *keyboard);

GList              *eek_keyboard_get_pressed_keys
                                     (EekKeyboard        *keyboard);
GList              *eek_keyboard_get_locked_keys
                                     (EekKeyboard        *keyboard);

EekModifierKey     *eek_modifier_key_copy
                                     (EekModifierKey     *modkey);
void                eek_modifier_key_free
                                     (EekModifierKey      *modkey);

void eek_keyboard_press_key(EekKeyboard *keyboard, EekKey *key, guint32 timestamp);
void eek_keyboard_release_key(EekKeyboard *keyboard, EekKey *key, guint32 timestamp);

G_END_DECLS
#endif  /* EEK_KEYBOARD_H */
