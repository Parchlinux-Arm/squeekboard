/* 
 * Copyright (C) 2011 Daiki Ueno <ueno@unixuser.org>
 * Copyright (C) 2011 Red Hat, Inc.
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

#ifndef EEK_RENDERER_H
#define EEK_RENDERER_H 1

#include <gtk/gtk.h>
#include <pango/pangocairo.h>

#include "eek-keyboard.h"
#include "eek-types.h"

G_BEGIN_DECLS

#define EEK_TYPE_RENDERER (eek_renderer_get_type())
G_DECLARE_DERIVABLE_TYPE (EekRenderer, eek_renderer, EEK, RENDERER, GObject)

struct _EekRendererClass
{
    GObjectClass parent_class;

    void             (* render_key_outline) (EekRenderer *self,
                                             cairo_t     *cr,
                                             EekKeyboard *view,
                                             EekKey      *key,
                                             gdouble      scale,
                                             gboolean     rotate);

    void             (* render_key)         (EekRenderer *self,
                                             cairo_t     *cr,
                                             EekKeyboard *view,
                                             EekKey      *key,
                                             gdouble      scale,
                                             gboolean     rotate);

    void             (* render_keyboard)    (EekRenderer *self,
                                             cairo_t     *cr);

    cairo_surface_t *(* get_icon_surface)   (EekRenderer *self,
                                             const gchar *icon_name,
                                             gint         size,
                                             gint         scale);

    /*< private >*/
    /* padding */
    gpointer pdummy[23];
};

GType            eek_renderer_get_type         (void) G_GNUC_CONST;
EekRenderer     *eek_renderer_new              (LevelKeyboard     *keyboard,
                                                PangoContext    *pcontext,
                                                GtkStyleContext *scontext);
void             eek_renderer_set_allocation_size
                                               (EekRenderer     *renderer,
                                                gdouble          width,
                                                gdouble          height);
void             eek_renderer_get_size         (EekRenderer     *renderer,
                                                gdouble         *width,
                                                gdouble         *height);
void             eek_renderer_get_key_bounds   (EekRenderer     *renderer, EekKeyboard *view,
                                                EekKey          *key,
                                                EekBounds       *bounds,
                                                gboolean         rotate);

gdouble          eek_renderer_get_scale        (EekRenderer     *renderer);
void             eek_renderer_set_scale_factor (EekRenderer     *renderer,
                                                gint             scale);

PangoLayout     *eek_renderer_create_pango_layout
                                               (EekRenderer     *renderer);
void             eek_renderer_render_key_label (EekRenderer     *renderer,
                                                PangoLayout     *layout,
                                                EekKey          *key);

void             eek_renderer_render_key_outline
                                               (EekRenderer     *renderer,
                                                cairo_t         *cr, EekKeyboard *view,
                                                EekKey          *key,
                                                gdouble          scale,
                                                gboolean         rotate);

void             eek_renderer_render_key       (EekRenderer     *renderer,
                                                cairo_t         *cr,
                                                EekKeyboard *view,
                                                EekKey          *key,
                                                gdouble          scale,
                                                gboolean         rotate);

cairo_surface_t *eek_renderer_get_icon_surface (EekRenderer     *renderer,
                                                const gchar     *icon_name,
                                                gint             size,
                                                gint             scale);

void             eek_renderer_render_keyboard  (EekRenderer     *renderer,
                                                cairo_t         *cr);

void             eek_renderer_set_default_foreground_color
                                               (EekRenderer     *renderer,
                                                const EekColor  *color);
void             eek_renderer_set_default_background_color
                                               (EekRenderer     *renderer,
                                                const EekColor  *color);
void             eek_renderer_get_foreground_color
                                               (EekRenderer     *renderer,
                                                GtkStyleContext *context,
                                                EekColor        *color);
void             eek_renderer_set_border_width (EekRenderer     *renderer,
                                                gdouble          border_width);
EekKey          *eek_renderer_find_key_by_position
                                               (EekRenderer     *renderer, EekKeyboard *view,
                                                gdouble          x,
                                                gdouble          y);
void             eek_renderer_apply_transformation_for_key
                                               (EekRenderer     *renderer,
                                                cairo_t         *cr, EekKeyboard *view,
                                                EekKey          *key,
                                                gdouble          scale,
                                                gboolean         rotate);

G_END_DECLS
#endif  /* EEK_RENDERER_H */
