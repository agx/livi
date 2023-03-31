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


enum {
  PROP_0,
  PROP_MUTED,
  LAST_PROP,
};
static GParamSpec *props[LAST_PROP];


struct _LiviWindow
{
  GtkApplicationWindow  parent_instance;

  GtkStack             *stack_content;

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

  GtkBox               *box_error;
  GtkBox               *box_placeholder;

  GstPlay              *player;
  GstPlaySignalAdapter *signal_adapter;
  GstPlayState          state;
  guint                 cookie;

  guint64               duration;
  guint64               position;

  gboolean              muted;
};

G_DEFINE_TYPE (LiviWindow, livi_window, GTK_TYPE_APPLICATION_WINDOW)


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
update_slider (LiviWindow *self, GstClockTime value)
{
  g_autofree char *text = NULL;
  guint64 pos, dur;

  g_assert (LIVI_IS_WINDOW (self));
  gtk_adjustment_set_value (self->adj_duration, value);

  dur = self->duration / GST_SECOND;
  pos = value / GST_SECOND;

  text = g_strdup_printf ("%.2ld:%.2ld / %.2ld:%.2ld",
                          pos / 60, pos % 60,
                          dur / 60, dur % 60);
  gtk_label_set_text (self->lbl_time, text);
}

static gboolean
on_slider_value_changed (LiviWindow *self, GtkScrollType scroll, double value)
{
  g_assert (LIVI_IS_WINDOW (self));

  gst_play_seek (self->player, value);

  update_slider(self, value);

  return TRUE;
}

static void
toggle_controls (LiviWindow *self)
{
  gboolean revealed;

  g_assert (LIVI_IS_WINDOW (self));
  revealed = gtk_revealer_get_child_revealed (self->revealer_controls);

  gtk_revealer_set_reveal_child (self->revealer_controls, !revealed);
  gtk_revealer_set_reveal_child (self->revealer_info, !revealed);
}


static void
on_img_clicked (LiviWindow *self)
{
  toggle_controls (self);
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

  if (self->state == GST_PLAY_STATE_PLAYING)
    gst_play_pause (self->player);
  else
    gst_play_play (self->player);
}


static void
on_ff_rev_activated (GtkWidget  *widget, const char *action_name, GVariant *unused)
{
  LiviWindow *self = LIVI_WINDOW (widget);
  GstClockTime pos;

  pos = gst_play_get_position (self->player);
  if (g_strcmp0 (action_name, "win.ff") == 0)
    pos += GST_SECOND * 30;
  else
    pos -= GST_SECOND * 10;

  gst_play_seek (self->player, pos);
}


static void
on_player_error (GstPlaySignalAdapter *adapter, GError *error, LiviWindow *self)
{
  g_warning ("Player error: %s", error->message);

  gtk_stack_set_visible_child (self->stack_content, GTK_WIDGET (self->box_error));
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
  GtkStyleContext *context;
  GValue item = { 0, };
  gboolean found = FALSE;
  const char *icons[] = { "speedometer2-symbolic",
    "speedometer4-symbolic" };
  const char *style_class[] = { "no-accel",
    "accel" };

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

  g_object_set (self->img_accel, "icon-name", icons[found], NULL);

  context = gtk_widget_get_style_context (GTK_WIDGET (self->img_accel));
  gtk_style_context_add_class (context, style_class[found]);
  gtk_style_context_remove_class (context, style_class[!found]);
}

static void
on_player_state_changed (GstPlaySignalAdapter *adapter, GstPlayState state, gpointer user_data)
{
  LiviWindow *self = LIVI_WINDOW (user_data);
  GApplication *app = g_application_get_default ();
  g_autofree char *msg = NULL;
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

  g_debug ("Duration %lds", duration / GST_SECOND);
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
on_media_info_updated (GstPlaySignalAdapter *adapter, GstPlayMediaInfo * info, gpointer user_data)
{
  LiviWindow *self = LIVI_WINDOW (user_data);
  g_autofree char *text = NULL;
  const gchar *title;
  gboolean show;

  show = gst_play_media_info_get_number_of_audio_streams (info);
  gtk_widget_set_visible (GTK_WIDGET (self->btn_mute), !!show);

  title = gst_play_media_info_get_title (info);
  if (!title)
    title = gst_play_media_info_get_uri (info);

  gtk_label_set_text (self->lbl_title, title);
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

  g_clear_object (&self->signal_adapter);
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

  g_object_class_install_properties (object_class, LAST_PROP, props);

  gtk_widget_class_set_template_from_resource (widget_class, "/org/sigxcpu/Livi/livi-window.ui");
  gtk_widget_class_bind_template_child (widget_class, LiviWindow, adj_duration);
  gtk_widget_class_bind_template_child (widget_class, LiviWindow, box_content);
  gtk_widget_class_bind_template_child (widget_class, LiviWindow, box_error);

  gtk_widget_class_bind_template_child (widget_class, LiviWindow, box_placeholder);
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
  gtk_widget_class_bind_template_child (widget_class, LiviWindow, stack_content);
  gtk_widget_class_bind_template_callback (widget_class, on_fullscreen);
  gtk_widget_class_bind_template_callback (widget_class, on_realize);
  gtk_widget_class_bind_template_callback (widget_class, on_slider_value_changed);

  gtk_widget_class_install_property_action (widget_class, "win.fullscreen", "fullscreened");
  gtk_widget_class_install_property_action (widget_class, "win.mute", "muted");
  gtk_widget_class_install_action (widget_class, "win.toggle-controls", NULL, on_toggle_controls_activated);
  gtk_widget_class_install_action (widget_class, "win.ff", NULL, on_ff_rev_activated);
  gtk_widget_class_install_action (widget_class, "win.rev", NULL, on_ff_rev_activated);
  gtk_widget_class_install_action (widget_class, "win.toggle-play", NULL, on_toggle_play_activated);

  provider = gtk_css_provider_new ();
  gtk_css_provider_load_from_resource (provider, "/org/sigxcpu/Livi/style.css");
  gtk_style_context_add_provider_for_display (gdk_display_get_default (),
                                              GTK_STYLE_PROVIDER (provider),
                                              GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);

  if (adw_style_manager_get_dark (manager))
    adw_style_manager_set_color_scheme (manager, ADW_COLOR_SCHEME_FORCE_LIGHT);
}


static void
livi_window_init (LiviWindow *self)
{
  GtkGesture *gesture;

  gtk_widget_init_template (GTK_WIDGET (self));

  self->paintable = livi_gst_paintable_new ();

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
  gtk_stack_set_visible_child (self->stack_content, GTK_WIDGET (self->box_content));
  gst_play_set_uri (self->player, uri);
}


void
livi_window_set_placeholder (LiviWindow *self)
{
  gtk_stack_set_visible_child (self->stack_content, GTK_WIDGET (self->box_placeholder));
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
