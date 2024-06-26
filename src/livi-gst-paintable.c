/*
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 * Based on gtk-gst-paintable from GTK:
 * Copyright © 2018 Benjamin Otte
 *
 * Authors: Benjamin Otte <otte@gnome.org>
 *          Guido Günther <agx@sigxcpu.org>
 */

#include "livi-gst-paintable.h"
#include "livi-gst-sink.h"

#include <gtk/gtk.h>
#include <gst/play/gstplay-video-renderer.h>
#include <gsk/gl/gskglrenderer.h>

#include <math.h>

struct _LiviGstPaintable {
  GObject       parent_instance;

  GdkPaintable *image;
  double        pixel_aspect_ratio;
  graphene_rect_t viewport;

  GdkGLContext *context;
};

static void livi_gst_paintable_paintable_init (GdkPaintableInterface *iface);
static void livi_gst_paintable_video_renderer_init (GstPlayVideoRendererInterface *iface);

G_DEFINE_TYPE_WITH_CODE (LiviGstPaintable, livi_gst_paintable, G_TYPE_OBJECT,
                         G_IMPLEMENT_INTERFACE (GDK_TYPE_PAINTABLE,
                                                livi_gst_paintable_paintable_init)
                         G_IMPLEMENT_INTERFACE (GST_TYPE_PLAY_VIDEO_RENDERER,
                                                livi_gst_paintable_video_renderer_init));

static void
livi_gst_paintable_paintable_snapshot (GdkPaintable *paintable,
                                       GdkSnapshot  *snapshot,
                                       double        width,
                                       double        height)
{
  LiviGstPaintable *self = LIVI_GST_PAINTABLE (paintable);

  if (self->image)
    {
      float sx, sy;

      gtk_snapshot_save (snapshot);

      sx = gdk_paintable_get_intrinsic_width (self->image) / self->viewport.size.width;
      sy = gdk_paintable_get_intrinsic_height (self->image) / self->viewport.size.height;

      gtk_snapshot_push_clip (snapshot, &GRAPHENE_RECT_INIT (0, 0, width, height));

      gtk_snapshot_translate (snapshot, &GRAPHENE_POINT_INIT (-self->viewport.origin.x * width / self->viewport.size.width,
                                                              -self->viewport.origin.y * height / self->viewport.size.height));

      gdk_paintable_snapshot (self->image, snapshot, width * sx, height * sy);

      gtk_snapshot_pop (snapshot);
      gtk_snapshot_restore (snapshot);
    }
}

static GdkPaintable *
livi_gst_paintable_paintable_get_current_image (GdkPaintable *paintable)
{
  LiviGstPaintable *self = LIVI_GST_PAINTABLE (paintable);

  if (self->image)
    return GDK_PAINTABLE (g_object_ref (self->image));

  return gdk_paintable_new_empty (0, 0);
}

static int
livi_gst_paintable_paintable_get_intrinsic_width (GdkPaintable *paintable)
{
  LiviGstPaintable *self = LIVI_GST_PAINTABLE (paintable);

  if (self->image)
    return round (self->pixel_aspect_ratio * self->viewport.size.width);

  return 0;
}

static int
livi_gst_paintable_paintable_get_intrinsic_height (GdkPaintable *paintable)
{
  LiviGstPaintable *self = LIVI_GST_PAINTABLE (paintable);

  if (self->image)
    return ceil (self->viewport.size.height);

  return 0;
}

static double
livi_gst_paintable_paintable_get_intrinsic_aspect_ratio (GdkPaintable *paintable)
{
  LiviGstPaintable *self = LIVI_GST_PAINTABLE (paintable);

  if (self->image)
    return self->viewport.size.width / self->viewport.size.height;

  return 0.0;
};

static void
livi_gst_paintable_paintable_init (GdkPaintableInterface *iface)
{
  iface->snapshot = livi_gst_paintable_paintable_snapshot;
  iface->get_current_image = livi_gst_paintable_paintable_get_current_image;
  iface->get_intrinsic_width = livi_gst_paintable_paintable_get_intrinsic_width;
  iface->get_intrinsic_height = livi_gst_paintable_paintable_get_intrinsic_height;
  iface->get_intrinsic_aspect_ratio = livi_gst_paintable_paintable_get_intrinsic_aspect_ratio;
}

static GstElement *
livi_gst_paintable_video_renderer_create_video_sink (GstPlayVideoRenderer *renderer,
                                                     GstPlay              *player)
{
  LiviGstPaintable *self = LIVI_GST_PAINTABLE (renderer);
  GstElement *sink, *glsinkbin;
  g_autoptr (GdkGLContext) ctx = NULL;

  sink = g_object_new (LIVI_TYPE_GST_SINK,
                       "paintable", self,
                       "gl-context", self->context,
                       NULL);

  g_object_get (LIVI_GST_SINK (sink), "gl-context", &ctx, NULL);

  /* GL rendering won't work otherwise */
  g_assert (self->context != NULL && ctx != NULL);
  glsinkbin = gst_element_factory_make ("glsinkbin", NULL);

  g_object_set (glsinkbin, "sink", sink, NULL);

  g_debug ("created gl sink");
  return glsinkbin;
}

static void
livi_gst_paintable_video_renderer_init (GstPlayVideoRendererInterface *iface)
{
  iface->create_video_sink = livi_gst_paintable_video_renderer_create_video_sink;
}

static void
livi_gst_paintable_dispose (GObject *object)
{
  LiviGstPaintable *self = LIVI_GST_PAINTABLE (object);

  g_clear_object (&self->image);

  G_OBJECT_CLASS (livi_gst_paintable_parent_class)->dispose (object);
}

static void
livi_gst_paintable_class_init (LiviGstPaintableClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->dispose = livi_gst_paintable_dispose;
}

static void
livi_gst_paintable_init (LiviGstPaintable *self)
{
}

GdkPaintable *
livi_gst_paintable_new (void)
{
  return g_object_new (LIVI_TYPE_GST_PAINTABLE, NULL);
}

void
livi_gst_paintable_realize (LiviGstPaintable *self,
                            GdkSurface       *surface)
{
  g_autoptr (GError) error = NULL;

  if (self->context)
    return;

  self->context = gdk_surface_create_gl_context (surface, &error);
  if (self->context == NULL) {
    GST_INFO ("failed to create GDK GL context: %s", error->message);
    return;
  }

  if (!gdk_gl_context_realize (self->context, &error)) {
    GST_INFO ("failed to realize GDK GL context: %s", error->message);
    g_clear_object (&self->context);
    return;
  }
}

void
livi_gst_paintable_unrealize (LiviGstPaintable *self,
                              GdkSurface       *surface)
{
  /* XXX: We could be smarter here and:
   * - track how often we were realized with that surface
   * - track alternate surfaces
   */
  if (self->context == NULL)
    return;

  if (gdk_gl_context_get_surface (self->context) == surface)
    g_clear_object (&self->context);
}

static void
livi_gst_paintable_set_paintable (LiviGstPaintable      *self,
                                  GdkPaintable          *paintable,
                                  double                 pixel_aspect_ratio,
                                  const graphene_rect_t *viewport)
{
  gboolean size_changed;

  if (self->image == paintable)
    return;

  if (self->image == NULL ||
      gdk_paintable_get_intrinsic_height (self->image) != gdk_paintable_get_intrinsic_height (paintable) ||
      !G_APPROX_VALUE (self->pixel_aspect_ratio * gdk_paintable_get_intrinsic_width (self->image),
                       pixel_aspect_ratio * gdk_paintable_get_intrinsic_width (paintable),
                       FLT_EPSILON) ||
      !G_APPROX_VALUE (gdk_paintable_get_intrinsic_aspect_ratio (self->image),
                       gdk_paintable_get_intrinsic_aspect_ratio (paintable),
                       FLT_EPSILON) ||
      !graphene_rect_equal (viewport, &self->viewport)) {
    size_changed = TRUE;
  } else {
    size_changed = FALSE;
  }

  g_set_object (&self->image, paintable);
  self->pixel_aspect_ratio = pixel_aspect_ratio;
  self->viewport = *viewport;

  if (size_changed)
    gdk_paintable_invalidate_size (GDK_PAINTABLE (self));

  gdk_paintable_invalidate_contents (GDK_PAINTABLE (self));
}

typedef struct _SetTextureInvocation {
  LiviGstPaintable *paintable;
  GdkTexture       *texture;
  double            pixel_aspect_ratio;
  graphene_rect_t   viewport;
} SetTextureInvocation;

static void
set_texture_invocation_free (SetTextureInvocation *invoke)
{
  g_object_unref (invoke->paintable);
  g_object_unref (invoke->texture);

  g_slice_free (SetTextureInvocation, invoke);
}

static gboolean
livi_gst_paintable_set_texture_invoke (gpointer data)
{
  SetTextureInvocation *invoke = data;

  livi_gst_paintable_set_paintable (invoke->paintable,
                                    GDK_PAINTABLE (invoke->texture),
                                    invoke->pixel_aspect_ratio,
                                    &invoke->viewport);

  return G_SOURCE_REMOVE;
}

void
livi_gst_paintable_queue_set_texture (LiviGstPaintable      *self,
                                      GdkTexture            *texture,
                                      double                 pixel_aspect_ratio,
                                      const graphene_rect_t *viewport)
{
  SetTextureInvocation *invoke;

  invoke = g_slice_new0 (SetTextureInvocation);
  invoke->paintable = g_object_ref (self);
  invoke->texture = g_object_ref (texture);
  invoke->pixel_aspect_ratio = pixel_aspect_ratio;
  invoke->viewport = *viewport;

  g_main_context_invoke_full (NULL,
                              G_PRIORITY_DEFAULT,
                              livi_gst_paintable_set_texture_invoke,
                              invoke,
                              (GDestroyNotify) set_texture_invocation_free);
}
