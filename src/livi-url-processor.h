/*
 * Copyright 2023 Guido Günther
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Author: Guido Günther <agx@sigxcpu.org>
 */
#pragma once

#include <glib-object.h>
#include <gio/gio.h>

G_BEGIN_DECLS

#define LIVI_TYPE_URL_PROCESSOR (livi_url_processor_get_type ())

G_DECLARE_FINAL_TYPE (LiviUrlProcessor, livi_url_processor, LIVI, URL_PROCESSOR, GObject)

LiviUrlProcessor *livi_url_processor_new (void);

void              livi_url_processor_run (LiviUrlProcessor    *self,
                                          const char          *uri,
                                          GCancellable        *cancellable,
                                          GAsyncReadyCallback  callback,
                                          gpointer             user_data);
char             *livi_url_processor_run_finish (LiviUrlProcessor *self,
                                                 GAsyncResult     *res,
                                                 GError          **error);

const char       *livi_url_processor_get_name (LiviUrlProcessor *self);

G_END_DECLS
