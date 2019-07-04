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

#ifndef EEK_KEYSYM_H
#define EEK_KEYSYM_H 1

#include <X11/XKBlib.h>
#include "eek-symbol.h"

G_BEGIN_DECLS

/**
 * EEK_INVALID_KEYSYM:
 *
 * Pseudo keysym used for error reporting.
 */
#define EEK_INVALID_KEYSYM (0)

#define EEK_TYPE_KEYSYM (eek_keysym_get_type())
G_DECLARE_DERIVABLE_TYPE (EekKeysym, eek_keysym, EEK, KEYSYM, EekSymbol)

struct _EekKeysymClass {
    /*< private >*/
    EekSymbolClass parent_class;
};

GType      eek_keysym_get_type          (void) G_GNUC_CONST;
EekKeysym *eek_keysym_new               (guint           xkeysym);
guint      eek_keysym_get_xkeysym       (EekKeysym      *keysym);

EekKeysym *eek_keysym_new_from_name     (const gchar    *name);
EekKeysym *eek_keysym_new_with_modifier (guint           xkeysym,
                                         EekModifierType modifier_mask);

G_END_DECLS

#endif  /* EEK_KEYSYM_H */
