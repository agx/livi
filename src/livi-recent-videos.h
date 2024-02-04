/*
 * Copyright (C) 2024 Guido Günther <agx@sigxcpu.org>
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once

#include <glib-object.h>

G_BEGIN_DECLS

#define LIVI_TYPE_RECENT_VIDEOS (livi_recent_videos_get_type ())

G_DECLARE_FINAL_TYPE (LiviRecentVideos, livi_recent_videos, LIVI, RECENT_VIDEOS, GObject)

LiviRecentVideos *livi_recent_videos_new (void);
void              livi_recent_videos_update (LiviRecentVideos *self,
                                              const char       *uri,
                                              guint64           position_ns);
gint32            livi_recent_videos_get_pos (LiviRecentVideos *self, const char *uri);

G_END_DECLS
