/* livi-window.h
 *
 * Copyright 2021 Purism SPC
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Author: Guido Günther <agx@sigxcpu.org>
 */

#pragma once

#include <adwaita.h>

G_BEGIN_DECLS

#define LIVI_TYPE_WINDOW (livi_window_get_type())

G_DECLARE_FINAL_TYPE (LiviWindow, livi_window, LIVI, WINDOW, AdwApplicationWindow)

void livi_window_set_uri (LiviWindow *self, const char *uri);
void livi_window_set_placeholder (LiviWindow *self);
void livi_window_set_error (LiviWindow *self, const char *description);
void livi_window_set_play (LiviWindow *self);
void livi_window_set_pause (LiviWindow *self);

G_END_DECLS
