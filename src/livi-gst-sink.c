/*
 * Copyright 2021 Purism SPC
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Authors: Matthew Waters <matthew@centricular.com>
 *          Benjamin Otte <otte@gnome.org>
 *          Guido Günther <agx@sigxcpu.org>
 *
 * heavily based on gtk-gst-sink.c from GTK4 which in turn picked it
 * up from GStreamer:
 * Copyright (C) 2015 Matthew Waters <matthew@centricular.com>
 */

#include "livi-gst-sink.h"

#include "livi-gst-paintable.h"

#include <gdk/wayland/gdkwayland.h>
#include <gst/gl/wayland/gstgldisplay_wayland.h>

#include <gst/gl/gstglfuncs.h>

enum {
  PROP_0,
  PROP_PAINTABLE,
  PROP_GL_CONTEXT,

  N_PROPS,
};


struct _LiviGstSink {
  GstVideoSink      parent;

  GstVideoInfo      v_info;
  GtkGstPaintable * paintable;
  GdkGLContext    * gdk_context;
  GstGLDisplay    * gst_display;
  GstGLContext    * gst_app_context;
  GstGLContext    * gst_context;
};


GST_DEBUG_CATEGORY (livi_debug_gst_sink);
#define GST_CAT_DEFAULT livi_debug_gst_sink

#define FORMATS "{ BGRA, ARGB, RGBA, ABGR, RGB, BGR }"

#define NOGL_CAPS GST_VIDEO_CAPS_MAKE (FORMATS)

static GstStaticPadTemplate livi_gst_sink_template =
  GST_STATIC_PAD_TEMPLATE ("sink",
                           GST_PAD_SINK,
                           GST_PAD_ALWAYS,
                           GST_STATIC_CAPS ("video/x-raw(" GST_CAPS_FEATURE_MEMORY_GL_MEMORY "), "
                                            "format = (string) RGBA, "
                                            "width = " GST_VIDEO_SIZE_RANGE ", "
                                            "height = " GST_VIDEO_SIZE_RANGE ", "
                                            "framerate = " GST_VIDEO_FPS_RANGE ", "
                                            "texture-target = (string) 2D"
                                            "; " NOGL_CAPS)
                           );

G_DEFINE_TYPE_WITH_CODE (LiviGstSink, livi_gst_sink,
                         GST_TYPE_VIDEO_SINK,
                         GST_DEBUG_CATEGORY_INIT (livi_debug_gst_sink,
                                                  "livistsink", 0, "Livi Video Sink"));

static GParamSpec *properties[N_PROPS] = { NULL, };


static void
livi_gst_sink_get_times (GstBaseSink  *bsink,
                         GstBuffer    *buf,
                         GstClockTime *start,
                         GstClockTime *end)
{
  LiviGstSink *livi_sink;

  livi_sink = LIVI_GST_SINK (bsink);

  if (GST_BUFFER_TIMESTAMP_IS_VALID (buf)) {
    *start = GST_BUFFER_TIMESTAMP (buf);
    if (GST_BUFFER_DURATION_IS_VALID (buf))
      *end = *start + GST_BUFFER_DURATION (buf);
    else{
      if (GST_VIDEO_INFO_FPS_N (&livi_sink->v_info) > 0) {
        *end = *start +
               gst_util_uint64_scale_int (GST_SECOND,
                                          GST_VIDEO_INFO_FPS_D (&livi_sink->v_info),
                                          GST_VIDEO_INFO_FPS_N (&livi_sink->v_info));
      }
    }
  }
}

static GstCaps *
livi_gst_sink_get_caps (GstBaseSink *bsink,
                        GstCaps     *filter)
{
  LiviGstSink *self = LIVI_GST_SINK (bsink);
  GstCaps *tmp;
  GstCaps *result;

  if (self->gst_context) {
    tmp = gst_pad_get_pad_template_caps (GST_BASE_SINK_PAD (bsink));
  }else   {
    tmp = gst_caps_from_string (NOGL_CAPS);
  }
  GST_DEBUG_OBJECT (self, "advertising own caps %" GST_PTR_FORMAT, tmp);

  if (filter) {
    GST_DEBUG_OBJECT (self, "intersecting with filter caps %" GST_PTR_FORMAT, filter);

    result = gst_caps_intersect_full (filter, tmp, GST_CAPS_INTERSECT_FIRST);
    gst_caps_unref (tmp);
  }else   {
    result = tmp;
  }

  GST_DEBUG_OBJECT (self, "returning caps: %" GST_PTR_FORMAT, result);

  return result;
}

static gboolean
livi_gst_sink_set_caps (GstBaseSink *bsink,
                        GstCaps     *caps)
{
  LiviGstSink *self = LIVI_GST_SINK (bsink);

  GST_DEBUG_OBJECT (self, "set caps with %" GST_PTR_FORMAT, caps);

  if (!gst_video_info_from_caps (&self->v_info, caps))
    return FALSE;

  return TRUE;
}

static gboolean
livi_gst_sink_query (GstBaseSink *bsink,
                     GstQuery    *query)
{
  LiviGstSink *self = LIVI_GST_SINK (bsink);

  if (GST_QUERY_TYPE (query) == GST_QUERY_CONTEXT &&
      self->gst_display != NULL &&
      gst_gl_handle_context_query (GST_ELEMENT (self), query, self->gst_display, self->gst_context, self->gst_app_context))
    return TRUE;

  return GST_BASE_SINK_CLASS (livi_gst_sink_parent_class)->query (bsink, query);
}

static gboolean
livi_gst_sink_propose_allocation (GstBaseSink *bsink,
                                  GstQuery    *query)
{
  LiviGstSink *self = LIVI_GST_SINK (bsink);
  GstBufferPool *pool = NULL;
  GstStructure *config;
  GstCaps *caps;
  guint size;
  gboolean need_pool;
  GstVideoInfo info;

  if (!self->gst_context)
    return FALSE;

  gst_query_parse_allocation (query, &caps, &need_pool);

  if (caps == NULL) {
    GST_DEBUG_OBJECT (bsink, "no caps specified");
    return FALSE;
  }

  if (!gst_caps_features_contains (gst_caps_get_features (caps, 0), GST_CAPS_FEATURE_MEMORY_GL_MEMORY))
    return FALSE;

  if (!gst_video_info_from_caps (&info, caps)) {
    GST_DEBUG_OBJECT (self, "invalid caps specified");
    return FALSE;
  }

  /* the normal size of a frame */
  size = info.size;

  if (need_pool) {
    GST_DEBUG_OBJECT (self, "create new pool");
    pool = gst_gl_buffer_pool_new (self->gst_context);

    config = gst_buffer_pool_get_config (pool);
    gst_buffer_pool_config_set_params (config, caps, size, 0, 0);
    gst_buffer_pool_config_add_option (config, GST_BUFFER_POOL_OPTION_GL_SYNC_META);

    if (!gst_buffer_pool_set_config (pool, config)) {
      GST_DEBUG_OBJECT (bsink, "failed setting config");
      gst_object_unref (pool);
      return FALSE;
    }
  }

  /* we need at least 2 buffer because we hold on to the last one */
  gst_query_add_allocation_pool (query, pool, size, 2, 0);
  if (pool)
    gst_object_unref (pool);

  /* we also support various metadata */
  gst_query_add_allocation_meta (query, GST_VIDEO_META_API_TYPE, 0);

  if (self->gst_context->gl_vtable->FenceSync)
    gst_query_add_allocation_meta (query, GST_GL_SYNC_META_API_TYPE, 0);

  return TRUE;
}

static GdkMemoryFormat
gtk_gst_memory_format_from_video (GstVideoFormat format)
{
  switch ((guint) format)
  {
  case GST_VIDEO_FORMAT_BGRA:
    return GDK_MEMORY_B8G8R8A8;
  case GST_VIDEO_FORMAT_ARGB:
    return GDK_MEMORY_A8R8G8B8;
  case GST_VIDEO_FORMAT_RGBA:
    return GDK_MEMORY_R8G8B8A8;
  case GST_VIDEO_FORMAT_ABGR:
    return GDK_MEMORY_A8B8G8R8;
  case GST_VIDEO_FORMAT_RGB:
    return GDK_MEMORY_R8G8B8;
  case GST_VIDEO_FORMAT_BGR:
    return GDK_MEMORY_B8G8R8;
  default:
    g_assert_not_reached ();
    return GDK_MEMORY_A8R8G8B8;
  }
}

static void
video_frame_free (GstVideoFrame *frame)
{
  gst_video_frame_unmap (frame);
  g_free (frame);
}

static GdkTexture *
livi_gst_sink_texture_from_buffer (LiviGstSink *self,
                                   GstBuffer   *buffer,
                                   double      *pixel_aspect_ratio)
{
  GstVideoFrame *frame = g_new (GstVideoFrame, 1);
  GdkTexture *texture;

  if (self->gdk_context &&
      gst_video_frame_map (frame, &self->v_info, buffer, GST_MAP_READ | GST_MAP_GL)) {
    GstGLSyncMeta *sync_meta;

    sync_meta = gst_buffer_get_gl_sync_meta (buffer);
    if (sync_meta) {
      gst_gl_sync_meta_set_sync_point (sync_meta, self->gst_context);
      gst_gl_sync_meta_wait (sync_meta, self->gst_context);
    }

    texture = gdk_gl_texture_new (self->gdk_context,
                                  *(guint *) frame->data[0],
                                  frame->info.width,
                                  frame->info.height,
                                  (GDestroyNotify) video_frame_free,
                                  frame);

    *pixel_aspect_ratio = ((double) frame->info.par_n) / ((double) frame->info.par_d);
  } else if (gst_video_frame_map (frame, &self->v_info, buffer, GST_MAP_READ)) {
    GBytes *bytes;

    bytes = g_bytes_new_with_free_func (frame->data[0],
                                        frame->info.height * frame->info.stride[0],
                                        (GDestroyNotify) video_frame_free,
                                        frame);
    texture = gdk_memory_texture_new (frame->info.width,
                                      frame->info.height,
                                      gtk_gst_memory_format_from_video (GST_VIDEO_FRAME_FORMAT (frame)),
                                      bytes,
                                      frame->info.stride[0]);
    g_bytes_unref (bytes);

    *pixel_aspect_ratio = ((double) frame->info.par_n) / ((double) frame->info.par_d);
  } else {
    GST_ERROR_OBJECT (self, "Could not convert buffer to texture.");
    texture = NULL;
    g_free (frame);
  }

  return texture;
}

static GstFlowReturn
livi_gst_sink_show_frame (GstVideoSink *vsink,
                          GstBuffer    *buf)
{
  LiviGstSink *self;
  GdkTexture *texture;
  double pixel_aspect_ratio;

  GST_TRACE ("rendering buffer:%p", buf);

  self = LIVI_GST_SINK (vsink);

  GST_OBJECT_LOCK (self);

  texture = livi_gst_sink_texture_from_buffer (self, buf, &pixel_aspect_ratio);
  if (texture) {
    gtk_gst_paintable_queue_set_texture (self->paintable, texture, pixel_aspect_ratio);
    g_object_unref (texture);
  }

  GST_OBJECT_UNLOCK (self);

  return GST_FLOW_OK;
}

#define HANDLE_EXTERNAL_WGL_MAKE_CURRENT(ctx)
#define DEACTIVATE_WGL_CONTEXT(ctx)
#define REACTIVATE_WGL_CONTEXT(ctx)

static gboolean
livi_gst_sink_initialize_gl (LiviGstSink *self)
{
  GdkDisplay *display;
  GError *error = NULL;
  GstGLPlatform platform = GST_GL_PLATFORM_NONE;
  GstGLAPI gl_api = GST_GL_API_NONE;
  guintptr gl_handle = 0;
  gboolean succeeded = FALSE;

  display = gdk_gl_context_get_display (self->gdk_context);

  HANDLE_EXTERNAL_WGL_MAKE_CURRENT (self->gdk_context);
  gdk_gl_context_make_current (self->gdk_context);

  if (GDK_IS_WAYLAND_DISPLAY (display)) {
    platform = GST_GL_PLATFORM_EGL;

    GST_DEBUG_OBJECT (self, "got EGL on Wayland!");

    gl_api = gst_gl_context_get_current_gl_api (platform, NULL, NULL);
    gl_handle = gst_gl_context_get_current_gl_context (platform);

    if (gl_handle) {
      struct wl_display *wayland_display;

      wayland_display = gdk_wayland_display_get_wl_display (display);
      self->gst_display = GST_GL_DISPLAY (gst_gl_display_wayland_new_with_display (wayland_display));
      self->gst_app_context = gst_gl_context_new_wrapped (self->gst_display, gl_handle, platform, gl_api);
    }else   {
      GST_ERROR_OBJECT (self, "Failed to get handle from GdkGLContext, not using Wayland EGL");
      return FALSE;
    }
  }else   {
    GST_INFO_OBJECT (self, "Unsupported GDK display %s for GL", G_OBJECT_TYPE_NAME (display));
    return FALSE;
  }

  g_assert (self->gst_app_context != NULL);

  gst_gl_context_activate (self->gst_app_context, TRUE);

  if (!gst_gl_context_fill_info (self->gst_app_context, &error)) {
    GST_ERROR_OBJECT (self, "failed to retrieve GDK context info: %s", error->message);
    g_clear_error (&error);
    g_clear_object (&self->gst_app_context);
    g_clear_object (&self->gst_display);
    HANDLE_EXTERNAL_WGL_MAKE_CURRENT (self->gdk_context);
    return FALSE;
  }else   {
    DEACTIVATE_WGL_CONTEXT (self->gdk_context);
    gst_gl_context_activate (self->gst_app_context, FALSE);
  }

  succeeded = gst_gl_display_create_context (self->gst_display, self->gst_app_context, &self->gst_context, &error);

  if (!succeeded) {
    GST_ERROR_OBJECT (self, "Couldn't create GL context: %s", error->message);
    g_error_free (error);
    g_clear_object (&self->gst_app_context);
    g_clear_object (&self->gst_display);
  }

  HANDLE_EXTERNAL_WGL_MAKE_CURRENT (self->gdk_context);
  REACTIVATE_WGL_CONTEXT (self->gdk_context);
  return succeeded;
}

static void
livi_gst_sink_set_property (GObject      *object,
                            guint         prop_id,
                            const GValue *value,
                            GParamSpec   *pspec)

{
  LiviGstSink *self = LIVI_GST_SINK (object);

  switch (prop_id)
  {
  case PROP_PAINTABLE:
    self->paintable = g_value_dup_object (value);
    if (self->paintable == NULL)
      self->paintable = GTK_GST_PAINTABLE (gtk_gst_paintable_new ());
    break;

  case PROP_GL_CONTEXT:
    self->gdk_context = g_value_dup_object (value);
    if (self->gdk_context != NULL && !livi_gst_sink_initialize_gl (self))
      g_clear_object (&self->gdk_context);
    break;

  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    break;
  }
}

static void
livi_gst_sink_get_property (GObject    *object,
                            guint       prop_id,
                            GValue     *value,
                            GParamSpec *pspec)
{
  LiviGstSink *self = LIVI_GST_SINK (object);

  switch (prop_id)
  {
  case PROP_PAINTABLE:
    g_value_set_object (value, self->paintable);
    break;
  case PROP_GL_CONTEXT:
    g_value_set_object (value, self->gdk_context);
    break;

  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    break;
  }
}

static void
livi_gst_sink_dispose (GObject *object)
{
  LiviGstSink *self = LIVI_GST_SINK (object);

  g_clear_object (&self->paintable);
  g_clear_object (&self->gst_app_context);
  g_clear_object (&self->gst_display);
  g_clear_object (&self->gdk_context);

  G_OBJECT_CLASS (livi_gst_sink_parent_class)->dispose (object);
}

static void
livi_gst_sink_class_init (LiviGstSinkClass * klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  GstElementClass *gstelement_class = GST_ELEMENT_CLASS (klass);
  GstBaseSinkClass *gstbasesink_class = GST_BASE_SINK_CLASS (klass);
  GstVideoSinkClass *gstvideosink_class = GST_VIDEO_SINK_CLASS (klass);

  gobject_class->set_property = livi_gst_sink_set_property;
  gobject_class->get_property = livi_gst_sink_get_property;
  gobject_class->dispose = livi_gst_sink_dispose;

  gstbasesink_class->set_caps = livi_gst_sink_set_caps;
  gstbasesink_class->get_times = livi_gst_sink_get_times;
  gstbasesink_class->query = livi_gst_sink_query;
  gstbasesink_class->propose_allocation = livi_gst_sink_propose_allocation;
  gstbasesink_class->get_caps = livi_gst_sink_get_caps;

  gstvideosink_class->show_frame = livi_gst_sink_show_frame;

  /**
   * LiviGstSink:paintable:
   *
   * The paintable that provides the picture for this sink.
   */
  properties[PROP_PAINTABLE] =
    g_param_spec_object ("paintable",
                         "paintable",
                         "Paintable providing the picture",
                         GTK_TYPE_GST_PAINTABLE,
                         G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS);

  /**
   * LiviGstSink:gl-context:
   *
   * The #GdkGLContext to use for GL rendering.
   */
  properties[PROP_GL_CONTEXT] =
    g_param_spec_object ("gl-context",
                         "gl-context",
                         "GL context to use for rendering",
                         GDK_TYPE_GL_CONTEXT,
                         G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS);

  g_object_class_install_properties (gobject_class, N_PROPS, properties);

  gst_element_class_set_metadata (gstelement_class,
                                  "µPlayer Video Sink",
                                  "Sink/Video", "The video sink used by µPlayer",
                                  "Matthew Waters <matthew@centricular.com>, "
                                  "Benjamin Otte <otte@gnome.org>");

  gst_element_class_add_static_pad_template (gstelement_class,
                                             &livi_gst_sink_template);
}

static void
livi_gst_sink_init (LiviGstSink *self)
{
}
