/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Authors: Benjamin Otte <otte@gnome.org>
 *          Guido Günther <agx@sigxcpu.org>
 *
 * Based on gtk-gst-paintable from GTK:
 * Copyright © 2018 Benjamin Otte
 */

#pragma once

#include <gdk/gdk.h>

G_BEGIN_DECLS

#define LIVI_TYPE_GST_PAINTABLE (livi_gst_paintable_get_type ())

G_DECLARE_FINAL_TYPE (LiviGstPaintable, livi_gst_paintable, LIVI, GST_PAINTABLE, GObject)

GdkPaintable *  livi_gst_paintable_new                   (void);

void livi_gst_paintable_realize               (LiviGstPaintable *self,
                                               GdkSurface       *surface);
void livi_gst_paintable_unrealize             (LiviGstPaintable *self,
                                               GdkSurface       *surface);
void livi_gst_paintable_queue_set_texture     (LiviGstPaintable *self,
                                               GdkTexture       *texture,
                                               double            pixel_aspect_ratio);

G_END_DECLS
