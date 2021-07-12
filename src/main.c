/* main.c
 *
 * Copyright 2021 Purism SPC
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Author: Guido Günther <agx@sigxcpu.org>
 */

#define G_LOG_DOMAIN "livi-main"

#include "livi-config.h"
#include "livi-window.h"

#include <adwaita.h>

#include <glib/gi18n.h>
#include <gst/gst.h>

#define H264_DEMO_VIDEO "https://jell.yfish.us/media/jellyfish-15-mbps-hd-h264.mkv"


static void
on_activate (GtkApplication *app)
{
  GtkWindow *window;
  gchar *url;

  g_assert (GTK_IS_APPLICATION (app));

  g_debug ("Activate");

  window = gtk_application_get_active_window (app);
  url = g_object_get_data (G_OBJECT (app), "video");

  gtk_window_present (window);
  if (url) {
    g_debug ("Playing %s", url);
    livi_window_set_uri (LIVI_WINDOW (window), url);
    livi_window_set_play (LIVI_WINDOW (window));
  }
}


static void
on_startup (GApplication *app)
{
  GtkWindow *window;

  g_assert (GTK_IS_APPLICATION (app));

  g_debug ("Startup");

  adw_init ();

  window = gtk_application_get_active_window (GTK_APPLICATION (app));
  if (window == NULL)
    window = g_object_new (LIVI_TYPE_WINDOW,
			   "application", app,
			   "default-width", 600,
			   "default-height", 300,
			   NULL);
}


static int
on_command_line (GApplication *app, GApplicationCommandLine *cmdline)
{
  gboolean success;
  const gchar * const *remaining = NULL;
  g_autoptr (GFile) file = NULL;
  g_autoptr (GError) err = NULL;
  char *url = NULL;
  GVariantDict *options;
  gboolean h264_demo;

  success = g_application_register (app, NULL, &err);
  if (!success) {
    g_warning ("Error registering: %s", err->message);
    return 1;
  }

  options = g_application_command_line_get_options_dict (cmdline);

  success = g_variant_dict_lookup (options, "h264-demo", "b", &h264_demo);
  if (success) {
    url = g_strdup (H264_DEMO_VIDEO);
  } else {
    success = g_variant_dict_lookup (options, G_OPTION_REMAINING, "^a&s", &remaining);
    if (success && remaining[0] != NULL) {
      file = g_file_new_for_commandline_arg (remaining[0]);
      url = g_file_get_uri (file);
    }
  }

  if (url) {
    g_debug ("Video: %s", url);
    g_object_set_data_full (G_OBJECT (app), "video", g_steal_pointer (&url), g_free);
  }

  g_application_activate (app);
  return -1;
}


static void
on_screensaver_active_changed (GtkApplication *app)
{
  gboolean active;

  g_object_get (G_OBJECT (app), "screensaver-active", &active, NULL);
  if (active) {
    GtkWindow *window = gtk_application_get_active_window (app);

    if (window) {
      g_debug ("Screensaver active, pausing player");
      livi_window_set_pause (LIVI_WINDOW (window));
    }
  }
}


int
main (int   argc,
      char *argv[])
{
  g_autoptr (GtkApplication) app = NULL;
  const GOptionEntry options[] = {
    { "h264-demo", 0, 0, G_OPTION_ARG_NONE, NULL, "Play h264 demo", NULL },
    { G_OPTION_REMAINING, 0, 0, G_OPTION_ARG_STRING_ARRAY, NULL, NULL, "[FILE]" },
    { NULL,}
  };

  /* TODO: Until we configure the full pipeline */
  g_setenv ("USE_PLAYBIN3", "1", TRUE);

  bindtextdomain (GETTEXT_PACKAGE, LOCALEDIR);
  bind_textdomain_codeset (GETTEXT_PACKAGE, "UTF-8");
  textdomain (GETTEXT_PACKAGE);

  gst_init (&argc, &argv);
  app = GTK_APPLICATION (g_object_new (GTK_TYPE_APPLICATION,
				       "application-id", "org.sigxcpu.Livi",
				       "flags", G_APPLICATION_HANDLES_COMMAND_LINE,
				       "register-session", TRUE,
				       NULL));

  g_application_add_main_option_entries (G_APPLICATION (app), options);

  g_signal_connect (app, "activate", G_CALLBACK (on_activate), NULL);
  g_signal_connect (app, "startup", G_CALLBACK (on_startup), NULL);
  g_signal_connect (app, "command-line", G_CALLBACK (on_command_line), NULL);
  g_signal_connect (app, "notify::screensaver-active", G_CALLBACK (on_screensaver_active_changed), NULL);

  return g_application_run (G_APPLICATION (app), argc, argv);
}
