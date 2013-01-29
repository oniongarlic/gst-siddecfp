/* GStreamer
 * Copyright (C) <1999> Erik Walthinsen <omega@cse.ogi.edu>
 * Copyright (C) <2013> Kaj-Michael Lang <milang@tal.org>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */


#ifndef __GST_SIDDECFP_H__
#define __GST_SIDDECFP_H__

#include <stdlib.h>
#include <sidplayfp/sidplay2.h>
#include <sidplayfp/event.h>
#include <sidplayfp/SidTuneMod.h>
#include <sidplayfp/sidbuilder.h>
#include <sidplayfp/builders/residfp.h>
#include <sidplayfp/builders/resid.h>

#include <gst/gst.h>

G_BEGIN_DECLS

#define GST_TYPE_SIDDECFP \
  (gst_siddecfp_get_type())
#define GST_SIDDECFP(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_SIDDECFP,GstSidDecfp))
#define GST_SIDDECFP_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_SIDDECFP,GstSidDecfpClass))
#define GST_IS_SIDDECFP(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_SIDDECFP))
#define GST_IS_SIDDEC_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_SIDDECFP))

typedef struct _GstSidDecfp GstSidDecfp;
typedef struct _GstSidDecfpClass GstSidDecfpClass;

struct _GstSidDecfp {
  GstElement     element;

  /* pads */
  GstPad        *sinkpad,
                *srcpad;

  guchar        *tune_buffer;
  gint           tune_len;
  gint           tune_number;
  guint64        total_bytes;

  sidplay2       engine;
  SidTuneMod     tune;
  sid2_config_t  config;

  gulong         blocksize;
};

struct _GstSidDecfpClass {
  GstElementClass parent_class;
};

GType gst_siddecfp_get_type (void);

G_END_DECLS

#endif /* __GST_SIDDECFP_H__ */
