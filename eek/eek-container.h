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

#ifndef EEK_CONTAINER_H
#define EEK_CONTAINER_H 1

#include "eek-element.h"

G_BEGIN_DECLS

#define EEK_TYPE_CONTAINER (eek_container_get_type())
G_DECLARE_DERIVABLE_TYPE (EekContainer, eek_container, EEK, CONTAINER, EekElement)

/**
 * EekCallback:
 * @element: an #EekElement
 * @user_data: user-supplied data
 *
 * The type of the callback function used for iterating over the
 * children of a container, see eek_container_foreach_child().
 */
typedef void (*EekCallback) (EekElement *element, gpointer user_data);
typedef gint (*EekCompareFunc) (EekElement *element, gpointer user_data);

/**
 * EekContainerClass:
 * @foreach_child: virtual function for iterating over the container's children
 * @find: virtual function for looking up a child
 * @child_added: class handler for #EekContainer::child-added
 * @child_removed: class handler for #EekContainer::child-added
 */
struct _EekContainerClass
{
    /*< private >*/
    EekElementClass parent_class;

    void        (* add_child)      (EekContainer      *self,
                                    EekElement        *element);

    void        (* remove_child)   (EekContainer      *self,
                                    EekElement        *element);

    /*< public >*/
    void        (* foreach_child)  (EekContainer      *self,
                                    EekCallback        callback,
                                    gpointer           user_data);
    EekElement *(* find)           (EekContainer      *self,
                                    EekCompareFunc     func,
                                    gpointer           data);

    /* signals */
    void        (* child_added)    (EekContainer      *self,
                                    EekElement        *element);
    void        (* child_removed)  (EekContainer      *self,
                                    EekElement        *element);
    /*< private >*/
    /* padding */
    gpointer pdummy[24];
};

GType       eek_container_get_type      (void) G_GNUC_CONST;

void        eek_container_foreach_child (EekContainer  *container,
                                         EekCallback    callback,
                                         gpointer       user_data);
EekElement *eek_container_find          (EekContainer  *container,
                                         EekCompareFunc func,
                                         gpointer       user_data);
void        eek_container_add_child     (EekContainer  *container,
                                         EekElement    *element);

G_END_DECLS
#endif  /* EEK_CONTAINER_H */
