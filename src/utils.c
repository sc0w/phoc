/*
 * Copyright (C) 2020,2021 Purism SPC
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Authors: Arnaud Ferraris <arnaud.ferraris@collabora.com>
 *          Clayton Craft <clayton@craftyguy.net>
 *          Guido Günther <agx@sigxcpu.org>
 */

#define G_LOG_DOMAIN "phoc-utils"

#include <wlr/util/box.h>
#include <wlr/version.h>
#include "utils.h"

void
phoc_utils_fix_transform (enum wl_output_transform *transform)
{
  /*
   * Starting from version 0.11.0, wlroots rotates counter-clockwise, while
   * it was rotating clockwise previously.
   * In order to maintain the same behavior, we need to modify the transform
   * before applying it
   */
  switch (*transform) {
  case WL_OUTPUT_TRANSFORM_90:
    *transform = WL_OUTPUT_TRANSFORM_270;
    break;
  case WL_OUTPUT_TRANSFORM_270:
    *transform = WL_OUTPUT_TRANSFORM_90;
    break;
  case WL_OUTPUT_TRANSFORM_FLIPPED_90:
    *transform = WL_OUTPUT_TRANSFORM_FLIPPED_270;
    break;
  case WL_OUTPUT_TRANSFORM_FLIPPED_270:
    *transform = WL_OUTPUT_TRANSFORM_FLIPPED_90;
    break;
  default:
    /* Nothing to be done */
    break;
  }
}

/**
 * phoc_utils_rotate_child_position:
 *
 * Rotate a child's position relative to a parent. The parent size is (pw, ph),
 * the child position is (*sx, *sy) and its size is (sw, sh).
 */
void
phoc_utils_rotate_child_position (double *sx, double *sy, double sw, double sh,
                                  double pw, double ph, float rotation)
{
  if (rotation == 0.0) {
    return;
  }

  // Coordinates relative to the center of the subsurface
  double cx = *sx - pw/2 + sw/2,
         cy = *sy - ph/2 + sh/2;
  // Rotated coordinates
  double rx = cos (rotation)*cx - sin (rotation)*cy,
         ry = cos (rotation)*cy + sin (rotation)*cx;

  *sx = rx + pw/2 - sw/2;
  *sy = ry + ph/2 - sh/2;
}

/**
 * phoc_utils_rotated_bounds:
 *
 * Stores the smallest box that can contain provided box after rotating it
 * by specified rotation into *dest.
 */
void
phoc_utils_rotated_bounds (struct wlr_box *dest, const struct wlr_box *box, float rotation)
{
  if (rotation == 0) {
    *dest = *box;
    return;
  }

  double ox = box->x + (double) box->width / 2;
  double oy = box->y + (double) box->height / 2;

  double c = fabs (cos (rotation));
  double s = fabs (sin (rotation));

  double x1 = ox + (box->x - ox) * c + (box->y - oy) * s;
  double x2 = ox + (box->x + box->width - ox) * c + (box->y + box->height - oy) * s;

  double y1 = oy + (box->x - ox) * s + (box->y - oy) * c;
  double y2 = oy + (box->x + box->width - ox) * s + (box->y + box->height - oy) * c;

  dest->x = floor (fmin (x1, x2));
  dest->width = ceil (fmax (x1, x2) - fmin (x1, x2));
  dest->y = floor (fmin (y1, y2));
  dest->height = ceil (fmax (y1, y2) - fmin (y1, y2));
}

/**
 * phoc_ease_in_cubic:
 * @t: The term
 *
 * Ease in using cubic interpolation.
 */
double
phoc_ease_in_cubic (double t)
{
  double p = t;
  return p * p * p;
}


/**
 * phoc_ease_out_cubic:
 * @t: The term
 *
 * Ease out using cubic interpolation
 */
double
phoc_ease_out_cubic (double t)
{
  double p = t - 1;
  return p * p * p + 1;
}
