/*
 * Copyright (C) 2022 Purism SPC
 *
 * SPDX-License-Identifier: GPL-3.0+
 * Author: Guido Günther <agx@sigxcpu.org>
 */

#define G_LOG_DOMAIN "phoc-layer-shell-effects"

#include "config.h"
#include "layers.h"
#include "layer-shell-effects.h"
#include "phoc-enums.h"
#include "server.h"
#include "utils.h"

#include <glib-object.h>

#define LAYER_SHELL_EFFECTS_VERSION 1

/**
 * PhocLayerShellEffects:
 *
 * Additional effects for layer surfaces.
 */
struct _PhocLayerShellEffects {
  GObject             parent;

  guint32             version;
  struct wl_global   *global;
  GSList             *resources;
  GSList             *drag_surfaces;
  GHashTable         *drag_surfaces_by_layer_sufrace;
};

G_DEFINE_TYPE (PhocLayerShellEffects, phoc_layer_shell_effects, G_TYPE_OBJECT)

static PhocLayerShellEffects    *phoc_layer_shell_effects_from_resource    (struct wl_resource *resource);
static PhocDraggableLayerSurface *phoc_draggable_layer_surface_from_resource (struct wl_resource *resource);


static void resource_handle_destroy(struct wl_client *client,
		struct wl_resource *resource) {
	wl_resource_destroy(resource);
}


static void
handle_draggable_layer_surface_set_margins (struct wl_client   *client,
                                            struct wl_resource *resource,
                                            int32_t             margin_folded,
                                            int32_t             margin_unfolded)
{
  PhocDraggableLayerSurface *drag_surface = wl_resource_get_user_data (resource);

  g_assert (drag_surface);
  g_assert (PHOC_IS_LAYER_SURFACE (drag_surface->layer_surface));
  /* TODO: We only handle 0 unfolded margin atm */
  g_assert (margin_unfolded == 0);

  g_debug ("Draggable Layer surface margins for %p: %d,%d", drag_surface,
           margin_folded, margin_unfolded);

  switch (drag_surface->layer_surface->layer_surface->current.anchor) {
  case PHOC_LAYER_SHELL_EFFECT_DRAG_FROM_TOP:
    G_GNUC_FALLTHROUGH;
  case PHOC_LAYER_SHELL_EFFECT_DRAG_FROM_BOTTOM:
    G_GNUC_FALLTHROUGH;
  case PHOC_LAYER_SHELL_EFFECT_DRAG_FROM_LEFT:
    G_GNUC_FALLTHROUGH;
  case PHOC_LAYER_SHELL_EFFECT_DRAG_FROM_RIGHT:
    drag_surface->drag.draggable = drag_surface->layer_surface->layer_surface->current.anchor;
    break;
  default:
    wl_resource_post_error (resource, ZPHOC_LAYER_SHELL_EFFECTS_V1_ERROR_BAD_ANCHORS,
                            "Surface not anchored on three edges");
    return;
  }

  drag_surface->pending.folded = margin_folded;
  drag_surface->pending.unfolded = margin_unfolded;
}


static void
handle_draggable_layer_surface_set_exclusive (struct wl_client   *client,
                                              struct wl_resource *resource,
                                              uint32_t            exclusive)
{
  PhocDraggableLayerSurface *drag_surface = wl_resource_get_user_data (resource);

  g_assert (PHOC_IS_LAYER_SURFACE (drag_surface->layer_surface));
  drag_surface->pending.exclusive = exclusive;
}


static void
handle_draggable_layer_surface_set_threshold (struct wl_client   *client,
                                              struct wl_resource *resource,
                                              wl_fixed_t          threshold_f)
{
  PhocDraggableLayerSurface *drag_surface = wl_resource_get_user_data (resource);
  double threshold;

  g_assert (PHOC_IS_LAYER_SURFACE (drag_surface->layer_surface));

  threshold = wl_fixed_to_double (threshold_f);
  g_debug ("Draggable Layer surface threshold for %p: %f", drag_surface, threshold);
  threshold = (threshold < 1.0) ? threshold : 1.0;
  threshold = (threshold > 0.0) ? threshold : 0.0;

  drag_surface->pending.threshold = threshold;
}

static void
handle_draggable_layer_surface_set_state (struct wl_client *client,
                                          struct wl_resource *resource,
                                          uint32_t state)
{
  PhocDraggableLayerSurface *drag_surface = wl_resource_get_user_data (resource);
  PhocAnimDir dir;

  g_assert (drag_surface->layer_surface);

  switch (state) {
  case ZPHOC_DRAGGABLE_LAYER_SURFACE_V1_DRAG_END_STATE_FOLDED:
    dir = ANIM_DIR_IN;
    break;
  case ZPHOC_DRAGGABLE_LAYER_SURFACE_V1_DRAG_END_STATE_UNFOLDED:
    dir = ANIM_DIR_OUT;
    break;
  default:
    g_warning ("Drag surface %p: Ignoring invalid drag state: %d", drag_surface, state);
    return;
  }

  g_debug ("Sliding %p: %d", drag_surface, dir);
  phoc_draggable_layer_surface_slide (drag_surface, dir);
}


static const struct zphoc_draggable_layer_surface_v1_interface draggable_layer_surface_v1_impl = {
  .set_margins = handle_draggable_layer_surface_set_margins,
  .set_exclusive = handle_draggable_layer_surface_set_exclusive,
  .set_threshold = handle_draggable_layer_surface_set_threshold,
  .set_state = handle_draggable_layer_surface_set_state,
  .destroy = resource_handle_destroy,
};


static PhocDraggableLayerSurface *
phoc_draggable_layer_surface_from_resource (struct wl_resource *resource)
{
  g_assert (wl_resource_instance_of (resource, &zphoc_draggable_layer_surface_v1_interface,
                                     &draggable_layer_surface_v1_impl));
  return wl_resource_get_user_data (resource);
}


static void
phoc_draggable_layer_surface_destroy (PhocDraggableLayerSurface *self)
{
  PhocLayerShellEffects *layer_shell_effects;

  if (self == NULL)
    return;

  g_debug ("Destroying draggable_layer_surface %p (res %p)", self, self->resource);
  layer_shell_effects = PHOC_LAYER_SHELL_EFFECTS (self->layer_shell_effects);
  g_assert (PHOC_IS_LAYER_SHELL_EFFECTS (layer_shell_effects));

  wl_list_remove (&self->surface_handle_commit.link);
  wl_list_remove (&self->layer_surface_handle_destroy.link);

  g_hash_table_remove (layer_shell_effects->drag_surfaces_by_layer_sufrace, self->layer_surface);
  layer_shell_effects->drag_surfaces = g_slist_remove (layer_shell_effects->drag_surfaces, self);

  wl_resource_set_user_data (self->resource, NULL);
  g_free (self);
}


static void
draggable_layer_surface_handle_resource_destroy (struct wl_resource *resource)
{
  PhocDraggableLayerSurface *self = phoc_draggable_layer_surface_from_resource (resource);

  phoc_draggable_layer_surface_destroy (self);
}

/*
 * TODO: Use wlr_layer_surface_v1_from_resource instead
 *  https://gitlab.freedesktop.org/wlroots/wlroots/-/merge_requests/3480
 */
static struct wlr_layer_surface_v1*
phoc_wlr_layer_surface_from_resource (struct wl_resource *resource)
{
  return wl_resource_get_user_data (resource);
}

static void
layer_surface_handle_destroy (struct wl_listener *listener, void *data)
{
  PhocDraggableLayerSurface *drag_surface =
    wl_container_of(listener, drag_surface, layer_surface_handle_destroy);

  phoc_draggable_layer_surface_destroy (drag_surface);
}


static void
surface_handle_commit (struct wl_listener *listener, void *data)
{
  PhocDraggableLayerSurface *drag_surface =
    wl_container_of(listener, drag_surface, surface_handle_commit);
  PhocLayerSurface *layer = drag_surface->layer_surface;
  gboolean changed = FALSE;

  if (layer == NULL)
    return;

  if (drag_surface->current.folded != drag_surface->pending.folded) {
    drag_surface->current.folded = drag_surface->pending.folded;
    changed = TRUE;
  }
  drag_surface->current.unfolded = drag_surface->pending.unfolded;
  drag_surface->current.exclusive = drag_surface->pending.exclusive;
  drag_surface->current.threshold = drag_surface->pending.threshold;

  /* Update animation end in case it's ongoing to compensate for size changes */
  if (changed && drag_surface->drag.anim_dir == ANIM_DIR_IN) {
      drag_surface->drag.anim_end = drag_surface->current.folded;
      phoc_draggable_layer_surface_slide (drag_surface, ANIM_DIR_IN);
  }

  /* Keep in sync with layer surface geometry changes */
  if (drag_surface->geo.width == layer->geo.width &&
      drag_surface->geo.height == layer->geo.height)
    return;

  g_debug ("Geometry changed %dx%d", layer->geo.width, layer->geo.height);
  drag_surface->geo = layer->geo;
}


static void
handle_get_draggable_layer_surface (struct wl_client   *client,
                                    struct wl_resource *layer_shell_effects_resource,
                                    uint32_t            id,
                                    struct wl_resource *layer_surface_resource)
{
  PhocLayerShellEffects *self;
  g_autofree PhocDraggableLayerSurface *drag_surface = NULL;
  struct wlr_surface *wlr_surface;
  struct wlr_layer_surface_v1 *wlr_layer_surface;
  int version;

  self = phoc_layer_shell_effects_from_resource (layer_shell_effects_resource);
  g_assert (PHOC_IS_LAYER_SHELL_EFFECTS (self));
  wlr_layer_surface = phoc_wlr_layer_surface_from_resource (layer_surface_resource);
  wlr_surface = wlr_layer_surface->surface;
  g_assert (wlr_surface);

  drag_surface = g_new0 (PhocDraggableLayerSurface, 1);

  version = wl_resource_get_version (layer_shell_effects_resource);
  drag_surface->layer_shell_effects = self;
  drag_surface->resource = wl_resource_create (client,
                                               &zphoc_draggable_layer_surface_v1_interface,
                                               version,
                                               id);
  if (drag_surface->resource == NULL) {
    wl_client_post_no_memory(client);
    return;
  }

  g_debug ("New draggable layer_surface %p (res %p)", drag_surface, drag_surface->resource);
  wl_resource_set_implementation (drag_surface->resource,
                                  &draggable_layer_surface_v1_impl,
                                  drag_surface,
                                  draggable_layer_surface_handle_resource_destroy);

  drag_surface->layer_surface = wlr_layer_surface->data;
  if (!drag_surface->layer_surface) {
    wl_resource_post_error (layer_shell_effects_resource,
                            ZPHOC_LAYER_SHELL_EFFECTS_V1_ERROR_BAD_SURFACE,
                            "Layer surface not yet committed");
    return;
  }

  g_assert (PHOC_IS_LAYER_SURFACE (drag_surface->layer_surface));

  drag_surface->surface_handle_commit.notify = surface_handle_commit;
  wl_signal_add (&wlr_surface->events.commit, &drag_surface->surface_handle_commit);

  drag_surface->layer_surface_handle_destroy.notify = layer_surface_handle_destroy;
  wl_signal_add (&wlr_layer_surface->events.destroy, &drag_surface->layer_surface_handle_destroy);

  g_hash_table_insert (self->drag_surfaces_by_layer_sufrace,
                       drag_surface->layer_surface, drag_surface);
  self->drag_surfaces = g_slist_prepend (self->drag_surfaces, g_steal_pointer (&drag_surface));
}


static void
layer_shell_effects_handle_resource_destroy (struct wl_resource *resource)
{
  PhocLayerShellEffects *self = wl_resource_get_user_data (resource);

  g_assert (PHOC_IS_LAYER_SHELL_EFFECTS (self));

  g_debug ("Destroying layer_shell_effects %p (res %p)", self, resource);
  self->resources = g_slist_remove (self->resources, resource);
}


static const struct zphoc_layer_shell_effects_v1_interface layer_shell_effects_impl = {
  .destroy = resource_handle_destroy,
  .get_draggable_layer_surface = handle_get_draggable_layer_surface,
};


static void
layer_shell_effects_bind (struct wl_client *client, void *data, uint32_t version, uint32_t id)
{
  PhocLayerShellEffects *self = PHOC_LAYER_SHELL_EFFECTS (data);
  struct wl_resource *resource  = wl_resource_create (client, &zphoc_layer_shell_effects_v1_interface,
                                                      version, id);

  g_assert (PHOC_IS_LAYER_SHELL_EFFECTS (self));

  wl_resource_set_implementation (resource,
                                  &layer_shell_effects_impl,
                                  self,
                                  layer_shell_effects_handle_resource_destroy);

  self->resources = g_slist_prepend (self->resources, resource);
  self->version = version;
  return;
}


static PhocLayerShellEffects *
phoc_layer_shell_effects_from_resource (struct wl_resource *resource)
{
  g_assert (wl_resource_instance_of (resource, &zphoc_layer_shell_effects_v1_interface,
                                     &layer_shell_effects_impl));
  return wl_resource_get_user_data (resource);
}


static void
phoc_layer_shell_effects_finalize (GObject *object)
{
  PhocLayerShellEffects *self = PHOC_LAYER_SHELL_EFFECTS (object);

  wl_global_destroy (self->global);

  G_OBJECT_CLASS (phoc_layer_shell_effects_parent_class)->finalize (object);
}


static void
phoc_layer_shell_effects_class_init (PhocLayerShellEffectsClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->finalize = phoc_layer_shell_effects_finalize;
}


static void
phoc_layer_shell_effects_init (PhocLayerShellEffects *self)
{
  struct wl_display *display = phoc_server_get_default ()->wl_display;

  self->drag_surfaces_by_layer_sufrace = g_hash_table_new (g_direct_hash, g_direct_equal);
  self->global = wl_global_create (display, &zphoc_layer_shell_effects_v1_interface,
                                   LAYER_SHELL_EFFECTS_VERSION, self, layer_shell_effects_bind);

}


PhocLayerShellEffects *
phoc_layer_shell_effects_new (void)
{
  return PHOC_LAYER_SHELL_EFFECTS (g_object_new (PHOC_TYPE_LAYER_SHELL_EFFECTS, NULL));
}


static void
on_render_start (PhocDraggableLayerSurface *self, PhocOutput *output, PhocRenderer *renderer)
{
  struct wlr_layer_surface_v1 *layer = self->layer_surface->layer_surface;
  struct wlr_output *wlr_output = layer->output;
  double margin;

  g_assert (PHOC_IS_OUTPUT (output));
  g_assert (self->state == PHOC_DRAGGABLE_SURFACE_STATE_ANIMATING);

  if (output->wlr_output != wlr_output)
    return;

  /* TODO: use a render clock independent timer */
#define TICK 50
  self->drag.anim_t += ((float)TICK) / 1000.0;

  if (self->drag.anim_t > 1.0) {
    self->drag.anim_t = 1.0;
  }

  switch (layer->current.anchor) {
  case PHOC_LAYER_SHELL_EFFECT_DRAG_FROM_TOP:
    margin = (int32_t)layer->client_pending.margin.top;
    break;
  case PHOC_LAYER_SHELL_EFFECT_DRAG_FROM_BOTTOM:
    margin = (int32_t)layer->client_pending.margin.bottom;
    break;
  case PHOC_LAYER_SHELL_EFFECT_DRAG_FROM_LEFT:
    margin = (int32_t)layer->client_pending.margin.left;
    break;
  case PHOC_LAYER_SHELL_EFFECT_DRAG_FROM_RIGHT:
    margin = (int32_t)layer->client_pending.margin.right;
    break;
  default:
    g_assert_not_reached ();
    break;
  }

  if ((self->drag.anim_dir == ANIM_DIR_IN && margin <= self->drag.anim_end) ||
      (self->drag.anim_dir == ANIM_DIR_OUT && margin >= self->drag.anim_end)) {
    enum zphoc_draggable_layer_surface_v1_drag_end_state state;

    g_debug ("Ending animation for %p, margin: %f", self, margin);
    g_clear_signal_handler (&self->drag.anim_id, renderer);

    state = self->drag.anim_dir ? ZPHOC_DRAGGABLE_LAYER_SURFACE_V1_DRAG_END_STATE_UNFOLDED :
      ZPHOC_DRAGGABLE_LAYER_SURFACE_V1_DRAG_END_STATE_FOLDED;

    zphoc_draggable_layer_surface_v1_send_drag_end (self->resource, state);

    self->state = PHOC_DRAGGABLE_SURFACE_STATE_NONE;
    return;
  }

  if (self->drag.anim_dir == ANIM_DIR_IN) {
    /* TODO: should be ~200ms independent of render freq or 300ms for full screen */
    /* TODO: Animation should pick up swiping speed */
    margin = self->drag.anim_start +
      (self->drag.anim_end - self->drag.anim_start) * phoc_ease_in_cubic (self->drag.anim_t);
  } else if (self->drag.anim_dir == ANIM_DIR_OUT) {
    /* TODO: should be ~250ms independent of render freq or 350ms for full screen */
    margin = self->drag.anim_start -
      (self->drag.anim_start * phoc_ease_out_cubic (self->drag.anim_t));
  } else {
    g_assert_not_reached ();
  }

  /* The client is not supposed to update margin or exclusive zone so
   * keep current client_pending in sync */
  g_debug ("%s: margin: %f %f", __func__, self->drag.anim_t, margin);
  switch (layer->current.anchor) {
  case PHOC_LAYER_SHELL_EFFECT_DRAG_FROM_TOP:
    layer->client_pending.margin.top = (int32_t)margin;
    layer->current.margin.top = layer->client_pending.margin.top;
    break;
  case PHOC_LAYER_SHELL_EFFECT_DRAG_FROM_BOTTOM:
    layer->client_pending.margin.bottom = (int32_t)margin;
    layer->current.margin.bottom = layer->client_pending.margin.bottom;
    break;
  case PHOC_LAYER_SHELL_EFFECT_DRAG_FROM_LEFT:
    layer->client_pending.margin.left = (int32_t)margin;
    layer->current.margin.left = layer->client_pending.margin.left;
    break;
  case PHOC_LAYER_SHELL_EFFECT_DRAG_FROM_RIGHT:
    layer->client_pending.margin.right = (int32_t)margin;
    layer->current.margin.right = layer->client_pending.margin.right;
    break;
  default:
    g_assert_not_reached ();
    break;
  }

  layer->client_pending.exclusive_zone = -margin + self->current.exclusive;
  layer->current.exclusive_zone = layer->client_pending.exclusive_zone;

  zphoc_draggable_layer_surface_v1_send_dragged (self->resource, (int32_t)margin);
  phoc_layer_shell_arrange (output);

  /* FIXME: way too much damage */
  phoc_output_damage_whole (output);
}


void
phoc_draggable_layer_surface_slide (PhocDraggableLayerSurface *self, PhocAnimDir anim_dir)
{
  struct wlr_layer_surface_v1 *layer = self->layer_surface->layer_surface;
  double margin;
  PhocRenderer *renderer = phoc_server_get_default()->renderer;

  switch (layer->current.anchor) {
  case PHOC_LAYER_SHELL_EFFECT_DRAG_FROM_TOP:
    margin = (double)(int32_t)layer->client_pending.margin.top;
    break;
  case PHOC_LAYER_SHELL_EFFECT_DRAG_FROM_BOTTOM:
    margin = (double)(int32_t)layer->client_pending.margin.bottom;
    break;
  case PHOC_LAYER_SHELL_EFFECT_DRAG_FROM_LEFT:
    margin = (double)(int32_t)layer->client_pending.margin.left;
    break;
  case PHOC_LAYER_SHELL_EFFECT_DRAG_FROM_RIGHT:
    margin = (double)(int32_t)layer->client_pending.margin.right;
    break;
  default:
    g_assert_not_reached ();
    break;
  }

  self->drag.anim_t = 0;
  self->drag.anim_start = margin;
  self->drag.anim_dir = anim_dir;
  self->drag.anim_end = (anim_dir == ANIM_DIR_OUT) ? 0 : self->current.folded;

  self->state = PHOC_DRAGGABLE_SURFACE_STATE_ANIMATING;

  g_debug ("%s: start: %d, end: %d dir: %d", __func__,
          self->drag.anim_start, self->drag.anim_end, self->drag.anim_dir);
  g_clear_signal_handler (&self->drag.anim_id, renderer);
  self->drag.anim_id = g_signal_connect_swapped (renderer,
                                                 "render-start",
                                                 G_CALLBACK (on_render_start),
                                                 self);
}


gboolean
phoc_draggable_layer_surface_is_draggable (PhocDraggableLayerSurface *self)
{
  return !!self->drag.draggable;
}


PhocDraggableSurfaceState
phoc_draggable_layer_surface_drag_start (PhocDraggableLayerSurface *self, double lx, double ly)
{
  struct wlr_layer_surface_v1 *layer = self->layer_surface->layer_surface;
  int32_t margin;

  /* The user "catched" the surface during an animation */
  if (self->state == PHOC_DRAGGABLE_SURFACE_STATE_ANIMATING) {
    /* TODO: rather end the animation */
    return PHOC_DRAGGABLE_SURFACE_STATE_ANIMATING;
  }

  /* TODO: can happen when the user "catches" an animated surface */
  g_return_val_if_fail (self->state == PHOC_DRAGGABLE_SURFACE_STATE_NONE, self->state);

  switch (layer->current.anchor) {
  case PHOC_LAYER_SHELL_EFFECT_DRAG_FROM_TOP:
    margin = (int32_t)layer->client_pending.margin.top;
    break;
  case PHOC_LAYER_SHELL_EFFECT_DRAG_FROM_BOTTOM:
    margin = (int32_t)layer->client_pending.margin.bottom;
    break;
  case PHOC_LAYER_SHELL_EFFECT_DRAG_FROM_LEFT:
    margin = (int32_t)layer->client_pending.margin.left;
    break;
  case PHOC_LAYER_SHELL_EFFECT_DRAG_FROM_RIGHT:
    margin = (int32_t)layer->client_pending.margin.right;
    break;
  default:
    g_assert_not_reached ();
    break;
  }

  g_debug ("%s: %f,%f, margin-top: %d", __func__, lx, ly, margin);

  /* The 'update' signal is relative to the start point */
  self->drag.last_x = 0.0;
  self->drag.last_y = 0.0;
  self->drag.pending_threshold = 0;

  self->state = PHOC_DRAGGABLE_SURFACE_STATE_PENDING;
  return self->state;
}


static gboolean
phoc_draggable_surface_is_vertical (PhocDraggableLayerSurface *self)
{
  struct wlr_layer_surface_v1 *layer = self->layer_surface->layer_surface;

  switch (layer->current.anchor) {
  case PHOC_LAYER_SHELL_EFFECT_DRAG_FROM_TOP:
  case PHOC_LAYER_SHELL_EFFECT_DRAG_FROM_BOTTOM:
    return TRUE;
  case PHOC_LAYER_SHELL_EFFECT_DRAG_FROM_LEFT:
  case PHOC_LAYER_SHELL_EFFECT_DRAG_FROM_RIGHT:
    return FALSE;
  default:
    g_assert_not_reached ();
  }
}

#define DRAG_THRESHOLD_DISTANCE 16

PhocDraggableSurfaceState
phoc_draggable_layer_surface_drag_update (PhocDraggableLayerSurface *self, double lx, double ly)
{
  struct wlr_layer_surface_v1 *layer = self->layer_surface->layer_surface;
  struct wlr_output *wlr_output = layer->output;
  PhocOutput *output;
  int32_t margin = 0;
  int32_t dx, dy;
  gboolean is_delta_vertical;

  if (!wlr_output) {
    self->state = PHOC_DRAGGABLE_SURFACE_STATE_REJECTED;
    return self->state;
  }

  output = PHOC_OUTPUT (wlr_output->data);
  g_assert (PHOC_IS_OUTPUT (output));

  /* TODO: can happen when the user "catches" an animated surface */
  g_return_val_if_fail (self->state == PHOC_DRAGGABLE_SURFACE_STATE_PENDING ||
                        self->state == PHOC_DRAGGABLE_SURFACE_STATE_DRAGGING, self->state);

  dx = lx - self->drag.last_x;
  dy = ly - self->drag.last_y;
  is_delta_vertical = (ABS (dy) > ABS (dx));
  if (phoc_draggable_surface_is_vertical (self) && is_delta_vertical) {
    self->drag.pending_threshold += dy;
  } else if (!phoc_draggable_surface_is_vertical (self) && !is_delta_vertical) {
    self->drag.pending_threshold += dx;
  }
  if (ABS (self->drag.pending_threshold) < DRAG_THRESHOLD_DISTANCE &&
      self->state == PHOC_DRAGGABLE_SURFACE_STATE_PENDING)
    return self->state;

  g_debug ("%s: %f,%f, margin %d,%d  dy: %f",
           __func__,
           lx, ly,
           layer->client_pending.margin.top,
           layer->client_pending.margin.bottom,
           ly - self->drag.last_y);

  switch (layer->current.anchor) {
  case PHOC_LAYER_SHELL_EFFECT_DRAG_FROM_TOP:
    margin = (int32_t)layer->client_pending.margin.top + dy;
    if (margin >= 0)
      margin = 0;

    if (margin <= self->current.folded)
      margin = self->current.folded;

    layer->client_pending.margin.top = layer->current.margin.top = margin;
    break;
  case PHOC_LAYER_SHELL_EFFECT_DRAG_FROM_BOTTOM:
    margin = (int32_t)layer->client_pending.margin.bottom - dy;
    if (margin >= 0)
      margin = 0;

    if (margin <= self->current.folded)
      layer->client_pending.margin.bottom = self->current.folded;

    layer->client_pending.margin.bottom = layer->current.margin.bottom = margin;
    break;
  case PHOC_LAYER_SHELL_EFFECT_DRAG_FROM_LEFT:
    margin = (int32_t)layer->client_pending.margin.left + dx;
    if (margin >= 0)
      margin = 0;

    if (margin <= self->current.folded)
      margin = self->current.folded;

    layer->client_pending.margin.left = layer->current.margin.left = margin;
    break;
  case PHOC_LAYER_SHELL_EFFECT_DRAG_FROM_RIGHT:
    margin = (int32_t) layer->client_pending.margin.right - dx;
    if (margin >= 0)
      margin = 0;

    if (margin <= self->current.folded)
      margin = self->current.folded;

    layer->client_pending.margin.right = layer->current.margin.right = margin;
    break;
  default:
    g_assert_not_reached ();
    break;
  }

  layer->client_pending.exclusive_zone = -margin + self->current.exclusive;
  layer->current.exclusive_zone = layer->client_pending.exclusive_zone;

  zphoc_draggable_layer_surface_v1_send_dragged (self->resource, margin);
  phoc_layer_shell_arrange (output);
  self->drag.last_x = lx;
  self->drag.last_y = ly;

  /* FIXME: way too much damage */
  phoc_output_damage_whole (output);

  self->state = PHOC_DRAGGABLE_SURFACE_STATE_DRAGGING;
  return self->state;
}


void
phoc_draggable_layer_surface_drag_end (PhocDraggableLayerSurface *self, double lx, double ly)
{
  PhocOutput *output;
  int32_t margin;

  struct wlr_layer_surface_v1 *layer = self->layer_surface->layer_surface;
  struct wlr_output *wlr_output = layer->output;

  if (!wlr_output)
    return;

  output = PHOC_OUTPUT (wlr_output->data);
  g_assert (PHOC_IS_OUTPUT (output));

  g_debug ("%s: %f,%f, margin %d,%d  dy: %f",
           __func__,
           lx, ly,
           layer->client_pending.margin.top,
           layer->client_pending.margin.bottom,
           ly - self->drag.last_y);

  switch (layer->current.anchor) {
  case PHOC_LAYER_SHELL_EFFECT_DRAG_FROM_TOP:
    margin = layer->client_pending.margin.top;
    break;
  case PHOC_LAYER_SHELL_EFFECT_DRAG_FROM_BOTTOM:
    margin = layer->client_pending.margin.bottom;
    break;
  case PHOC_LAYER_SHELL_EFFECT_DRAG_FROM_LEFT:
    margin = layer->client_pending.margin.left;
    break;
  case PHOC_LAYER_SHELL_EFFECT_DRAG_FROM_RIGHT:
    margin = layer->client_pending.margin.right;
    break;
  default:
    g_assert_not_reached ();
    break;
  }

  phoc_layer_shell_arrange (output);
  self->drag.last_x = lx;
  self->drag.last_y = ly;
  self->drag.pending_threshold = 0;

  g_debug ("%s: %d %f" , __func__, self->current.folded - margin, self->current.threshold * self->current.folded);
  if ((self->current.folded - margin) > (self->current.threshold * self->current.folded)) {
    g_debug ("Not pulled far enough, rolling back, margin: %d", margin);
    phoc_draggable_layer_surface_slide (self, ANIM_DIR_IN);
  } else {
    phoc_draggable_layer_surface_slide (self, ANIM_DIR_OUT);
  }

  self->state = PHOC_DRAGGABLE_SURFACE_STATE_ANIMATING;
}


/**
 * phoc_layer_shell_effects_get_draggable_layer_surface_from_layer_surface:
 * @self: The effects object that tracks the surfaces
 * @layer_surface: The layer-surface to look up
 *
 * Looks up the [type@PhocDraggableLayerSurface] attached to a [type@PhocLayerSurface].
 *
 * Returns:(transfer none) (nullable): The draggable layer surface.
 */
PhocDraggableLayerSurface *
phoc_layer_shell_effects_get_draggable_layer_surface_from_layer_surface (
  PhocLayerShellEffects *self,
  PhocLayerSurface *layer_surface)
{
  g_return_val_if_fail (PHOC_IS_LAYER_SHELL_EFFECTS (self), NULL);
  g_return_val_if_fail (PHOC_IS_LAYER_SURFACE (layer_surface), NULL);

  return g_hash_table_lookup (self->drag_surfaces_by_layer_sufrace, layer_surface);
}
