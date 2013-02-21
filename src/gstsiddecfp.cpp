/* GStreamer
 * Copyright (C) <1999> Erik Walthinsen <omega@cse.ogi.edu>
 *           (C) <2006> Wim Taymans <wim@fluendo.com>
 *           (C) <2013> Kaj-Michael Lang <milang@tal.org>
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

/**
 * SECTION:element-siddecfp
 *
 * This element decodes .sid files to raw audio. .sid files are in fact
 * small Commodore 64 programs that are executed on an emulated 6502 CPU and a
 * MOS 6581 sound chip.
 *
 * This element uses the new fork of libsidplay, libsidplayfp and is based on siddec element
 *
 * This plugin will first load the complete program into memory before starting
 * the emulator and producing output.
 *
 * Seeking is not (and cannot be) implemented.
 *
 * <refsect2>
 * <title>Example pipelines</title>
 * |[
 * gst-launch -v filesrc location=Hawkeye.sid ! siddecfp ! audioconvert ! alsasink
 * ]| Decode a sid file and play back the audio using alsasink.
 * </refsect2>
 *
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <string.h>
#include "gstsiddecfp.h"

#define DEFAULT_TUNE		0
#define DEFAULT_CLOCK		SIDTUNE_CLOCK_PAL
#define DEFAULT_FILTER		TRUE
#define DEFAULT_MEASURED_VOLUME	TRUE
#define DEFAULT_MOS8580		FALSE
#define DEFAULT_FORCE_SPEED	FALSE
#define DEFAULT_BLOCKSIZE	(8*1024)
#define MIN_BLOCKSIZE	(1*1024)
#define MAX_BLOCKSIZE	(64*1024)

enum
{
  PROP_0,
  PROP_TUNE,
  PROP_CLOCK,
  PROP_MEMORY,
  PROP_FILTER,
  PROP_MEASURED_VOLUME,
  PROP_MOS8580,
  PROP_FORCE_SPEED,
  PROP_BLOCKSIZE,
  PROP_METADATA
};

static GstStaticPadTemplate sink_templ = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("audio/x-sid")
    );

static GstStaticPadTemplate src_templ = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("audio/x-raw-int, "
        "endianness = (int) BYTE_ORDER, "
        "signed = (boolean) { true }, "
        "width = (int) { 16 }, "
        "depth = (int) { 16 }, "
        "rate = (int) [ 8000, 48000 ], " "channels = (int) [ 1, 2 ]")
    );

GST_DEBUG_CATEGORY_STATIC (gst_siddecfp_debug);
#define GST_CAT_DEFAULT gst_siddecfp_debug

#define GST_TYPE_SID_CLOCK (gst_sid_clock_get_type())
static GType
gst_sid_clock_get_type (void)
{
  static GType sid_clock_type = 0;
  static const GEnumValue sid_clock[] = {
    {SIDTUNE_CLOCK_PAL, "PAL", "pal"},
    {SIDTUNE_CLOCK_NTSC, "NTSC", "ntsc"},
    {SIDTUNE_CLOCK_ANY, "ANY", "any"},
    {0, NULL, NULL},
  };

  if (!sid_clock_type) {
    sid_clock_type = g_enum_register_static ("GstSidClock", sid_clock);
  }
  return sid_clock_type;
}

static void gst_siddecfp_finalize (GObject * object);

static GstFlowReturn gst_siddecfp_chain (GstPad * pad, GstBuffer * buffer);
static gboolean gst_siddecfp_sink_event (GstPad * pad, GstEvent * event);

static gboolean gst_siddecfp_src_convert (GstPad * pad, GstFormat src_format, gint64 src_value, GstFormat * dest_format, gint64 * dest_value);
static gboolean gst_siddecfp_src_event (GstPad * pad, GstEvent * event);
static gboolean gst_siddecfp_src_query (GstPad * pad, GstQuery * query);

static void gst_siddecfp_get_property (GObject * object, guint prop_id, GValue * value, GParamSpec * pspec);
static void gst_siddecfp_set_property (GObject * object, guint prop_id, const GValue * value, GParamSpec * pspec);

#define _do_init(bla) GST_DEBUG_CATEGORY_INIT (gst_siddecfp_debug, "siddecfp", 0, "C64 sid song player");

GST_BOILERPLATE_FULL (GstSidDecfp, gst_siddecfp, GstElement, GST_TYPE_ELEMENT, _do_init);

static void
gst_siddecfp_base_init (gpointer g_class)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);

  gst_element_class_set_details_simple (element_class,
      "Sid decoder",
      "Codec/Decoder/Audio", "Use libsidplayfp to decode SID audio tunes",
      "Kaj-Michael Lang <milang@tal.org>");

  gst_element_class_add_static_pad_template (element_class, &src_templ);
  gst_element_class_add_static_pad_template (element_class, &sink_templ);
}

static void
gst_siddecfp_class_init (GstSidDecfpClass * klass)
{
  GObjectClass *gobject_class;

  gobject_class = (GObjectClass *) klass;

  gobject_class->finalize = gst_siddecfp_finalize;
  gobject_class->set_property = gst_siddecfp_set_property;
  gobject_class->get_property = gst_siddecfp_get_property;

  g_object_class_install_property (G_OBJECT_CLASS (klass), PROP_TUNE,
      g_param_spec_int ("tune", "tune", "tune", 0, 100, DEFAULT_TUNE,
          (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));
  g_object_class_install_property (G_OBJECT_CLASS (klass), PROP_CLOCK,
      g_param_spec_enum ("clock", "clock", "clock",
          GST_TYPE_SID_CLOCK, DEFAULT_CLOCK,
          (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));
  g_object_class_install_property (G_OBJECT_CLASS (klass), PROP_FILTER,
      g_param_spec_boolean ("filter", "filter", "filter", DEFAULT_FILTER,
          (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));
  g_object_class_install_property (G_OBJECT_CLASS (klass), PROP_MEASURED_VOLUME,
      g_param_spec_boolean ("measured-volume", "measured_volume",
          "measured_volume", DEFAULT_MEASURED_VOLUME,
          (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));
  g_object_class_install_property (G_OBJECT_CLASS (klass), PROP_MOS8580,
      g_param_spec_boolean ("mos8580", "mos8580", "mos8580", DEFAULT_MOS8580,
          (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));
  g_object_class_install_property (G_OBJECT_CLASS (klass), PROP_FORCE_SPEED,
      g_param_spec_boolean ("force-speed", "force_speed", "force_speed",
          DEFAULT_FORCE_SPEED,
          (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));
  g_object_class_install_property (G_OBJECT_CLASS (klass), PROP_BLOCKSIZE,
      g_param_spec_ulong ("blocksize", "Block size", "Size in bytes to output per buffer", MIN_BLOCKSIZE, MAX_BLOCKSIZE,
          DEFAULT_BLOCKSIZE,
          (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));
  g_object_class_install_property (gobject_class, PROP_METADATA,
      g_param_spec_boxed ("metadata", "Metadata", "Metadata", GST_TYPE_CAPS,
          (GParamFlags)(G_PARAM_READABLE | G_PARAM_STATIC_STRINGS)));
}

static void
gst_siddecfp_init (GstSidDecfp * siddecfp, GstSidDecfpClass * klass)
{
  siddecfp->sinkpad = gst_pad_new_from_static_template (&sink_templ, "sink");
  gst_pad_set_event_function (siddecfp->sinkpad, gst_siddecfp_sink_event);
  gst_pad_set_chain_function (siddecfp->sinkpad, gst_siddecfp_chain);
  gst_element_add_pad (GST_ELEMENT (siddecfp), siddecfp->sinkpad);

  siddecfp->srcpad = gst_pad_new_from_static_template (&src_templ, "src");
  gst_pad_set_event_function (siddecfp->srcpad, gst_siddecfp_src_event);
  gst_pad_set_query_function (siddecfp->srcpad, gst_siddecfp_src_query);
  gst_pad_use_fixed_caps (siddecfp->srcpad);
  gst_element_add_pad (GST_ELEMENT (siddecfp), siddecfp->srcpad);

  siddecfp->engine = new sidplay2();
  siddecfp->config = siddecfp->engine->config();

  siddecfp->config.sidDefault = SID2_MOS6581;
  siddecfp->config.clockDefault = SID2_CLOCK_PAL;
  siddecfp->config.clockForced = false;
  siddecfp->config.clockSpeed = SID2_CLOCK_CORRECT;
  siddecfp->config.environment = sid2_envR;
  siddecfp->config.frequency = 48000;
  siddecfp->config.samplingMethod = SID2_INTERPOLATE;
  siddecfp->config.fastSampling = false;
  siddecfp->config.playback = sid2_stereo;
  siddecfp->config.sidModel = SID2_MODEL_CORRECT;
  siddecfp->config.sidSamples = true;

  // siddecfp->engine->config(siddecfp->config);

  siddecfp->tune_buffer = (guchar *) g_malloc (SIDTUNE_MAX_FILELEN);
  siddecfp->tune_len = 0;
  siddecfp->tune_number = 0;
  siddecfp->total_bytes = 0;
  siddecfp->blocksize = DEFAULT_BLOCKSIZE;
}

static void
gst_siddecfp_finalize (GObject * object)
{
  GstSidDecfp *siddecfp = GST_SIDDECFP (object);

  siddecfp->engine->stop();
  siddecfp->engine->load(NULL);

  g_free (siddecfp->tune_buffer);
  delete siddecfp->rs;
  delete siddecfp->engine;

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
update_tags (GstSidDecfp * siddecfp)
{
  SidTuneInfo info;
  GstTagList *list;

  siddecfp->tune->getInfo(info);
  list = gst_tag_list_new ();

  if (info.infoString[0]) {
     gst_tag_list_add (list, GST_TAG_MERGE_REPLACE, GST_TAG_TITLE, info.infoString[0], (void *) NULL);
  }
  if (info.infoString[1]) {
    gst_tag_list_add (list, GST_TAG_MERGE_REPLACE, GST_TAG_ARTIST, info.infoString[1], (void *) NULL);
  }
  if (info.infoString[2]) {
    gst_tag_list_add (list, GST_TAG_MERGE_REPLACE, GST_TAG_COPYRIGHT, info.infoString[2], (void *) NULL);
  }
  gst_element_found_tags_for_pad (GST_ELEMENT_CAST (siddecfp), siddecfp->srcpad, list);
}

static gboolean
siddecfp_negotiate (GstSidDecfp * siddecfp)
{
  GstCaps *allowed;
  gint width = 16, depth = 16;
  GstStructure *structure;
  int rate = 48000;
  int channels = 1;
  GstCaps *newcaps, *caps;

  allowed = gst_pad_get_allowed_caps (siddecfp->srcpad);
  if (!allowed)
    goto nothing_allowed;

  GST_DEBUG_OBJECT (siddecfp, "allowed caps: %" GST_PTR_FORMAT, allowed);

  newcaps = gst_caps_copy_nth (allowed, 0);
  gst_caps_unref (allowed);

  gst_pad_fixate_caps (siddecfp->srcpad, newcaps);
  gst_pad_set_caps (siddecfp->srcpad, newcaps);

  structure = gst_caps_get_structure (newcaps, 0);

  gst_structure_get_int (structure, "rate", &rate);
  gst_structure_get_int (structure, "channels", &channels);

  siddecfp->config.frequency = rate;
  siddecfp->config.playback = channels==2 ? sid2_stereo : sid2_mono;

  caps = gst_caps_new_simple ("audio/x-raw-int",
      "endianness", G_TYPE_INT, G_BYTE_ORDER,
      "signed", G_TYPE_BOOLEAN, TRUE,
      "width", G_TYPE_INT, depth,
      "depth", G_TYPE_INT, depth,
      "rate", G_TYPE_INT, rate,
      "channels", G_TYPE_INT, channels, NULL);
  gst_pad_set_caps (siddecfp->srcpad, caps);
  gst_caps_unref (caps);

  return TRUE;

  /* ERRORS */
nothing_allowed:
  {
    GST_DEBUG_OBJECT (siddecfp, "could not get allowed caps");
    return FALSE;
  }
}

static void
play_loop (GstPad * pad)
{
  GstFlowReturn ret;
  GstSidDecfp *siddecfp;
  GstBuffer *out=NULL;
  gint64 value, offset, time;
  guint played;
  GstFormat format;
  short buffer[MAX_BLOCKSIZE];
  gint bytes_per_sample;

  siddecfp = GST_SIDDECFP (gst_pad_get_parent (pad));

  ret = gst_pad_alloc_buffer_and_set_caps (siddecfp->srcpad, GST_BUFFER_OFFSET_NONE, siddecfp->blocksize, GST_PAD_CAPS (siddecfp->srcpad), &out);
  if (ret != GST_FLOW_OK)
    goto pause;

  played = siddecfp->engine->play(buffer, siddecfp->blocksize/2)*2;

  memcpy (GST_BUFFER_DATA(out), buffer, played);

  /* get offset in samples */
  format = GST_FORMAT_DEFAULT;
  gst_siddecfp_src_convert (siddecfp->srcpad, GST_FORMAT_BYTES, siddecfp->total_bytes, &format, &offset);
  GST_BUFFER_OFFSET (out) = offset;

  /* get current timestamp */
  format = GST_FORMAT_TIME;
  gst_siddecfp_src_convert (siddecfp->srcpad, GST_FORMAT_BYTES, siddecfp->total_bytes, &format, &time);
  GST_BUFFER_TIMESTAMP (out) = time;

  /* update position and get new timestamp to calculate duration */
  siddecfp->total_bytes += played;

  /* get offset in samples */
  format = GST_FORMAT_DEFAULT;
  gst_siddecfp_src_convert (siddecfp->srcpad, GST_FORMAT_BYTES, siddecfp->total_bytes, &format, &value);
  GST_BUFFER_OFFSET_END (out) = value;

  format = GST_FORMAT_TIME;
  gst_siddecfp_src_convert (siddecfp->srcpad, GST_FORMAT_BYTES, siddecfp->total_bytes, &format, &value);
  GST_BUFFER_DURATION (out) = value - time;

  if ((ret = gst_pad_push (siddecfp->srcpad, out)) != GST_FLOW_OK)
    goto pause;

  if (played < siddecfp->blocksize) {
    gst_pad_push_event (pad, gst_event_new_eos ());
    gst_pad_pause_task (pad);
  }

done:
  gst_object_unref (siddecfp);

  return;

  /* ERRORS */
pause:
  {
    const gchar *reason = gst_flow_get_name (ret);

    if (ret == GST_FLOW_UNEXPECTED) {
      /* perform EOS logic, FIXME, segment seek? */
      gst_pad_push_event (pad, gst_event_new_eos ());
    } else  if (ret < GST_FLOW_UNEXPECTED || ret == GST_FLOW_NOT_LINKED) {
      /* for fatal errors we post an error message */
      GST_ELEMENT_ERROR (siddecfp, STREAM, FAILED, (NULL), ("streaming task paused, reason %s", reason));
      gst_pad_push_event (pad, gst_event_new_eos ());
    }

    GST_INFO_OBJECT (siddecfp, "pausing task, reason: %s", reason);
    gst_pad_pause_task (pad);
    goto done;
  }
}

static gboolean
start_play_tune (GstSidDecfp * siddecfp)
{
  gboolean res;

  siddecfp->tune=new SidTuneMod(NULL);

  siddecfp->tune->read(siddecfp->tune_buffer, siddecfp->tune_len);
  if (!siddecfp->tune->getStatus())
    goto could_not_load_tune;

  siddecfp->rs = new ReSIDfpBuilder("ReSIDfp");
  if (!siddecfp->rs)
      goto could_not_init_resid;

  siddecfp->config.sidEmulation = siddecfp->rs;
  siddecfp->rs->create(2);

  if (!siddecfp_negotiate (siddecfp))
    goto could_not_negotiate;

  if (siddecfp->engine->config(siddecfp->config)<0)
    goto could_not_set_config;

  siddecfp->tune->selectSong(siddecfp->tune_number);
  if (siddecfp->engine->load(siddecfp->tune)<0)
    goto could_not_load_engine;

  update_tags (siddecfp);

  gst_pad_push_event (siddecfp->srcpad, gst_event_new_new_segment (FALSE, 1.0, GST_FORMAT_TIME, 0, -1, 0));
  siddecfp->total_bytes = 0;

  siddecfp->engine->fastForward (100);

  res = gst_pad_start_task (siddecfp->srcpad, (GstTaskFunction) play_loop, siddecfp->srcpad);
  return res;

  /* ERRORS */
could_not_init_resid:
  {
    GST_ELEMENT_ERROR (siddecfp, LIBRARY, INIT, ("Could not init residfp tune"), ("Could not init residfp"));
    return FALSE;
  }
could_not_load_tune:
  {
    GST_ELEMENT_ERROR (siddecfp, LIBRARY, INIT, ("Could not load tune into engine"),
    	("Could not load tune into engine: %s (Size: %d)", siddecfp->tune->getInfo().statusString, siddecfp->tune_len));
    return FALSE;
  }
could_not_load_engine:
  {
    GST_ELEMENT_ERROR (siddecfp, LIBRARY, INIT, ("Could not load tune into engine"),
    	("Could not load tune into engine: %s (Size: %d)", siddecfp->engine->error(), siddecfp->tune_len));
    return FALSE;
  }
could_not_negotiate:
  {
    GST_ELEMENT_ERROR (siddecfp, CORE, NEGOTIATION, ("Could not negotiate format"), ("Could not negotiate format"));
    return FALSE;
  }
could_not_set_config:
  {
    GST_ELEMENT_ERROR (siddecfp, LIBRARY, INIT, ("Could not set engine config"), ("Could not set engine config"));
    return FALSE;
  }
}

static gboolean
gst_siddecfp_sink_event (GstPad * pad, GstEvent * event)
{
  GstSidDecfp *siddecfp;
  gboolean res;

  siddecfp = GST_SIDDECFP (gst_pad_get_parent (pad));

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_EOS:
      res = start_play_tune (siddecfp);
      break;
    case GST_EVENT_NEWSEGMENT:
      res = FALSE;
      break;
    default:
      res = FALSE;
      break;
  }
  gst_event_unref (event);
  gst_object_unref (siddecfp);

  return res;
}

static GstFlowReturn
gst_siddecfp_chain (GstPad * pad, GstBuffer * buffer)
{
  GstSidDecfp *siddecfp;
  guint64 size;

  siddecfp = GST_SIDDECFP (gst_pad_get_parent (pad));

  size = GST_BUFFER_SIZE (buffer);
  if (siddecfp->tune_len + size > SIDTUNE_MAX_FILELEN)
    goto overflow;

  memcpy (siddecfp->tune_buffer + siddecfp->tune_len, GST_BUFFER_DATA (buffer), size);
  siddecfp->tune_len += size;

  gst_buffer_unref (buffer);
  gst_object_unref (siddecfp);

  return GST_FLOW_OK;

  /* ERRORS */
overflow:
  {
    GST_ELEMENT_ERROR (siddecfp, STREAM, DECODE, (NULL), ("Input data bigger than allowed buffer size"));
    gst_object_unref (siddecfp);
    return GST_FLOW_ERROR;
  }
}

static gboolean
gst_siddecfp_src_convert (GstPad * pad, GstFormat src_format, gint64 src_value, GstFormat * dest_format, gint64 * dest_value)
{
  gboolean res = TRUE;
  guint scale = 1;
  GstSidDecfp *siddecfp;
  gint bytes_per_sample;

  siddecfp = GST_SIDDECFP (gst_pad_get_parent (pad));

  if (src_format == *dest_format) {
    *dest_value = src_value;
    return TRUE;
  }

  bytes_per_sample = 2 * (siddecfp->config.playback==sid2_stereo ? 2 : 1);

  switch (src_format) {
    case GST_FORMAT_BYTES:
      switch (*dest_format) {
        case GST_FORMAT_DEFAULT:
          if (bytes_per_sample == 0)
            return FALSE;
          *dest_value = src_value / bytes_per_sample;
          break;
        case GST_FORMAT_TIME:
        {
          gint byterate = bytes_per_sample * siddecfp->config.frequency;

          if (byterate == 0)
            return FALSE;
          *dest_value = gst_util_uint64_scale_int (src_value, GST_SECOND, byterate);
          break;
        }
        default:
          res = FALSE;
      }
      break;
    case GST_FORMAT_DEFAULT:
      switch (*dest_format) {
        case GST_FORMAT_BYTES:
          *dest_value = src_value * bytes_per_sample;
          break;
        case GST_FORMAT_TIME:
          if (siddecfp->config.frequency == 0)
            return FALSE;
          *dest_value = gst_util_uint64_scale_int (src_value, GST_SECOND, siddecfp->config.frequency);
          break;
        default:
          res = FALSE;
      }
      break;
    case GST_FORMAT_TIME:
      switch (*dest_format) {
        case GST_FORMAT_BYTES:
          scale = bytes_per_sample;
          /* fallthrough */
        case GST_FORMAT_DEFAULT:
          *dest_value = gst_util_uint64_scale_int (src_value, scale * siddecfp->config.frequency, GST_SECOND);
          break;
        default:
          res = FALSE;
      }
      break;
    default:
      res = FALSE;
  }

  return res;
}

static gboolean
gst_siddecfp_src_event (GstPad * pad, GstEvent * event)
{
  gboolean res = FALSE;
  GstSidDecfp *siddecfp;

  siddecfp = GST_SIDDECFP (gst_pad_get_parent (pad));

  switch (GST_EVENT_TYPE (event)) {
    default:
      break;
  }
  gst_event_unref (event);

  gst_object_unref (siddecfp);

  return res;
}

static gboolean
gst_siddecfp_src_query (GstPad * pad, GstQuery * query)
{
  gboolean res = TRUE;
  GstSidDecfp *siddecfp;

  siddecfp = GST_SIDDECFP (gst_pad_get_parent (pad));

  switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_POSITION:
    {
      GstFormat format;
      gint64 current;

      gst_query_parse_position (query, &format, NULL);

      /* we only know about our bytes, convert to requested format */
      res &= gst_siddecfp_src_convert (pad, GST_FORMAT_BYTES, siddecfp->total_bytes, &format, &current);
      if (res) {
        gst_query_set_position (query, format, current);
      }
      break;
    }
    default:
      res = gst_pad_query_default (pad, query);
      break;
  }
  gst_object_unref (siddecfp);

  return res;
}

static void
gst_siddecfp_set_property (GObject * object, guint prop_id, const GValue * value,
    GParamSpec * pspec)
{
  GstSidDecfp *siddecfp = GST_SIDDECFP (object);

  switch (prop_id) {
    case PROP_TUNE:
      siddecfp->tune_number = g_value_get_int (value);
      break;
    case PROP_CLOCK:
      //siddecfp->config->clockSpeed = g_value_get_enum (value);
      break;
    case PROP_MEMORY:
      //siddecfp->config->memoryMode = g_value_get_enum (value);
      break;
    case PROP_FILTER:
      //siddecfp->config->emulateFilter = g_value_get_boolean (value);
      break;
    case PROP_MEASURED_VOLUME:
      //siddecfp->config->measuredVolume = g_value_get_boolean (value);
      break;
    case PROP_MOS8580:
      //siddecfp->config->mos8580 = g_value_get_boolean (value);
      break;
    case PROP_BLOCKSIZE:
      siddecfp->blocksize = g_value_get_ulong (value);
      break;
    case PROP_FORCE_SPEED:
      //siddecfp->config->forceSongSpeed = g_value_get_boolean (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      return;
  }
  siddecfp->engine->config(siddecfp->config);
}

static void
gst_siddecfp_get_property (GObject * object, guint prop_id, GValue * value,
    GParamSpec * pspec)
{
  GstSidDecfp *siddecfp = GST_SIDDECFP (object);

  switch (prop_id) {
    case PROP_TUNE:
      g_value_set_int (value, siddecfp->tune_number);
      break;
    case PROP_CLOCK:
      //g_value_set_enum (value, siddecfp->config->clockSpeed);
      break;
    case PROP_MEMORY:
      //g_value_set_enum (value, siddecfp->config->memoryMode);
      break;
    case PROP_FILTER:
      //g_value_set_boolean (value, siddecfp->config->emulateFilter);
      break;
    case PROP_MEASURED_VOLUME:
      //g_value_set_boolean (value, siddecfp->config->measuredVolume);
      break;
    case PROP_MOS8580:
      //g_value_set_boolean (value, siddecfp->config->mos8580);
      break;
    case PROP_FORCE_SPEED:
      //g_value_set_boolean (value, siddecfp->config->forceSongSpeed);
      break;
    case PROP_BLOCKSIZE:
      g_value_set_ulong (value, siddecfp->blocksize);
      break;
    case PROP_METADATA:
      g_value_set_boxed (value, NULL);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static gboolean
plugin_init (GstPlugin * plugin)
{
  return gst_element_register (plugin, "siddecfp", GST_RANK_PRIMARY, GST_TYPE_SIDDECFP);
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    "siddecfp",
    "Uses libsidplayfp to decode .sid files",
    plugin_init, VERSION, "GPL", "gst-siddecfp", "na");
