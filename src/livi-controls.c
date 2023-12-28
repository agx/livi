/*
 * Copyright (C) 2023 Guido Günther <agx@sigxcpu.org>
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Author: Guido Günther <agx@sigxcpu.org>
 */

#define G_LOG_DOMAIN "livi-controls"

#include "livi-config.h"

#include "livi-controls.h"

#include <gst/gst.h>

/**
 * LiviControls:
 *
 * The controls at the windows bottom
 */

struct _LiviControls {
  AdwBin                parent;

  GtkAdjustment        *adj_duration;
  GtkButton            *btn_play;
  GtkImage             *img_play;
  GtkButton            *btn_mute;
  GtkImage             *img_mute;
  GtkLabel             *lbl_time;

  guint64               duration;
  guint64               position;

  gboolean              dummy;
};
G_DEFINE_TYPE (LiviControls, livi_controls, ADW_TYPE_BIN)


static void
update_slider (LiviControls *self, GstClockTime value)
{
  g_autofree char *text = NULL;
  guint64 pos, dur;

  g_assert (LIVI_IS_CONTROLS (self));
  gtk_adjustment_set_value (self->adj_duration, value);

  dur = self->duration / GST_SECOND;
  pos = value / GST_SECOND;

  text = g_strdup_printf ("%.2" G_GUINT64_FORMAT ":%.2" G_GUINT64_FORMAT
                          " / %.2" G_GUINT64_FORMAT " :%.2" G_GUINT64_FORMAT,
                          pos / 60, pos % 60, dur / 60, dur % 60);
  gtk_label_set_text (self->lbl_time, text);
}


static gboolean
on_slider_value_changed (LiviControls *self, GtkScrollType scroll, double value)
{
  /* Slider is ns, actions are ms */
  value /= GST_MSECOND;

  if (value > G_MAXINT)
    value = G_MAXINT;

  gtk_widget_activate_action (GTK_WIDGET (self), "win.seek", "i", (int)value);

  return TRUE;
}


static void
livi_controls_class_init (LiviControlsClass *klass)
{
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

  gtk_widget_class_set_template_from_resource (widget_class, "/org/sigxcpu/Livi/livi-controls.ui");
  gtk_widget_class_bind_template_child (widget_class, LiviControls, adj_duration);
  gtk_widget_class_bind_template_child (widget_class, LiviControls, btn_mute);
  gtk_widget_class_bind_template_child (widget_class, LiviControls, btn_play);
  gtk_widget_class_bind_template_child (widget_class, LiviControls, img_mute);
  gtk_widget_class_bind_template_child (widget_class, LiviControls, img_play);
  gtk_widget_class_bind_template_child (widget_class, LiviControls, lbl_time);
  gtk_widget_class_bind_template_callback (widget_class, on_slider_value_changed);

  gtk_widget_class_set_css_name (widget_class, "livi-controls");
}


static void
livi_controls_init (LiviControls *self)
{
  gtk_widget_init_template (GTK_WIDGET (self));
}


LiviControls *
livi_controls_new (void)
{
  return g_object_new (LIVI_TYPE_CONTROLS, NULL);
}


void
livi_controls_set_duration (LiviControls *self, guint64 duration_ns)
{
  g_assert (LIVI_IS_CONTROLS (self));

  self->duration = duration_ns;
  gtk_adjustment_set_upper (self->adj_duration, self->duration);
}


void
livi_controls_set_position (LiviControls *self, guint64 position_ns)
{
  g_assert (LIVI_IS_CONTROLS (self));

  update_slider(self, position_ns);
}

void
livi_controls_show_mute_button (LiviControls *self, gboolean show)
{
  g_assert (LIVI_IS_CONTROLS (self));

  gtk_widget_set_visible (GTK_WIDGET (self->btn_mute), show);
}

void
livi_controls_set_mute_icon (LiviControls *self, const char *icon_name)
{
  g_assert (LIVI_IS_CONTROLS (self));
  g_assert (icon_name);

  g_object_set (self->img_mute, "icon-name", icon_name, NULL);
}


void
livi_controls_set_play_icon (LiviControls *self, const char *icon_name)
{
  g_assert (LIVI_IS_CONTROLS (self));
  g_assert (icon_name);

  g_object_set (self->img_play, "icon-name", icon_name, NULL);
}
