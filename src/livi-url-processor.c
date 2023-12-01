/*
 * Copyright 2023 Guido Günther <agx@sigxcpu.org>
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Author: Guido Günther <agx@sigxcpu.org>
 */
#define G_LOG_DOMAIN "livi-url-processor"

#include "livi-config.h"

#include "livi-url-processor.h"

#define URL_PROCESSOR "yt-dlp"

/**
 * LiviUrlProcessor:
 *
 * Process an URL via yt-dlp so it can be streamed.
 */

struct _LiviUrlProcessor {
  GObject               parent;

  GCancellable         *cancel;
  char                 *name;
};

G_DEFINE_TYPE (LiviUrlProcessor, livi_url_processor, G_TYPE_OBJECT);


static void
livi_url_processor_finalize (GObject *object)
{
  LiviUrlProcessor *self = LIVI_URL_PROCESSOR(object);

  g_cancellable_cancel (self->cancel);
  g_clear_object (&self->cancel);

  G_OBJECT_CLASS (livi_url_processor_parent_class)->finalize (object);
}


static void
livi_url_processor_class_init (LiviUrlProcessorClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->finalize = livi_url_processor_finalize;
}


static void
livi_url_processor_init (LiviUrlProcessor *self)
{
  self->cancel = g_cancellable_new ();
  self->name = URL_PROCESSOR;
}


LiviUrlProcessor *
livi_url_processor_new (void)
{
  return g_object_new (LIVI_TYPE_URL_PROCESSOR, NULL);
}


const char *
livi_url_processor_get_name (LiviUrlProcessor *self)
{
  g_assert (LIVI_IS_URL_PROCESSOR (self));

  return self->name;
}


static void
on_url_processor_process_finish (GObject *source_object, GAsyncResult *res, gpointer user_data)
{
  gboolean success;
  g_autoptr (GError) err = NULL;
  g_autoptr (GTask) task = G_TASK (user_data);
  char *new_url = NULL;
  GSubprocess *proc = G_SUBPROCESS (source_object);
  GInputStream *stdout, *stderr;
  g_autoptr (GDataInputStream) stdout_stream = NULL;
  g_autoptr (GDataInputStream) stderr_stream = NULL;

  success = g_subprocess_wait_finish (G_SUBPROCESS (source_object), res, &err);

  if (!success) {
    g_task_return_error (task, err);
    goto done;
  }

  stdout = g_subprocess_get_stdout_pipe (proc);
  if (!stdout) {
    g_task_return_new_error (task,
                             G_IO_ERROR, G_IO_ERROR_FAILED, "Couldn't get stdout");
    goto done;
  }

  stderr = g_subprocess_get_stderr_pipe (proc);
  if (!stderr) {
    g_task_return_new_error (task,
                             G_IO_ERROR, G_IO_ERROR_FAILED, "Couldn't get stdout");
    goto done;
  }

  stdout_stream = g_data_input_stream_new (stdout);
  stderr_stream = g_data_input_stream_new (stderr);

  new_url = g_data_input_stream_read_line (stdout_stream, NULL, NULL, &err);
  if (!new_url) {
    if (err) {
      g_task_return_error (task, err);
    } else {
      g_autofree char *errmsg = g_data_input_stream_read_line (stderr_stream, NULL, NULL, &err);

      g_task_return_new_error (task,
                               G_IO_ERROR, G_IO_ERROR_FAILED, "%s", errmsg);
    }
    goto done;
  }

  g_debug ("Got URL '%s'", new_url);
  g_task_return_pointer (task, new_url, g_free);
 done:
  g_object_unref (source_object);
}


void
livi_url_processor_run (LiviUrlProcessor    *self,
                        const char          *uri,
                        GCancellable        *cancellable,
                        GAsyncReadyCallback  callback,
                        gpointer             user_data)
{
  g_autoptr (GSubprocess) proc = NULL;
  g_autoptr (GError) err = NULL;
  g_autoptr (GTask) task = NULL;

  task = g_task_new (self, cancellable, callback, user_data);
  g_task_set_name (task, "[livi] Url processor run");

  g_debug ("Resolving '%s'", uri);
  proc = g_subprocess_new (G_SUBPROCESS_FLAGS_SEARCH_PATH_FROM_ENVP |
                           G_SUBPROCESS_FLAGS_STDOUT_PIPE |
                           G_SUBPROCESS_FLAGS_STDERR_PIPE,
                           &err, URL_PROCESSOR,
                           "--get-url", uri,
                           NULL);
  if (!proc) {
    g_warning ("Failed to find " URL_PROCESSOR ": %s", err->message);
    g_task_return_error (task, err);
  }

  g_task_set_source_tag (task, livi_url_processor_run);
  g_subprocess_wait_async (g_steal_pointer (&proc),
                           cancellable,
                           on_url_processor_process_finish,
                           g_steal_pointer (&task));
}


char *
livi_url_processor_run_finish (LiviUrlProcessor *self,
                               GAsyncResult     *res,
                               GError          **error)
{
  g_assert (LIVI_IS_URL_PROCESSOR (self));
  g_assert (G_IS_TASK (res));
  g_assert (!error || !*error);

  return g_task_propagate_pointer (G_TASK (res), error);
}
