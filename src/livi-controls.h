/*
 * Copyright (C) 2023 Guido Günther <agx@sigxcpu.org>
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once

#include <adwaita.h>

G_BEGIN_DECLS

#define LIVI_TYPE_CONTROLS (livi_controls_get_type ())

G_DECLARE_FINAL_TYPE (LiviControls, livi_controls, LIVI, CONTROLS, AdwBin)

LiviControls *livi_controls_new (void);
void          livi_controls_set_duration (LiviControls *self, guint64 duration_ns);
void          livi_controls_set_position (LiviControls *self, guint64 position_ns);
void          livi_controls_show_mute_button (LiviControls *self, gboolean show);
void          livi_controls_set_mute_icon (LiviControls *self, const char *icon_name);
void          livi_controls_set_play_icon (LiviControls *self, const char *icon_name);
void          livi_controls_set_langs (LiviControls *self, GMenuModel *lang);

G_END_DECLS
