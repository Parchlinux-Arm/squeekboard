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

#include "config.h"

#include <math.h>
#include <string.h>
#include <gdk-pixbuf/gdk-pixbuf.h>
#include <gtk/gtk.h>

#include "eek-key.h"
#include "eek-section.h"
#include "eek-renderer.h"

enum {
    PROP_0,
    PROP_KEYBOARD,
    PROP_PCONTEXT,
    PROP_LAST
};

typedef struct _EekRendererPrivate
{
    EekKeyboard *keyboard;
    PangoContext *pcontext;

    EekColor default_foreground_color;
    EekColor default_background_color;
    gdouble border_width;

    gdouble allocation_width;
    gdouble allocation_height;
    gdouble scale;
    gint scale_factor; /* the outputs scale factor */
    gint origin_x;
    gint origin_y;

    PangoFontDescription *ascii_font;
    PangoFontDescription *font;
    GHashTable *outline_surface_cache;
    GHashTable *active_outline_surface_cache;
    GHashTable *icons;
    cairo_surface_t *keyboard_surface;
    gulong symbol_index_changed_handler;

    EekTheme *theme;
} EekRendererPrivate;

G_DEFINE_TYPE_WITH_PRIVATE (EekRenderer, eek_renderer, G_TYPE_OBJECT)

static const EekColor DEFAULT_FOREGROUND_COLOR = {0.3, 0.3, 0.3, 1.0};
static const EekColor DEFAULT_BACKGROUND_COLOR = {1.0, 1.0, 1.0, 1.0};

struct _TextProperty {
    gint category;
    gboolean ascii;
    gdouble scale;
    gboolean ellipses;
};
typedef struct _TextProperty TextProperty;

/* eek-keyboard-drawing.c */
extern void _eek_rounded_polygon               (cairo_t     *cr,
                                                gdouble      radius,
                                                EekPoint    *points,
                                                guint         num_points);

static void eek_renderer_real_render_key_label (EekRenderer *self,
                                                PangoLayout *layout,
                                                EekKey      *key);

static void invalidate                         (EekRenderer *renderer);
static void render_key                         (EekRenderer *self,
                                                cairo_t     *cr,
                                                EekKey      *key,
                                                gboolean     active);
static void on_symbol_index_changed            (EekKeyboard *keyboard,
                                                gint         group,
                                                gint         level,
                                                gpointer     user_data);

struct _CreateKeyboardSurfaceCallbackData {
    cairo_t *cr;
    EekRenderer *renderer;
};
typedef struct _CreateKeyboardSurfaceCallbackData CreateKeyboardSurfaceCallbackData;

static void
create_keyboard_surface_key_callback (EekElement *element,
                                      gpointer    user_data)
{
    CreateKeyboardSurfaceCallbackData *data = user_data;
    EekBounds bounds;

    cairo_save (data->cr);

    eek_element_get_bounds (element, &bounds);
    cairo_translate (data->cr, bounds.x, bounds.y);
    cairo_rectangle (data->cr,
                     0.0,
                     0.0,
                     bounds.width + 100,
                     bounds.height + 100);
    cairo_clip (data->cr);
    render_key (data->renderer, data->cr, EEK_KEY(element), FALSE);

    cairo_restore (data->cr);
}

static void
create_keyboard_surface_section_callback (EekElement *element,
                                          gpointer    user_data)
{
    CreateKeyboardSurfaceCallbackData *data = user_data;
    EekBounds bounds;
    gint angle;

    cairo_save (data->cr);

    eek_element_get_bounds (element, &bounds);
    cairo_translate (data->cr, bounds.x, bounds.y);

    angle = eek_section_get_angle (EEK_SECTION(element));
    cairo_rotate (data->cr, angle * G_PI / 180);

    eek_container_foreach_child (EEK_CONTAINER(element),
                                 create_keyboard_surface_key_callback,
                                 data);

    cairo_restore (data->cr);
}

void
render_keyboard_surface (EekRenderer *renderer)
{
    EekRendererPrivate *priv = eek_renderer_get_instance_private (renderer);
    EekBounds bounds;
    CreateKeyboardSurfaceCallbackData data;
    EekColor foreground, background;

    eek_renderer_get_foreground_color (renderer,
                                       EEK_ELEMENT(priv->keyboard),
                                       &foreground);
    eek_renderer_get_background_color (renderer,
                                       EEK_ELEMENT(priv->keyboard),
                                       &background);

    eek_element_get_bounds (EEK_ELEMENT(priv->keyboard), &bounds);

    data.cr = cairo_create (priv->keyboard_surface);
    data.renderer = renderer;

    cairo_save (data.cr);
    cairo_scale (data.cr, priv->scale, priv->scale);
    cairo_translate (data.cr, bounds.x, bounds.y);

    /* blank background */
    cairo_set_source_rgba (data.cr,
                           background.red,
                           background.green,
                           background.blue,
                           background.alpha);
    cairo_paint (data.cr);

    cairo_set_source_rgba (data.cr,
                           foreground.red,
                           foreground.green,
                           foreground.blue,
                           foreground.alpha);

    /* draw sections */
    eek_container_foreach_child (EEK_CONTAINER(priv->keyboard),
                                 create_keyboard_surface_section_callback,
                                 &data);
    cairo_restore (data.cr);

    cairo_destroy (data.cr);
}

static void
render_key_outline (EekRenderer *renderer,
                    cairo_t     *cr,
                    EekKey      *key,
                    gboolean     active)
{
    EekRendererPrivate *priv = eek_renderer_get_instance_private (renderer);
    EekOutline *outline;
    EekBounds bounds;
    guint oref;
    EekThemeNode *theme_node;
    EekColor foreground, background, gradient_start, gradient_end, border_color;
    EekGradientType gradient_type;
    gint border_width;
    gint border_radius;

    oref = eek_key_get_oref (key);
    outline = eek_keyboard_get_outline (priv->keyboard, oref);
    if (outline == NULL)
        return;

    theme_node = g_object_get_data (G_OBJECT(key),
                                    active ?
                                    "theme-node-pressed" :
                                    "theme-node");
    if (theme_node) {
        eek_theme_node_get_foreground_color (theme_node, &foreground);
        eek_theme_node_get_background_color (theme_node, &background);
        eek_theme_node_get_background_gradient (theme_node,
                                                &gradient_type,
                                                &gradient_start,
                                                &gradient_end);
        border_width = eek_theme_node_get_border_width (theme_node,
                                                        EEK_SIDE_TOP);
        border_radius = eek_theme_node_get_border_radius (theme_node,
                                                          EEK_CORNER_TOPLEFT);
        eek_theme_node_get_border_color (theme_node, EEK_SIDE_TOP,
                                         &border_color);
    } else {
        foreground = priv->default_foreground_color;
        background = priv->default_background_color;
        gradient_type = EEK_GRADIENT_NONE;
        border_width = (gint)round(priv->border_width);
        border_radius = -1;
        border_color.red = ABS(background.red - foreground.red) * 0.7;
        border_color.green = ABS(background.green - foreground.green) * 0.7;
        border_color.blue = ABS(background.blue - foreground.blue) * 0.7;
        border_color.alpha = foreground.alpha;
    }

    eek_element_get_bounds(EEK_ELEMENT(key), &bounds);
    outline = eek_outline_copy (outline);

    cairo_translate (cr, border_width, border_width);

    if (gradient_type != EEK_GRADIENT_NONE) {
        cairo_pattern_t *pat;
        gdouble cx, cy;

        switch (gradient_type) {
        case EEK_GRADIENT_VERTICAL:
            pat = cairo_pattern_create_linear (0.0,
                                               0.0,
                                               0.0,
                                               bounds.height);
            break;
        case EEK_GRADIENT_HORIZONTAL:
            pat = cairo_pattern_create_linear (0.0,
                                               0.0,
                                               bounds.width,
                                               0.0);
            break;
        case EEK_GRADIENT_RADIAL:
            cx = bounds.width / 2;
            cy = bounds.height / 2;
            pat = cairo_pattern_create_radial (cx,
                                               cy,
                                               0,
                                               cx,
                                               cy,
                                               MIN(cx, cy));
            break;
        default:
            g_assert_not_reached ();
            break;
        }

        cairo_pattern_add_color_stop_rgba (pat,
                                           1,
                                           gradient_start.red * 0.5,
                                           gradient_start.green * 0.5,
                                           gradient_start.blue * 0.5,
                                           gradient_start.alpha);
        cairo_pattern_add_color_stop_rgba (pat,
                                           0,
                                           gradient_end.red,
                                           gradient_end.green,
                                           gradient_end.blue,
                                           gradient_end.alpha);
        cairo_set_source (cr, pat);
        cairo_pattern_destroy (pat);
    } else {
        cairo_set_source_rgba (cr,
                               background.red,
                               background.green,
                               background.blue,
                               background.alpha);
    }

    _eek_rounded_polygon (cr,
                          border_radius >= 0 ? border_radius : outline->corner_radius,
                          outline->points,
                          outline->num_points);
    cairo_fill (cr);

    /* paint the border */
    cairo_set_line_width (cr, border_width);
    cairo_set_line_join (cr, CAIRO_LINE_JOIN_ROUND);

    cairo_set_source_rgba (cr,
                           border_color.red,
                           border_color.green,
                           border_color.blue,
                           border_color.alpha);

    _eek_rounded_polygon (cr,
                          border_radius >= 0 ? border_radius : outline->corner_radius,
                          outline->points,
                          outline->num_points);
    cairo_stroke (cr);

    cairo_translate (cr, -border_width, -border_width);

    eek_outline_free (outline);
}

static void
render_key (EekRenderer *self,
            cairo_t     *cr,
            EekKey      *key,
            gboolean     active)
{
    EekRendererPrivate *priv = eek_renderer_get_instance_private (self);
    EekOutline *outline;
    cairo_surface_t *outline_surface;
    EekBounds bounds;
    guint oref;
    EekSymbol *symbol;
    GHashTable *outline_surface_cache;
    PangoLayout *layout;
    PangoRectangle extents = { 0, };
    EekColor foreground;

    if (!eek_key_has_label(key))
        return;

    oref = eek_key_get_oref (key);
    outline = eek_keyboard_get_outline (priv->keyboard, oref);
    if (outline == NULL)
        return;

    /* render outline */
    eek_element_get_bounds (EEK_ELEMENT(key), &bounds);

    if (active)
        outline_surface_cache = priv->active_outline_surface_cache;
    else
        outline_surface_cache = priv->outline_surface_cache;

    outline_surface = g_hash_table_lookup (outline_surface_cache, outline);
    if (!outline_surface) {
        cairo_t *cr;

        // Outline will be drawn on the outside of the button, so the
        // surface needs to be bigger than the button
        outline_surface =
            cairo_image_surface_create (CAIRO_FORMAT_ARGB32,
                                        (int)ceil(bounds.width) + 10,
                                        (int)ceil(bounds.height) + 10);
        cr = cairo_create (outline_surface);

        /* blank background */
        cairo_set_source_rgba (cr, 0.0, 0.0, 0.0, 0.0);
        cairo_paint (cr);

        cairo_save (cr);
        eek_renderer_apply_transformation_for_key (self, cr, key, 1.0, FALSE);
        render_key_outline (self, cr, key, active);
        cairo_restore (cr);

        cairo_destroy (cr);

        g_hash_table_insert (outline_surface_cache,
                             outline,
                             outline_surface);
    }

    cairo_set_source_surface (cr, outline_surface, 0.0, 0.0);
    cairo_paint (cr);

    eek_renderer_get_foreground_color (self, EEK_ELEMENT(key), &foreground);
    /* render icon (if any) */
    symbol = eek_key_get_symbol_with_fallback (key, 0, 0);
    if (!symbol)
        return;

    if (eek_symbol_get_icon_name (symbol)) {
        gint scale = priv->scale_factor;
        cairo_surface_t *icon_surface =
            eek_renderer_get_icon_surface (self,
                                           eek_symbol_get_icon_name (symbol),
                                           16,
                                           scale);
        if (icon_surface) {
            gint width = cairo_image_surface_get_width (icon_surface);
            gint height = cairo_image_surface_get_height (icon_surface);

            cairo_save (cr);
            cairo_translate (cr,
                             (bounds.width - width / scale) / 2,
                             (bounds.height - height / scale) / 2);
            cairo_rectangle (cr, 0, 0, width, height);
            cairo_clip (cr);
            /* Draw the shape of the icon using the foreground color */
            cairo_set_source_rgba (cr, foreground.red,
                                       foreground.green,
                                       foreground.blue,
                                       foreground.alpha);
            cairo_mask_surface (cr, icon_surface, 0.0, 0.0);
            cairo_fill (cr);

            cairo_restore (cr);
            return;
        }
    }

    /* render label */
    layout = pango_cairo_create_layout (cr);
    eek_renderer_render_key_label (self, layout, key);
    pango_layout_get_extents (layout, NULL, &extents);

    cairo_save (cr);
    cairo_move_to
        (cr,
         (bounds.width - extents.width / PANGO_SCALE) / 2,
         (bounds.height - extents.height / PANGO_SCALE) / 2);

    cairo_set_source_rgba (cr,
                           foreground.red,
                           foreground.green,
                           foreground.blue,
                           foreground.alpha);

    pango_cairo_show_layout (cr, layout);
    cairo_restore (cr);
    g_object_unref (layout);
}

void
eek_renderer_apply_transformation_for_key (EekRenderer *self,
                                           cairo_t     *cr,
                                           EekKey      *key,
                                           gdouble      scale,
                                           gboolean     rotate)
{
    EekElement *section;
    EekBounds bounds, rotated_bounds;
    gint angle;
    gdouble s;

    eek_renderer_get_key_bounds (self, key, &bounds, FALSE);
    eek_renderer_get_key_bounds (self, key, &rotated_bounds, TRUE);

    section = eek_element_get_parent (EEK_ELEMENT(key));
    angle = eek_section_get_angle (EEK_SECTION(section));

    cairo_scale (cr, scale, scale);
    if (rotate) {
        s = sin (angle * G_PI / 180);
        if (s < 0)
            cairo_translate (cr, 0, - bounds.width * s);
        else
            cairo_translate (cr, bounds.height * s, 0);
        cairo_rotate (cr, angle * G_PI / 180);
    }
}

static const TextProperty *
get_text_property_for_category (EekSymbolCategory category)
{
    static const TextProperty props[EEK_SYMBOL_CATEGORY_LAST] = {
        { EEK_SYMBOL_CATEGORY_LETTER, FALSE, 1.0, FALSE },
        { EEK_SYMBOL_CATEGORY_FUNCTION, TRUE, 0.5, FALSE },
        { EEK_SYMBOL_CATEGORY_KEYNAME, TRUE, 0.5, TRUE }
    };

    for (uint i = 0; i < G_N_ELEMENTS(props); i++)
        if (props[i].category == category)
            return &props[i];

    g_return_val_if_reached (NULL);
}

static void
eek_renderer_real_render_key_label (EekRenderer *self,
                                    PangoLayout *layout,
                                    EekKey      *key)
{
    EekRendererPrivate *priv = eek_renderer_get_instance_private (self);
    EekSymbol *symbol;
    EekSymbolCategory category;
    const gchar *label;
    EekBounds bounds;
    const TextProperty *prop;
    PangoFontDescription *font;
    PangoLayoutLine *line;
    gdouble scale;

    symbol = eek_key_get_symbol_with_fallback (key, 0, 0);
    if (!symbol)
        return;

    label = eek_symbol_get_label (symbol);
    if (!label)
        return;

    if (!priv->font) {
        const PangoFontDescription *base_font;
        gdouble ascii_size, size;
        EekThemeNode *theme_node;

        theme_node = g_object_get_data (G_OBJECT(key), "theme-node");
        if (theme_node)
            base_font = eek_theme_node_get_font (theme_node);
        else
            base_font = pango_context_get_font_description (priv->pcontext);
        // FIXME: Base font size on the same size unit used for button sizing,
        // and make the default about 1/3 of the current row height
        ascii_size = 30000.0;
        priv->ascii_font = pango_font_description_copy (base_font);
        pango_font_description_set_size (priv->ascii_font,
                                         (gint)round(ascii_size));

        size = 30000.0;
        priv->font = pango_font_description_copy (base_font);
        pango_font_description_set_size (priv->font, (gint)round(size * 0.6));
    }

    eek_element_get_bounds (EEK_ELEMENT(key), &bounds);
    scale = MIN((bounds.width - priv->border_width) / bounds.width,
                (bounds.height - priv->border_width) / bounds.height);

    category = eek_symbol_get_category (symbol);
    prop = get_text_property_for_category (category);

    font = pango_font_description_copy (prop->ascii ?
                                        priv->ascii_font :
                                        priv->font);
    pango_font_description_set_size (font,
                                     pango_font_description_get_size (font) *
                                     (gint)round(prop->scale * scale));
    pango_layout_set_font_description (layout, font);
    pango_font_description_free (font);

    pango_layout_set_text (layout, label, -1);
    line = pango_layout_get_line (layout, 0);
    if (line->resolved_dir == PANGO_DIRECTION_RTL)
        pango_layout_set_alignment (layout, PANGO_ALIGN_RIGHT);
    pango_layout_set_width (layout,
                            PANGO_SCALE * bounds.width * scale);
    if (prop->ellipses)
        pango_layout_set_ellipsize (layout, PANGO_ELLIPSIZE_END);
}

static void
eek_renderer_real_render_key_outline (EekRenderer *self,
                                      cairo_t     *cr,
                                      EekKey      *key,
                                      gdouble      scale,
                                      gboolean     rotate)
{
    cairo_save (cr);
    eek_renderer_apply_transformation_for_key (self, cr, key, scale, rotate);
    render_key_outline (self, cr, key, eek_key_is_pressed (key) || eek_key_is_locked (key));
    cairo_restore (cr);
}

static void
eek_renderer_real_render_key (EekRenderer *self,
                              cairo_t     *cr,
                              EekKey      *key,
                              gdouble      scale,
                              gboolean     rotate)
{
    EekRendererPrivate *priv = eek_renderer_get_instance_private (self);

    cairo_save (cr);
    cairo_translate (cr, priv->origin_x, priv->origin_y);
    eek_renderer_apply_transformation_for_key (self, cr, key, scale, rotate);
    render_key (self, cr, key, eek_key_is_pressed (key) || eek_key_is_locked (key));
    cairo_restore (cr);
}

static void
eek_renderer_real_render_keyboard (EekRenderer *self,
                                   cairo_t     *cr)
{
    EekRendererPrivate *priv = eek_renderer_get_instance_private (self);
    cairo_pattern_t *source;

    g_return_if_fail (priv->keyboard);
    g_return_if_fail (priv->allocation_width > 0.0);
    g_return_if_fail (priv->allocation_height > 0.0);

    cairo_save (cr);

    cairo_translate (cr, priv->origin_x, priv->origin_y);

    if (priv->keyboard_surface)
        cairo_surface_destroy (priv->keyboard_surface);

    priv->keyboard_surface = cairo_surface_create_for_rectangle (
        cairo_get_target (cr), 0, 0,
        priv->allocation_width, priv->allocation_height);

    render_keyboard_surface (self);

    cairo_set_source_surface (cr, priv->keyboard_surface, 0.0, 0.0);
    source = cairo_get_source (cr);
    cairo_pattern_set_extend (source, CAIRO_EXTEND_PAD);
    cairo_paint (cr);

    cairo_restore (cr);
}

static void
eek_renderer_set_property (GObject      *object,
                           guint         prop_id,
                           const GValue *value,
                           GParamSpec   *pspec)
{
    EekRendererPrivate *priv = eek_renderer_get_instance_private (
		    EEK_RENDERER(object));

    switch (prop_id) {
    case PROP_KEYBOARD:
        priv->keyboard = g_value_get_object (value);
        g_object_ref (priv->keyboard);

        priv->symbol_index_changed_handler =
            g_signal_connect (priv->keyboard, "symbol-index-changed",
                              G_CALLBACK(on_symbol_index_changed),
                              object);
        break;
    case PROP_PCONTEXT:
        priv->pcontext = g_value_get_object (value);
        g_object_ref (priv->pcontext);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
        break;
    }
}

static void
eek_renderer_get_property (GObject    *object,
                           guint       prop_id,
                           GValue     *value,
                           GParamSpec *pspec)
{
    EekRendererPrivate *priv = eek_renderer_get_instance_private (
		    EEK_RENDERER(object));

    switch (prop_id) {
    case PROP_KEYBOARD:
        g_value_set_object (value, priv->keyboard);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
        break;
    }
}

static void
eek_renderer_dispose (GObject *object)
{
    EekRenderer        *self = EEK_RENDERER (object);
    EekRendererPrivate *priv = eek_renderer_get_instance_private (self);

    if (priv->keyboard) {
        if (g_signal_handler_is_connected (priv->keyboard,
                                           priv->symbol_index_changed_handler))
            g_signal_handler_disconnect (priv->keyboard,
                                         priv->symbol_index_changed_handler);
        g_object_unref (priv->keyboard);
        priv->keyboard = NULL;
    }
    if (priv->pcontext) {
        g_object_unref (priv->pcontext);
        priv->pcontext = NULL;
    }

    g_clear_pointer (&priv->icons, g_hash_table_destroy);

    /* this will release all allocated surfaces and font if any */
    invalidate (EEK_RENDERER(object));

    G_OBJECT_CLASS (eek_renderer_parent_class)->dispose (object);
}

static void
eek_renderer_finalize (GObject *object)
{
    EekRenderer        *self = EEK_RENDERER(object);
    EekRendererPrivate *priv = eek_renderer_get_instance_private (self);

    g_hash_table_destroy (priv->outline_surface_cache);
    g_hash_table_destroy (priv->active_outline_surface_cache);
    pango_font_description_free (priv->ascii_font);
    pango_font_description_free (priv->font);
    G_OBJECT_CLASS (eek_renderer_parent_class)->finalize (object);
}

static void
eek_renderer_class_init (EekRendererClass *klass)
{
    GObjectClass      *gobject_class = G_OBJECT_CLASS (klass);
    GParamSpec        *pspec;

    klass->render_key_label = eek_renderer_real_render_key_label;
    klass->render_key_outline = eek_renderer_real_render_key_outline;
    klass->render_key = eek_renderer_real_render_key;
    klass->render_keyboard = eek_renderer_real_render_keyboard;

    gobject_class->set_property = eek_renderer_set_property;
    gobject_class->get_property = eek_renderer_get_property;
    gobject_class->dispose = eek_renderer_dispose;
    gobject_class->finalize = eek_renderer_finalize;

    pspec = g_param_spec_object ("keyboard",
                                 "Keyboard",
                                 "Keyboard",
                                 EEK_TYPE_KEYBOARD,
                                 G_PARAM_CONSTRUCT_ONLY | G_PARAM_READWRITE);
    g_object_class_install_property (gobject_class,
                                     PROP_KEYBOARD,
                                     pspec);

    pspec = g_param_spec_object ("pango-context",
                                 "Pango Context",
                                 "Pango Context",
                                 PANGO_TYPE_CONTEXT,
                                 G_PARAM_CONSTRUCT_ONLY | G_PARAM_WRITABLE);
    g_object_class_install_property (gobject_class,
                                     PROP_PCONTEXT,
                                     pspec);
}

static void
eek_renderer_init (EekRenderer *self)
{
    EekRendererPrivate *priv = eek_renderer_get_instance_private (self);

    priv->keyboard = NULL;
    priv->pcontext = NULL;
    priv->default_foreground_color = DEFAULT_FOREGROUND_COLOR;
    priv->default_background_color = DEFAULT_BACKGROUND_COLOR;
    priv->border_width = 1.0;
    priv->allocation_width = 0.0;
    priv->allocation_height = 0.0;
    priv->scale = 1.0;
    priv->scale_factor = 1;
    priv->font = NULL;
    priv->outline_surface_cache =
        g_hash_table_new_full (g_direct_hash,
                               g_direct_equal,
                               NULL,
                               (GDestroyNotify)cairo_surface_destroy);
    priv->active_outline_surface_cache =
        g_hash_table_new_full (g_direct_hash,
                               g_direct_equal,
                               NULL,
                               (GDestroyNotify)cairo_surface_destroy);
    priv->keyboard_surface = NULL;
    priv->symbol_index_changed_handler = 0;

    GtkIconTheme *theme = gtk_icon_theme_get_default ();

    gtk_icon_theme_add_resource_path (theme, "/sm/puri/squeekboard/icons");
    priv->icons = g_hash_table_new_full (g_str_hash,
                                         g_str_equal,
                                         g_free,
                                         (GDestroyNotify)cairo_surface_destroy);
}

static void
invalidate (EekRenderer *renderer)
{
    EekRendererPrivate *priv = eek_renderer_get_instance_private (renderer);

    if (priv->outline_surface_cache)
        g_hash_table_remove_all (priv->outline_surface_cache);

    if (priv->active_outline_surface_cache)
        g_hash_table_remove_all (priv->active_outline_surface_cache);

    if (priv->keyboard_surface) {
        cairo_surface_destroy (priv->keyboard_surface);
        priv->keyboard_surface = NULL;
    }
}

static void
on_symbol_index_changed (EekKeyboard *keyboard,
                         gint         group,
                         gint         level,
                         gpointer     user_data)
{
    EekRenderer *renderer = user_data;
    invalidate (renderer);
}

EekRenderer *
eek_renderer_new (EekKeyboard  *keyboard,
                  PangoContext *pcontext)
{
    return g_object_new (EEK_TYPE_RENDERER,
                         "keyboard", keyboard,
                         "pango-context", pcontext,
                         NULL);
}

void
eek_renderer_set_allocation_size (EekRenderer *renderer,
                                  gdouble      width,
                                  gdouble      height)
{
    EekBounds bounds;
    gdouble scale;

    g_return_if_fail (EEK_IS_RENDERER(renderer));
    g_return_if_fail (width > 0.0 && height > 0.0);

    EekRendererPrivate *priv = eek_renderer_get_instance_private (renderer);

    priv->allocation_width = width;
    priv->allocation_height = height;

    /* Calculate a scale factor to use when rendering the keyboard into the
       available space. */
    eek_element_get_bounds (EEK_ELEMENT(priv->keyboard), &bounds);

    gdouble w = (bounds.x * 2) + bounds.width;
    gdouble h = (bounds.y * 2) + bounds.height;

    scale = MIN(width / w, height / h);

    if (scale != priv->scale) {
        priv->scale = scale;
        priv->origin_x = 0;
        priv->origin_y = 0;
        invalidate (renderer);
    }
}

void
eek_renderer_get_size (EekRenderer *renderer,
                       gdouble     *width,
                       gdouble     *height)
{
    EekBounds bounds;

    g_return_if_fail (EEK_IS_RENDERER(renderer));

    EekRendererPrivate *priv = eek_renderer_get_instance_private (renderer);

    eek_element_get_bounds (EEK_ELEMENT(priv->keyboard), &bounds);
    if (width)
        *width = bounds.width;
    if (height)
        *height = bounds.height;
}

void
eek_renderer_get_key_bounds (EekRenderer *renderer,
                             EekKey      *key,
                             EekBounds   *bounds,
                             gboolean     rotate)
{
    EekElement *section;
    EekBounds section_bounds, keyboard_bounds;
    gint angle = 0;
    EekPoint points[4], min, max;

    g_return_if_fail (EEK_IS_RENDERER(renderer));
    g_return_if_fail (EEK_IS_KEY(key));
    g_return_if_fail (bounds != NULL);

    EekRendererPrivate *priv = eek_renderer_get_instance_private (renderer);

    section = eek_element_get_parent (EEK_ELEMENT(key));

    eek_element_get_bounds (EEK_ELEMENT(key), bounds);
    eek_element_get_bounds (section, &section_bounds);
    eek_element_get_bounds (EEK_ELEMENT(priv->keyboard),
                            &keyboard_bounds);

    if (!rotate) {
        bounds->x += keyboard_bounds.x + section_bounds.x;
        bounds->y += keyboard_bounds.y + section_bounds.y;
        return;
    }
    points[0].x = bounds->x;
    points[0].y = bounds->y;
    points[1].x = points[0].x + bounds->width;
    points[1].y = points[0].y;
    points[2].x = points[1].x;
    points[2].y = points[1].y + bounds->height;
    points[3].x = points[0].x;
    points[3].y = points[2].y;

    if (rotate)
        angle = eek_section_get_angle (EEK_SECTION(section));

    min = points[2];
    max = points[0];
    for (uint i = 0; i < G_N_ELEMENTS(points); i++) {
        eek_point_rotate (&points[i], angle);
        if (points[i].x < min.x)
            min.x = points[i].x;
        if (points[i].x > max.x)
            max.x = points[i].x;
        if (points[i].y < min.y)
            min.y = points[i].y;
        if (points[i].y > max.y)
            max.y = points[i].y;
    }
    bounds->x = keyboard_bounds.x + section_bounds.x + min.x;
    bounds->y = keyboard_bounds.y + section_bounds.y + min.y;
    bounds->width = (max.x - min.x);
    bounds->height = (max.y - min.y);
}

gdouble
eek_renderer_get_scale (EekRenderer *renderer)
{
    g_return_val_if_fail (EEK_IS_RENDERER(renderer), 0);

    EekRendererPrivate *priv = eek_renderer_get_instance_private (renderer);

    return priv->scale;
}

void
eek_renderer_set_scale_factor (EekRenderer *renderer, gint scale)
{
    g_return_if_fail (EEK_IS_RENDERER(renderer));

    EekRendererPrivate *priv = eek_renderer_get_instance_private (renderer);
    priv->scale_factor = scale;
}

PangoLayout *
eek_renderer_create_pango_layout (EekRenderer  *renderer)
{
    g_return_val_if_fail (EEK_IS_RENDERER(renderer), NULL);

    EekRendererPrivate *priv = eek_renderer_get_instance_private (renderer);

    return pango_layout_new (priv->pcontext);
}

void
eek_renderer_render_key_label (EekRenderer *renderer,
                               PangoLayout *layout,
                               EekKey      *key)
{
    g_return_if_fail (EEK_IS_RENDERER(renderer));
    g_return_if_fail (EEK_IS_KEY(key));

    EEK_RENDERER_GET_CLASS(renderer)->
        render_key_label (renderer, layout, key);
}

void
eek_renderer_render_key_outline (EekRenderer *renderer,
                                 cairo_t     *cr,
                                 EekKey      *key,
                                 gdouble      scale,
                                 gboolean     rotate)
{
    g_return_if_fail (EEK_IS_RENDERER(renderer));
    g_return_if_fail (EEK_IS_KEY(key));
    g_return_if_fail (scale >= 0.0);

    EEK_RENDERER_GET_CLASS(renderer)->render_key_outline (renderer,
                                                          cr,
                                                          key,
                                                          scale,
                                                          rotate);
}

cairo_surface_t *
eek_renderer_get_icon_surface (EekRenderer *renderer,
                               const gchar *icon_name,
                               gint size,
                               gint scale)
{
    GError *error = NULL;
    cairo_surface_t *surface;

    g_return_val_if_fail (EEK_IS_RENDERER(renderer), NULL);

    EekRendererPrivate *priv = eek_renderer_get_instance_private (renderer);

    surface = g_hash_table_lookup (priv->icons, icon_name);
    if (!surface) {
        surface = gtk_icon_theme_load_surface (gtk_icon_theme_get_default (),
                                               icon_name,
                                               size,
                                               scale,
                                               NULL,
                                               0,
                                               &error);
        g_hash_table_insert (priv->icons, g_strdup(icon_name), surface);
        if (surface == NULL) {
            g_warning ("can't get icon surface for %s: %s",
                       icon_name,
                       error->message);
            g_error_free (error);
            return NULL;
        }
    }
    return surface;
}

void
eek_renderer_render_key (EekRenderer *renderer,
                         cairo_t     *cr,
                         EekKey      *key,
                         gdouble      scale,
                         gboolean     rotate)
{
    g_return_if_fail (EEK_IS_RENDERER(renderer));
    g_return_if_fail (EEK_IS_KEY(key));
    g_return_if_fail (scale >= 0.0);

    EEK_RENDERER_GET_CLASS(renderer)->
        render_key (renderer, cr, key, scale, rotate);
}

void
eek_renderer_render_keyboard (EekRenderer *renderer,
                              cairo_t     *cr)
{
    g_return_if_fail (EEK_IS_RENDERER(renderer));
    EEK_RENDERER_GET_CLASS(renderer)->render_keyboard (renderer, cr);
}

void
eek_renderer_set_default_foreground_color (EekRenderer    *renderer,
                                           const EekColor *color)
{
    g_return_if_fail (EEK_IS_RENDERER(renderer));
    g_return_if_fail (color);

    EekRendererPrivate *priv = eek_renderer_get_instance_private (renderer);

    memcpy (&priv->default_foreground_color, color, sizeof(EekColor));
}

void
eek_renderer_set_default_background_color (EekRenderer    *renderer,
                                           const EekColor *color)
{
    g_return_if_fail (EEK_IS_RENDERER(renderer));
    g_return_if_fail (color);

    EekRendererPrivate *priv = eek_renderer_get_instance_private (renderer);

    memcpy (&priv->default_background_color, color, sizeof(EekColor));
}

void
eek_renderer_get_foreground_color (EekRenderer *renderer,
                                   EekElement  *element,
                                   EekColor    *color)
{
    EekThemeNode *theme_node;

    g_return_if_fail (EEK_IS_RENDERER(renderer));
    g_return_if_fail (color);

    EekRendererPrivate *priv = eek_renderer_get_instance_private (renderer);

    theme_node = g_object_get_data (G_OBJECT(element), "theme-node");
    if (theme_node)
        eek_theme_node_get_foreground_color (theme_node, color);
    else
        memcpy (color, &priv->default_foreground_color,
                sizeof(EekColor));
}

void
eek_renderer_get_background_color (EekRenderer *renderer,
                                   EekElement  *element,
                                   EekColor    *color)
{
    EekThemeNode *theme_node;

    g_return_if_fail (EEK_IS_RENDERER(renderer));
    g_return_if_fail (color);

    EekRendererPrivate *priv = eek_renderer_get_instance_private (renderer);

    theme_node = g_object_get_data (G_OBJECT(element), "theme-node");
    if (theme_node)
        eek_theme_node_get_background_color (theme_node, color);
    else
        memcpy (color, &priv->default_background_color,
                sizeof(EekColor));
}

void
eek_renderer_get_background_gradient (EekRenderer     *renderer,
                                      EekElement      *element,
                                      EekGradientType *type,
                                      EekColor        *start,
                                      EekColor        *end)
{
    EekThemeNode *theme_node;

    g_return_if_fail (EEK_IS_RENDERER(renderer));
    g_return_if_fail (EEK_IS_ELEMENT(element));
    g_return_if_fail (type);
    g_return_if_fail (start);
    g_return_if_fail (end);

    theme_node = g_object_get_data (G_OBJECT(element), "theme-node");
    if (theme_node)
        eek_theme_node_get_background_gradient (theme_node, type, start, end);
    else
        *type = EEK_GRADIENT_NONE;
}

struct _FindKeyByPositionCallbackData {
    EekPoint point;
    EekPoint origin;
    gint angle;
    EekKey *key;
    EekRenderer *renderer;
};
typedef struct _FindKeyByPositionCallbackData FindKeyByPositionCallbackData;

static gboolean
sign (EekPoint *p1, EekPoint *p2, EekPoint *p3)
{
    return (p1->x - p3->x) * (p2->y - p3->y) -
        (p2->x - p3->x) * (p1->y - p3->y);
}

static gint
find_key_by_position_key_callback (EekElement *element,
                                   gpointer user_data)
{
    FindKeyByPositionCallbackData *data = user_data;
    EekBounds bounds;
    EekPoint points[4];
    gboolean b1, b2, b3;

    eek_element_get_bounds (element, &bounds);

    points[0].x = bounds.x;
    points[0].y = bounds.y;
    points[1].x = points[0].x + bounds.width;
    points[1].y = points[0].y;
    points[2].x = points[1].x;
    points[2].y = points[1].y + bounds.height;
    points[3].x = points[0].x;
    points[3].y = points[2].y;

    for (uint i = 0; i < G_N_ELEMENTS(points); i++) {
        eek_point_rotate (&points[i], data->angle);
        points[i].x += data->origin.x;
        points[i].y += data->origin.y;
    }

    b1 = sign (&data->point, &points[0], &points[1]) < 0.0;
    b2 = sign (&data->point, &points[1], &points[2]) < 0.0;
    b3 = sign (&data->point, &points[2], &points[0]) < 0.0;

    if (b1 == b2 && b2 == b3) {
        data->key = EEK_KEY(element);
        return 0;
    }

    b1 = sign (&data->point, &points[2], &points[3]) < 0.0;
    b2 = sign (&data->point, &points[3], &points[0]) < 0.0;
    b3 = sign (&data->point, &points[0], &points[2]) < 0.0;

    if (b1 == b2 && b2 == b3) {
        data->key = EEK_KEY(element);
        return 0;
    }

    return -1;
}

static gint
find_key_by_position_section_callback (EekElement *element,
                                       gpointer user_data)
{
    FindKeyByPositionCallbackData *data = user_data;
    EekBounds bounds;
    EekPoint origin;

    origin = data->origin;
    eek_element_get_bounds (element, &bounds);
    data->origin.x += bounds.x;
    data->origin.y += bounds.y;
    data->angle = eek_section_get_angle (EEK_SECTION(element));

    eek_container_find (EEK_CONTAINER(element),
                        find_key_by_position_key_callback,
                        data);
    data->origin = origin;
    return data->key ? 0 : -1;
}

EekKey *
eek_renderer_find_key_by_position (EekRenderer *renderer,
                                   gdouble      x,
                                   gdouble      y)
{
    EekBounds bounds;
    FindKeyByPositionCallbackData data;

    g_return_val_if_fail (EEK_IS_RENDERER(renderer), NULL);

    EekRendererPrivate *priv = eek_renderer_get_instance_private (renderer);
    x /= priv->scale;
    y /= priv->scale;
    x -= priv->origin_x;
    y -= priv->origin_y;

    eek_element_get_bounds (EEK_ELEMENT(priv->keyboard), &bounds);

    if (x < bounds.x ||
        y < bounds.y ||
        x > bounds.width ||
        y > bounds.height)
        return NULL;

    data.point.x = x;
    data.point.y = y;
    data.origin.x = bounds.x;
    data.origin.y = bounds.y;
    data.key = NULL;
    data.renderer = renderer;

    eek_container_find (EEK_CONTAINER(priv->keyboard),
                        find_key_by_position_section_callback,
                        &data);
    return data.key;
}

struct _CreateThemeNodeData {
    EekThemeContext *context;
    EekThemeNode *parent;
    EekRenderer *renderer;
};
typedef struct _CreateThemeNodeData CreateThemeNodeData;

void
create_theme_node_key_callback (EekElement *element,
                                gpointer    user_data)
{
    CreateThemeNodeData *data = user_data;
    EekThemeNode *theme_node;

    EekRendererPrivate *priv = eek_renderer_get_instance_private (data->renderer);

    theme_node = eek_theme_node_new (data->context,
                                     data->parent,
                                     priv->theme,
                                     EEK_TYPE_KEY,
                                     eek_element_get_name (element),
                                     "key",
                                     NULL,
                                     NULL);
    g_object_set_data_full (G_OBJECT(element),
                            "theme-node",
                            theme_node,
                            (GDestroyNotify)g_object_unref);

    theme_node = eek_theme_node_new (data->context,
                                     data->parent,
                                     priv->theme,
                                     EEK_TYPE_KEY,
                                     eek_element_get_name (element),
                                     "key",
                                     "active",
                                     NULL);
    g_object_set_data_full (G_OBJECT(element),
                            "theme-node-pressed",
                            theme_node,
                            (GDestroyNotify)g_object_unref);
}

void
create_theme_node_section_callback (EekElement *element,
                                    gpointer    user_data)
{
    CreateThemeNodeData *data = user_data;
    EekThemeNode *theme_node, *parent;

    EekRendererPrivate *priv = eek_renderer_get_instance_private (data->renderer);

    theme_node = eek_theme_node_new (data->context,
                                     data->parent,
                                     priv->theme,
                                     EEK_TYPE_SECTION,
                                     eek_element_get_name (element),
                                     "section",
                                     NULL,
                                     NULL);
    g_object_set_data_full (G_OBJECT(element),
                            "theme-node",
                            theme_node,
                            (GDestroyNotify)g_object_unref);

    parent = data->parent;
    data->parent = theme_node;
    eek_container_foreach_child (EEK_CONTAINER(element),
                                 create_theme_node_key_callback,
                                 data);
    data->parent = parent;
}

void
eek_renderer_set_theme (EekRenderer *renderer,
                        EekTheme    *theme)
{
    EekThemeContext *theme_context;
    EekThemeNode *theme_node;
    CreateThemeNodeData data;

    g_return_if_fail (EEK_IS_RENDERER(renderer));
    g_return_if_fail (EEK_IS_THEME(theme));

    EekRendererPrivate *priv = eek_renderer_get_instance_private (renderer);

    g_return_if_fail (priv->keyboard);

    if (priv->theme)
        g_object_unref (priv->theme);
    priv->theme = g_object_ref (theme);

    theme_context = eek_theme_context_new ();
    theme_node = eek_theme_node_new (theme_context,
                                     NULL,
                                     priv->theme,
                                     EEK_TYPE_KEYBOARD,
                                     "keyboard",
                                     "keyboard",
                                     NULL,
                                     NULL);
    g_object_set_data_full (G_OBJECT(priv->keyboard),
                            "theme-node",
                            theme_node,
                            (GDestroyNotify)g_object_unref);

    data.context = theme_context;
    data.parent = theme_node;
    data.renderer = renderer;
    eek_container_foreach_child (EEK_CONTAINER(priv->keyboard),
                                 create_theme_node_section_callback,
                                 &data);
}
