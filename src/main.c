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
#include <glib-unix.h>
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


static gboolean
on_shutdown_signal (gpointer user_data)
{
  GActionGroup *app = G_ACTION_GROUP (user_data);
  GtkWindow *window;

  g_assert (LIVI_IS_APPLICATION (app));

  window = gtk_application_get_active_window (GTK_APPLICATION (app));
  if (window)
    gtk_widget_activate_action (GTK_WIDGET (window), "window.close", NULL);

  return G_SOURCE_REMOVE;
}


int
main (int argc, char *argv[])
{
  g_autoptr (LiviApplication) app = NULL;

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

  gdk_set_allowed_backends ("wayland");
  if (!gtk_init_check ()) {
    g_critical ("Can't init GTK, are you on Wayland?");
    return 1;
  }

  app = livi_application_new ();

  g_signal_connect (app, "notify::screensaver-active", G_CALLBACK (on_screensaver_active_changed), NULL);
  g_unix_signal_add (SIGINT, on_shutdown_signal, app);
  g_unix_signal_add (SIGTERM, on_shutdown_signal, app);

  return g_application_run (G_APPLICATION (app), argc, argv);
}
