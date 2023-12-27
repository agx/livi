/*
 * Copyright (C) 2023 Guido Günther <agx@sigxcpu.org>
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Author: Guido Günther <agx@sigxcpu.org>
 */

#define G_LOG_DOMAIN "livi-application"

#include "livi-config.h"

#include "livi-application.h"
#include "livi-window.h"

#include <glib/gi18n.h>


struct _LiviApplication {
  AdwApplication parent;
};
G_DEFINE_TYPE (LiviApplication, livi_application, ADW_TYPE_APPLICATION)


static void
on_about_activated (GSimpleAction *action, GVariant *state, gpointer user_data)
{
  GtkApplication *app = GTK_APPLICATION (user_data);
  GtkWindow *window = gtk_application_get_active_window (app);
  GtkWidget *about;
  const char *developers[] = {
    "Guido Günther",
    NULL
  };
  const char *designers[] = {
    "Allan Day",
    NULL
  };

  about = adw_about_window_new_from_appdata ("/org/sigxcpu/Livi/org.sigxcpu.Livi.metainfo.xml", NULL);
  gtk_window_set_transient_for (GTK_WINDOW (about), window);
  adw_about_window_set_copyright (ADW_ABOUT_WINDOW (about), "© 2021 Purism SPC\n© 2023 Guido Günther");
  adw_about_window_set_developers (ADW_ABOUT_WINDOW (about), developers);
  adw_about_window_set_designers (ADW_ABOUT_WINDOW (about), designers);
  adw_about_window_set_translator_credits (ADW_ABOUT_WINDOW (about), _("translator-credits"));
  gtk_window_present (GTK_WINDOW (about));
}


static void
on_quit_activated (GSimpleAction *action, GVariant *parameter, gpointer user_data)
{
  GtkApplication *app = GTK_APPLICATION (user_data);

  GtkWindow *window = gtk_application_get_active_window (app);

  gtk_window_destroy (window);
  g_application_quit (G_APPLICATION (app));
}


static GActionEntry app_entries[] =
{
  { "about", on_about_activated, NULL, NULL, NULL },
  { "quit", on_quit_activated, NULL, NULL, NULL },
};


static void
livi_application_startup (GApplication *g_application)
{
  LiviApplication *self = LIVI_APPLICATION (g_application);
  GtkWindow *window;

  g_debug ("Startup");

  G_APPLICATION_CLASS (livi_application_parent_class)->startup (g_application);

  window = gtk_application_get_active_window (GTK_APPLICATION (self));
  if (window == NULL)
    window = g_object_new (LIVI_TYPE_WINDOW,
                           "application", self,
                           NULL);

  g_action_map_add_action_entries (G_ACTION_MAP (self),
                                   app_entries, G_N_ELEMENTS (app_entries),
                                   self);

  gtk_application_set_accels_for_action (GTK_APPLICATION (self),
					 "win.fullscreen",
                                         (const char *[]){ "f", "F11", NULL });
  gtk_application_set_accels_for_action (GTK_APPLICATION (self),
					 "win.mute",
                                         (const char *[]){ "m", NULL });
  gtk_application_set_accels_for_action (GTK_APPLICATION (self),
					 "win.ff",
                                         (const char *[]){ "Right", NULL });
  gtk_application_set_accels_for_action (GTK_APPLICATION (self),
					 "win.rev",
                                         (const char *[]){ "Left", NULL });
  gtk_application_set_accels_for_action (GTK_APPLICATION (self),
					 "win.toggle-controls",
                                         (const char *[]){ "Escape", NULL });
  gtk_application_set_accels_for_action (GTK_APPLICATION (self),
					 "win.toggle-play",
                                         (const char *[]){"space", NULL, });
  gtk_application_set_accels_for_action (GTK_APPLICATION (self),
					 "win.open-file",
                                         (const char *[]){"<ctrl>o", NULL, });
  gtk_application_set_accels_for_action (GTK_APPLICATION (self),
                                         "app.quit",
                                         (const char *[]){ "q", NULL });
}


static void
livi_application_dispose (GObject *object)
{
  G_OBJECT_CLASS (livi_application_parent_class)->dispose (object);
}


static void
livi_application_class_init (LiviApplicationClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GApplicationClass *application_class = G_APPLICATION_CLASS (klass);

  object_class->dispose = livi_application_dispose;

  application_class->startup = livi_application_startup;
}


static void
livi_application_init (LiviApplication *self)
{
}


LiviApplication *
livi_application_new (void)
{
  return g_object_new (LIVI_TYPE_APPLICATION, NULL);
}
