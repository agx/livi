/* livi-window.c
 *
 * Copyright 2021 Purism SPC
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Author: Guido Günther <agx@sigxcpu.org>
 */

#define G_LOG_DOMAIN "livi-window"

#include "livi-config.h"
#include "livi-window.h"
#include "livi-gst-paintable.h"

#include <gst/gst.h>
#include <gst/play/gstplay.h>
#include <gst/play/gstplay-signal-adapter.h>

#include <adwaita.h>
#include <glib/gi18n.h>

#define STR_IS_NULL_OR_EMPTY(x) ((x) == NULL || (x)[0] == '\0')

enum {
  PROP_0,
  PROP_MUTED,
  PROP_PLAYBACK_SPEED,
  LAST_PROP,
};
static GParamSpec *props[LAST_PROP];


struct _LiviWindow
{
  AdwApplicationWindow  parent_instance;

  GtkStack             *stack_content;

  GtkBox               *box_content;
  GtkPicture           *picture_video;
  GdkPaintable         *paintable;
  GtkOverlay           *overlay;

  AdwToolbarView       *toolbar;
  /* topbar */
  GtkLabel             *lbl_status;
  GtkImage             *img_accel;
  /* bottombar */
  GtkButton            *btn_play;
  GtkImage             *img_play;
  GtkButton            *btn_mute;
  GtkImage             *img_mute;
  GtkLabel             *lbl_time;
  GtkAdjustment        *adj_duration;
  GtkImage             *img_fullscreen;

  GtkRevealer          *revealer_center;
  GtkRevealer          *box_center;
  GtkLabel             *lbl_center;
  GtkImage             *img_center;
  guint                 reveal_id;

  AdwStatusPage        *error_state;
  GtkBox               *empty_state;

  GstPlay              *player;
  GstPlaySignalAdapter *signal_adapter;
  GstPlayState          state;
  guint                 cookie;

  guint64               duration;
  guint64               position;

  gboolean              muted;
  int                   playback_speed;

  GtkFileFilter        *video_filter;
  char                 *last_local_uri;
};

G_DEFINE_TYPE (LiviWindow, livi_window, ADW_TYPE_APPLICATION_WINDOW)


static void
livi_window_set_playback_speed (LiviWindow *self, int percent)
{
  g_debug ("Setting Rate to : %f", percent / 100.0);

  if (percent == self->playback_speed)
    return;

  if (self->player)
    gst_play_set_rate (self->player, percent / 100.0);

  self->playback_speed = percent;
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
      self->muted = muted;
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
      g_value_set_boolean (value, self->muted);
      break;
    case PROP_PLAYBACK_SPEED:
      g_value_set_int (value, self->playback_speed);
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

  adw_toolbar_view_set_reveal_top_bars (self->toolbar, !fullscreen);
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

  g_object_set (self->img_center, "icon-name", icon_name, NULL);
  gtk_revealer_set_reveal_child (self->revealer_center, TRUE);

  if (fade)
    self->reveal_id = g_timeout_add_once (500, on_reveal_timeout, self);
}


static void
hide_center_overlay (LiviWindow *self)
{
  gtk_revealer_set_reveal_child (self->revealer_center, FALSE);
}


static void
update_slider (LiviWindow *self, GstClockTime value)
{
  g_autofree char *text = NULL;
  guint64 pos, dur;

  g_assert (LIVI_IS_WINDOW (self));
  gtk_adjustment_set_value (self->adj_duration, value);

  dur = self->duration / GST_SECOND;
  pos = value / GST_SECOND;

  text = g_strdup_printf ("%.2" G_GUINT64_FORMAT ":%.2" G_GUINT64_FORMAT
                          " / %.2" G_GUINT64_FORMAT " :%.2" G_GUINT64_FORMAT,
                          pos / 60, pos % 60, dur / 60, dur % 60);
  gtk_label_set_text (self->lbl_time, text);
}

static gboolean
on_slider_value_changed (LiviWindow *self, GtkScrollType scroll, double value)
{
  const char *icon_name;

  if (scroll == GTK_SCROLL_JUMP) {
    if (value > self->position)
      icon_name = "media-seek-forward-symbolic";
    else
      icon_name = "media-seek-backward-symbolic";

    show_center_overlay (self, icon_name, NULL, TRUE);
  }

  gst_play_seek (self->player, value);

  return TRUE;
}

static void
toggle_controls (LiviWindow *self)
{
  gboolean revealed, fullscreen;

  g_assert (LIVI_IS_WINDOW (self));
  revealed = adw_toolbar_view_get_reveal_bottom_bars (self->toolbar);
  adw_toolbar_view_set_reveal_bottom_bars (self->toolbar, !revealed);

  fullscreen = gtk_window_is_fullscreen (GTK_WINDOW (self));
  /* Only hide the topbar when fullscreen */
  if (fullscreen)
    adw_toolbar_view_set_reveal_top_bars (self->toolbar, !revealed);
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
  livi_window_play_url (self, uri);
  g_free (self->last_local_uri);
  self->last_local_uri = g_steal_pointer (&uri);
}


static void
on_open_file_activated (GtkWidget *widget, const char *action_name, GVariant *unused)
{
  LiviWindow *self = LIVI_WINDOW (widget);
  GtkFileDialog *dialog;

  dialog = gtk_file_dialog_new ();
  gtk_file_dialog_set_title (dialog, _("Choose a video to play"));
  gtk_file_dialog_set_default_filter (dialog, self->video_filter);

  if (!STR_IS_NULL_OR_EMPTY (self->last_local_uri)) {
    g_autoptr (GFile) current_file = g_file_new_for_uri (self->last_local_uri);
    gtk_file_dialog_set_initial_file (dialog, current_file);
  }

  gtk_file_dialog_open (dialog, GTK_WINDOW (self), NULL, on_file_chooser_done, self);
}


static void
on_ff_rev_activated (GtkWidget *widget, const char *action_name, GVariant *param)
{
  LiviWindow *self = LIVI_WINDOW (widget);
  GstClockTime pos;
  const char *icon_name;
  g_autofree char *label;
  gint64 offset;

  offset = g_variant_get_int32 (param) * GST_MSECOND;

  pos = gst_play_get_position (self->player);
  pos += offset;
  label = g_strdup_printf (_("%.2lds"), labs(offset / GST_SECOND));
  icon_name = (offset > 0) ? "media-seek-forward-symbolic" : "media-seek-backward-symbolic";

  show_center_overlay (self, icon_name, label, TRUE);
  gst_play_seek (self->player, pos);
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

    if (g_str_has_prefix (GST_OBJECT_NAME (elem), "v4l2slh264dec") ||
        g_str_has_prefix (GST_OBJECT_NAME (elem), "v4l2slh265dec") ||
        g_str_has_prefix (GST_OBJECT_NAME (elem), "v4l2slmpeg2dec") ||
        g_str_has_prefix (GST_OBJECT_NAME (elem), "v4l2slvp8dec") ||
        g_str_has_prefix (GST_OBJECT_NAME (elem), "v4l2slvp9dec")) {
      found = TRUE;
      g_value_unset (&item);
      break;
    }

    g_value_unset (&item);
  }

  if (!found)
    g_warning ("V4L stateless codec not in use, playback will likely be slow");

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

    hide_center_overlay (self);
  } else {
    icon = "media-playback-start-symbolic";
    if (self->cookie) {
      gtk_application_uninhibit (GTK_APPLICATION (app), self->cookie);
      self->cookie = 0;
    }
  }

  g_object_set (self->img_play, "icon-name", icon, NULL);
}


static void
on_player_mute_changed (GstPlaySignalAdapter *adapter, gboolean muted, gpointer user_data)
{
  LiviWindow *self = LIVI_WINDOW (user_data);
  const char *icon;

  g_assert (LIVI_IS_WINDOW (self));

  if  (self->muted == muted)
    return;

  self->muted = muted;
  g_debug ("Muted %d", muted);
  icon = muted ? "audio-volume-medium-symbolic" : "audio-volume-muted-symbolic";
  g_object_set (self->img_mute, "icon-name", icon, NULL);

  g_object_notify_by_pspec (G_OBJECT (self), props[PROP_MUTED]);
}


static void
on_player_duration_changed (GstPlaySignalAdapter *adapter, guint64 duration, gpointer user_data)
{
  LiviWindow *self = LIVI_WINDOW (user_data);

  g_assert (LIVI_IS_WINDOW (self));

  g_debug ("Duration %" G_GUINT64_FORMAT "s", duration / GST_SECOND);
  self->duration = duration;

  gtk_adjustment_set_upper (self->adj_duration, self->duration);
}


static void
on_player_position_updated (GstPlaySignalAdapter *adapter, guint64 position, gpointer user_data)
{
  LiviWindow *self = LIVI_WINDOW (user_data);

  g_assert (LIVI_IS_WINDOW (self));

  self->position = position;

  update_slider(self, position);
}


static void
on_media_info_updated (GstPlaySignalAdapter *adapter, GstPlayMediaInfo *info, gpointer user_data)
{
  LiviWindow *self = LIVI_WINDOW (user_data);
  gboolean show;

  show = gst_play_media_info_get_number_of_audio_streams (info);
  gtk_widget_set_visible (GTK_WIDGET (self->btn_mute), !!show);
}


static void
on_end_of_stream (GstPlaySignalAdapter *adapter, gpointer user_data)
{
  LiviWindow *self = LIVI_WINDOW (user_data);

  g_debug ("End of stream");
  show_center_overlay (self, "starred-symbolic", _("Video ended"), FALSE);
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


static void
livi_window_dispose (GObject *obj)
{
  LiviWindow *self = LIVI_WINDOW (obj);

  g_clear_pointer (&self->last_local_uri, g_free);
  g_clear_object (&self->signal_adapter);
  g_clear_object (&self->player);
  if (self->cookie) {
    GApplication *app = g_application_get_default ();

    gtk_application_uninhibit (GTK_APPLICATION (app), self->cookie);
    self->cookie = 0;
  }

  G_OBJECT_CLASS (livi_window_parent_class)->dispose (obj);
}


static void
livi_window_class_init (LiviWindowClass *klass)
{
  GObjectClass *object_class = (GObjectClass *)klass;
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);
  GtkCssProvider *provider;
  AdwStyleManager *manager = adw_style_manager_get_default ();

  object_class->get_property = livi_window_get_property;
  object_class->set_property = livi_window_set_property;
  object_class->dispose = livi_window_dispose;

  props[PROP_MUTED] =
    g_param_spec_boolean (
      "muted",
      "",
      "",
      FALSE,
      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

  props[PROP_PLAYBACK_SPEED] =
    g_param_spec_int (
      "playback-speed", "", "",
      10, G_MAXINT, 100,
      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

  g_object_class_install_properties (object_class, LAST_PROP, props);

  gtk_widget_class_set_template_from_resource (widget_class, "/org/sigxcpu/Livi/livi-window.ui");
  gtk_widget_class_bind_template_child (widget_class, LiviWindow, adj_duration);
  gtk_widget_class_bind_template_child (widget_class, LiviWindow, box_content);

  gtk_widget_class_bind_template_child (widget_class, LiviWindow, box_center);
  gtk_widget_class_bind_template_child (widget_class, LiviWindow, empty_state);
  gtk_widget_class_bind_template_child (widget_class, LiviWindow, error_state);
  gtk_widget_class_bind_template_child (widget_class, LiviWindow, btn_mute);
  gtk_widget_class_bind_template_child (widget_class, LiviWindow, btn_play);
  gtk_widget_class_bind_template_child (widget_class, LiviWindow, img_fullscreen);
  gtk_widget_class_bind_template_child (widget_class, LiviWindow, img_accel);
  gtk_widget_class_bind_template_child (widget_class, LiviWindow, img_play);
  gtk_widget_class_bind_template_child (widget_class, LiviWindow, img_mute);
  gtk_widget_class_bind_template_child (widget_class, LiviWindow, img_center);
  gtk_widget_class_bind_template_child (widget_class, LiviWindow, lbl_center);
  gtk_widget_class_bind_template_child (widget_class, LiviWindow, lbl_status);
  gtk_widget_class_bind_template_child (widget_class, LiviWindow, lbl_time);
  gtk_widget_class_bind_template_child (widget_class, LiviWindow, overlay);
  gtk_widget_class_bind_template_child (widget_class, LiviWindow, picture_video);
  gtk_widget_class_bind_template_child (widget_class, LiviWindow, revealer_center);
  gtk_widget_class_bind_template_child (widget_class, LiviWindow, stack_content);
  gtk_widget_class_bind_template_child (widget_class, LiviWindow, toolbar);
  gtk_widget_class_bind_template_child (widget_class, LiviWindow, video_filter);
  gtk_widget_class_bind_template_callback (widget_class, on_fullscreen);
  gtk_widget_class_bind_template_callback (widget_class, on_realize);
  gtk_widget_class_bind_template_callback (widget_class, on_slider_value_changed);

  gtk_widget_class_install_property_action (widget_class, "win.fullscreen", "fullscreened");
  gtk_widget_class_install_property_action (widget_class, "win.mute", "muted");
  gtk_widget_class_install_property_action (widget_class, "win.playback-speed", "playback-speed");
  gtk_widget_class_install_action (widget_class, "win.toggle-controls", NULL,
                                   on_toggle_controls_activated);
  gtk_widget_class_install_action (widget_class, "win.ff", "i", on_ff_rev_activated);
  gtk_widget_class_install_action (widget_class, "win.rev", "i", on_ff_rev_activated);
  gtk_widget_class_install_action (widget_class, "win.toggle-play", NULL, on_toggle_play_activated);
  gtk_widget_class_install_action (widget_class, "win.open-file", NULL, on_open_file_activated);

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


static void
livi_window_init (LiviWindow *self)
{
  self->playback_speed = 100;

  gtk_widget_init_template (GTK_WIDGET (self));

  self->paintable = livi_gst_paintable_new ();
  gtk_picture_set_paintable (self->picture_video, self->paintable);

  add_controls_toggle (self, GTK_WIDGET (self->picture_video));
  add_controls_toggle (self, GTK_WIDGET (self->revealer_center));
}


void
livi_window_set_uri (LiviWindow *self, const char *uri)
{
  gtk_stack_set_visible_child (self->stack_content, GTK_WIDGET (self->box_content));
  gst_play_set_uri (self->player, uri);
}


void
livi_window_set_empty_state (LiviWindow *self)
{
  gtk_stack_set_visible_child (self->stack_content, GTK_WIDGET (self->empty_state));
}

void
livi_window_set_error_state (LiviWindow *self, const char *description)
{
  gtk_stack_set_visible_child (self->stack_content, GTK_WIDGET (self->error_state));
  adw_status_page_set_description (self->error_state, description);
}

void
livi_window_set_play (LiviWindow *self)
{
  gst_play_play (self->player);
}

void
livi_window_set_pause (LiviWindow *self)
{
  gst_play_pause (self->player);
}

void
livi_window_play_url (LiviWindow *self, const char *url)
{
  g_debug ("Playing %s", url);
  livi_window_set_uri (self, url);
  livi_window_set_play (self);
}
