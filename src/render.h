/*
 * Copyright (C) 2020,2021 Purism SPC
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#pragma once

#include "output.h"

#include <glib-object.h>

#include <wlr/render/wlr_renderer.h>

G_BEGIN_DECLS

#define PHOC_TYPE_RENDERER (phoc_renderer_get_type ())

G_DECLARE_FINAL_TYPE (PhocRenderer, phoc_renderer, PHOC, RENDERER, GObject)

PhocRenderer *phoc_renderer_new (struct wlr_backend *wlr_backend, GError **error);
void          phoc_renderer_render_output (PhocRenderer *self, PhocOutput *output);
gboolean      phoc_renderer_render_view_to_buffer (PhocRenderer *self,
                                                   PhocView     *view,
                                                   int           width,
                                                   int           height,
                                                   int           stride,
                                                   uint32_t     *flags,
                                                   void         *data);
G_END_DECLS
