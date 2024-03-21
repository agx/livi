/*
 * Copyright (C) 2024 Guido Günther <agx@sigxcpu.org>
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Author: Guido Günther <agx@sigxcpu.org>
 */

#define G_LOG_DOMAIN "livi-recent-videos"

#include "livi-config.h"

#include "livi-recent-videos.h"

#include <gst/gst.h>
#include <gio/gio.h>


/**
 * LiviRecentVideos:
 *
 * Tracks the list of recent videos.
 */

typedef struct _LiviRecentVideo {
  char    *uri;
  gint32   pos_ms;
  guint64  lastseen_us;
  gboolean preprocessed;
} LiviRecentVideo;


struct _LiviRecentVideos {
  GObject               parent;

  GSettings            *settings;
  GHashTable           *videos;
};
G_DEFINE_TYPE (LiviRecentVideos, livi_recent_videos, G_TYPE_OBJECT)


static void
livi_recent_video_free (LiviRecentVideo *video)
{
  g_free (video->uri);

  g_free (video);
}


static LiviRecentVideo *
livi_recent_video_new (const char *uri, gint32 pos_ms, guint64 lastseen_us, gboolean preprocessed)
{
  LiviRecentVideo *video = g_new0 (LiviRecentVideo, 1);

  video->uri = g_strdup (uri);
  video->pos_ms = pos_ms;
  video->lastseen_us = lastseen_us;
  video->preprocessed = preprocessed;

  return video;
}


static gint
compare_recent_func (LiviRecentVideo *a, LiviRecentVideo *b)
{
  return b->lastseen_us - a->lastseen_us;
}


static void
serialize_videos (LiviRecentVideos *self)
{
  g_autoptr (GPtrArray) videos = NULL;
  GVariantBuilder builder;
  guint max;

  max = g_settings_get_uint (self->settings, "max-recent-videos");

  g_variant_builder_init (&builder, G_VARIANT_TYPE ("aa{sv}"));

  videos = g_hash_table_get_values_as_ptr_array (self->videos);
  g_ptr_array_sort_values (videos, (GCompareFunc)compare_recent_func);
  for (int i = 0; i < videos->len && i < max; i++) {
    LiviRecentVideo *video = g_ptr_array_index (videos, i);

    g_variant_builder_open (&builder, G_VARIANT_TYPE_VARDICT);
    g_variant_builder_add (&builder, "{sv}", "uri", g_variant_new_string (video->uri));
    g_variant_builder_add (&builder, "{sv}", "position", g_variant_new_int32 (video->pos_ms));
    g_variant_builder_add (&builder, "{sv}", "lastseen", g_variant_new_uint64 (video->lastseen_us));
    g_variant_builder_add (&builder, "{sv}", "preprocessed", g_variant_new_boolean (video->preprocessed));
    g_variant_builder_close (&builder);
  }

  g_settings_set_value (self->settings, "recent-videos", g_variant_builder_end (&builder));
}


static void
deserialize_videos (LiviRecentVideos *self)
{
  GVariantIter videos_iter, dict_iter;
  g_autoptr (GVariant) videos = NULL;
  GVariant *video;

  g_hash_table_remove_all (self->videos);

  videos = g_settings_get_value (self->settings, "recent-videos");

  g_variant_iter_init (&videos_iter, videos);
  while ((video = g_variant_iter_next_value (&videos_iter))) {
    LiviRecentVideo *v;
    g_autofree char *uri = NULL;
    gint32 pos_ms = 0;
    guint64 lastseen = 0;
    gboolean preprocessed = FALSE;
    const char *key;
    GVariant *value;

    g_variant_iter_init (&dict_iter, video);
    while (g_variant_iter_next (&dict_iter, "{&sv}", &key, &value)) {

      if (g_strcmp0 (key, "uri") == 0)
        uri = g_variant_dup_string (value, NULL);
      else if (g_strcmp0 (key, "position") == 0)
        pos_ms = g_variant_get_int32 (value);
      else if (g_strcmp0 (key, "lastseen") == 0)
        lastseen = g_variant_get_uint64 (value);
      else if (g_strcmp0 (key, "preprocessed") == 0)
        preprocessed = g_variant_get_boolean (value);

      g_variant_unref (value);
    }

    if (uri) {
      v = livi_recent_video_new (uri, pos_ms, lastseen, preprocessed);
      g_hash_table_insert (self->videos, g_steal_pointer (&uri), g_steal_pointer (&v));
    } else {
      g_warning ("RecentVideos: got video but no uri");
    }

    g_variant_unref (video);
  }
}


static void
livi_recent_videos_dispose (GObject *object)
{
  LiviRecentVideos *self = LIVI_RECENT_VIDEOS (object);

  g_clear_pointer (&self->videos, g_hash_table_destroy);
  g_clear_object (&self->settings);

  G_OBJECT_CLASS (livi_recent_videos_parent_class)->dispose (object);
}


static void
livi_recent_videos_class_init (LiviRecentVideosClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->dispose = livi_recent_videos_dispose;
}


static void
livi_recent_videos_init (LiviRecentVideos *self)
{
  self->settings = g_settings_new ("org.sigxcpu.Livi");

  self->videos = g_hash_table_new_full (g_str_hash,
                                        g_str_equal,
                                        g_free,
                                        (GDestroyNotify)livi_recent_video_free);
  deserialize_videos (self);
}


LiviRecentVideos *
livi_recent_videos_new (void)
{
  return g_object_new (LIVI_TYPE_RECENT_VIDEOS, NULL);
}


void
livi_recent_videos_update (LiviRecentVideos *self,
                           const char       *uri,
                           gboolean          preprocessed,
                           guint64           position_ns)
{
  LiviRecentVideo *video;
  gint32 pos_ms;

  g_assert (LIVI_IS_RECENT_VIDEOS (self));
  g_assert (uri);

  g_debug ("Recent update '%s': %"G_GUINT64_FORMAT", preprocessed: %d",
           uri, position_ns / GST_SECOND, preprocessed);

  /* Don't overflow */
  if (position_ns / GST_MSECOND > G_MAXINT32) {
    g_warning ("Stream pos too far: %"G_GUINT64_FORMAT, position_ns);
    pos_ms = 0;
  } else {
    pos_ms = position_ns / GST_MSECOND;
  }

  video = g_hash_table_lookup (self->videos, uri);
  if (video) {
    video->pos_ms = pos_ms;
    video->lastseen_us = g_get_real_time ();
  } else {
    video = livi_recent_video_new (uri, pos_ms, g_get_real_time (), preprocessed);
    g_hash_table_insert (self->videos, g_strdup (uri), video);
  }

  /* TODO: only on shutdown */
  serialize_videos (self);
}


gint32
livi_recent_videos_get_pos (LiviRecentVideos *self, const char *uri)
{
  LiviRecentVideo *video;

  g_assert (LIVI_IS_RECENT_VIDEOS (self));
  g_assert (uri);

  video = g_hash_table_lookup (self->videos, uri);
  if (!video) {
    g_debug ("Video '%s' not yet known", uri);
    return 0;
  }

  g_debug ("Found position %ds for '%s'", video->pos_ms / 1000, uri);
  return video->pos_ms;
}


gint64
livi_recent_videos_get_last_seen (LiviRecentVideos *self, const char *uri)
{
  LiviRecentVideo *video;

  g_assert (LIVI_IS_RECENT_VIDEOS (self));
  g_assert (uri);

  video = g_hash_table_lookup (self->videos, uri);
  if (!video) {
    g_debug ("Video '%s' not yet known", uri);
    return 0;
  }

  return video->lastseen_us;
}



char *
livi_recent_videos_get_nth_recent_url (LiviRecentVideos *self, guint index, gboolean *preprocessed)
{
  g_autoptr (GPtrArray) videos = NULL;
  LiviRecentVideo *video;

  g_assert (LIVI_IS_RECENT_VIDEOS (self));

  videos = g_hash_table_get_values_as_ptr_array (self->videos);

  if (index >= videos->len)
    return NULL;

  g_ptr_array_sort_values (videos, (GCompareFunc)compare_recent_func);

  video = g_ptr_array_index (videos, index);
  if (!video)
    return NULL;

  if (preprocessed)
    *preprocessed = video->preprocessed;

  return g_strdup (video->uri);
}
