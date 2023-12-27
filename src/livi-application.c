/*
 * Copyright (C) 2023 Guido Günther <agx@sigxcpu.org>
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Author: Guido Günther <agx@sigxcpu.org>
 */

#define G_LOG_DOMAIN "livi-application"

#include "livi-config.h"

#include "livi-application.h"

struct _LiviApplication {
  AdwApplication parent;
};
G_DEFINE_TYPE (LiviApplication, livi_application, ADW_TYPE_APPLICATION)


static void
livi_application_dispose (GObject *object)
{
  G_OBJECT_CLASS (livi_application_parent_class)->dispose (object);
}


static void
livi_application_class_init (LiviApplicationClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->dispose = livi_application_dispose;
}


static void
livi_application_init (LiviApplication *self)
{
}


LiviApplication *
livi_application_new (void)
{
  return g_object_new (LIVI_TYPE_APPLICATION, NULL);
}
