/*
 * Copyright 2021 Purism SPC
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include "livi-gst-paintable.h"

#include <gst/gst.h>
#define GST_USE_UNSTABLE_API
#include <gst/gl/gl.h>
#include <gst/video/gstvideosink.h>
#include <gst/video/video.h>

G_BEGIN_DECLS

#define LIVI_TYPE_GST_SINK (livi_gst_sink_get_type ())

G_DECLARE_FINAL_TYPE (LiviGstSink, livi_gst_sink, LIVI, GST_SINK, GstVideoSink)

G_END_DECLS
