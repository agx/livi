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

#define H264_DEMO_VIDEO "https://test-videos.co.uk/vids/jellyfish/mp4/h264/1080/Jellyfish_1080_10s_20MB.mp4"
#define VP8_DEMO_VIDEO  "https://test-videos.co.uk/vids/jellyfish/webm/vp8/1080/Jellyfish_1080_10s_20MB.webm"


typedef struct _LiviContext {
  LiviUrlProcessor *url_processor;
  GtkApplication   *app;
} LiviContext;


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
  if (url)
    livi_window_play_url (LIVI_WINDOW (window), url);
  else
    livi_window_set_placeholder (LIVI_WINDOW (window));
}


static void
on_url_processed (LiviUrlProcessor *url_processor, GAsyncResult *res, gpointer user_data)
{
  g_autoptr (GError) err = NULL;
  g_autofree char *url = NULL;
  LiviContext *ctx = (LiviContext*)user_data;

  g_assert (ctx);
  g_assert (GTK_IS_APPLICATION (ctx->app));

  url = livi_url_processor_run_finish (url_processor, res, &err);
  if (!url) {
    GtkWindow *window;

    g_warning ("Failed to process url: %s", err->message);

    window = gtk_application_get_active_window (ctx->app);
    if (window)
      livi_window_set_error (LIVI_WINDOW (window), err->message);
    return;
  }

  g_debug ("Processed URL: %s", url);
  g_object_set_data_full (G_OBJECT (ctx->app), "video", g_steal_pointer (&url), g_free);
  g_application_activate (G_APPLICATION (ctx->app));
}


static int
on_command_line (GApplication *app, GApplicationCommandLine *cmdline, LiviContext *ctx)
{
  gboolean success;
  const gchar * const *remaining = NULL;
  g_autoptr (GFile) file = NULL;
  g_autoptr (GError) err = NULL;
  char *url = NULL;
  GVariantDict *options;
  gboolean demo;
  gboolean use_ytdlp = FALSE;

  success = g_application_register (app, NULL, &err);
  if (!success) {
    g_warning ("Error registering: %s", err->message);
    return 1;
  }

  options = g_application_command_line_get_options_dict (cmdline);

  success = g_variant_dict_lookup (options, "h264-demo", "b", &demo);
  if (success)
    url = g_strdup (H264_DEMO_VIDEO);

  if (url == NULL) {
    success = g_variant_dict_lookup (options, "vp8-demo", "b", &demo);
    if (success)
      url = g_strdup (VP8_DEMO_VIDEO);
  }

  if (url == NULL) {
    success = g_variant_dict_lookup (options, G_OPTION_REMAINING, "^a&s", &remaining);
    if (success && remaining[0] != NULL) {
      file = g_file_new_for_commandline_arg (remaining[0]);
      url = g_file_get_uri (file);
    }
  }

  g_variant_dict_lookup (options, "yt-dlp", "b", &use_ytdlp);

  if (url) {
    if (use_ytdlp) {
      livi_url_processor_run (ctx->url_processor, url, NULL, (GAsyncReadyCallback)on_url_processed, ctx);
    } else {
      g_debug ("Video: %s", url);
      g_object_set_data_full (G_OBJECT (app), "video", g_steal_pointer (&url), g_free);
    }
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
    { "yt-dlp", 0, 0, G_OPTION_ARG_NONE, NULL, "Let yt-dlp process the URL", NULL },
    { G_OPTION_REMAINING, 0, 0, G_OPTION_ARG_STRING_ARRAY, NULL, NULL, "[FILE]" },
    { NULL,}
  };
  LiviContext ctx = { 0 };

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

  ctx.url_processor = livi_url_processor_new ();
  app = g_object_new (LIVI_TYPE_APPLICATION,
                      "application-id", APP_ID,
                      "flags", G_APPLICATION_HANDLES_COMMAND_LINE,
                      "register-session", TRUE,
                      NULL);
  ctx.app = GTK_APPLICATION (app);

  g_application_add_main_option_entries (G_APPLICATION (app), options);

  g_signal_connect (app, "activate", G_CALLBACK (on_activate), &ctx);
  g_signal_connect (app, "command-line", G_CALLBACK (on_command_line), &ctx);
  g_signal_connect (app, "notify::screensaver-active", G_CALLBACK (on_screensaver_active_changed), NULL);

  return g_application_run (G_APPLICATION (app), argc, argv);
}
