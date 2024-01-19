/*
 * Copyright (C) 2023-2024 Guido Günther <agx@sigxcpu.org>
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Author: Guido Günther <agx@sigxcpu.org>
 */

#define G_LOG_DOMAIN "livi-application"

#include "livi-config.h"

#include "livi-application.h"
#include "livi-mpris.h"
#include "livi-url-processor.h"
#include "livi-utils.h"
#include "livi-window.h"

#include <glib/gi18n.h>


#define H264_DEMO_VIDEO "https://test-videos.co.uk/vids/jellyfish/mp4/h264/1080/Jellyfish_1080_10s_20MB.mp4"
#define VP8_DEMO_VIDEO  "https://test-videos.co.uk/vids/jellyfish/webm/vp8/1080/Jellyfish_1080_10s_20MB.webm"


struct _LiviApplication {
  AdwApplication    parent;

  LiviUrlProcessor *url_processor;
  LiviMpris        *mpris;
  char             *video_url;
};
G_DEFINE_TYPE (LiviApplication, livi_application, ADW_TYPE_APPLICATION)


static void
set_video_url (LiviApplication *self, const char *video_url)
{
  g_free (self->video_url);
  self->video_url = g_strdup (video_url);

  if (!STR_IS_NULL_OR_EMPTY (self->video_url))
    livi_mpris_export (self->mpris);
  else
    livi_mpris_unexport (self->mpris);
}


static void
on_mpris_raise (LiviMpris *self)
{
  GtkWindow *window;

  window = gtk_application_get_active_window (GTK_APPLICATION (self));
  gtk_window_present (window);
}


static void
livi_application_activate (GApplication *g_application)
{
  LiviApplication *self = LIVI_APPLICATION (g_application);
  GtkWindow *window;

  g_debug ("Activate");

  G_APPLICATION_CLASS (livi_application_parent_class)->activate (g_application);

  window = gtk_application_get_active_window (GTK_APPLICATION (self));

  gtk_window_present (window);
  if (self->video_url)
    livi_window_play_url (LIVI_WINDOW (window), self->video_url);
  else
    livi_window_set_empty_state (LIVI_WINDOW (window));
}


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
on_clipboard_read_ready (GObject *source_object, GAsyncResult *res, gpointer user_data)
{
  g_autoptr (GError) err = NULL;
  LiviApplication *self = LIVI_APPLICATION (user_data);
  const GValue *value;
  g_autofree char *uri = NULL;

  value = gdk_clipboard_read_value_finish (GDK_CLIPBOARD (source_object), res, &err);
  if (!value) {
    g_warning ("Failed to read clipboard: %s", err->message);

    /* Not parseable as file list, try as string */
    gdk_clipboard_read_value_async (GDK_CLIPBOARD (source_object),
                                    G_TYPE_STRING,
                                    G_PRIORITY_DEFAULT,
                                    NULL,
                                    on_clipboard_read_ready,
                                    self);
    return;
  }

  if (G_VALUE_HOLDS_STRING (value)) {
    uri = g_strdup (g_value_get_string (value));

    if (!g_uri_is_valid (uri, G_URI_FLAGS_NONE, &err)) {
      g_warning ("Pasted uri not valid");
      return;
    }
  } else if (G_VALUE_HOLDS_BOXED (value)) {
    GSList *list = g_value_get_boxed (value);
    for (GSList *l = list; l && l->data; l = g_slist_next (l)) {
      GFile* file = G_FILE (l->data);

      if (g_file_is_native (file) || g_file_has_uri_scheme (file, "https")) {
        uri = g_file_get_uri (file);
        break;
      }
    }

    if (!uri)
      return;
  } else {
    g_assert_not_reached ();
  }

  g_debug ("Opening pasted uri '%s'", uri);
  set_video_url (self, uri);
  g_application_activate (G_APPLICATION (self));
}



static void
on_paste_activated (GSimpleAction *action, GVariant *state, gpointer user_data)
{
  GtkApplication *self = GTK_APPLICATION (user_data);
  GtkWindow *window = gtk_application_get_active_window (self);
  GdkClipboard *clipboard;

  clipboard = gtk_widget_get_clipboard (GTK_WIDGET (window));
  gdk_clipboard_read_value_async (clipboard,
                                  GDK_TYPE_FILE_LIST,
                                  G_PRIORITY_DEFAULT,
                                  NULL,
                                  on_clipboard_read_ready,
                                  self);
}


static GActionEntry app_entries[] =
{
  { "about", on_about_activated, NULL, NULL, NULL },
  { "paste", on_paste_activated, NULL, NULL, NULL },
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
                                         "app.paste",
                                         (const char *[]){ "<ctrl>v", NULL });
  gtk_application_set_accels_for_action (GTK_APPLICATION (self),
					 "win.fullscreen",
                                         (const char *[]){ "f", "F11", NULL });
  gtk_application_set_accels_for_action (GTK_APPLICATION (self),
					 "win.mute",
                                         (const char *[]){ "m", NULL });
  gtk_application_set_accels_for_action (GTK_APPLICATION (self),
					 "win.ff(+30000)",
                                         (const char *[]){ "Right", NULL });
  gtk_application_set_accels_for_action (GTK_APPLICATION (self),
					 "win.ff(-10000)",
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
                                         "window.close",
                                         (const char *[]){ "q", NULL });

  g_object_bind_property (window, "state", self->mpris, "player-state", G_BINDING_SYNC_CREATE);
  g_object_bind_property (window, "title", self->mpris, "title", G_BINDING_SYNC_CREATE);
}


static void
on_url_processed (LiviUrlProcessor *url_processor, GAsyncResult *res, gpointer user_data)
{
  LiviApplication *self = LIVI_APPLICATION (user_data);
  g_autoptr (GError) err = NULL;
  g_autofree char *url = NULL;

  g_assert (LIVI_IS_APPLICATION (self));

  url = livi_url_processor_run_finish (url_processor, res, &err);
  if (!url) {
    GtkWindow *window;

    g_warning ("Failed to process url: %s", err->message);

    window = gtk_application_get_active_window (GTK_APPLICATION (self));
    if (window)
      livi_window_set_error_state (LIVI_WINDOW (window), err->message);
    return;
  }

  g_debug ("Processed URL: %s", url);
  set_video_url (self, url);

  g_application_activate (G_APPLICATION (self));
}


static int
livi_application_command_line (GApplication *g_application, GApplicationCommandLine *cmdline)
{
  LiviApplication *self = LIVI_APPLICATION (g_application);
  const gchar * const *remaining = NULL;
  g_autoptr (GFile) file = NULL;
  g_autoptr (GError) err = NULL;
  gboolean use_ytdlp = FALSE;
  char *url = NULL;
  GVariantDict *options;
  gboolean demo;
  gboolean success;

  success = g_application_register (G_APPLICATION (self), NULL, &err);
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
      livi_url_processor_run (self->url_processor, url, NULL, (GAsyncReadyCallback)on_url_processed, self);
    } else {
      g_debug ("Video: %s", url);
      set_video_url (self, url);
    }
  }

  g_application_activate (G_APPLICATION (self));
  return 0;
}


static void
livi_application_dispose (GObject *object)
{
  LiviApplication *self = LIVI_APPLICATION (object);

  g_free (self->video_url);
  g_clear_object (&self->url_processor);
  g_clear_object (&self->mpris);

  G_OBJECT_CLASS (livi_application_parent_class)->dispose (object);
}


static void
livi_application_class_init (LiviApplicationClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GApplicationClass *application_class = G_APPLICATION_CLASS (klass);

  object_class->dispose = livi_application_dispose;

  application_class->startup = livi_application_startup;
  application_class->activate = livi_application_activate;
  application_class->command_line = livi_application_command_line;
}


static void
livi_application_init (LiviApplication *self)
{
  self->url_processor = livi_url_processor_new ();
  self->mpris = livi_mpris_new ();

  g_signal_connect_swapped (self->mpris, "raise", G_CALLBACK (on_mpris_raise), self);
}


LiviApplication *
livi_application_new (void)
{
  return g_object_new (LIVI_TYPE_APPLICATION, NULL);
}
