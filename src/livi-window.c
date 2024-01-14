/* livi-window.c
 *
 * Copyright 2021 Purism SPC
 *           2023-2024 Guido Günther
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Author: Guido Günther <agx@sigxcpu.org>
 */

#define G_LOG_DOMAIN "livi-window"

#include "livi-config.h"
#include "livi-controls.h"
#include "livi-recent-videos.h"
#include "livi-window.h"
#include "livi-utils.h"
#include "livi-gst-paintable.h"

#include <gst/gst.h>
#include <gst/play/gstplay.h>
#include <gst/play/gstplay-visualization.h>
#include <gst/play/gstplay-signal-adapter.h>

#include <adwaita.h>
#include <glib/gi18n.h>

enum {
  PROP_0,
  PROP_MUTED,
  PROP_PLAYBACK_SPEED,
  PROP_STATE,
  PROP_TITLE,
  LAST_PROP,
};
static GParamSpec *props[LAST_PROP];


typedef enum _StreamTargetState {
  STREAM_TARGET_STATE_NONE    = 0,
  STREAM_TARGET_STATE_PREVIEW = 1,
  STREAM_TARGET_STATE_PLAY    = 2,
  STREAM_TARGET_STATE_PAUSE   = 3,
} StreamTargetState;


struct _LiviWindow
{
  AdwApplicationWindow  parent_instance;

  GtkStack             *stack_content;

  GtkBox               *box_content;
  GtkPicture           *picture_video;
  GdkPaintable         *paintable;
  GtkOverlay           *overlay;

  AdwToolbarView       *toolbar;
  /* top bar */
  GtkLabel             *lbl_status;
  GtkImage             *img_accel;
  GtkImage             *img_fullscreen;
  /* bottom bar */
  LiviControls         *controls;
  guint                 hide_controls_id;

  GtkRevealer          *revealer_center;
  GtkStack             *stack_center;
  GtkBox               *box_center;
  GtkLabel             *lbl_center;
  GtkImage             *img_center;
  GtkBox               *box_resume_or_restart;
  GtkButton            *btn_resume;
  guint                 reveal_id;

  AdwStatusPage        *error_state;
  GtkBox               *empty_state;

  GstPlay              *player;
  GstPlaySignalAdapter *signal_adapter;
  GstPlayState          state;
  guint                 cookie;

  struct {
    gboolean            muted;
    int                 playback_speed;
    guint               num_audio_streams;
    guint               num_subtitle_streams;
    char               *title;
    char               *ref_uri;
  } stream;

  GtkFileFilter        *video_filter;
  char                 *last_local_uri;

  /* seeking */
  gboolean              seek_lock;
  StreamTargetState     seek_target_state;

  LiviRecentVideos     *recent_videos;

  gboolean              have_pointer;
};

G_DEFINE_TYPE (LiviWindow, livi_window, ADW_TYPE_APPLICATION_WINDOW)


static void
hide_controls (LiviWindow *self)
{
  adw_toolbar_view_set_reveal_bottom_bars (self->toolbar, FALSE);

  if (gtk_stack_get_visible_child (self->stack_content) == GTK_WIDGET (self->box_content))
    adw_toolbar_view_set_reveal_top_bars (self->toolbar, FALSE);

  gtk_widget_set_cursor_from_name (GTK_WIDGET (self), "none");
}


static void
on_hide_controls_timeout (gpointer user_data)
{
  LiviWindow *self = LIVI_WINDOW (user_data);

  if (self->state == GST_PLAY_STATE_PLAYING ||
      self->seek_target_state == STREAM_TARGET_STATE_PAUSE) {
    hide_controls (self);
  }

  self->hide_controls_id = 0;
}


static void
arm_hide_controls_timer (LiviWindow *self)
{
  g_clear_handle_id (&self->hide_controls_id, g_source_remove);
  self->hide_controls_id = g_timeout_add_seconds_once (2, on_hide_controls_timeout, self);
  g_source_set_name_by_id (self->hide_controls_id, "[p-o-s] hide_controls_timer");
}


static void
show_controls (LiviWindow *self)
{
  adw_toolbar_view_set_reveal_top_bars (self->toolbar, TRUE);

  if (gtk_stack_get_visible_child (self->stack_content) == GTK_WIDGET (self->box_content))
    adw_toolbar_view_set_reveal_bottom_bars (self->toolbar, TRUE);

  if (self->have_pointer)
    arm_hide_controls_timer (self);

  gtk_widget_set_cursor_from_name (GTK_WIDGET (self), "default");
}


static void
on_pointer_enter (LiviWindow *self)
{
  g_clear_handle_id (&self->hide_controls_id, g_source_remove);
}


static void
on_pointer_motion (LiviWindow *self, double x, double y)
{
  static double old_x, old_y;

  /* Avoid busy work when nothing changed */
  if (G_APPROX_VALUE (old_x, x, FLT_EPSILON) &&
      G_APPROX_VALUE (old_y, y, FLT_EPSILON)) {
    return;
  }

  old_x = x;
  old_y = y;

  self->have_pointer = TRUE;
  show_controls (self);
}


static void
reset_stream (LiviWindow *self)
{
  if (self->stream.title) {
    g_free (self->stream.title);
    g_free (self->stream.ref_uri);
    g_object_notify_by_pspec (G_OBJECT (self), props[PROP_TITLE]);
  }
  memset (&self->stream, 0, sizeof (self->stream));
  self->stream.playback_speed = 100;
}


static void
livi_window_set_playback_speed (LiviWindow *self, int percent)
{
  g_debug ("Setting Rate to : %f", percent / 100.0);

  if (percent == self->stream.playback_speed)
    return;

  if (self->player)
    gst_play_set_rate (self->player, percent / 100.0);

  self->stream.playback_speed = percent;
  g_object_notify_by_pspec (G_OBJECT (self), props[PROP_PLAYBACK_SPEED]);
}


static void
livi_window_set_property (GObject      *object,
                          guint         property_id,
                          const GValue *value,
                          GParamSpec    *pspec)
{
  LiviWindow *self = LIVI_WINDOW (object);
  gboolean muted;

  switch (property_id) {
  case PROP_MUTED:
    muted = g_value_get_boolean (value);
    if (self->player)
      gst_play_set_mute (self->player, muted);
    else
      self->stream.muted = muted;
    break;
  case PROP_PLAYBACK_SPEED:
    livi_window_set_playback_speed (self, g_value_get_int (value));
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    break;
  }
}


static void
livi_window_get_property (GObject    *object,
                          guint       property_id,
                          GValue     *value,
                          GParamSpec *pspec)
{
  LiviWindow *self = LIVI_WINDOW (object);

  switch (property_id) {
    case PROP_MUTED:
      g_value_set_boolean (value, self->stream.muted);
      break;
    case PROP_PLAYBACK_SPEED:
      g_value_set_int (value, self->stream.playback_speed);
      break;
    case PROP_STATE:
      g_value_set_enum (value, self->state);
      break;
    case PROP_TITLE:
      g_value_set_string (value, self->stream.title);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
  }
}


static void
on_fullscreen (LiviWindow *self)
{
  const char *icon_names[] = {
    "view-fullscreen-symbolic",
    "view-unfullscreen-symbolic"
  };
  gboolean fullscreen;

  g_assert (LIVI_IS_WINDOW (self));

  fullscreen = gtk_window_is_fullscreen (GTK_WINDOW (self));
  g_debug ("Fullscreen: %d", fullscreen);

  g_object_set (self->img_fullscreen, "icon-name", icon_names[fullscreen], NULL);
}


static void
on_reveal_timeout (gpointer data)
{
  LiviWindow *self = LIVI_WINDOW (data);

  gtk_revealer_set_reveal_child (self->revealer_center, FALSE);

  self->reveal_id = 0;
}


static void
show_center_overlay (LiviWindow *self, const char *icon_name, const char *label, gboolean fade)
{
  gtk_widget_set_visible (GTK_WIDGET (self->lbl_center), !!label);
  gtk_label_set_label (self->lbl_center, label);

  gtk_stack_set_visible_child (self->stack_center, GTK_WIDGET (self->box_center));
  g_object_set (self->img_center, "icon-name", icon_name, NULL);
  gtk_revealer_set_reveal_child (self->revealer_center, TRUE);

  if (fade)
    self->reveal_id = g_timeout_add_once (500, on_reveal_timeout, self);
}


static void
hide_center_overlay (LiviWindow *self)
{
  if (self->seek_lock)
    return;

  gtk_revealer_set_reveal_child (self->revealer_center, FALSE);
}


static void
show_resume_or_restart_overlay (LiviWindow *self, gboolean can_resume)
{
  gtk_widget_set_visible (GTK_WIDGET (self->btn_resume), can_resume);
  gtk_stack_set_visible_child (self->stack_center, GTK_WIDGET (self->box_resume_or_restart));
  gtk_revealer_set_reveal_child (self->revealer_center, TRUE);

  self->seek_target_state = STREAM_TARGET_STATE_PREVIEW;
}


static void
toggle_controls (LiviWindow *self)
{
  gboolean revealed;

  g_assert (LIVI_IS_WINDOW (self));

  revealed = adw_toolbar_view_get_reveal_bottom_bars (self->toolbar);

  if (!revealed)
    show_controls (self);
  else
    hide_controls (self);
}


static void
on_toggle_controls_activated (GtkWidget  *widget, const char *action_name, GVariant *unused)
{
  toggle_controls (LIVI_WINDOW (widget));
}


static void
on_toggle_play_activated (GtkWidget  *widget, const char *action_name, GVariant *unused)
{
  LiviWindow *self = LIVI_WINDOW (widget);
  const char *icon_name;
  gboolean fade;

  if (self->state == GST_PLAY_STATE_PLAYING) {
    gst_play_pause (self->player);
    icon_name = "media-playback-pause-symbolic";
    fade = FALSE;
  } else {
    gst_play_play (self->player);
    icon_name = "media-playback-start-symbolic";
    fade = TRUE;
  }

  show_center_overlay (self, icon_name, NULL, fade);
}


static void
on_restart_activated (GtkWidget  *widget, const char *action_name, GVariant *unused)
{
  LiviWindow *self = LIVI_WINDOW (widget);

  self->seek_target_state = STREAM_TARGET_STATE_PLAY;
  gst_play_seek (self->player, 0);
}


static void
on_subtitle_stream_action_changed_state (GSimpleAction *action, GVariant *param, gpointer user_data)
{
  LiviWindow *self = LIVI_WINDOW (user_data);
  gboolean enable = FALSE;
  gint index;

  index = g_variant_get_int32 (param);

  if (index >= 0) {
    gst_play_set_subtitle_track (self->player, index);
    enable = TRUE;
  }

  gst_play_set_subtitle_track_enabled (self->player, enable);

  g_simple_action_set_state(action, param);
}


static void
on_audio_stream_action_changed_state (GSimpleAction *action, GVariant *param, gpointer user_data)
{
  LiviWindow *self = LIVI_WINDOW (user_data);
  gint index;

  index = g_variant_get_int32 (param);

  gst_play_set_audio_track (self->player, index);

  g_simple_action_set_state(action, param);
}


static void
on_file_chooser_done (GObject *object, GAsyncResult *response, gpointer user_data)
{
  LiviWindow *self = LIVI_WINDOW (user_data);
  g_autoptr (GtkFileDialog) dialog = GTK_FILE_DIALOG (object);
  g_autoptr (GFile) file = NULL;
  g_autoptr (GError) err = NULL;
  g_autofree char *uri = NULL;

  file = gtk_file_dialog_open_finish (dialog, response, &err);
  if (!file) {
    if (!g_error_matches (err, GTK_DIALOG_ERROR, GTK_DIALOG_ERROR_DISMISSED))
        g_warning ("Failed to select file: %s", err->message);
    return;
  }

  uri = g_file_get_uri (file);
  livi_window_play_uri (self, uri, NULL);
  g_free (self->last_local_uri);
  self->last_local_uri = g_steal_pointer (&uri);
}


static void
on_open_file_activated (GtkWidget *widget, const char *action_name, GVariant *unused)
{
  LiviWindow *self = LIVI_WINDOW (widget);
  GtkFileDialog *dialog;

  /* Otherwise the portal dialog can set this as proper parent */
  gtk_window_unfullscreen (GTK_WINDOW (self));

  dialog = gtk_file_dialog_new ();
  gtk_file_dialog_set_title (dialog, _("Choose a video to play"));
  gtk_file_dialog_set_default_filter (dialog, self->video_filter);

  if (!STR_IS_NULL_OR_EMPTY (self->last_local_uri)) {
    g_autoptr (GFile) current_file = g_file_new_for_uri (self->last_local_uri);
    gtk_file_dialog_set_initial_file (dialog, current_file);
  } else {
    const char *dir;

    dir = g_get_user_special_dir (G_USER_DIRECTORY_VIDEOS);
    if (dir) {
      g_autoptr (GFile) videos_dir = g_file_new_for_path (dir);
      gtk_file_dialog_set_initial_folder (dialog, videos_dir);
    }
  }

  gtk_file_dialog_open (dialog, GTK_WINDOW (self), NULL, on_file_chooser_done, self);
}


static void
move_stream_to_pos (LiviWindow *self, GstClockTime pos, const char *label)
{
  GstClockTime current;
  const char *icon_name;

  current = gst_play_get_position (self->player);

  if (pos == current)
    return;

  icon_name = (pos > current) ? "media-seek-forward-symbolic" : "media-seek-backward-symbolic";
  show_center_overlay (self, icon_name, label, TRUE);
  gst_play_seek (self->player, pos);
}


static void
on_ff_rev_activated (GtkWidget *widget, const char *action_name, GVariant *param)
{
  LiviWindow *self = LIVI_WINDOW (widget);
  GstClockTime pos;
  g_autofree char *label = NULL;
  gint64 offset;

  if (self->seek_lock)
    return;
  self->seek_lock = TRUE;

  offset = g_variant_get_int32 (param) * GST_MSECOND;
  pos = gst_play_get_position (self->player);

  if (offset < 0 && labs(offset) > pos)
    pos = 0;
  else
    pos += offset;

  label = g_strdup_printf (_("%.2lds"), labs(offset / GST_SECOND));
  move_stream_to_pos (self, pos, label);
}


static void
on_seek_activated (GtkWidget *widget, const char *action_name, GVariant *param)
{
  LiviWindow *self = LIVI_WINDOW (widget);
  gint64 pos;

  if (self->seek_lock)
    return;

  self->seek_lock = TRUE;

  pos = g_variant_get_int32 (param) * GST_MSECOND;
  move_stream_to_pos (self, pos, NULL);
}



static void
on_player_error (GstPlaySignalAdapter *adapter,
                 GError               *error,
                 GstStructure         *details,
                 LiviWindow           *self)
{
  g_warning ("Player error: %s", error->message);

  livi_window_set_error_state (self, error->message);
}


static void
on_player_warning (GstPlaySignalAdapter *adapter,
                   GError               *error,
                   GstStructure         *details,
                   LiviWindow           *self)
{
  g_warning ("Player warning: %s", error->message);
}


static void
on_player_buffering (GstPlaySignalAdapter *adapter, gint percent, gpointer user_data)
{
  LiviWindow *self = LIVI_WINDOW (user_data);
  g_autofree char *msg = NULL;

  g_assert (LIVI_IS_WINDOW (self));

  g_debug ("Buffering %d", percent);
  if (percent == 100) {
    gtk_widget_set_visible (GTK_WIDGET (self->lbl_status), FALSE);
    return;
  }

  msg = g_strdup_printf (_("Buffering %d/100"), percent);
  gtk_label_set_text (self->lbl_status, msg);
  gtk_widget_set_visible (GTK_WIDGET (self->lbl_status), TRUE);
}


static void
check_pipeline (LiviWindow *self, GstPlay *player)
{
  g_autoptr (GstElement) bin = gst_play_get_pipeline (player);
  g_autoptr (GstIterator) iter = gst_bin_iterate_recurse (GST_BIN (bin));
  GValue item = { 0, };
  gboolean found = FALSE;

  while (iter && gst_iterator_next (iter, &item) == GST_ITERATOR_OK) {
    GstElement *elem = g_value_get_object (&item);

    if (g_str_has_prefix (GST_OBJECT_NAME (elem), "v4l2slav1dec") ||
        g_str_has_prefix (GST_OBJECT_NAME (elem), "v4l2slh264dec") ||
        g_str_has_prefix (GST_OBJECT_NAME (elem), "v4l2slh265dec") ||
        g_str_has_prefix (GST_OBJECT_NAME (elem), "v4l2slmpeg2dec") ||
        g_str_has_prefix (GST_OBJECT_NAME (elem), "v4l2slvp8dec") ||
        g_str_has_prefix (GST_OBJECT_NAME (elem), "v4l2slvp9dec") ||
        g_str_has_prefix (GST_OBJECT_NAME (elem), "v4l2h264dec") ||
        g_str_has_prefix (GST_OBJECT_NAME (elem), "v4l2h265dec") ||
        g_str_has_prefix (GST_OBJECT_NAME (elem), "v4l2mpeg2dec") ||
        g_str_has_prefix (GST_OBJECT_NAME (elem), "v4l2vp8dec") ||
        g_str_has_prefix (GST_OBJECT_NAME (elem), "v4l2vp9dec") ||
        g_str_has_prefix (GST_OBJECT_NAME (elem), "vaav1dec") ||
        g_str_has_prefix (GST_OBJECT_NAME (elem), "vah264dec") ||
        g_str_has_prefix (GST_OBJECT_NAME (elem), "vah265dec") ||
        g_str_has_prefix (GST_OBJECT_NAME (elem), "vampeg2dec") ||
        g_str_has_prefix (GST_OBJECT_NAME (elem), "vavp8dec") ||
        g_str_has_prefix (GST_OBJECT_NAME (elem), "vavp9dec") ||
        g_str_has_prefix (GST_OBJECT_NAME (elem), "vulkanav1dec") ||
        g_str_has_prefix (GST_OBJECT_NAME (elem), "vulkanh264dec") ||
        g_str_has_prefix (GST_OBJECT_NAME (elem), "vulkanh265dec") ||
        g_str_has_prefix (GST_OBJECT_NAME (elem), "vulkanvp9dec")) {
      found = TRUE;
      g_value_unset (&item);
      break;
    }

    g_value_unset (&item);
  }

  if (!found)
    g_warning ("Hardware accelerated video decoding not in use, playback will likely be slow");

  gtk_widget_set_visible (GTK_WIDGET (self->img_accel), !found);
}

static void
on_player_state_changed (GstPlaySignalAdapter *adapter, GstPlayState state, gpointer user_data)
{
  LiviWindow *self = LIVI_WINDOW (user_data);
  GApplication *app = g_application_get_default ();
  const char *icon;

  g_assert (LIVI_IS_WINDOW (self));

  g_debug ("State %s", gst_play_state_get_name (state));
  self->state = state;

  if (state == GST_PLAY_STATE_PLAYING) {
    icon = "media-playback-pause-symbolic";
    self->cookie = gtk_application_inhibit (GTK_APPLICATION (app),
                                            GTK_WINDOW (self),
                                            GTK_APPLICATION_INHIBIT_SUSPEND | GTK_APPLICATION_INHIBIT_IDLE,
                                            "Playing video");
    check_pipeline (self, self->player);

    if (self->seek_target_state == STREAM_TARGET_STATE_PREVIEW) {
      /* The stream was only started to have a preview picture */
      self->seek_target_state = STREAM_TARGET_STATE_NONE;
      livi_window_set_pause (self);
    } else {
      hide_center_overlay (self);
      if (self->have_pointer)
        arm_hide_controls_timer (self);
    }
  } else {
    icon = "media-playback-start-symbolic";
    if (self->cookie) {
      gtk_application_uninhibit (GTK_APPLICATION (app), self->cookie);
      self->cookie = 0;
    }
  }

  /* Switch to desired target state after seek */
  switch (self->seek_target_state) {
  case STREAM_TARGET_STATE_PLAY:
    self->seek_target_state = STREAM_TARGET_STATE_NONE;
    gst_play_play (self->player);
    break;
  case STREAM_TARGET_STATE_PAUSE:
    self->seek_target_state = STREAM_TARGET_STATE_NONE;
    gst_play_pause (self->player);
    break;
  case STREAM_TARGET_STATE_PREVIEW:
  case STREAM_TARGET_STATE_NONE:
    break;
  default:
    g_assert_not_reached ();
  }

  livi_controls_set_play_icon (self->controls, icon);
  livi_recent_videos_update (self->recent_videos, self->stream.ref_uri, gst_play_get_position (self->player));

  g_object_notify_by_pspec (G_OBJECT (self), props[PROP_STATE]);
}


static void
on_player_mute_changed (GstPlaySignalAdapter *adapter, gboolean muted, gpointer user_data)
{
  LiviWindow *self = LIVI_WINDOW (user_data);
  const char *icon;

  g_assert (LIVI_IS_WINDOW (self));

  if  (self->stream.muted == muted)
    return;

  self->stream.muted = muted;
  g_debug ("Muted %d", muted);
  icon = muted ? "audio-volume-muted-symbolic" : "audio-volume-medium-symbolic";
  livi_controls_set_mute_icon (self->controls, icon);

  g_object_notify_by_pspec (G_OBJECT (self), props[PROP_MUTED]);
}


static void
on_player_duration_changed (GstPlaySignalAdapter *adapter, guint64 duration, gpointer user_data)
{
  LiviWindow *self = LIVI_WINDOW (user_data);

  g_assert (LIVI_IS_WINDOW (self));

  g_debug ("Duration %" G_GUINT64_FORMAT "s", duration / GST_SECOND);
  livi_controls_set_duration (self->controls, duration);
}


static void
on_player_position_updated (GstPlaySignalAdapter *adapter, guint64 position, gpointer user_data)
{
  LiviWindow *self = LIVI_WINDOW (user_data);

  self->seek_lock = FALSE;

  g_assert (LIVI_IS_WINDOW (self));
  livi_controls_set_position (self->controls, position);
}


G_DEFINE_AUTOPTR_CLEANUP_FUNC (GstPlayAudioInfo, g_object_unref)
G_DEFINE_AUTOPTR_CLEANUP_FUNC (GstPlaySubtitleInfo, g_object_unref)


static void
update_audio_streams (LiviWindow *self, GstPlayMediaInfo *info)
{
  g_autoptr (GMenu) menu = NULL;
  g_autoptr (GMenu) lang_section = NULL;
  g_autoptr (GMenu) subtitles_section = NULL;
  guint num_audio_streams, num_subtitle_streams;
  GList *streams;

  num_audio_streams = gst_play_media_info_get_number_of_audio_streams (info);
  num_subtitle_streams = gst_play_media_info_get_number_of_subtitle_streams (info);

  if (num_audio_streams == self->stream.num_audio_streams &&
      num_subtitle_streams == self->stream.num_subtitle_streams) {
    return;
  }

  self->stream.num_audio_streams = num_audio_streams;
  self->stream.num_subtitle_streams = num_subtitle_streams;

  if (num_audio_streams >= 2) {
    g_autoptr (GstPlayAudioInfo) current = NULL;
    GAction *sa;
    gint index = -1;

    current = gst_play_get_current_audio_track (self->player);
    if (current)
      index = gst_play_stream_info_get_index (GST_PLAY_STREAM_INFO (current));

    /* Ensure the current subtitle is pre-selected in the popover */
    sa = g_action_map_lookup_action (G_ACTION_MAP (self), "audio-stream");
    g_simple_action_set_state (G_SIMPLE_ACTION (sa), g_variant_new_int32 (index));

    streams = gst_play_media_info_get_audio_streams (info);
    lang_section = g_menu_new ();

    for (GList *l = streams; l; l = l->next) {
      GstPlayAudioInfo *ai = GST_PLAY_AUDIO_INFO (l->data);
      g_autofree char *action = NULL;
      const char *lang;

      index = gst_play_stream_info_get_index (GST_PLAY_STREAM_INFO (ai));
      if (index < 0)
        continue;

      lang = gst_play_audio_info_get_language (ai);
      action = g_strdup_printf ("win.audio-stream(%d)", index);
      g_menu_insert (lang_section, -1, lang, action);
    }
  }

  if (num_subtitle_streams) {
    g_autoptr (GstPlaySubtitleInfo) current = NULL;
    GAction *sa;
    gint index = -1;

    current = gst_play_get_current_subtitle_track (self->player);
    if (current)
      index = gst_play_stream_info_get_index (GST_PLAY_STREAM_INFO (current));

    /* Ensure the current subtitle is pre-selected in the popover */
    sa = g_action_map_lookup_action (G_ACTION_MAP (self), "subtitle-stream");
    g_simple_action_set_state (G_SIMPLE_ACTION (sa), g_variant_new_int32 (index));

    streams = gst_play_media_info_get_subtitle_streams (info);
    subtitles_section = g_menu_new ();

    /* Translators: None here means: disable subtitles */
    g_menu_insert (subtitles_section, -1, _("None"), "win.subtitle-stream(-1)");

    for (GList *l = streams; l; l = l->next) {
      GstPlaySubtitleInfo *si = GST_PLAY_SUBTITLE_INFO (l->data);
      g_autofree char *action = NULL;
      const char *lang;

      index = gst_play_stream_info_get_index (GST_PLAY_STREAM_INFO (si));
      if (index < 0)
        continue;

      lang = gst_play_subtitle_info_get_language (si);
      action = g_strdup_printf ("win.subtitle-stream(%d)", index);
      g_menu_insert (subtitles_section, -1, lang, action);
    }
  }

  if (!lang_section && !subtitles_section) {
    livi_controls_set_langs (self->controls, NULL);
    return;
  }

  menu = g_menu_new ();
  if (lang_section)
    g_menu_insert_section (menu, -1, _("Languages"), G_MENU_MODEL (lang_section));
  if (subtitles_section)
    g_menu_insert_section (menu, -1, _("Subtitles"), G_MENU_MODEL (subtitles_section));

  livi_controls_set_langs (self->controls, G_MENU_MODEL (menu));
}


static void
update_title (LiviWindow *self, GstPlayMediaInfo *info)
{
  g_autofree char *title = NULL;

  title = g_strdup (gst_play_media_info_get_title (info));

  if (!title) {
    const char *uri;
    g_autofree char *filename = NULL;

    uri = gst_play_media_info_get_uri (info);
    filename = g_filename_from_uri (uri, NULL, NULL);
    if (filename)
      title = g_path_get_basename (filename);
    else
      title = g_strdup (self->stream.ref_uri);
  }

  if (g_strcmp0 (title, self->stream.title) != 0) {
    g_free (self->stream.title);
    self->stream.title = g_steal_pointer (&title);
    g_object_notify_by_pspec (G_OBJECT (self), props[PROP_TITLE]);
  }
}


static void
update_video_streams (LiviWindow *self, GstPlayMediaInfo *info)
{
  guint num_video_streams;
  GstPlayVisualization **vis = NULL;
  gboolean success;
  const char *visname = NULL;

  num_video_streams = gst_play_media_info_get_number_of_video_streams (info);
  if (num_video_streams) {
    gst_play_set_visualization_enabled (self->player, FALSE);
    return;
  }

  vis = gst_play_visualizations_get ();

  if (!vis[0]) {
    g_warning ("No visualizations");
    goto out;
  }

  for (int i = 0; vis[i]; i++) {
    if (g_strcmp0 (vis[i]->name, "wavescope"))
      visname = "wavescope";
  }
  if (!visname)
    visname = vis[0]->name;

  success = gst_play_set_visualization (self->player, visname);
  if (!success) {
    g_warning ("Failed to enable visualization %s", visname);
    goto out;
  }

  g_debug ("Enabling visuzlization: %s", visname);
  gst_play_set_visualization_enabled (self->player, TRUE);

 out:
  gst_play_visualizations_free (vis);
}


static void
on_media_info_updated (GstPlaySignalAdapter *adapter, GstPlayMediaInfo *info, gpointer user_data)
{
  LiviWindow *self = LIVI_WINDOW (user_data);
  gboolean show;

  show = gst_play_media_info_get_number_of_audio_streams (info);

  update_audio_streams (self, info);
  update_video_streams (self, info);
  update_title (self, info);

  livi_controls_show_mute_button (self->controls, !!show);
}


static void
on_end_of_stream (GstPlaySignalAdapter *adapter, gpointer user_data)
{
  LiviWindow *self = LIVI_WINDOW (user_data);

  g_debug ("End of stream");

  show_resume_or_restart_overlay (self, FALSE);
}


static void
on_realize (LiviWindow *self)
{
  GdkSurface *surface;

  g_assert (LIVI_IS_WINDOW (self));

  surface = gtk_native_get_surface (gtk_widget_get_native (GTK_WIDGET (self->picture_video)));
  livi_gst_paintable_realize (LIVI_GST_PAINTABLE (self->paintable), surface);

  if (!self->player) {
    self->player = gst_play_new (GST_PLAY_VIDEO_RENDERER (g_object_ref (self->paintable)));
    self->signal_adapter = gst_play_signal_adapter_new (self->player);
    g_object_connect (self->signal_adapter,
                      "signal::error", G_CALLBACK (on_player_error), self,
                      "signal::warning", G_CALLBACK (on_player_warning), self,
                      "signal::buffering", G_CALLBACK (on_player_buffering), self,
                      "signal::state-changed", G_CALLBACK (on_player_state_changed), self,
                      "signal::mute-changed", G_CALLBACK (on_player_mute_changed), self,
                      "signal::duration-changed", G_CALLBACK (on_player_duration_changed), self,
                      "signal::position-updated", G_CALLBACK (on_player_position_updated), self,
                      "signal::media-info-updated", G_CALLBACK (on_media_info_updated), self,
                      "signal::end-of-stream", G_CALLBACK (on_end_of_stream), self,
                      NULL);
  }
}


static gboolean
livi_window_close_request (GtkWindow *window)
{
  LiviWindow *self = LIVI_WINDOW (window);

  if (self->stream.ref_uri) {
    livi_recent_videos_update (self->recent_videos,
                               self->stream.ref_uri,
                               gst_play_get_position (self->player));
  }

  return GTK_WINDOW_CLASS (livi_window_parent_class)->close_request (window);
}


static void
livi_window_dispose (GObject *obj)
{
  LiviWindow *self = LIVI_WINDOW (obj);

  g_clear_pointer (&self->last_local_uri, g_free);
  g_clear_object (&self->recent_videos);
  g_clear_object (&self->signal_adapter);
  g_clear_object (&self->player);
  if (self->cookie) {
    GApplication *app = g_application_get_default ();

    gtk_application_uninhibit (GTK_APPLICATION (app), self->cookie);
    self->cookie = 0;
  }
  reset_stream (self);

  G_OBJECT_CLASS (livi_window_parent_class)->dispose (obj);
}


static void
livi_window_class_init (LiviWindowClass *klass)
{
  GObjectClass *object_class = (GObjectClass *)klass;
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);
  GtkWindowClass *window_class = GTK_WINDOW_CLASS (klass);
  GtkCssProvider *provider;
  AdwStyleManager *manager = adw_style_manager_get_default ();

  object_class->get_property = livi_window_get_property;
  object_class->set_property = livi_window_set_property;
  object_class->dispose = livi_window_dispose;

  window_class->close_request = livi_window_close_request;

  props[PROP_MUTED] =
    g_param_spec_boolean ("muted", "", "",
                          FALSE,
                          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

  props[PROP_PLAYBACK_SPEED] =
    g_param_spec_int ("playback-speed", "", "",
                      10, G_MAXINT, 100,
                      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

  props[PROP_STATE] =
    g_param_spec_enum ("state", "", "",
                       GST_TYPE_PLAY_STATE,
                       GST_PLAY_STATE_STOPPED,
                       G_PARAM_READABLE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

  props[PROP_TITLE] =
    g_param_spec_string ("title", "", "",
                         NULL,
                         G_PARAM_READABLE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

  g_object_class_install_properties (object_class, LAST_PROP, props);

  g_type_ensure (LIVI_TYPE_CONTROLS);

  gtk_widget_class_set_template_from_resource (widget_class, "/org/sigxcpu/Livi/livi-window.ui");
  gtk_widget_class_bind_template_child (widget_class, LiviWindow, box_content);

  gtk_widget_class_bind_template_child (widget_class, LiviWindow, box_center);
  gtk_widget_class_bind_template_child (widget_class, LiviWindow, box_resume_or_restart);
  gtk_widget_class_bind_template_child (widget_class, LiviWindow, btn_resume);
  gtk_widget_class_bind_template_child (widget_class, LiviWindow, controls);
  gtk_widget_class_bind_template_child (widget_class, LiviWindow, empty_state);
  gtk_widget_class_bind_template_child (widget_class, LiviWindow, error_state);
  gtk_widget_class_bind_template_child (widget_class, LiviWindow, img_fullscreen);
  gtk_widget_class_bind_template_child (widget_class, LiviWindow, img_accel);
  gtk_widget_class_bind_template_child (widget_class, LiviWindow, img_center);
  gtk_widget_class_bind_template_child (widget_class, LiviWindow, lbl_center);
  gtk_widget_class_bind_template_child (widget_class, LiviWindow, lbl_status);
  gtk_widget_class_bind_template_child (widget_class, LiviWindow, overlay);
  gtk_widget_class_bind_template_child (widget_class, LiviWindow, picture_video);
  gtk_widget_class_bind_template_child (widget_class, LiviWindow, revealer_center);
  gtk_widget_class_bind_template_child (widget_class, LiviWindow, stack_center);
  gtk_widget_class_bind_template_child (widget_class, LiviWindow, stack_content);
  gtk_widget_class_bind_template_child (widget_class, LiviWindow, toolbar);
  gtk_widget_class_bind_template_child (widget_class, LiviWindow, video_filter);
  gtk_widget_class_bind_template_callback (widget_class, on_fullscreen);
  gtk_widget_class_bind_template_callback (widget_class, on_pointer_motion);
  gtk_widget_class_bind_template_callback (widget_class, on_pointer_enter);
  gtk_widget_class_bind_template_callback (widget_class, on_realize);

  gtk_widget_class_install_property_action (widget_class, "win.fullscreen", "fullscreened");
  gtk_widget_class_install_property_action (widget_class, "win.mute", "muted");
  gtk_widget_class_install_property_action (widget_class, "win.playback-speed", "playback-speed");
  gtk_widget_class_install_action (widget_class, "win.toggle-controls", NULL,
                                   on_toggle_controls_activated);
  gtk_widget_class_install_action (widget_class, "win.ff", "i", on_ff_rev_activated);
  gtk_widget_class_install_action (widget_class, "win.seek", "i", on_seek_activated);
  gtk_widget_class_install_action (widget_class, "win.toggle-play", NULL, on_toggle_play_activated);
  gtk_widget_class_install_action (widget_class, "win.open-file", NULL, on_open_file_activated);
  gtk_widget_class_install_action (widget_class, "win.restart", NULL, on_restart_activated);

  provider = gtk_css_provider_new ();
  gtk_css_provider_load_from_resource (provider, "/org/sigxcpu/Livi/style.css");
  gtk_style_context_add_provider_for_display (gdk_display_get_default (),
                                              GTK_STYLE_PROVIDER (provider),
                                              GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);

  adw_style_manager_set_color_scheme (manager, ADW_COLOR_SCHEME_PREFER_DARK);
}


static void
add_controls_toggle (LiviWindow *self, GtkWidget *widget)
{
  GtkGesture *gesture = gtk_gesture_click_new ();

  g_signal_connect_swapped (gesture, "pressed", G_CALLBACK (toggle_controls), self);
  gtk_event_controller_set_propagation_phase (GTK_EVENT_CONTROLLER (gesture), GTK_PHASE_TARGET);
  gtk_widget_add_controller (widget, GTK_EVENT_CONTROLLER (gesture));
}


static GActionEntry win_entries[] =
{
  { "subtitle-stream", NULL, "i", "-1", on_subtitle_stream_action_changed_state },
  { "audio-stream", NULL, "i", "0", on_audio_stream_action_changed_state },
};


static void
livi_window_init (LiviWindow *self)
{
  reset_stream (self);

  gtk_widget_init_template (GTK_WIDGET (self));

  self->paintable = livi_gst_paintable_new ();
  gtk_picture_set_paintable (self->picture_video, self->paintable);

  add_controls_toggle (self, GTK_WIDGET (self->picture_video));
  add_controls_toggle (self, GTK_WIDGET (self->revealer_center));

  arm_hide_controls_timer (self);

  self->recent_videos = livi_recent_videos_new ();

  g_action_map_add_action_entries (G_ACTION_MAP (self),
                                   win_entries, G_N_ELEMENTS (win_entries),
                                   self);
}


static void
livi_window_set_uris (LiviWindow *self, const char *uri, const char *ref_uri)
{
  gint64 pos;

  g_assert (LIVI_IS_WINDOW (self));

  reset_stream (self);
  gtk_stack_set_visible_child (self->stack_content, GTK_WIDGET (self->box_content));

  gst_play_set_uri (self->player, uri);

  if (ref_uri)
    self->stream.ref_uri = g_strdup (ref_uri);
  else
    self->stream.ref_uri = g_strdup (uri);

  pos = livi_recent_videos_get_pos (self->recent_videos, self->stream.ref_uri);
  if (pos > 0) {
    pos *= GST_MSECOND;
    /* Seek directly without showing any overlays */
    g_debug ("Found video %s, resuming at %ld s", uri, pos / GST_SECOND);
    gst_play_seek (self->player, pos);
    show_resume_or_restart_overlay (self, TRUE);
  }
}


void
livi_window_set_empty_state (LiviWindow *self)
{
  g_assert (LIVI_IS_WINDOW (self));

  gtk_stack_set_visible_child (self->stack_content, GTK_WIDGET (self->empty_state));
}

void
livi_window_set_error_state (LiviWindow *self, const char *description)
{
  g_assert (LIVI_IS_WINDOW (self));

  gtk_stack_set_visible_child (self->stack_content, GTK_WIDGET (self->error_state));
  adw_status_page_set_description (self->error_state, description);
}

void
livi_window_set_play (LiviWindow *self)
{
  g_assert (LIVI_IS_WINDOW (self));

  gst_play_play (self->player);
}

void
livi_window_set_pause (LiviWindow *self)
{
  g_assert (LIVI_IS_WINDOW (self));

  gst_play_pause (self->player);
}

/**
 * livi_window_play_uri:
 * @self: the window
 * @uri: The uri to play
 * @ref_uri:(nullable): The reference uri
 *
 * Plays the given URL. if `ref_url` is given that is used instead of the "real"
 * URL when e.g. remembering player state. This can be usedful for preprocessed
 * URLs that give the "backend" URL that changes between plays.
 *
 * If `ref_uri` is `NULL` it's assumed to be identical to the `uri`.
 */
void
livi_window_play_uri (LiviWindow *self, const char *uri, const char *ref_uri)
{
  g_assert (LIVI_IS_WINDOW (self));

  g_debug ("Playing %s %s", uri, ref_uri);
  livi_window_set_uris (self, uri, ref_uri);
  livi_window_set_play (self);

  arm_hide_controls_timer (self);
}
