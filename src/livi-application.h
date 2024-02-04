/*
 * Copyright (C) 2023 Guido Günther <agx@sigxcpu.org>
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once

#include <adwaita.h>

G_BEGIN_DECLS

#define LIVI_TYPE_APPLICATION (livi_application_get_type ())

G_DECLARE_FINAL_TYPE (LiviApplication, livi_application, LIVI, APPLICATION, AdwApplication)

LiviApplication *livi_application_new (void);
gboolean         livi_application_get_resume (LiviApplication *self);

G_END_DECLS
