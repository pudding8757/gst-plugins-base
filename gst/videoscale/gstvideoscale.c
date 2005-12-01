/* GStreamer
 * Copyright (C) <1999> Erik Walthinsen <omega@cse.ogi.edu>
 * Copyright (C) 2005 David Schleef <ds@schleef.org>
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


#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <string.h>

#include <gst/video/video.h>

#include "gstvideoscale.h"
#include "vs_image.h"


/* debug variable definition */
GST_DEBUG_CATEGORY (video_scale_debug);

/* elementfactory information */
static GstElementDetails video_scale_details =
GST_ELEMENT_DETAILS ("Video scaler",
    "Filter/Effect/Video",
    "Resizes video",
    "Wim Taymans <wim.taymans@chello.be>");

enum
{
  PROP_0,
  PROP_METHOD
      /* FILL ME */
};

static GstStaticCaps gst_video_scale_format_caps[] = {
  GST_STATIC_CAPS (GST_VIDEO_CAPS_RGBx),
  GST_STATIC_CAPS (GST_VIDEO_CAPS_xRGB),
  GST_STATIC_CAPS (GST_VIDEO_CAPS_BGRx),
  GST_STATIC_CAPS (GST_VIDEO_CAPS_xBGR),
  GST_STATIC_CAPS (GST_VIDEO_CAPS_RGB),
  GST_STATIC_CAPS (GST_VIDEO_CAPS_BGR),
  GST_STATIC_CAPS (GST_VIDEO_CAPS_YUV ("AYUV")),
  GST_STATIC_CAPS (GST_VIDEO_CAPS_YUV ("YUY2")),
  GST_STATIC_CAPS (GST_VIDEO_CAPS_YUV ("YVYU")),
  GST_STATIC_CAPS (GST_VIDEO_CAPS_YUV ("UYVY")),
  GST_STATIC_CAPS (GST_VIDEO_CAPS_YUV ("Y800")),
  GST_STATIC_CAPS (GST_VIDEO_CAPS_YUV ("I420")),
  GST_STATIC_CAPS (GST_VIDEO_CAPS_YUV ("YV12")),
  GST_STATIC_CAPS (GST_VIDEO_CAPS_RGB_16),
  GST_STATIC_CAPS (GST_VIDEO_CAPS_RGB_15)
};

enum
{
  GST_VIDEO_SCALE_RGBx = 0,
  GST_VIDEO_SCALE_xRGB,
  GST_VIDEO_SCALE_BGRx,
  GST_VIDEO_SCALE_xBGR,
  GST_VIDEO_SCALE_RGB,
  GST_VIDEO_SCALE_BGR,
  GST_VIDEO_SCALE_AYUV,
  GST_VIDEO_SCALE_YUY2,
  GST_VIDEO_SCALE_YVYU,
  GST_VIDEO_SCALE_UYVY,
  GST_VIDEO_SCALE_Y,
  GST_VIDEO_SCALE_I420,
  GST_VIDEO_SCALE_YV12,
  GST_VIDEO_SCALE_RGB565,
  GST_VIDEO_SCALE_RGB555
};

#define GST_TYPE_VIDEO_SCALE_METHOD (gst_video_scale_method_get_type())
static GType
gst_video_scale_method_get_type (void)
{
  static GType video_scale_method_type = 0;
  static GEnumValue video_scale_methods[] = {
    {GST_VIDEO_SCALE_NEAREST, "Nearest Neighbour", "nearest-neighbour"},
    {GST_VIDEO_SCALE_BILINEAR, "Bilinear", "bilinear"},
    {0, NULL, NULL},
  };

  if (!video_scale_method_type) {
    video_scale_method_type =
        g_enum_register_static ("GstVideoScaleMethod", video_scale_methods);
  }
  return video_scale_method_type;
}

static GstCaps *
gst_video_scale_get_capslist (void)
{
  static GstCaps *caps;

  if (caps == NULL) {
    int i;

    caps = gst_caps_new_empty ();
    for (i = 0; i < G_N_ELEMENTS (gst_video_scale_format_caps); i++)
      gst_caps_append (caps,
          gst_caps_make_writable
          (gst_static_caps_get (&gst_video_scale_format_caps[i])));
  }

  return caps;
}

static GstPadTemplate *
gst_video_scale_src_template_factory (void)
{
  return gst_pad_template_new ("src", GST_PAD_SRC, GST_PAD_ALWAYS,
      gst_caps_ref (gst_video_scale_get_capslist ()));
}

static GstPadTemplate *
gst_video_scale_sink_template_factory (void)
{
  return gst_pad_template_new ("sink", GST_PAD_SINK, GST_PAD_ALWAYS,
      gst_caps_ref (gst_video_scale_get_capslist ()));
}


static void gst_video_scale_base_init (gpointer g_class);
static void gst_video_scale_class_init (GstVideoScaleClass * klass);
static void gst_video_scale_init (GstVideoScale * videoscale);
static gboolean gst_video_scale_handle_src_event (GstPad * pad,
    GstEvent * event);

/* base transform vmethods */
static GstCaps *gst_video_scale_transform_caps (GstBaseTransform * trans,
    GstPadDirection direction, GstCaps * caps);
static gboolean gst_video_scale_set_caps (GstBaseTransform * trans,
    GstCaps * in, GstCaps * out);
static gboolean gst_video_scale_get_unit_size (GstBaseTransform * trans,
    GstCaps * caps, guint * size);
static GstFlowReturn gst_video_scale_transform (GstBaseTransform * trans,
    GstBuffer * in, GstBuffer * out);
static void gst_video_scale_fixate_caps (GstBaseTransform * base,
    GstPadDirection direction, GstCaps * caps, GstCaps * othercaps);

static void gst_video_scale_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_video_scale_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

static GstElementClass *parent_class = NULL;


GType
gst_video_scale_get_type (void)
{
  static GType video_scale_type = 0;

  if (!video_scale_type) {
    static const GTypeInfo video_scale_info = {
      sizeof (GstVideoScaleClass),
      gst_video_scale_base_init,
      NULL,
      (GClassInitFunc) gst_video_scale_class_init,
      NULL,
      NULL,
      sizeof (GstVideoScale),
      0,
      (GInstanceInitFunc) gst_video_scale_init,
    };

    video_scale_type =
        g_type_register_static (GST_TYPE_BASE_TRANSFORM, "GstVideoScale",
        &video_scale_info, 0);
  }
  return video_scale_type;
}

static void
gst_video_scale_base_init (gpointer g_class)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);

  gst_element_class_set_details (element_class, &video_scale_details);

  gst_element_class_add_pad_template (element_class,
      gst_video_scale_sink_template_factory ());
  gst_element_class_add_pad_template (element_class,
      gst_video_scale_src_template_factory ());
}

static void
gst_video_scale_class_init (GstVideoScaleClass * klass)
{
  GObjectClass *gobject_class;
  GstBaseTransformClass *trans_class;

  gobject_class = (GObjectClass *) klass;
  trans_class = (GstBaseTransformClass *) klass;

  gobject_class->set_property = gst_video_scale_set_property;
  gobject_class->get_property = gst_video_scale_get_property;

  g_object_class_install_property (G_OBJECT_CLASS (klass), PROP_METHOD,
      g_param_spec_enum ("method", "method", "method",
          GST_TYPE_VIDEO_SCALE_METHOD, 0, G_PARAM_READWRITE));

  trans_class->transform_caps =
      GST_DEBUG_FUNCPTR (gst_video_scale_transform_caps);
  trans_class->set_caps = GST_DEBUG_FUNCPTR (gst_video_scale_set_caps);
  trans_class->get_unit_size =
      GST_DEBUG_FUNCPTR (gst_video_scale_get_unit_size);
  trans_class->transform = GST_DEBUG_FUNCPTR (gst_video_scale_transform);
  trans_class->fixate_caps = GST_DEBUG_FUNCPTR (gst_video_scale_fixate_caps);

  trans_class->passthrough_on_same_caps = TRUE;

  parent_class = g_type_class_peek_parent (klass);
}

static void
gst_video_scale_init (GstVideoScale * videoscale)
{
  GstBaseTransform *trans = GST_BASE_TRANSFORM (videoscale);

  gst_pad_set_event_function (trans->srcpad, gst_video_scale_handle_src_event);

  videoscale->tmp_buf = NULL;
  videoscale->method = GST_VIDEO_SCALE_NEAREST;
  /*videoscale->method = GST_VIDEO_SCALE_BILINEAR; */
}


static void
gst_video_scale_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstVideoScale *src = GST_VIDEO_SCALE (object);

  switch (prop_id) {
    case PROP_METHOD:
      src->method = g_value_get_enum (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_video_scale_get_property (GObject * object, guint prop_id, GValue * value,
    GParamSpec * pspec)
{
  GstVideoScale *src = GST_VIDEO_SCALE (object);

  switch (prop_id) {
    case PROP_METHOD:
      g_value_set_enum (value, src->method);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static GstCaps *
gst_video_scale_transform_caps (GstBaseTransform * trans,
    GstPadDirection direction, GstCaps * caps)
{
  GstVideoScale *videoscale;
  GstCaps *ret;
  int i;

  videoscale = GST_VIDEO_SCALE (trans);

  ret = gst_caps_copy (caps);

  for (i = 0; i < gst_caps_get_size (ret); i++) {
    GstStructure *structure = gst_caps_get_structure (ret, i);

    gst_structure_set (structure,
        "width", GST_TYPE_INT_RANGE, 16, 4096,
        "height", GST_TYPE_INT_RANGE, 16, 4096, NULL);
    gst_structure_remove_field (structure, "pixel-aspect-ratio");
  }

  GST_DEBUG_OBJECT (trans, "returning caps: %" GST_PTR_FORMAT, ret);
  return ret;
}

static int
gst_video_scale_get_format (GstCaps * caps)
{
  int i;
  GstCaps *icaps, *scaps;

  for (i = 0; i < G_N_ELEMENTS (gst_video_scale_format_caps); i++) {
    scaps = gst_static_caps_get (&gst_video_scale_format_caps[i]);
    icaps = gst_caps_intersect (caps, scaps);
    if (!gst_caps_is_empty (icaps)) {
      gst_caps_unref (icaps);
      return i;
    }
    gst_caps_unref (icaps);
  }

  return -1;
}

/* calculate the size of a buffer */
static gboolean
gst_video_scale_prepare_size (gint format,
    VSImage * img, gint width, gint height, guint * size)
{
  gboolean res = TRUE;

  img->width = width;
  img->height = height;

  switch (format) {
    case GST_VIDEO_SCALE_RGBx:
    case GST_VIDEO_SCALE_xRGB:
    case GST_VIDEO_SCALE_BGRx:
    case GST_VIDEO_SCALE_xBGR:
    case GST_VIDEO_SCALE_AYUV:
      img->stride = img->width * 4;
      *size = img->stride * img->height;
      break;
    case GST_VIDEO_SCALE_RGB:
    case GST_VIDEO_SCALE_BGR:
      img->stride = GST_ROUND_UP_4 (img->width * 3);
      *size = img->stride * img->height;
      break;
    case GST_VIDEO_SCALE_YUY2:
    case GST_VIDEO_SCALE_YVYU:
    case GST_VIDEO_SCALE_UYVY:
      img->stride = GST_ROUND_UP_4 (img->width * 2);
      *size = img->stride * img->height;
      break;
    case GST_VIDEO_SCALE_Y:
      img->stride = GST_ROUND_UP_4 (img->width);
      *size = img->stride * img->height;
      break;
    case GST_VIDEO_SCALE_I420:
    case GST_VIDEO_SCALE_YV12:
    {
      gulong img_u_stride, img_u_height;

      img->stride = GST_ROUND_UP_4 (img->width);

      img_u_height = GST_ROUND_UP_2 (img->height) / 2;
      img_u_stride = GST_ROUND_UP_4 (img->stride / 2);

      *size = img->stride * GST_ROUND_UP_2 (img->height) +
          2 * img_u_stride * img_u_height;
      break;
    }
    case GST_VIDEO_SCALE_RGB565:
      img->stride = GST_ROUND_UP_4 (img->width * 2);
      *size = img->stride * img->height;
      break;
    case GST_VIDEO_SCALE_RGB555:
      img->stride = GST_ROUND_UP_4 (img->width * 2);
      *size = img->stride * img->height;
      break;
    default:
      g_warning ("don't know how to scale");
      res = FALSE;
      break;
  }

  return res;
}

static gboolean
parse_caps (GstCaps * caps, gint * format, gint * width, gint * height)
{
  gboolean ret;
  GstStructure *structure;

  structure = gst_caps_get_structure (caps, 0);
  ret = gst_structure_get_int (structure, "width", width);
  ret &= gst_structure_get_int (structure, "height", height);

  if (format)
    *format = gst_video_scale_get_format (caps);

  return ret;
}

static gboolean
gst_video_scale_set_caps (GstBaseTransform * trans, GstCaps * in, GstCaps * out)
{
  GstVideoScale *videoscale;
  gboolean ret;

  videoscale = GST_VIDEO_SCALE (trans);

  ret = parse_caps (in, &videoscale->format, &videoscale->from_width,
      &videoscale->from_height);
  ret &= parse_caps (out, NULL, &videoscale->to_width, &videoscale->to_height);
  if (!ret)
    goto done;

  ret = gst_video_scale_prepare_size (videoscale->format,
      &videoscale->src, videoscale->from_width, videoscale->from_height,
      &videoscale->src_size);

  ret &= gst_video_scale_prepare_size (videoscale->format,
      &videoscale->dest, videoscale->to_width, videoscale->to_height,
      &videoscale->dest_size);

  if (!ret)
    goto done;

  if (videoscale->tmp_buf)
    g_free (videoscale->tmp_buf);

  videoscale->tmp_buf = g_malloc (videoscale->dest.stride * 2);

  /* FIXME: par */
  GST_DEBUG_OBJECT (videoscale, "from=%dx%d, size %d -> to=%dx%d, size %d",
      videoscale->from_width, videoscale->from_height, videoscale->src_size,
      videoscale->to_width, videoscale->to_height, videoscale->dest_size);

done:
  return ret;
}

static gboolean
gst_video_scale_get_unit_size (GstBaseTransform * trans, GstCaps * caps,
    guint * size)
{
  GstVideoScale *videoscale;
  gint format, width, height;
  VSImage img;

  g_return_val_if_fail (size, FALSE);

  videoscale = GST_VIDEO_SCALE (trans);

  if (!parse_caps (caps, &format, &width, &height))
    return FALSE;

  if (!gst_video_scale_prepare_size (format, &img, width, height, size))
    return FALSE;

  return TRUE;
}

static void
gst_video_scale_fixate_caps (GstBaseTransform * base, GstPadDirection direction,
    GstCaps * caps, GstCaps * othercaps)
{
  GstStructure *ins, *outs;
  const GValue *from_par, *to_par;

  g_return_if_fail (gst_caps_is_fixed (caps));

  GST_DEBUG_OBJECT (base, "trying to fixate othercaps %" GST_PTR_FORMAT
      " based on caps %" GST_PTR_FORMAT, othercaps, caps);

  ins = gst_caps_get_structure (caps, 0);
  outs = gst_caps_get_structure (othercaps, 0);

  from_par = gst_structure_get_value (ins, "pixel-aspect-ratio");
  to_par = gst_structure_get_value (outs, "pixel-aspect-ratio");

  if (from_par && to_par) {
    GValue to_ratio = { 0, };   /* w/h of output video */
    int from_w, from_h, from_par_n, from_par_d, to_par_n, to_par_d;
    int count = 0, w = 0, h = 0, num, den;

    /* if both width and height are already fixed, we can't do anything
     * about it anymore */
    if (gst_structure_get_int (outs, "width", &w))
      ++count;
    if (gst_structure_get_int (outs, "height", &h))
      ++count;
    if (count == 2) {
      GST_DEBUG_OBJECT (base, "dimensions already set to %dx%d, not fixating",
          w, h);
      return;
    }

    gst_structure_get_int (ins, "width", &from_w);
    gst_structure_get_int (ins, "height", &from_h);
    from_par_n = gst_value_get_fraction_numerator (from_par);
    from_par_d = gst_value_get_fraction_denominator (from_par);
    to_par_n = gst_value_get_fraction_numerator (to_par);
    to_par_d = gst_value_get_fraction_denominator (to_par);

    g_value_init (&to_ratio, GST_TYPE_FRACTION);
    gst_value_set_fraction (&to_ratio, from_w * from_par_n * to_par_d,
        from_h * from_par_d * to_par_n);
    num = gst_value_get_fraction_numerator (&to_ratio);
    den = gst_value_get_fraction_denominator (&to_ratio);
    GST_DEBUG_OBJECT (base,
        "scaling input with %dx%d and PAR %d/%d to output PAR %d/%d",
        from_w, from_h, from_par_n, from_par_d, to_par_n, to_par_d);
    GST_DEBUG_OBJECT (base, "resulting output should respect ratio of %d/%d",
        num, den);

    /* now find a width x height that respects this display ratio.
     * prefer those that have one of w/h the same as the incoming video
     * using wd / hd = num / den */

    /* start with same height, because of interlaced video */
    /* check hd / den is an integer scale factor, and scale wd with the PAR */
    if (from_h % den == 0) {
      GST_DEBUG_OBJECT (base, "keeping video height");
      h = from_h;
      w = h * num / den;
    } else if (from_w % num == 0) {
      GST_DEBUG_OBJECT (base, "keeping video width");
      w = from_w;
      h = w * den / num;
    } else {
      GST_DEBUG_OBJECT (base, "approximating but keeping video height");
      h = from_h;
      w = h * num / den;
    }
    GST_DEBUG_OBJECT (base, "scaling to %dx%d", w, h);

    /* now fixate */
    gst_structure_fixate_field_nearest_int (outs, "width", w);
    gst_structure_fixate_field_nearest_int (outs, "height", h);
  } else {
    gint width, height;

    if (gst_structure_get_int (ins, "width", &width)) {
      if (gst_structure_has_field (outs, "width")) {
        gst_structure_fixate_field_nearest_int (outs, "width", width);
      }
    }
    if (gst_structure_get_int (ins, "height", &height)) {
      if (gst_structure_has_field (outs, "height")) {
        gst_structure_fixate_field_nearest_int (outs, "height", height);
      }
    }
  }

  GST_DEBUG_OBJECT (base, "fixated othercaps to %" GST_PTR_FORMAT, othercaps);
}

static gboolean
gst_video_scale_prepare_image (gint format, GstBuffer * buf,
    VSImage * img, VSImage * img_u, VSImage * img_v)
{
  gboolean res = TRUE;

  img->pixels = GST_BUFFER_DATA (buf);

  switch (format) {
    case GST_VIDEO_SCALE_I420:
    case GST_VIDEO_SCALE_YV12:
      img_u->pixels = img->pixels + GST_ROUND_UP_2 (img->height) * img->stride;
      img_u->height = GST_ROUND_UP_2 (img->height) / 2;
      img_u->width = GST_ROUND_UP_2 (img->width) / 2;
      img_u->stride = GST_ROUND_UP_4 (img->stride / 2);
      memcpy (img_v, img_u, sizeof (*img_v));
      img_v->pixels = img_u->pixels + img_u->height * img_u->stride;
      break;
    default:
      break;
  }
  return res;
}

static GstFlowReturn
gst_video_scale_transform (GstBaseTransform * trans, GstBuffer * in,
    GstBuffer * out)
{
  GstVideoScale *videoscale;
  GstFlowReturn ret = GST_FLOW_OK;
  VSImage *dest;
  VSImage *src;
  VSImage dest_u;
  VSImage dest_v;
  VSImage src_u;
  VSImage src_v;

  videoscale = GST_VIDEO_SCALE (trans);

  gst_buffer_stamp (out, in);

  src = &videoscale->src;
  dest = &videoscale->dest;

  gst_video_scale_prepare_image (videoscale->format, in, src, &src_u, &src_v);
  gst_video_scale_prepare_image (videoscale->format, out, dest, &dest_u,
      &dest_v);

  switch (videoscale->method) {
    case GST_VIDEO_SCALE_NEAREST:
      switch (videoscale->format) {
        case GST_VIDEO_SCALE_RGBx:
        case GST_VIDEO_SCALE_xRGB:
        case GST_VIDEO_SCALE_BGRx:
        case GST_VIDEO_SCALE_xBGR:
        case GST_VIDEO_SCALE_AYUV:
          vs_image_scale_nearest_RGBA (dest, src, videoscale->tmp_buf);
          break;
        case GST_VIDEO_SCALE_RGB:
        case GST_VIDEO_SCALE_BGR:
          vs_image_scale_nearest_RGB (dest, src, videoscale->tmp_buf);
          break;
        case GST_VIDEO_SCALE_YUY2:
        case GST_VIDEO_SCALE_YVYU:
          vs_image_scale_nearest_YUYV (dest, src, videoscale->tmp_buf);
          break;
        case GST_VIDEO_SCALE_UYVY:
          vs_image_scale_nearest_UYVY (dest, src, videoscale->tmp_buf);
          break;
        case GST_VIDEO_SCALE_Y:
          vs_image_scale_nearest_Y (dest, src, videoscale->tmp_buf);
          break;
        case GST_VIDEO_SCALE_I420:
        case GST_VIDEO_SCALE_YV12:
          vs_image_scale_nearest_Y (dest, src, videoscale->tmp_buf);
          vs_image_scale_nearest_Y (&dest_u, &src_u, videoscale->tmp_buf);
          vs_image_scale_nearest_Y (&dest_v, &src_v, videoscale->tmp_buf);
          break;
        case GST_VIDEO_SCALE_RGB565:
          vs_image_scale_nearest_RGB565 (dest, src, videoscale->tmp_buf);
          break;
        case GST_VIDEO_SCALE_RGB555:
          vs_image_scale_nearest_RGB555 (dest, src, videoscale->tmp_buf);
          break;
        default:
          g_warning ("don't know how to scale");
      }
      break;
    case GST_VIDEO_SCALE_BILINEAR:
      switch (videoscale->format) {
        case GST_VIDEO_SCALE_RGBx:
        case GST_VIDEO_SCALE_xRGB:
        case GST_VIDEO_SCALE_BGRx:
        case GST_VIDEO_SCALE_xBGR:
        case GST_VIDEO_SCALE_AYUV:
          vs_image_scale_linear_RGBA (dest, src, videoscale->tmp_buf);
          break;
        case GST_VIDEO_SCALE_RGB:
        case GST_VIDEO_SCALE_BGR:
          vs_image_scale_linear_RGB (dest, src, videoscale->tmp_buf);
          break;
        case GST_VIDEO_SCALE_YUY2:
        case GST_VIDEO_SCALE_YVYU:
          vs_image_scale_linear_YUYV (dest, src, videoscale->tmp_buf);
          break;
        case GST_VIDEO_SCALE_UYVY:
          vs_image_scale_linear_UYVY (dest, src, videoscale->tmp_buf);
          break;
        case GST_VIDEO_SCALE_Y:
          vs_image_scale_linear_Y (dest, src, videoscale->tmp_buf);
          break;
        case GST_VIDEO_SCALE_I420:
        case GST_VIDEO_SCALE_YV12:
          vs_image_scale_linear_Y (dest, src, videoscale->tmp_buf);
          //memset (dest_u.pixels, 128, dest_u.stride * dest_u.height);
          //memset (dest_v.pixels, 128, dest_v.stride * dest_v.height);
          vs_image_scale_linear_Y (&dest_u, &src_u, videoscale->tmp_buf);
          vs_image_scale_linear_Y (&dest_v, &src_v, videoscale->tmp_buf);
          break;
        case GST_VIDEO_SCALE_RGB565:
          vs_image_scale_linear_RGB565 (dest, src, videoscale->tmp_buf);
          break;
        case GST_VIDEO_SCALE_RGB555:
          vs_image_scale_linear_RGB555 (dest, src, videoscale->tmp_buf);
          break;
        default:
          g_warning ("don't know how to scale");
      }
      break;
    default:
      ret = GST_FLOW_ERROR;
      break;
  }

  GST_LOG_OBJECT (videoscale, "pushing buffer of %d bytes",
      GST_BUFFER_SIZE (out));

  return ret;
}

static gboolean
gst_video_scale_handle_src_event (GstPad * pad, GstEvent * event)
{
  GstVideoScale *videoscale;
  gboolean ret;
  double a;
  GstStructure *structure;

  videoscale = GST_VIDEO_SCALE (gst_pad_get_parent (pad));

  GST_DEBUG_OBJECT (videoscale, "handling %s event",
      GST_EVENT_TYPE_NAME (event));

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_NAVIGATION:
      event =
          GST_EVENT (gst_mini_object_make_writable (GST_MINI_OBJECT (event)));

      structure = (GstStructure *) gst_event_get_structure (event);
      if (gst_structure_get_double (structure, "pointer_x", &a)) {
        gst_structure_set (structure, "pointer_x", G_TYPE_DOUBLE,
            a * videoscale->from_width / videoscale->to_width, NULL);
      }
      if (gst_structure_get_double (structure, "pointer_y", &a)) {
        gst_structure_set (structure, "pointer_y", G_TYPE_DOUBLE,
            a * videoscale->from_height / videoscale->to_height, NULL);
      }
      break;
    default:
      break;
  }

  ret = gst_pad_event_default (pad, event);

  gst_object_unref (videoscale);

  return ret;
}

static gboolean
plugin_init (GstPlugin * plugin)
{
  if (!gst_element_register (plugin, "videoscale", GST_RANK_NONE,
          GST_TYPE_VIDEO_SCALE))
    return FALSE;

  GST_DEBUG_CATEGORY_INIT (video_scale_debug, "videoscale", 0,
      "videoscale element");

  return TRUE;
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    "videoscale",
    "Resizes video", plugin_init, VERSION, GST_LICENSE, GST_PACKAGE_NAME,
    GST_PACKAGE_ORIGIN)
