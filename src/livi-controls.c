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

enum {
  PROP_0,
  PROP_NARROW,
  LAST_PROP,
};
static GParamSpec *props[LAST_PROP];


struct _LiviControls {
  AdwBin                parent;

  GtkStack             *stack;
  /* wide layout */
  GtkAdjustment        *wide_controls;
  GtkAdjustment        *adj_duration;
  GtkMenuButton        *btn_menu;
  GtkMenuButton        *btn_lang_menu;
  GtkButton            *btn_play;
  GtkImage             *img_play;
  GtkButton            *btn_mute;
  GtkImage             *img_mute;
  GtkLabel             *lbl_pos;
  GtkLabel             *lbl_duration;
  /* narrow layout */
  GtkMenuButton        *nrw_btn_menu;
  GtkMenuButton        *nrw_btn_lang_menu;

  guint64               duration;
  guint64               position;

  GtkPopover           *playback_menu;
  GtkPopoverMenu       *lang_menu;

  gboolean              narrow;
};
G_DEFINE_TYPE (LiviControls, livi_controls, ADW_TYPE_BIN)


static void
set_narrow (LiviControls *self, gboolean narrow)
{
  if (narrow == self->narrow)
    return;
  self->narrow = narrow;

  /*
   * Hide the wide layout when narrow as even a non visible stack page
   * adds up to the window size
   */
  gtk_widget_set_visible (GTK_WIDGET (self->wide_controls), !narrow);

  gtk_stack_set_visible_child_name (self->stack, narrow ? "narrow" : "wide");

  /* Switch over popovers as then can only have one parent */
  if (narrow) {
    gtk_menu_button_set_popover (self->btn_menu, NULL);
    gtk_menu_button_set_popover (self->nrw_btn_menu, GTK_WIDGET (self->playback_menu));

    gtk_menu_button_set_popover (self->btn_lang_menu, NULL);
    gtk_menu_button_set_popover (self->nrw_btn_lang_menu, GTK_WIDGET (self->lang_menu));
  } else {
    gtk_menu_button_set_popover (self->nrw_btn_menu, NULL);
    gtk_menu_button_set_popover (self->btn_menu, GTK_WIDGET (self->playback_menu));

    gtk_menu_button_set_popover (self->nrw_btn_lang_menu, NULL);
    gtk_menu_button_set_popover (self->btn_lang_menu, GTK_WIDGET (self->lang_menu));
  }

  g_object_notify_by_pspec (G_OBJECT (self), props[PROP_NARROW]);
}


static void
livi_controls_set_property (GObject      *object,
                            guint         property_id,
                            const GValue *value,
                            GParamSpec    *pspec)
{
  LiviControls *self = LIVI_CONTROLS (object);

  switch (property_id) {
  case PROP_NARROW:
    set_narrow (self, g_value_get_boolean (value));
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    break;
  }
}


static void
livi_controls_get_property (GObject    *object,
                          guint       property_id,
                          GValue     *value,
                          GParamSpec *pspec)
{
  LiviControls *self = LIVI_CONTROLS (object);

  switch (property_id) {
  case PROP_NARROW:
    g_value_set_boolean (value, self->narrow);
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    break;
  }
}


/* Verbatim from libcall-ui */
static char *
cui_call_format_duration (double duration)
{
#define MINUTE 60
#define HOUR   (60 * MINUTE)
  guint seconds, minutes;
  GString *str = g_string_new ("");

  if (duration > HOUR) {
    int hours = (int) (duration / HOUR);
    g_string_append_printf (str, "%u:", hours);
    duration -= (hours * HOUR);
  }

  minutes = (int) (duration / MINUTE);
  seconds = duration - (minutes * MINUTE);
  g_string_append_printf (str, "%02u:%02u", minutes, seconds);

  return g_string_free (str, FALSE);
#undef HOUR
#undef MINUTE
}


static void
update_slider (LiviControls *self, GstClockTime value)
{
  g_autofree char *dur = NULL, *pos = NULL;

  g_assert (LIVI_IS_CONTROLS (self));
  gtk_adjustment_set_value (self->adj_duration, value);

  dur = cui_call_format_duration (self->duration / GST_SECOND);
  pos = cui_call_format_duration (value / GST_SECOND);

  gtk_label_set_text (self->lbl_pos, pos);
  gtk_label_set_text (self->lbl_duration, dur);
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
  GObjectClass *object_class = (GObjectClass *)klass;
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

  object_class->get_property = livi_controls_get_property;
  object_class->set_property = livi_controls_set_property;

  props[PROP_NARROW] =
    g_param_spec_boolean ("narrow", "", "",
                          FALSE,
                          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);
  g_object_class_install_properties (object_class, LAST_PROP, props);

  gtk_widget_class_set_template_from_resource (widget_class, "/org/sigxcpu/Livi/livi-controls.ui");
  gtk_widget_class_bind_template_child (widget_class, LiviControls, adj_duration);
  gtk_widget_class_bind_template_child (widget_class, LiviControls, btn_menu);
  gtk_widget_class_bind_template_child (widget_class, LiviControls, btn_mute);
  gtk_widget_class_bind_template_child (widget_class, LiviControls, btn_lang_menu);
  gtk_widget_class_bind_template_child (widget_class, LiviControls, btn_play);
  gtk_widget_class_bind_template_child (widget_class, LiviControls, img_mute);
  gtk_widget_class_bind_template_child (widget_class, LiviControls, img_play);
  gtk_widget_class_bind_template_child (widget_class, LiviControls, lang_menu);
  gtk_widget_class_bind_template_child (widget_class, LiviControls, lbl_duration);
  gtk_widget_class_bind_template_child (widget_class, LiviControls, lbl_pos);
  gtk_widget_class_bind_template_child (widget_class, LiviControls, nrw_btn_menu);
  gtk_widget_class_bind_template_child (widget_class, LiviControls, nrw_btn_lang_menu);
  gtk_widget_class_bind_template_child (widget_class, LiviControls, playback_menu);
  gtk_widget_class_bind_template_child (widget_class, LiviControls, stack);
  gtk_widget_class_bind_template_child (widget_class, LiviControls, wide_controls);
  gtk_widget_class_bind_template_callback (widget_class, on_slider_value_changed);

  gtk_widget_class_set_css_name (widget_class, "livi-controls");
}


static void
livi_controls_init (LiviControls *self)
{
  gtk_widget_init_template (GTK_WIDGET (self));

  livi_controls_set_langs (self, NULL);
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


void
livi_controls_set_langs (LiviControls *self, GMenuModel *lang)
{
  g_assert (LIVI_IS_CONTROLS (self));
  g_assert (lang == NULL || G_IS_MENU_MODEL (lang));

  gtk_popover_menu_set_menu_model (self->lang_menu, lang);
  gtk_widget_set_visible (GTK_WIDGET (self->btn_lang_menu), !!lang);
}
