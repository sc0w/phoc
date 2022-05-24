/*
 * Copyright (C) 2022
 * SPDX-License-Identifier: GPL-3.0+
 * Author: Pablo Barciela <scow@riseup.net>
 */

#include "testlib.h"

#pragma once

G_BEGIN_DECLS

typedef struct _PhocTestXdgToplevelSurface
{
  struct wl_surface *wl_surface;
  struct xdg_surface *xdg_surface;
  struct xdg_toplevel *xdg_toplevel;
  PhocTestForeignToplevel *foreign_toplevel;
  char* title;
  PhocTestBuffer buffer;
  guint32 width, height;
  gboolean configured;
  gboolean toplevel_configured;
} PhocTestXdgToplevelSurface;

PhocTestXdgToplevelSurface *phoc_test_xdg_surface_new (PhocTestClientGlobals *globals,
                                                       guint32 width,
                                                       guint32 height,
                                                       char* title,
                                                       guint32 color);

void phoc_test_xdg_surface_free (PhocTestXdgToplevelSurface *xs);

G_END_DECLS
