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
#include <gst/player/gstplayer.h>
#include <gst/player/gstplayer-g-main-context-signal-dispatcher.h>

#include <glib/gi18n.h>


struct _LiviWindow
{
  GtkApplicationWindow  parent_instance;

  GtkBox               *box_content;
  GtkPicture           *picture_video;
  GdkPaintable         *paintable;
  GtkOverlay           *overlay;

  GtkRevealer          *revealer_info;
  GtkLabel             *lbl_title;
  GtkLabel             *lbl_status;
  GtkImage             *img_accel;

  GtkRevealer          *revealer_controls;
  GtkButton            *btn_play;
  GtkImage             *img_play;
  GtkButton            *btn_mute;
  GtkImage             *img_mute;
  GtkLabel             *lbl_time;
  GtkAdjustment        *adj_duration;
  GtkImage             *img_fullscreen;

  GstPlayer            *player;
  GstPlayerState        state;
  guint                 cookie;

  guint64               duration;
  guint64               position;
};

G_DEFINE_TYPE (LiviWindow, livi_window, GTK_TYPE_APPLICATION_WINDOW)


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


static gboolean
on_slider_value_changed (LiviWindow *self, GtkScrollType scroll, double value)
{
  g_assert (LIVI_IS_WINDOW (self));

  gst_player_seek (self->player, value);
  return TRUE;
}


static void
on_img_clicked (LiviWindow *self)
{
  gboolean revealed;

  g_assert (LIVI_IS_WINDOW (self));
  revealed = gtk_revealer_get_child_revealed (self->revealer_controls);

  gtk_revealer_set_reveal_child (self->revealer_controls, !revealed);
  gtk_revealer_set_reveal_child (self->revealer_info, !revealed);
}


static void
on_btn_fullscreen_clicked (LiviWindow *self)
{
  gboolean fullscreen;

  g_assert (LIVI_IS_WINDOW (self));
  fullscreen = gtk_window_is_fullscreen (GTK_WINDOW (self));

  g_object_set (self, "fullscreened", !fullscreen, NULL);
}


static void
on_btn_mute_clicked (LiviWindow *self)
{
  gboolean mute;

  g_assert (LIVI_IS_WINDOW (self));

  mute = gst_player_get_mute (self->player);
  gst_player_set_mute (self->player, !mute);
}


static void
on_btn_play_clicked (LiviWindow *self)
{
  g_assert (LIVI_IS_WINDOW (self));

  if (self->state == GST_PLAYER_STATE_PLAYING)
    gst_player_pause (self->player);
  else
    gst_player_play (self->player);
}


static void
on_player_error (GstPlayer *player, GError *error, LiviWindow *self)
{
  g_warning ("Player error: %s", error->message);
}


static void
on_player_buffering (GstPlayer *player, gint percent, gpointer user_data)
{
  LiviWindow *self = LIVI_WINDOW (user_data);
  g_autofree char *msg = NULL;

  g_assert (LIVI_IS_WINDOW (self));

  g_debug ("Buffering %d", percent);
  if (percent == 100) {
    gtk_widget_hide (GTK_WIDGET (self->lbl_status));
    return;
  }

  msg = g_strdup_printf (_("Buffering %d/100"), percent);
  gtk_label_set_text (self->lbl_status, msg);
  gtk_widget_show (GTK_WIDGET (self->lbl_status));
}


static void
check_pipeline (LiviWindow *self, GstPlayer *player)
{
  g_autoptr (GstElement) bin = gst_player_get_pipeline (player);
  g_autoptr (GstIterator) iter = gst_bin_iterate_recurse (GST_BIN (bin));
  GtkStyleContext *context;
  GValue item = { 0, };
  gboolean found = FALSE;
  const char *icons[] = { "speedometer2-symbolic",
    "speedometer4-symbolic" };
  const char *style_class[] = { "no-accel",
    "accel" };

  while (iter && gst_iterator_next (iter, &item) == GST_ITERATOR_OK) {
    GstElement *elem = g_value_get_object (&item);

    if (g_str_has_prefix (GST_OBJECT_NAME (elem), "v4l2slh264dec")) {
      found = TRUE;
      g_value_unset (&item);
      break;
    }

    g_value_unset (&item);
  }

  if (!found)
    g_warning ("V4L stateless codec not in use, playback will likely be slow");

  g_object_set (self->img_accel, "icon-name", icons[found], NULL);

  context = gtk_widget_get_style_context (GTK_WIDGET (self->img_accel));
  gtk_style_context_add_class (context, style_class[found]);
  gtk_style_context_remove_class (context, style_class[!found]);
}

static void
on_player_state_changed (GstPlayer *player, GstPlayerState state, gpointer user_data)
{
  LiviWindow *self = LIVI_WINDOW (user_data);
  GApplication *app = g_application_get_default ();
  g_autofree char *msg = NULL;
  const char *icon;

  g_assert (LIVI_IS_WINDOW (self));

  g_debug ("State %s", gst_player_state_get_name (state));
  self->state = state;

  if (state == GST_PLAYER_STATE_PLAYING) {
    icon = "media-playback-pause-symbolic";
    self->cookie = gtk_application_inhibit (GTK_APPLICATION (app),
					    GTK_WINDOW (self),
					    GTK_APPLICATION_INHIBIT_SUSPEND | GTK_APPLICATION_INHIBIT_IDLE,
					    "Playing video");
    check_pipeline (self, player);
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
on_player_mute_changed (GstPlayer *player, gpointer user_data)
{
  LiviWindow *self = LIVI_WINDOW (user_data);
  gboolean muted;
  const char *icon;

  g_assert (LIVI_IS_WINDOW (self));

  muted = gst_player_get_mute (self->player);
  g_debug ("Muted %d", muted);
  icon = muted ? "audio-volume-medium-symbolic" : "audio-volume-muted-symbolic";
  g_object_set (self->img_mute, "icon-name", icon, NULL);
}


static void
on_player_duration_changed (GstPlayer *player, guint64 duration, gpointer user_data)
{
  LiviWindow *self = LIVI_WINDOW (user_data);

  g_assert (LIVI_IS_WINDOW (self));

  g_debug ("Duration %lds", duration / GST_SECOND);
  self->duration = duration;

  gtk_adjustment_set_upper (self->adj_duration, self->duration);
}


static void
on_player_position_updated (GstPlayer *player, guint64 position, gpointer user_data)
{
  LiviWindow *self = LIVI_WINDOW (user_data);
  g_autofree char *text = NULL;
  guint64 pos, dur;

  g_assert (LIVI_IS_WINDOW (self));

  self->position = position;

  gtk_adjustment_set_value (self->adj_duration, self->position);
  dur = self->duration / GST_SECOND;
  pos = self->position / GST_SECOND;

  if (pos == dur)
    return;

  text = g_strdup_printf ("%.2ld:%.2ld / %.2ld:%.2ld",
			  pos / 60, pos % 60,
			  dur / 60, dur % 60);
  gtk_label_set_text (self->lbl_time, text);
}


static void
on_media_info_updated (GstPlayer *player, GstPlayerMediaInfo * info, gpointer user_data)
{
  LiviWindow *self = LIVI_WINDOW (user_data);
  g_autofree char *text = NULL;
  const gchar *title;
  gint show;

  show = gst_player_media_info_get_number_of_audio_streams (info);
  gtk_widget_set_visible (GTK_WIDGET (self->btn_mute), !!show);

  title = gst_player_media_info_get_title (info);
  if (!title)
    title = gst_player_media_info_get_uri (info);

  gtk_label_set_text (self->lbl_title, title);
}


static void
on_realize (LiviWindow *self)
{
  GdkSurface *surface;

  g_assert (LIVI_IS_WINDOW (self));

  surface = gtk_native_get_surface (gtk_widget_get_native (GTK_WIDGET (self->picture_video)));
  gtk_gst_paintable_realize (GTK_GST_PAINTABLE (self->paintable), surface);

  if (!self->player) {
    self->player = gst_player_new (GST_PLAYER_VIDEO_RENDERER (g_object_ref (self->paintable)),
				   gst_player_g_main_context_signal_dispatcher_new (NULL));
    g_object_connect (self->player,
		      "signal::error", G_CALLBACK (on_player_error), self,
		      "signal::buffering", G_CALLBACK (on_player_buffering), self,
		      "signal::state-changed", G_CALLBACK (on_player_state_changed), self,
		      "signal::mute-changed", G_CALLBACK (on_player_mute_changed), self,
		      "signal::duration-changed", G_CALLBACK (on_player_duration_changed), self,
		      "signal::position-updated", G_CALLBACK (on_player_position_updated), self,
		      "signal::media-info-updated", G_CALLBACK (on_media_info_updated), self,
		      NULL);
  }
}


static void
livi_window_dispose (GObject *obj)
{
  LiviWindow *self = LIVI_WINDOW (obj);

  g_clear_object (&self->player);
  if (self->cookie) {
    GApplication *app = g_application_get_default ();

    gtk_application_uninhibit (GTK_APPLICATION (app), self->cookie);
  }

  G_OBJECT_CLASS (livi_window_parent_class)->dispose (obj);
}


static void
livi_window_class_init (LiviWindowClass *klass)
{
  GObjectClass *object_class = (GObjectClass *)klass;
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);
  GtkCssProvider *provider;

  object_class->dispose = livi_window_dispose;

  gtk_widget_class_set_template_from_resource (widget_class, "/org/sigxcpu/Livi/livi-window.ui");
  gtk_widget_class_bind_template_child (widget_class, LiviWindow, adj_duration);
  gtk_widget_class_bind_template_child (widget_class, LiviWindow, box_content);
  gtk_widget_class_bind_template_child (widget_class, LiviWindow, btn_mute);
  gtk_widget_class_bind_template_child (widget_class, LiviWindow, btn_play);
  gtk_widget_class_bind_template_child (widget_class, LiviWindow, img_fullscreen);
  gtk_widget_class_bind_template_child (widget_class, LiviWindow, img_accel);
  gtk_widget_class_bind_template_child (widget_class, LiviWindow, img_play);
  gtk_widget_class_bind_template_child (widget_class, LiviWindow, img_mute);
  gtk_widget_class_bind_template_child (widget_class, LiviWindow, lbl_status);
  gtk_widget_class_bind_template_child (widget_class, LiviWindow, lbl_time);
  gtk_widget_class_bind_template_child (widget_class, LiviWindow, lbl_title);
  gtk_widget_class_bind_template_child (widget_class, LiviWindow, overlay);
  gtk_widget_class_bind_template_child (widget_class, LiviWindow, picture_video);
  gtk_widget_class_bind_template_child (widget_class, LiviWindow, revealer_controls);
  gtk_widget_class_bind_template_child (widget_class, LiviWindow, revealer_info);
  gtk_widget_class_bind_template_callback (widget_class, on_btn_fullscreen_clicked);
  gtk_widget_class_bind_template_callback (widget_class, on_btn_mute_clicked);
  gtk_widget_class_bind_template_callback (widget_class, on_btn_play_clicked);
  gtk_widget_class_bind_template_callback (widget_class, on_fullscreen);
  gtk_widget_class_bind_template_callback (widget_class, on_realize);
  gtk_widget_class_bind_template_callback (widget_class, on_slider_value_changed);

  provider = gtk_css_provider_new ();
  gtk_css_provider_load_from_resource (provider, "/org/sigxcpu/Livi/style.css");
  gtk_style_context_add_provider_for_display (gdk_display_get_default (),
                                              GTK_STYLE_PROVIDER (provider),
                                              GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);

  g_object_set (gtk_settings_get_default (),
                "gtk-application-prefer-dark-theme", TRUE,
                NULL);
}


static void
livi_window_init (LiviWindow *self)
{
  GtkGesture *gesture;

  gtk_widget_init_template (GTK_WIDGET (self));

  self->paintable = gtk_gst_paintable_new ();

  gtk_picture_set_paintable (self->picture_video, self->paintable);

  gesture = gtk_gesture_click_new ();
  g_signal_connect_swapped (gesture, "pressed",
			    G_CALLBACK (on_img_clicked), self);
  gtk_event_controller_set_propagation_phase (GTK_EVENT_CONTROLLER (gesture),
					      GTK_PHASE_TARGET);
  gtk_widget_add_controller (GTK_WIDGET (self->picture_video), GTK_EVENT_CONTROLLER (gesture));
}


void
livi_window_set_uri (LiviWindow *self, const char *uri)
{
  gst_player_set_uri (self->player, uri);
}


void
livi_window_set_play (LiviWindow *self)
{
  gst_player_play (self->player);
}

void
livi_window_set_pause (LiviWindow *self)
{
  gst_player_pause (self->player);
}
