/*
 * Copyright (C) 2018 Purism SPC
 *
 * SPDX-License-Identifier: GPL-3.0+
 */


/*

WARNING: this file is taken directly from phosh, with no modificaions apart from this message. Please update phosh instead of changing this file. Please copy the file back here afterwards, with the same notice.

*/

#pragma once

#include <gtk/gtk.h>
/* TODO: We use the enum constants from here, use glib-mkenums */
#include "wlr-layer-shell-unstable-v1-client-protocol.h"

#define PHOSH_TYPE_LAYER_SURFACE                 (phosh_layer_surface_get_type ())

G_DECLARE_DERIVABLE_TYPE (PhoshLayerSurface, phosh_layer_surface, PHOSH, LAYER_SURFACE, GtkWindow)

/**
 * PhoshLayerSurfaceClass
 * @parent_class: The parent class
 */
struct _PhoshLayerSurfaceClass
{
  GtkWindowClass parent_class;

  /* Signals
   */
  void (*configured)   (PhoshLayerSurface    *self);
};

GtkWidget *phosh_layer_surface_new (gpointer layer_shell,
                                    gpointer wl_output);
struct     zwlr_layer_surface_v1 *phosh_layer_surface_get_layer_surface(PhoshLayerSurface *self);
struct     wl_surface            *phosh_layer_surface_get_wl_surface(PhoshLayerSurface *self);
void                              phosh_layer_surface_set_size(PhoshLayerSurface *self,
                                                               gint width,
                                                               gint height);
void                              phosh_layer_surface_set_margins(PhoshLayerSurface *self,
                                                                  gint top,
                                                                  gint right,
                                                                  gint bottom,
                                                                  gint left);
void                              phosh_layer_surface_set_exclusive_zone(PhoshLayerSurface *self,
                                                                         gint zone);
void                              phosh_layer_surface_set_kbd_interactivity(PhoshLayerSurface *self,
                                                                            gboolean interactivity);
void                              phosh_layer_surface_wl_surface_commit (PhoshLayerSurface *self);
