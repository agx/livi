/*
 * Copyright (C) 2023 Guido Günther <agx@sigxcpu.org>
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Author: Guido Günther <agx@sigxcpu.org>
 */

#define G_LOG_DOMAIN "livi-mpris"

#include "livi-config.h"

#include "livi-mpris.h"
#include "livi-mpris-dbus.h"

#include <gst/play/gstplay.h>
#include <gtk/gtk.h>

#define MPRIS_BUS_NAME_PREFIX "org.mpris.MediaPlayer2."
#define MPRIS_OBJECT_NAME "/org/mpris/MediaPlayer2"

enum {
  RAISE,
  N_SIGNALS
};
static guint signals[N_SIGNALS] = { 0 };


enum {
  PROP_0,
  PROP_PLAYER_STATE,
  PROP_TITLE,
  LAST_PROP,
};
static GParamSpec *props[LAST_PROP];


struct _LiviMpris {
  GObject                     parent;

  int                         dbus_name_id;

  LiviDBusMediaPlayer2       *skeleton;
  LiviDBusMediaPlayer2Player *skeleton_player;

  GstPlayState                state;
  char                       *title;
};
G_DEFINE_TYPE (LiviMpris, livi_mpris, G_TYPE_OBJECT)



static void
livi_mpris_set_player_state (LiviMpris *self, GstPlayState state)
{
  const char *dbus_state = "Stopped";

  if (self->state == state)
    return;
  self->state = state;

  switch (self->state) {
  case GST_PLAY_STATE_PLAYING:
    dbus_state = "Playing";
    break;
  case GST_PLAY_STATE_PAUSED:
  case GST_PLAY_STATE_BUFFERING:
    dbus_state = "Paused";
    break;
  case GST_PLAY_STATE_STOPPED:
  default:
    break;
  }
  livi_dbus_media_player2_player_set_playback_status (self->skeleton_player, dbus_state);

  g_object_notify_by_pspec (G_OBJECT (self), props[PROP_PLAYER_STATE]);
}


static void
livi_mpris_set_title (LiviMpris *self, const char *title)
{
  GVariantDict dict;

  if (g_strcmp0 (self->title, title) == 0)
    return;

  g_free (self->title);
  self->title = g_strdup (title);

  g_variant_dict_init (&dict, NULL);
  g_variant_dict_insert (&dict, "xesam:title", "s", self->title);

  livi_dbus_media_player2_player_set_metadata (self->skeleton_player,
                                               g_variant_dict_end (&dict));

  g_object_notify_by_pspec (G_OBJECT (self), props[PROP_TITLE]);
}


static void
livi_mpris_set_property (GObject      *object,
                         guint         property_id,
                         const GValue *value,
                         GParamSpec    *pspec)
{
  LiviMpris *self = LIVI_MPRIS (object);

  switch (property_id) {
  case PROP_PLAYER_STATE:
    livi_mpris_set_player_state (self, g_value_get_enum (value));
    break;
  case PROP_TITLE:
    livi_mpris_set_title (self, g_value_get_string (value));
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    break;
  }
}


static void
livi_mpris_get_property (GObject    *object,
                         guint       property_id,
                         GValue     *value,
                         GParamSpec *pspec)
{
  LiviMpris *self = LIVI_MPRIS (object);

  switch (property_id) {
    case PROP_PLAYER_STATE:
      g_value_set_enum (value, self->state);
      break;
    case PROP_TITLE:
      g_value_set_string (value, self->title);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
  }
}


static gboolean
on_media_player_handle_raise (LiviMpris             *self,
                              GDBusMethodInvocation *invocation,
                              LiviDBusMediaPlayer2  *skeleton)
{
  g_debug ("Raise");

  g_signal_emit (self, signals[RAISE], 0);

  livi_dbus_media_player2_complete_raise (skeleton, invocation);
  return TRUE;
}


static gboolean
on_media_player_handle_play_pause (LiviMpris                  *self,
                                   GDBusMethodInvocation      *invocation,
                                   LiviDBusMediaPlayer2Player *skeleton)
{
  GApplication *app = g_application_get_default ();
  GtkWindow *window = gtk_application_get_active_window (GTK_APPLICATION (app));

  g_debug ("Play-pause");

  gtk_widget_activate_action (GTK_WIDGET (window), "win.toggle-play", NULL);

  livi_dbus_media_player2_player_complete_play_pause (skeleton, invocation);
  return TRUE;
}


static gboolean
on_media_player_handle_seek (LiviMpris                  *self,
                             GDBusMethodInvocation      *invocation,
                             gint64                      pos_us,
                             LiviDBusMediaPlayer2Player *skeleton)
{
  GApplication *app = g_application_get_default ();
  GtkWindow *window = gtk_application_get_active_window (GTK_APPLICATION (app));
  gint32 pos_ms;

  pos_ms = pos_us / 1000;
  if (pos_ms > G_MAXINT32)
    pos_ms = G_MAXINT32;

  if (pos_ms < G_MININT32)
    pos_ms = G_MININT32;

  g_debug ("Seek %" G_GINT32_MODIFIER "d", pos_ms);
  gtk_widget_activate_action (GTK_WIDGET (window), "win.ff", "i", pos_ms);

  livi_dbus_media_player2_player_complete_seek (skeleton, invocation);
  return TRUE;
}


static void
on_name_acquired (GDBusConnection *connection,
                  const char      *name,
                  gpointer         user_data)
{
  g_debug ("Acquired name %s", name);
}


static void
on_name_lost (GDBusConnection *connection,
              const char      *name,
              gpointer         user_data)
{
  g_debug ("Lost or failed to acquire name %s", name);
}


static void
on_bus_acquired (GDBusConnection *connection,
                 const char      *name,
                 gpointer         user_data)
{
  g_autoptr (GError) err = NULL;
  LiviMpris *self = LIVI_MPRIS (user_data);
  gboolean success;

  success = g_dbus_interface_skeleton_export (G_DBUS_INTERFACE_SKELETON (self->skeleton),
                                              connection,
                                              MPRIS_OBJECT_NAME,
                                              &err);
  if (!success) {
    g_warning ("Failed to export mpris base: %s", err->message);
    g_clear_error (&err);
  }

  success = g_dbus_interface_skeleton_export (G_DBUS_INTERFACE_SKELETON (self->skeleton_player),
                                              connection,
                                              MPRIS_OBJECT_NAME,
                                              &err);
  if (!success)
    g_warning ("Failed to export mpris player: %s", err->message);
}


static void
livi_mpris_dispose (GObject *object)
{
  LiviMpris *self = LIVI_MPRIS (object);

  livi_mpris_unexport (self);
  g_clear_object (&self->skeleton);
  g_clear_object (&self->skeleton_player);

  G_OBJECT_CLASS (livi_mpris_parent_class)->dispose (object);
}


static void
livi_mpris_finalize (GObject *object)
{
  LiviMpris *self = LIVI_MPRIS (object);

  g_clear_pointer (&self->title, g_free);

  G_OBJECT_CLASS (livi_mpris_parent_class)->finalize (object);
}


static void
livi_mpris_class_init (LiviMprisClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->get_property = livi_mpris_get_property;
  object_class->set_property = livi_mpris_set_property;
  object_class->dispose = livi_mpris_dispose;
  object_class->finalize = livi_mpris_finalize;

  props[PROP_PLAYER_STATE] =
    g_param_spec_enum ("player-state", "", "",
                       GST_TYPE_PLAY_STATE,
                       GST_PLAY_STATE_STOPPED,
                       G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

  props[PROP_TITLE] =
    g_param_spec_string ("title", "", "",
                         NULL,
                         G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

  g_object_class_install_properties (object_class, LAST_PROP, props);

  signals[RAISE] = g_signal_new ("raise",
                                 G_TYPE_FROM_CLASS (klass),
                                 G_SIGNAL_RUN_LAST,
                                 0, NULL, NULL, NULL,
                                 G_TYPE_NONE, 0);
}


static void
livi_mpris_init (LiviMpris *self)
{
  self->skeleton = livi_dbus_media_player2_skeleton_new ();
  self->skeleton_player = livi_dbus_media_player2_player_skeleton_new ();

  livi_dbus_media_player2_set_desktop_entry (self->skeleton, APP_ID);
  livi_dbus_media_player2_set_identity (self->skeleton, DISPLAY_NAME);
  livi_dbus_media_player2_set_can_raise (self->skeleton, TRUE);

  /* FIXME: need to unset this e.g. at end of play */
  livi_dbus_media_player2_player_set_can_play (self->skeleton_player, TRUE);
  livi_dbus_media_player2_player_set_can_seek (self->skeleton_player, TRUE);
  livi_dbus_media_player2_player_set_playback_status (self->skeleton_player, "Stopped");

  g_signal_connect_swapped (self->skeleton,
                            "handle-raise",
                            G_CALLBACK (on_media_player_handle_raise),
                            self);
  g_object_connect (self->skeleton_player,
                    "swapped-signal::handle-play-pause", on_media_player_handle_play_pause, self,
                    "swapped-signal::handle-seek", on_media_player_handle_seek, self,
                    NULL);
}


LiviMpris *
livi_mpris_new (void)
{
  return g_object_new (LIVI_TYPE_MPRIS, NULL);
}


void
livi_mpris_export (LiviMpris *self)
{
  g_assert (LIVI_IS_MPRIS (self));

  if (self->dbus_name_id)
    return;

  self->dbus_name_id = g_bus_own_name (G_BUS_TYPE_SESSION,
                                       MPRIS_BUS_NAME_PREFIX PROJECT_NAME,
                                       G_BUS_NAME_OWNER_FLAGS_NONE,
                                       on_bus_acquired,
                                       on_name_acquired,
                                       on_name_lost,
                                       self,
                                       NULL);
}


void
livi_mpris_unexport (LiviMpris *self)
{
  g_assert (LIVI_IS_MPRIS (self));

  if (!self->dbus_name_id)
    return;

  g_dbus_interface_skeleton_unexport (G_DBUS_INTERFACE_SKELETON (self->skeleton));
  g_dbus_interface_skeleton_unexport (G_DBUS_INTERFACE_SKELETON (self->skeleton_player));

  g_clear_handle_id (&self->dbus_name_id, g_bus_unown_name);
}
