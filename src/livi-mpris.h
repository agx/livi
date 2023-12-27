/*
 * Copyright (C) 2023 Guido Günther <agx@sigxcpu.org>
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once

#include <glib-object.h>
#include <gio/gio.h>

G_BEGIN_DECLS

#define LIVI_TYPE_MPRIS (livi_mpris_get_type ())

G_DECLARE_FINAL_TYPE (LiviMpris, livi_mpris, LIVI, MPRIS, GObject)

LiviMpris *livi_mpris_new (void);
void       livi_mpris_export (LiviMpris       *self);
void       livi_mpris_unexport (LiviMpris *self);

G_END_DECLS
