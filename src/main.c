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
#include "livi-application.h"
#include "livi-url-processor.h"
#include "livi-window.h"

#include <adwaita.h>

#include <glib/gi18n.h>
#include <gst/gst.h>


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


static gboolean
fix_broken_cache (void)
{
  const gchar *name;
  g_autoptr (GstElementFactory) factory = NULL;

  if (!g_getenv ("FLATPAK_SANDBOX_DIR"))
    return TRUE;

  factory = gst_element_factory_find ("playbin");
  name = gst_element_factory_get_metadata (factory, GST_ELEMENT_METADATA_LONGNAME);
  g_debug ("playbin plugin is %s", gst_element_factory_get_metadata (factory, GST_ELEMENT_METADATA_LONGNAME));

  /* If playbin is playbin 3 in the registry drop the cache, rebuilding is not enough */
  if (g_strcmp0 (name, "Player Bin 3") == 0) {
    const char * const arches[] = {"x86_64", "aarch64", NULL};

    g_warning ("Found playbin3 as playbin, this will cause problems. Removing cache");
    gst_deinit ();
    for (int i = 0; i < g_strv_length ((GStrv)arches); i++) {
      g_autofree char *path = NULL;

      path = g_strdup_printf (".var/app/" APP_ID "/cache/gstreamer-1.0/registry.%s.bin", arches[i]);
      if (unlink(path) == 0)
        g_debug ("Unlinked %s", path);
    }
    return FALSE;
  }
  return TRUE;
}


int
main (int argc, char *argv[])
{
  g_autoptr (LiviApplication) app = NULL;
  const GOptionEntry options[] = {
    { "h264-demo", 0, 0, G_OPTION_ARG_NONE, NULL, "Play h264 demo", NULL },
    { "vp8-demo", 0, 0, G_OPTION_ARG_NONE, NULL, "Play VP8 demo", NULL },
    { "yt-dlp", 'Y', 0, G_OPTION_ARG_NONE, NULL, "Let yt-dlp process the URL", NULL },
    { G_OPTION_REMAINING, 0, 0, G_OPTION_ARG_STRING_ARRAY, NULL, NULL, "[FILE]" },
    { NULL,}
  };

  /* TODO: Until we configure the full pipeline */
  g_setenv ("GST_PLAY_USE_PLAYBIN3", "1", TRUE);
  /* This causes all kinds of trouble since it swaps playbin3 in without without gst-play knowing */
  g_unsetenv ("USE_PLAYBIN3");

  bindtextdomain (GETTEXT_PACKAGE, LOCALEDIR);
  bind_textdomain_codeset (GETTEXT_PACKAGE, "UTF-8");
  textdomain (GETTEXT_PACKAGE);

  gst_init (&argc, &argv);
  if (!fix_broken_cache ())
    return 1;

  app = g_object_new (LIVI_TYPE_APPLICATION,
                      "application-id", APP_ID,
                      "flags", G_APPLICATION_HANDLES_COMMAND_LINE,
                      "register-session", TRUE,
                      NULL);

  g_application_add_main_option_entries (G_APPLICATION (app), options);

  g_signal_connect (app, "notify::screensaver-active", G_CALLBACK (on_screensaver_active_changed), NULL);

  return g_application_run (G_APPLICATION (app), argc, argv);
}
