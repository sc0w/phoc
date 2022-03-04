/*
 * Copyright (C) 2021 Purism SPC
 *
 * SPDX-License-Identifier: GPL-3-or-later or MIT
 */

#define G_LOG_DOMAIN "phoc-seat"

#include "config.h"

#define _POSIX_C_SOURCE 200112L
#include <assert.h>
#include <libinput.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <wayland-server-core.h>
#include <wlr/backend/libinput.h>
#include <wlr/config.h>
#include <wlr/types/wlr_data_device.h>
#include <wlr/types/wlr_idle.h>
#include <wlr/types/wlr_layer_shell_v1.h>
#include <wlr/types/wlr_primary_selection.h>
#include <wlr/types/wlr_switch.h>
#include <wlr/types/wlr_tablet_v2.h>
#include <wlr/types/wlr_xcursor_manager.h>
#include <wlr/types/wlr_tablet_pad.h>
#include <wlr/version.h>
#include "cursor.h"
#include "keyboard.h"
#include "pointer.h"
#include "seat.h"
#include "server.h"
#include "tablet.h"
#include "text_input.h"
#include "touch.h"
#include "xcursor.h"


enum {
  PROP_0,
  PROP_INPUT,
  PROP_NAME,
  PROP_LAST_PROP
};
static GParamSpec *props[PROP_LAST_PROP];

G_DEFINE_TYPE (PhocSeat, phoc_seat, G_TYPE_OBJECT)


static void
phoc_seat_set_property (GObject      *object,
                        guint         property_id,
                        const GValue *value,
                         GParamSpec   *pspec)
{
  PhocSeat *self = PHOC_SEAT (object);

  switch (property_id) {
  case PROP_INPUT:
    /* Don't hold a ref since the input object "owns" the seat */
    self->input = g_value_get_object (value);
    break;
  case PROP_NAME:
    self->name = g_value_dup_string (value);
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    break;
  }
}


static void
phoc_seat_get_property (GObject    *object,
                        guint       property_id,
                        GValue     *value,
                        GParamSpec *pspec)
{
  PhocSeat *self = PHOC_SEAT (object);

  switch (property_id) {
  case PROP_INPUT:
    g_value_set_object (value, self->input);
    break;
  case PROP_NAME:
    g_value_set_string (value, self->name);
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    break;
  }
}


static void
handle_cursor_motion (struct wl_listener *listener, void *data)
{
  PhocServer *server = phoc_server_get_default ();
  PhocCursor *cursor = wl_container_of (listener, cursor, motion);
  PhocDesktop *desktop = server->desktop;

  wlr_idle_notify_activity (desktop->idle, cursor->seat->seat);
  struct wlr_event_pointer_motion *event = data;

  phoc_cursor_handle_motion (cursor, event);
}

static void
handle_cursor_motion_absolute (struct wl_listener *listener,
                               void               *data)
{
  PhocServer *server = phoc_server_get_default ();
  PhocCursor *cursor = wl_container_of (listener, cursor, motion_absolute);
  PhocDesktop *desktop = server->desktop;

  wlr_idle_notify_activity (desktop->idle, cursor->seat->seat);
  struct wlr_event_pointer_motion_absolute *event = data;

  phoc_cursor_handle_motion_absolute (cursor, event);
}

static void
handle_cursor_button (struct wl_listener *listener, void *data)
{
  PhocServer *server = phoc_server_get_default ();
  PhocCursor *cursor = wl_container_of (listener, cursor, button);
  PhocDesktop *desktop = server->desktop;

  wlr_idle_notify_activity (desktop->idle, cursor->seat->seat);
  struct wlr_event_pointer_button *event = data;

  phoc_cursor_handle_button (cursor, event);
}

static void
handle_cursor_axis (struct wl_listener *listener, void *data)
{
  PhocServer *server = phoc_server_get_default ();
  PhocCursor *cursor = wl_container_of (listener, cursor, axis);
  PhocDesktop *desktop = server->desktop;

  wlr_idle_notify_activity (desktop->idle, cursor->seat->seat);
  struct wlr_event_pointer_axis *event = data;

  phoc_cursor_handle_axis (cursor, event);
}

static void
handle_cursor_frame (struct wl_listener *listener, void *data)
{
  PhocServer *server = phoc_server_get_default ();
  PhocCursor *cursor = wl_container_of (listener, cursor, frame);
  PhocDesktop *desktop = server->desktop;

  wlr_idle_notify_activity (desktop->idle, cursor->seat->seat);
  phoc_cursor_handle_frame (cursor);
}

static void
handle_swipe_begin (struct wl_listener *listener, void *data)
{
  PhocServer *server = phoc_server_get_default ();
  PhocCursor *cursor = wl_container_of (listener, cursor, swipe_begin);
  struct wlr_pointer_gestures_v1 *gestures = server->desktop->pointer_gestures;
  struct wlr_event_pointer_swipe_begin *event = data;

  wlr_pointer_gestures_v1_send_swipe_begin (gestures, cursor->seat->seat,
                                            event->time_msec, event->fingers);
}

static void
handle_swipe_update (struct wl_listener *listener, void *data)
{
  PhocServer *server = phoc_server_get_default ();
  PhocCursor *cursor = wl_container_of (listener, cursor, swipe_update);
  struct wlr_pointer_gestures_v1 *gestures = server->desktop->pointer_gestures;
  struct wlr_event_pointer_swipe_update *event = data;

  wlr_pointer_gestures_v1_send_swipe_update (gestures, cursor->seat->seat,
                                             event->time_msec, event->dx, event->dy);
}

static void
handle_swipe_end (struct wl_listener *listener, void *data)
{
  PhocServer *server = phoc_server_get_default ();
  PhocCursor *cursor = wl_container_of (listener, cursor, swipe_end);
  struct wlr_pointer_gestures_v1 *gestures = server->desktop->pointer_gestures;
  struct wlr_event_pointer_swipe_end *event = data;

  wlr_pointer_gestures_v1_send_swipe_end (gestures, cursor->seat->seat,
                                          event->time_msec, event->cancelled);
}

static void
handle_pinch_begin (struct wl_listener *listener, void *data)
{
  PhocServer *server = phoc_server_get_default ();
  PhocCursor *cursor = wl_container_of (listener, cursor, pinch_begin);
  struct wlr_pointer_gestures_v1 *gestures = server->desktop->pointer_gestures;
  struct wlr_event_pointer_pinch_begin *event = data;

  wlr_pointer_gestures_v1_send_pinch_begin (gestures, cursor->seat->seat,
                                            event->time_msec, event->fingers);
}

static void
handle_pinch_update (struct wl_listener *listener, void *data)
{
  PhocServer *server = phoc_server_get_default ();
  PhocCursor *cursor = wl_container_of (listener, cursor, pinch_update);
  struct wlr_pointer_gestures_v1 *gestures = server->desktop->pointer_gestures;
  struct wlr_event_pointer_pinch_update *event = data;

  wlr_pointer_gestures_v1_send_pinch_update (gestures, cursor->seat->seat,
                                             event->time_msec, event->dx, event->dy,
                                             event->scale, event->rotation);
}

static void
handle_pinch_end (struct wl_listener *listener, void *data)
{
  PhocServer *server = phoc_server_get_default ();
  PhocCursor *cursor = wl_container_of (listener, cursor, pinch_end);
  struct wlr_pointer_gestures_v1 *gestures =
    server->desktop->pointer_gestures;
  struct wlr_event_pointer_pinch_end *event = data;

  wlr_pointer_gestures_v1_send_pinch_end (gestures, cursor->seat->seat,
                                          event->time_msec, event->cancelled);
}

static void
handle_switch_toggle (struct wl_listener *listener, void *data)
{
  PhocServer *server = phoc_server_get_default ();
  struct roots_switch *switch_device =
    wl_container_of (listener, switch_device, toggle);
  PhocDesktop *desktop = server->desktop;

  wlr_idle_notify_activity (desktop->idle, switch_device->seat->seat);
  struct wlr_event_switch_toggle *event = data;

  roots_switch_handle_toggle (switch_device, event);
}

static void
handle_touch_down (struct wl_listener *listener, void *data)
{
  PhocServer *server = phoc_server_get_default ();
  PhocCursor *cursor = wl_container_of (listener, cursor, touch_down);
  struct wlr_event_touch_down *event = data;
  PhocDesktop *desktop = server->desktop;
  PhocOutput *output = g_hash_table_lookup (desktop->input_output_map,
                                            event->device->name);

  if (output && !output->wlr_output->enabled) {
    g_debug ("Touch event ignored since output '%s' is disabled.",
             output->wlr_output->name);
    return;
  }
  wlr_idle_notify_activity (desktop->idle, cursor->seat->seat);
  phoc_cursor_handle_touch_down (cursor, event);
}

static void
handle_touch_up (struct wl_listener *listener, void *data)
{
  PhocServer *server = phoc_server_get_default ();
  PhocCursor *cursor = wl_container_of (listener, cursor, touch_up);
  struct wlr_event_touch_up *event = data;
  PhocDesktop *desktop = server->desktop;
  PhocOutput *output = g_hash_table_lookup (desktop->input_output_map,
                                            event->device->name);

  /* handle touch up regardless of output status so events don't become stuck */
  phoc_cursor_handle_touch_up (cursor, event);
  if (output && !output->wlr_output->enabled) {
    g_debug ("Touch event ignored since output '%s' is disabled.",
             output->wlr_output->name);
    return;
  }
  wlr_idle_notify_activity (desktop->idle, cursor->seat->seat);
}

static void
handle_touch_motion (struct wl_listener *listener, void *data)
{
  PhocServer *server = phoc_server_get_default ();
  PhocCursor *cursor = wl_container_of (listener, cursor, touch_motion);
  struct wlr_event_touch_motion *event = data;
  PhocDesktop *desktop = server->desktop;
  PhocOutput *output = g_hash_table_lookup (desktop->input_output_map,
                                            event->device->name);

  /* handle touch motion regardless of output status so events don't become
     stuck */
  phoc_cursor_handle_touch_motion (cursor, event);
  if (output && !output->wlr_output->enabled) {
    g_debug ("Touch event ignored since output '%s' is disabled.",
             output->wlr_output->name);
    return;
  }
  wlr_idle_notify_activity (desktop->idle, cursor->seat->seat);
}

static void
handle_tablet_tool_position (PhocCursor *cursor,
                             PhocTablet *tablet,
                             struct wlr_tablet_tool *tool,
                             bool change_x, bool change_y,
                             double x, double y, double dx, double dy)
{
  PhocServer *server = phoc_server_get_default ();
  struct wlr_input_device *device = phoc_input_device_get_device (PHOC_INPUT_DEVICE (tablet));

  if (!change_x && !change_y) {
    return;
  }

  switch (tool->type) {
  case WLR_TABLET_TOOL_TYPE_MOUSE:
    // They are 0 either way when they weren't modified
    wlr_cursor_move (cursor->cursor, device, dx, dy);
    break;
  default:
    wlr_cursor_warp_absolute (cursor->cursor, device,
                              change_x ? x : NAN, change_y ? y : NAN);
  }

  double sx, sy;
  PhocDesktop *desktop = server->desktop;
  struct wlr_surface *surface = phoc_desktop_surface_at (desktop,
                                                         cursor->cursor->x, cursor->cursor->y, &sx, &sy, NULL);
  PhocTabletTool *phoc_tool = tool->data;

  if (!surface) {
    wlr_tablet_v2_tablet_tool_notify_proximity_out (phoc_tool->tablet_v2_tool);
    /* XXX: TODO: Fallback pointer semantics */
    return;
  }

  if (!wlr_surface_accepts_tablet_v2 (tablet->tablet_v2, surface)) {
    wlr_tablet_v2_tablet_tool_notify_proximity_out (phoc_tool->tablet_v2_tool);
    /* XXX: TODO: Fallback pointer semantics */
    return;
  }

  wlr_tablet_v2_tablet_tool_notify_proximity_in (phoc_tool->tablet_v2_tool,
                                                 tablet->tablet_v2, surface);

  wlr_tablet_v2_tablet_tool_notify_motion (phoc_tool->tablet_v2_tool, sx, sy);
}

static void
handle_tool_axis (struct wl_listener *listener, void *data)
{
  PhocServer *server = phoc_server_get_default ();
  PhocCursor *cursor = wl_container_of (listener, cursor, tool_axis);
  PhocDesktop *desktop = server->desktop;

  wlr_idle_notify_activity (desktop->idle, cursor->seat->seat);
  struct wlr_event_tablet_tool_axis *event = data;
  PhocTabletTool *phoc_tool = event->tool->data;

  if (!phoc_tool) {       // Should this be an assert?
    g_debug ("Tool Axis, before proximity");
    return;
  }

  /*
   * We need to handle them ourselves, not pass it into the cursor
   * without any consideration
   */
  handle_tablet_tool_position (cursor, event->device->data, event->tool,
                               event->updated_axes & WLR_TABLET_TOOL_AXIS_X,
                               event->updated_axes & WLR_TABLET_TOOL_AXIS_Y,
                               event->x, event->y, event->dx, event->dy);

  if (event->updated_axes & WLR_TABLET_TOOL_AXIS_PRESSURE) {
    wlr_tablet_v2_tablet_tool_notify_pressure (
      phoc_tool->tablet_v2_tool, event->pressure);
  }

  if (event->updated_axes & WLR_TABLET_TOOL_AXIS_DISTANCE) {
    wlr_tablet_v2_tablet_tool_notify_distance (
      phoc_tool->tablet_v2_tool, event->distance);
  }

  if (event->updated_axes & WLR_TABLET_TOOL_AXIS_TILT_X) {
    phoc_tool->tilt_x = event->tilt_x;
  }

  if (event->updated_axes & WLR_TABLET_TOOL_AXIS_TILT_Y) {
    phoc_tool->tilt_y = event->tilt_y;
  }

  if (event->updated_axes & (WLR_TABLET_TOOL_AXIS_TILT_X | WLR_TABLET_TOOL_AXIS_TILT_Y)) {
    wlr_tablet_v2_tablet_tool_notify_tilt (
      phoc_tool->tablet_v2_tool,
      phoc_tool->tilt_x, phoc_tool->tilt_y);
  }

  if (event->updated_axes & WLR_TABLET_TOOL_AXIS_ROTATION) {
    wlr_tablet_v2_tablet_tool_notify_rotation (
      phoc_tool->tablet_v2_tool, event->rotation);
  }

  if (event->updated_axes & WLR_TABLET_TOOL_AXIS_SLIDER) {
    wlr_tablet_v2_tablet_tool_notify_slider (
      phoc_tool->tablet_v2_tool, event->slider);
  }

  if (event->updated_axes & WLR_TABLET_TOOL_AXIS_WHEEL) {
    wlr_tablet_v2_tablet_tool_notify_wheel (
      phoc_tool->tablet_v2_tool, event->wheel_delta, 0);
  }
}

static void
handle_tool_tip (struct wl_listener *listener, void *data)
{
  PhocServer *server = phoc_server_get_default ();
  PhocCursor *cursor = wl_container_of (listener, cursor, tool_tip);
  PhocDesktop *desktop = server->desktop;

  wlr_idle_notify_activity (desktop->idle, cursor->seat->seat);
  struct wlr_event_tablet_tool_tip *event = data;
  PhocTabletTool *phoc_tool = event->tool->data;

  if (event->state == WLR_TABLET_TOOL_TIP_DOWN) {
    wlr_tablet_v2_tablet_tool_notify_down (phoc_tool->tablet_v2_tool);
    wlr_tablet_tool_v2_start_implicit_grab (phoc_tool->tablet_v2_tool);
  } else {
    wlr_tablet_v2_tablet_tool_notify_up (phoc_tool->tablet_v2_tool);
  }
}

static void
handle_tablet_tool_destroy (struct wl_listener *listener, void *data)
{
  PhocTabletTool *tool =
    wl_container_of (listener, tool, tool_destroy);

  wl_list_remove (&tool->link);
  wl_list_remove (&tool->tool_link);

  wl_list_remove (&tool->tool_destroy.link);
  wl_list_remove (&tool->set_cursor.link);

  free (tool);
}

static void
handle_tool_button (struct wl_listener *listener, void *data)
{
  PhocServer *server = phoc_server_get_default ();
  PhocCursor *cursor = wl_container_of (listener, cursor, tool_button);
  PhocDesktop *desktop = server->desktop;

  wlr_idle_notify_activity (desktop->idle, cursor->seat->seat);
  struct wlr_event_tablet_tool_button *event = data;
  PhocTabletTool *phoc_tool = event->tool->data;

  wlr_tablet_v2_tablet_tool_notify_button (phoc_tool->tablet_v2_tool,
                                           (enum zwp_tablet_pad_v2_button_state)event->button,
                                           (enum zwp_tablet_pad_v2_button_state)event->state);
}

static void
handle_tablet_tool_set_cursor (struct wl_listener *listener, void *data)
{
  PhocServer *server = phoc_server_get_default ();
  PhocTabletTool *tool =
    wl_container_of (listener, tool, set_cursor);
  struct wlr_tablet_v2_event_cursor *evt = data;
  PhocDesktop *desktop = server->desktop;

  struct wlr_seat_pointer_request_set_cursor_event event = {
    .surface = evt->surface,
    .hotspot_x = evt->hotspot_x,
    .hotspot_y = evt->hotspot_y,
    .serial = evt->serial,
    .seat_client = evt->seat_client,
  };

  wlr_idle_notify_activity (desktop->idle, tool->seat->seat);
  phoc_cursor_handle_request_set_cursor (tool->seat->cursor, &event);
}

static void
handle_tool_proximity (struct wl_listener *listener, void *data)
{
  PhocServer *server = phoc_server_get_default ();
  PhocCursor *cursor = wl_container_of (listener, cursor, tool_proximity);
  PhocDesktop *desktop = server->desktop;

  wlr_idle_notify_activity (desktop->idle, cursor->seat->seat);
  struct wlr_event_tablet_tool_proximity *event = data;

  struct wlr_tablet_tool *tool = event->tool;

  if (!tool->data) {
    PhocTabletTool *phoc_tool = g_new0 (PhocTabletTool, 1);

    phoc_tool->seat = cursor->seat;
    tool->data = phoc_tool;
    phoc_tool->tablet_v2_tool =
      wlr_tablet_tool_create (desktop->tablet_v2,
                              cursor->seat->seat, tool);
    phoc_tool->tool_destroy.notify = handle_tablet_tool_destroy;
    wl_signal_add (&tool->events.destroy, &phoc_tool->tool_destroy);

    phoc_tool->set_cursor.notify = handle_tablet_tool_set_cursor;
    wl_signal_add (&phoc_tool->tablet_v2_tool->events.set_cursor,
                   &phoc_tool->set_cursor);

    wl_list_init (&phoc_tool->link);
    wl_list_init (&phoc_tool->tool_link);
  }

  if (event->state == WLR_TABLET_TOOL_PROXIMITY_OUT) {
    PhocTabletTool *phoc_tool = tool->data;
    wlr_tablet_v2_tablet_tool_notify_proximity_out (phoc_tool->tablet_v2_tool);

    /* Clear cursor image if there's no pointing device. */
    if (phoc_seat_has_pointer (cursor->seat) == FALSE)
      phoc_seat_maybe_set_cursor (cursor->seat, cursor->default_xcursor);

    return;
  }

  handle_tablet_tool_position (cursor, event->device->data, event->tool,
                               true, true, event->x, event->y, 0, 0);
}

static void
handle_request_set_cursor (struct wl_listener *listener,
                           void               *data)
{
  PhocCursor *cursor = wl_container_of (listener, cursor, request_set_cursor);
  struct wlr_seat_pointer_request_set_cursor_event *event = data;

  phoc_cursor_handle_request_set_cursor (cursor, event);
}

static void
handle_pointer_focus_change (struct wl_listener *listener,
                             void               *data)
{
  PhocCursor *cursor = wl_container_of (listener, cursor, focus_change);
  struct wlr_seat_pointer_focus_change_event *event = data;

  phoc_cursor_handle_focus_change (cursor, event);
}


static PhocOutput *
get_output_from_settings (PhocSeat *self, PhocInputDevice *device)
{
  PhocServer *server = phoc_server_get_default ();
  PhocDesktop *desktop = server->desktop;
  GSettings *settings;
  g_auto (GStrv) edid = NULL;

  settings = g_hash_table_lookup (self->input_mapping_settings, device);
  g_assert (G_IS_SETTINGS (settings));

  edid = g_settings_get_strv (settings, "output");

  if (g_strv_length (edid) != 3) {
    g_warning ("EDID configuration for '%s' does not have 3 values",
               phoc_input_device_get_name (device));
    return NULL;
  }

  if (!*edid[0] && !*edid[1] && !*edid[2])
    return NULL;

  g_debug ("Looking up output %s/%s/%s", edid[0], edid[1], edid[2]);
  return phoc_desktop_find_output (desktop, edid[0], edid[1], edid[2]);
}


static void
seat_set_device_output_mappings (PhocSeat *self, PhocInputDevice *device)
{
  PhocServer *server = phoc_server_get_default ();
  PhocDesktop *desktop = server->desktop;
  struct wlr_cursor *cursor = self->cursor->cursor;
  PhocOutput *output;
  const char *type = "";

  switch (phoc_input_device_get_device_type (device)) {
  /* only map devices with absolute positions */
  case WLR_INPUT_DEVICE_TOUCH:
    type = "touch";
    break;
  case WLR_INPUT_DEVICE_TABLET_TOOL:
    type = "tablet";
    break;
  default:
    g_assert_not_reached ();
  }

  output = get_output_from_settings (self, device);

  if (!output)
    output = phoc_desktop_get_builtin_output (desktop);

  if (!output)
    return;

  g_debug ("Mapping %s device %s to %s", type, phoc_input_device_get_name (device),
           output->wlr_output->name);
  wlr_cursor_map_input_to_output (cursor,
                                  phoc_input_device_get_device (device),
                                  output->wlr_output);
  g_hash_table_insert (desktop->input_output_map,
                       g_strdup (phoc_input_device_get_name (device)),
                       output);
  return;
}


static void
reset_device_mappings (gpointer data, gpointer user_data)
{
  PhocInputDevice *device = PHOC_INPUT_DEVICE (data);
  PhocSeat *seat = PHOC_SEAT (user_data);
  struct wlr_cursor *cursor = seat->cursor->cursor;

  wlr_cursor_map_input_to_output (cursor, phoc_input_device_get_device (device), NULL);
}


void
phoc_seat_configure_cursor (PhocSeat *seat)
{
  struct wlr_cursor *cursor = seat->cursor->cursor;

  // reset mappings
  wlr_cursor_map_to_output (cursor, NULL);

  g_slist_foreach (seat->touch, reset_device_mappings, seat);
  g_slist_foreach (seat->tablets, reset_device_mappings, seat);

  // configure device to output mappings
  for (GSList *elem = seat->tablets; elem; elem = elem->next) {
    PhocInputDevice *input_device = PHOC_INPUT_DEVICE (elem->data);
    seat_set_device_output_mappings (seat, input_device);
  }
  for (GSList *elem = seat->touch; elem; elem = elem->next) {
    PhocInputDevice *input_device = PHOC_INPUT_DEVICE (elem->data);
    seat_set_device_output_mappings (seat, input_device);
  }
}

static void
phoc_seat_init_cursor (PhocSeat *seat)
{
  PhocServer *server = phoc_server_get_default ();

  seat->cursor = phoc_cursor_new (seat);

  struct wlr_cursor *wlr_cursor = seat->cursor->cursor;
  PhocDesktop *desktop = server->desktop;

  wlr_cursor_attach_output_layout (wlr_cursor, desktop->layout);

  phoc_seat_configure_cursor (seat);
  phoc_seat_configure_xcursor (seat);

  // add input signals
  wl_signal_add (&wlr_cursor->events.motion, &seat->cursor->motion);
  seat->cursor->motion.notify = handle_cursor_motion;

  wl_signal_add (&wlr_cursor->events.motion_absolute,
                 &seat->cursor->motion_absolute);
  seat->cursor->motion_absolute.notify = handle_cursor_motion_absolute;

  wl_signal_add (&wlr_cursor->events.button, &seat->cursor->button);
  seat->cursor->button.notify = handle_cursor_button;

  wl_signal_add (&wlr_cursor->events.axis, &seat->cursor->axis);
  seat->cursor->axis.notify = handle_cursor_axis;

  wl_signal_add (&wlr_cursor->events.frame, &seat->cursor->frame);
  seat->cursor->frame.notify = handle_cursor_frame;

  wl_signal_add (&wlr_cursor->events.swipe_begin, &seat->cursor->swipe_begin);
  seat->cursor->swipe_begin.notify = handle_swipe_begin;

  wl_signal_add (&wlr_cursor->events.swipe_update, &seat->cursor->swipe_update);
  seat->cursor->swipe_update.notify = handle_swipe_update;

  wl_signal_add (&wlr_cursor->events.swipe_end, &seat->cursor->swipe_end);
  seat->cursor->swipe_end.notify = handle_swipe_end;

  wl_signal_add (&wlr_cursor->events.pinch_begin, &seat->cursor->pinch_begin);
  seat->cursor->pinch_begin.notify = handle_pinch_begin;

  wl_signal_add (&wlr_cursor->events.pinch_update, &seat->cursor->pinch_update);
  seat->cursor->pinch_update.notify = handle_pinch_update;

  wl_signal_add (&wlr_cursor->events.pinch_end, &seat->cursor->pinch_end);
  seat->cursor->pinch_end.notify = handle_pinch_end;

  wl_signal_add (&wlr_cursor->events.touch_down, &seat->cursor->touch_down);
  seat->cursor->touch_down.notify = handle_touch_down;

  wl_signal_add (&wlr_cursor->events.touch_up, &seat->cursor->touch_up);
  seat->cursor->touch_up.notify = handle_touch_up;

  wl_signal_add (&wlr_cursor->events.touch_motion,
                 &seat->cursor->touch_motion);
  seat->cursor->touch_motion.notify = handle_touch_motion;

  wl_signal_add (&wlr_cursor->events.tablet_tool_axis,
                 &seat->cursor->tool_axis);
  seat->cursor->tool_axis.notify = handle_tool_axis;

  wl_signal_add (&wlr_cursor->events.tablet_tool_tip, &seat->cursor->tool_tip);
  seat->cursor->tool_tip.notify = handle_tool_tip;

  wl_signal_add (&wlr_cursor->events.tablet_tool_proximity, &seat->cursor->tool_proximity);
  seat->cursor->tool_proximity.notify = handle_tool_proximity;

  wl_signal_add (&wlr_cursor->events.tablet_tool_button, &seat->cursor->tool_button);
  seat->cursor->tool_button.notify = handle_tool_button;

  wl_signal_add (&seat->seat->events.request_set_cursor,
                 &seat->cursor->request_set_cursor);
  seat->cursor->request_set_cursor.notify = handle_request_set_cursor;

  wl_signal_add (&seat->seat->pointer_state.events.focus_change,
                 &seat->cursor->focus_change);
  seat->cursor->focus_change.notify = handle_pointer_focus_change;

  wl_list_init (&seat->cursor->constraint_commit.link);
}

static void
phoc_drag_icon_handle_surface_commit (struct wl_listener *listener,
                                      void               *data)
{
  PhocDragIcon *icon =
    wl_container_of (listener, icon, surface_commit);

  phoc_drag_icon_update_position (icon);
}

static void
phoc_drag_icon_handle_map (struct wl_listener *listener,
                           void               *data)
{
  PhocDragIcon *icon =
    wl_container_of (listener, icon, map);

  phoc_drag_icon_damage_whole (icon);
}

static void
phoc_drag_icon_handle_unmap (struct wl_listener *listener,
                             void               *data)
{
  PhocDragIcon *icon =
    wl_container_of (listener, icon, unmap);

  phoc_drag_icon_damage_whole (icon);
}

static void
phoc_drag_icon_handle_destroy (struct wl_listener *listener,
                                void               *data)
{
  PhocDragIcon *icon =
    wl_container_of (listener, icon, destroy);

  phoc_drag_icon_damage_whole (icon);

  assert (icon->seat->drag_icon == icon);
  icon->seat->drag_icon = NULL;

  wl_list_remove (&icon->surface_commit.link);
  wl_list_remove (&icon->unmap.link);
  wl_list_remove (&icon->destroy.link);
  free (icon);
}

static void
phoc_seat_handle_request_start_drag (struct wl_listener *listener,
                                     void               *data)
{
  PhocSeat *seat =
    wl_container_of (listener, seat, request_start_drag);
  struct wlr_seat_request_start_drag_event *event = data;

  if (wlr_seat_validate_pointer_grab_serial (seat->seat,
                                             event->origin, event->serial)) {
    wlr_seat_start_pointer_drag (seat->seat, event->drag, event->serial);
    return;
  }

  struct wlr_touch_point *point;

  if (wlr_seat_validate_touch_grab_serial (seat->seat,
                                           event->origin, event->serial, &point)) {
    wlr_seat_start_touch_drag (seat->seat, event->drag, event->serial, point);
    return;
  }

  g_debug ("Ignoring start_drag request: "
           "could not validate pointer or touch serial %" PRIu32, event->serial);
  wlr_data_source_destroy (event->drag->source);
}

static void
phoc_seat_handle_start_drag (struct wl_listener *listener,
                             void               *data)
{
  PhocSeat *seat = wl_container_of (listener, seat, start_drag);
  struct wlr_drag *wlr_drag = data;
  struct wlr_drag_icon *wlr_drag_icon = wlr_drag->icon;

  if (wlr_drag_icon == NULL) {
    return;
  }

  PhocDragIcon *icon = g_new0 (PhocDragIcon, 1);

  icon->seat = seat;
  icon->wlr_drag_icon = wlr_drag_icon;

  icon->surface_commit.notify = phoc_drag_icon_handle_surface_commit;
  wl_signal_add (&wlr_drag_icon->surface->events.commit, &icon->surface_commit);
  icon->unmap.notify = phoc_drag_icon_handle_unmap;
  wl_signal_add (&wlr_drag_icon->events.unmap, &icon->unmap);
  icon->map.notify = phoc_drag_icon_handle_map;
  wl_signal_add (&wlr_drag_icon->events.map, &icon->map);
  icon->destroy.notify = phoc_drag_icon_handle_destroy;
  wl_signal_add (&wlr_drag_icon->events.destroy, &icon->destroy);

  assert (seat->drag_icon == NULL);
  seat->drag_icon = icon;

  phoc_drag_icon_update_position (icon);
}

static void
phoc_seat_handle_request_set_selection (
  struct wl_listener *listener, void *data)
{
  PhocSeat *seat =
    wl_container_of (listener, seat, request_set_selection);
  struct wlr_seat_request_set_selection_event *event = data;

  wlr_seat_set_selection (seat->seat, event->source, event->serial);
}

static void
phoc_seat_handle_request_set_primary_selection (
  struct wl_listener *listener, void *data)
{
  PhocSeat *seat =
    wl_container_of (listener, seat, request_set_primary_selection);
  struct wlr_seat_request_set_primary_selection_event *event = data;

  wlr_seat_set_primary_selection (seat->seat, event->source, event->serial);
}

void
phoc_drag_icon_update_position (PhocDragIcon *icon)
{
  phoc_drag_icon_damage_whole (icon);

  PhocSeat *seat = icon->seat;
  struct wlr_drag *wlr_drag = icon->wlr_drag_icon->drag;

  assert (wlr_drag != NULL);

  switch (seat->seat->drag->grab_type) {
  case WLR_DRAG_GRAB_KEYBOARD:
    assert (false);
  case WLR_DRAG_GRAB_KEYBOARD_POINTER:;
    struct wlr_cursor *cursor = seat->cursor->cursor;
    icon->x = cursor->x;
    icon->y = cursor->y;
    break;
  case WLR_DRAG_GRAB_KEYBOARD_TOUCH:;
    struct wlr_touch_point *point =
      wlr_seat_touch_get_point (seat->seat, wlr_drag->touch_id);
    if (point == NULL) {
      return;
    }
    icon->x = seat->touch_x;
    icon->y = seat->touch_y;
    break;
  default:
    g_error ("Invalid drag grab type %d", seat->seat->drag->grab_type);
  }

  phoc_drag_icon_damage_whole (icon);
}

void
phoc_drag_icon_damage_whole (PhocDragIcon *icon)
{
  PhocServer *server = phoc_server_get_default ();
  PhocOutput *output;

  wl_list_for_each (output, &server->desktop->outputs, link) {
    phoc_output_damage_whole_drag_icon (output, icon);
  }
}

static void seat_view_destroy (PhocSeatView *seat_view);

static void
phoc_seat_handle_destroy (struct wl_listener *listener,
                          void               *data)
{
  PhocSeat *seat = wl_container_of (listener, seat, destroy);

  // TODO: probably more to be freed here
  wl_list_remove (&seat->destroy.link);

  phoc_input_method_relay_destroy (&seat->im_relay);

  PhocSeatView *view, *nview;

  wl_list_for_each_safe (view, nview, &seat->views, link)
  {
    seat_view_destroy (view);
  }
}


static void
seat_update_capabilities (PhocSeat *seat)
{
  uint32_t caps = 0;

  if (seat->keyboards != NULL) {
    caps |= WL_SEAT_CAPABILITY_KEYBOARD;
  }
  if (seat->pointers != NULL) {
    caps |= WL_SEAT_CAPABILITY_POINTER;
  }
  if (seat->touch != NULL) {
    caps |= WL_SEAT_CAPABILITY_TOUCH;
  }
  wlr_seat_set_capabilities (seat->seat, caps);

  phoc_seat_maybe_set_cursor (seat, seat->cursor->default_xcursor);
}


static void
on_settings_output_changed (PhocSeat *seat)
{
  g_assert (PHOC_IS_SEAT (seat));

  g_debug ("Input output mappings changed, reloading settings");
  phoc_seat_configure_cursor (seat);
}


static void
phoc_seat_add_input_mapping_settings (PhocSeat *self, PhocInputDevice *device)
{
  const char *schema, *group, *vendor, *product;
  g_autofree char *path = NULL;
  g_autoptr (GSettings) settings = NULL;

  switch (phoc_input_device_get_device_type (device)) {
  case WLR_INPUT_DEVICE_TOUCH:
    schema = "org.gnome.desktop.peripherals.touchscreen";
    group = "touchscreens";
    break;
  case WLR_INPUT_DEVICE_TABLET_TOOL:
    schema = "org.gnome.desktop.peripherals.tablet";
    group = "tablets";
    break;
  default:
    g_assert_not_reached ();
  }

  vendor = phoc_input_device_get_vendor_id (device);
  product = phoc_input_device_get_product_id (device);
  path = g_strdup_printf ("/org/gnome/desktop/peripherals/%s/%s:%s/",
                          group, vendor, product);

  g_debug ("Tracking config path %s for %s", path, phoc_input_device_get_name (device));
  settings = g_settings_new_with_path (schema, path);
  g_signal_connect_swapped (settings, "changed::output",
                            G_CALLBACK (on_settings_output_changed), self);
  g_hash_table_insert (self->input_mapping_settings, device, g_steal_pointer (&settings));
  on_settings_output_changed (self);
}


static void
on_keyboard_destroy (PhocSeat *self, PhocKeyboard *keyboard)
{
  g_assert (PHOC_IS_SEAT (self));
  g_assert (PHOC_IS_KEYBOARD (keyboard));

  self->keyboards = g_slist_remove (self->keyboards, keyboard);
  g_object_unref (keyboard);
  seat_update_capabilities (self);
}


static void
on_keyboard_activity (PhocSeat *self, PhocKeyboard *keyboard)
{
  PhocServer *server = phoc_server_get_default ();
  PhocDesktop *desktop = server->desktop;

  g_assert (PHOC_IS_SEAT (self));
  g_assert (PHOC_IS_KEYBOARD (keyboard));

  wlr_idle_notify_activity (desktop->idle, self->seat);
}


static void
seat_add_keyboard (PhocSeat                *seat,
                   struct wlr_input_device *device)
{
  assert (device->type == WLR_INPUT_DEVICE_KEYBOARD);
  PhocKeyboard *keyboard = phoc_keyboard_new (device, seat);

  seat->keyboards = g_slist_prepend (seat->keyboards, keyboard);

  g_signal_connect_swapped (keyboard, "device-destroy",
                            G_CALLBACK (on_keyboard_destroy),
                            seat);

  g_signal_connect_swapped (keyboard, "activity",
                            G_CALLBACK (on_keyboard_activity),
                            seat);

  wlr_seat_set_keyboard (seat->seat, device);
}

static void
on_pointer_destroy (PhocTouch *pointer)
{
  PhocSeat *seat = phoc_input_device_get_seat (PHOC_INPUT_DEVICE (pointer));
  struct wlr_input_device *device = phoc_input_device_get_device (PHOC_INPUT_DEVICE (pointer));

  g_assert (PHOC_IS_POINTER (pointer));
  g_debug ("Removing pointer device: %s", device->name);
  seat->pointers = g_slist_remove (seat->pointers, pointer);
  wlr_cursor_detach_input_device (seat->cursor->cursor, device);
  g_object_unref (pointer);

  seat_update_capabilities (seat);
}

static void
seat_add_pointer (PhocSeat                *seat,
                  struct wlr_input_device *device)
{
  PhocPointer *pointer = phoc_pointer_new (device, seat);

  seat->pointers = g_slist_prepend (seat->pointers, pointer);
  g_signal_connect (pointer, "device-destroy",
                    G_CALLBACK (on_pointer_destroy),
                    NULL);

  wlr_cursor_attach_input_device (seat->cursor->cursor, device);
}

static void
handle_switch_destroy (struct wl_listener *listener, void *data)
{
  struct roots_switch *switch_device =
    wl_container_of (listener, switch_device, device_destroy);
  PhocSeat *seat = switch_device->seat;

  wl_list_remove (&switch_device->link);
  wl_list_remove (&switch_device->device_destroy.link);
  free (switch_device);

  seat_update_capabilities (seat);
}

static void
seat_add_switch (PhocSeat                *seat,
                 struct wlr_input_device *device)
{
  assert (device->type == WLR_INPUT_DEVICE_SWITCH);
  struct roots_switch *switch_device = g_new0 (struct roots_switch, 1);

  device->data = switch_device;
  switch_device->device = device;
  switch_device->seat = seat;
  wl_list_insert (&seat->switches, &switch_device->link);
  switch_device->device_destroy.notify = handle_switch_destroy;

  switch_device->toggle.notify = handle_switch_toggle;
  wl_signal_add (&switch_device->device->switch_device->events.toggle, &switch_device->toggle);
}

static void
on_touch_destroy (PhocTouch *touch)
{
  PhocSeat *seat = phoc_input_device_get_seat (PHOC_INPUT_DEVICE (touch));
  PhocServer *server = phoc_server_get_default ();
  PhocDesktop *desktop = server->desktop;
  struct wlr_input_device *device = phoc_input_device_get_device (PHOC_INPUT_DEVICE (touch));

  g_assert (PHOC_IS_TOUCH (touch));
  g_debug ("Removing touch device: %s", device->name);
  g_hash_table_remove (desktop->input_output_map, device->name);
  g_hash_table_remove (seat->input_mapping_settings, touch);

  seat->touch = g_slist_remove (seat->touch, touch);
  wlr_cursor_detach_input_device (seat->cursor->cursor, device);
  g_object_unref (touch);

  seat_update_capabilities (seat);
}

static void
seat_add_touch (PhocSeat                *seat,
                struct wlr_input_device *device)
{
  PhocTouch *touch = phoc_touch_new (device, seat);

  seat->touch = g_slist_prepend (seat->touch, touch);
  g_signal_connect (touch, "device-destroy",
                    G_CALLBACK (on_touch_destroy),
                    NULL);

  wlr_cursor_attach_input_device (seat->cursor->cursor, device);
  phoc_seat_add_input_mapping_settings (seat, PHOC_INPUT_DEVICE (touch));
}

static void
handle_tablet_pad_destroy (struct wl_listener *listener,
                           void               *data)
{
  PhocTabletPad *tablet_pad =
    wl_container_of (listener, tablet_pad, device_destroy);
  PhocSeat *seat = tablet_pad->seat;

  wl_list_remove (&tablet_pad->device_destroy.link);
  wl_list_remove (&tablet_pad->tablet_destroy.link);
  wl_list_remove (&tablet_pad->attach.link);
  wl_list_remove (&tablet_pad->link);

  wl_list_remove (&tablet_pad->button.link);
  wl_list_remove (&tablet_pad->strip.link);
  wl_list_remove (&tablet_pad->ring.link);
  free (tablet_pad);

  seat_update_capabilities (seat);
}

static void
handle_pad_tool_destroy (struct wl_listener *listener, void *data)
{
  PhocTabletPad *pad =
    wl_container_of (listener, pad, tablet_destroy);

  pad->tablet = NULL;

  wl_list_remove (&pad->tablet_destroy.link);
  wl_list_init (&pad->tablet_destroy.link);
}

static void
attach_tablet_pad (PhocTabletPad *pad,
                   PhocTablet     *tool)
{
  struct wlr_input_device *device = phoc_input_device_get_device (PHOC_INPUT_DEVICE (tool));

  g_debug ("Attaching tablet pad \"%s\" to tablet tool \"%s\"",
           pad->device->name, device->name);

  pad->tablet = tool;

  wl_list_remove (&pad->tablet_destroy.link);
  pad->tablet_destroy.notify = handle_pad_tool_destroy;
  wl_signal_add (&device->events.destroy, &pad->tablet_destroy);
}

static void
handle_tablet_pad_attach (struct wl_listener *listener, void *data)
{
  PhocTabletPad *pad =
    wl_container_of (listener, pad, attach);
  struct wlr_tablet_tool *wlr_tool = data;
  PhocTablet *tool = wlr_tool->data;

  attach_tablet_pad (pad, tool);
}

static void
handle_tablet_pad_ring (struct wl_listener *listener, void *data)
{
  PhocTabletPad *pad =
    wl_container_of (listener, pad, ring);
  struct wlr_event_tablet_pad_ring *event = data;

  wlr_tablet_v2_tablet_pad_notify_ring (pad->tablet_v2_pad,
                                        event->ring, event->position,
                                        event->source == WLR_TABLET_PAD_RING_SOURCE_FINGER,
                                        event->time_msec);
}

static void
handle_tablet_pad_strip (struct wl_listener *listener, void *data)
{
  PhocTabletPad *pad =
    wl_container_of (listener, pad, strip);
  struct wlr_event_tablet_pad_strip *event = data;

  wlr_tablet_v2_tablet_pad_notify_strip (pad->tablet_v2_pad,
                                         event->strip, event->position,
                                         event->source == WLR_TABLET_PAD_STRIP_SOURCE_FINGER,
                                         event->time_msec);
}

static void
handle_tablet_pad_button (struct wl_listener *listener, void *data)
{
  PhocTabletPad *pad =
    wl_container_of (listener, pad, button);
  struct wlr_event_tablet_pad_button *event = data;

  wlr_tablet_v2_tablet_pad_notify_mode (pad->tablet_v2_pad,
                                        event->group, event->mode, event->time_msec);

  wlr_tablet_v2_tablet_pad_notify_button (pad->tablet_v2_pad,
                                          event->button, event->time_msec,
                                          (enum zwp_tablet_pad_v2_button_state)event->state);
}

static void
seat_add_tablet_pad (PhocSeat                *seat,
                     struct wlr_input_device *device)
{
  PhocServer *server = phoc_server_get_default ();
  PhocTabletPad *tablet_pad = g_new0 (PhocTabletPad, 1);

  device->data = tablet_pad;
  tablet_pad->device = device;
  tablet_pad->seat = seat;
  wl_list_insert (&seat->tablet_pads, &tablet_pad->link);

  tablet_pad->device_destroy.notify = handle_tablet_pad_destroy;
  wl_signal_add (&tablet_pad->device->events.destroy,
                 &tablet_pad->device_destroy);

  tablet_pad->attach.notify = handle_tablet_pad_attach;
  wl_signal_add (&tablet_pad->device->tablet_pad->events.attach_tablet,
                 &tablet_pad->attach);

  tablet_pad->button.notify = handle_tablet_pad_button;
  wl_signal_add (&tablet_pad->device->tablet_pad->events.button, &tablet_pad->button);

  tablet_pad->strip.notify = handle_tablet_pad_strip;
  wl_signal_add (&tablet_pad->device->tablet_pad->events.strip, &tablet_pad->strip);

  tablet_pad->ring.notify = handle_tablet_pad_ring;
  wl_signal_add (&tablet_pad->device->tablet_pad->events.ring, &tablet_pad->ring);

  wl_list_init (&tablet_pad->tablet_destroy.link);

  PhocDesktop *desktop = server->desktop;

  tablet_pad->tablet_v2_pad =
    wlr_tablet_pad_create (desktop->tablet_v2, seat->seat, device);

  /* Search for a sibling tablet */
  if (!wlr_input_device_is_libinput (device)) {
    /* We can only do this on libinput devices */
    return;
  }

  struct libinput_device_group *group =
    libinput_device_get_device_group (wlr_libinput_get_device_handle (device));

  for (GSList *elem = seat->tablets; elem; elem = elem->next) {
    PhocInputDevice *input_device = PHOC_INPUT_DEVICE (elem->data);
    //struct wlr_input_device *tool_device = phoc_input_device_get_device (input_deviceool);

    if (!phoc_input_device_get_is_libinput (input_device))
      continue;

    struct libinput_device *li_dev = phoc_input_device_get_libinput_device_handle (input_device);
    if (libinput_device_get_device_group (li_dev) == group) {
      attach_tablet_pad (tablet_pad, PHOC_TABLET (input_device));
      break;
    }
  }
}

static void
on_tablet_destroy (PhocSeat *seat, PhocTablet *tablet)
{
  PhocServer *server = phoc_server_get_default ();
  PhocDesktop *desktop = server->desktop;
  struct wlr_input_device *device = phoc_input_device_get_device (PHOC_INPUT_DEVICE (tablet));

  wlr_cursor_detach_input_device (seat->cursor->cursor, device);
  g_hash_table_remove (seat->input_mapping_settings, tablet);
  g_hash_table_remove (desktop->input_output_map, device->name);

  seat->tablets = g_slist_remove (seat->tablets, tablet);
  g_object_unref (tablet);

  seat_update_capabilities (seat);
}

static void
seat_add_tablet_tool (PhocSeat                *seat,
                      struct wlr_input_device *device)
{
  PhocServer *server = phoc_server_get_default ();

  if (!wlr_input_device_is_libinput (device))
    return;

  PhocTablet *tablet = phoc_tablet_new (device, seat);
  seat->tablets = g_slist_prepend (seat->tablets, tablet);
  g_signal_connect_swapped (tablet, "device-destroy",
                            G_CALLBACK (on_tablet_destroy),
                            seat);

  wlr_cursor_attach_input_device (seat->cursor->cursor, device);
  phoc_seat_add_input_mapping_settings (seat, PHOC_INPUT_DEVICE (tablet));

  PhocDesktop *desktop = server->desktop;

  tablet->tablet_v2 =
    wlr_tablet_create (desktop->tablet_v2, seat->seat, device);

  struct libinput_device_group *group =
    libinput_device_get_device_group (wlr_libinput_get_device_handle (device));
  PhocTabletPad *pad;

  wl_list_for_each (pad, &seat->tablet_pads, link) {
    if (!wlr_input_device_is_libinput (pad->device)) {
      continue;
    }

    struct libinput_device *li_dev =
      wlr_libinput_get_device_handle (pad->device);
    if (libinput_device_get_device_group (li_dev) == group) {
      attach_tablet_pad (pad, tablet);
    }
  }
}

void
phoc_seat_add_device (PhocSeat                *seat,
                      struct wlr_input_device *device)
{

  g_debug ("Adding device %s %d", device->name, device->type);
  switch (device->type) {
  case WLR_INPUT_DEVICE_KEYBOARD:
    seat_add_keyboard (seat, device);
    break;
  case WLR_INPUT_DEVICE_POINTER:
    seat_add_pointer (seat, device);
    break;
  case WLR_INPUT_DEVICE_SWITCH:
    seat_add_switch (seat, device);
    break;
  case WLR_INPUT_DEVICE_TOUCH:
    seat_add_touch (seat, device);
    break;
  case WLR_INPUT_DEVICE_TABLET_PAD:
    seat_add_tablet_pad (seat, device);
    break;
  case WLR_INPUT_DEVICE_TABLET_TOOL:
    seat_add_tablet_tool (seat, device);
    break;
  default:
    g_error ("Invalid device type %d", device->type);
  }

  seat_update_capabilities (seat);
}

void
phoc_seat_configure_xcursor (PhocSeat *seat)
{
  PhocServer *server = phoc_server_get_default ();
  PhocOutput *output;

  wl_list_for_each (output, &server->desktop->outputs, link) {
    float scale = output->wlr_output->scale;
    if (!wlr_xcursor_manager_load (seat->cursor->xcursor_manager, scale)) {
      g_critical ("Cannot load xcursor theme for output '%s' "
                  "with scale %f", output->wlr_output->name, scale);
    }
  }

  phoc_seat_maybe_set_cursor (seat, seat->cursor->default_xcursor);
  wlr_cursor_warp (seat->cursor->cursor, NULL, seat->cursor->cursor->x,
                   seat->cursor->cursor->y);
}

bool
phoc_seat_has_meta_pressed (PhocSeat *seat)
{
  for (GSList *elem = seat->keyboards; elem; elem = elem->next) {
    PhocKeyboard *keyboard = PHOC_KEYBOARD (elem->data);
    PhocInputDevice *input_device = PHOC_INPUT_DEVICE (keyboard);
    struct wlr_input_device *device = phoc_input_device_get_device (input_device);

    uint32_t modifiers =
      wlr_keyboard_get_modifiers (device->keyboard);
    if ((modifiers ^ phoc_keyboard_get_meta_key (keyboard)) == 0) {
      return true;
    }
  }

  return false;
}

PhocView *
phoc_seat_get_focus (PhocSeat *seat)
{
  if (!seat->has_focus || wl_list_empty (&seat->views)) {
    return NULL;
  }
  PhocSeatView *seat_view =
    wl_container_of (seat->views.next, seat_view, link);

  return seat_view->view;
}

static void
seat_view_destroy (PhocSeatView *seat_view)
{
  PhocSeat *seat = seat_view->seat;
  PhocView *view = seat_view->view;

  if (view == phoc_seat_get_focus (seat)) {
    seat->has_focus = false;
    seat->cursor->mode = PHOC_CURSOR_PASSTHROUGH;
  }

  if (seat_view == seat->cursor->pointer_view) {
    seat->cursor->pointer_view = NULL;
  }

  wl_list_remove (&seat_view->view_unmap.link);
  wl_list_remove (&seat_view->view_destroy.link);
  wl_list_remove (&seat_view->link);
  free (seat_view);

  if (view && view->parent) {
    phoc_seat_set_focus (seat, view->parent);
  } else if (!wl_list_empty (&seat->views)) {
    // Focus first view
    PhocSeatView *first_seat_view = wl_container_of (
      seat->views.next, first_seat_view, link);
    phoc_seat_set_focus (seat, first_seat_view->view);
  }
}

static void
seat_view_handle_unmap (struct wl_listener *listener, void *data)
{
  PhocSeatView *seat_view =
    wl_container_of (listener, seat_view, view_unmap);

  seat_view_destroy (seat_view);
}

static void
seat_view_handle_destroy (struct wl_listener *listener, void *data)
{
  PhocSeatView *seat_view =
    wl_container_of (listener, seat_view, view_destroy);

  seat_view_destroy (seat_view);
}

static PhocSeatView *
seat_add_view (PhocSeat *seat, PhocView *view)
{
  PhocSeatView *seat_view = g_new0 (PhocSeatView, 1);

  seat_view->seat = seat;
  seat_view->view = view;

  wl_list_insert (seat->views.prev, &seat_view->link);

  seat_view->view_unmap.notify = seat_view_handle_unmap;
  wl_signal_add (&view->events.unmap, &seat_view->view_unmap);
  seat_view->view_destroy.notify = seat_view_handle_destroy;
  wl_signal_add (&view->events.destroy, &seat_view->view_destroy);

  return seat_view;
}

PhocSeatView *
phoc_seat_view_from_view (PhocSeat *seat, PhocView *view)
{
  if (view == NULL) {
    return NULL;
  }

  bool found = false;
  PhocSeatView *seat_view = NULL;

  wl_list_for_each (seat_view, &seat->views, link) {
    if (seat_view->view == view) {
      found = true;
      break;
    }
  }
  if (!found)
    seat_view = seat_add_view (seat, view);

  return seat_view;
}

bool
phoc_seat_allow_input (PhocSeat           *seat,
                       struct wl_resource *resource)
{
  return !seat->exclusive_client ||
         wl_resource_get_client (resource) == seat->exclusive_client;
}

static void
seat_raise_view_stack (PhocSeat *seat, PhocView *view)
{
  PhocServer *server = phoc_server_get_default ();

  if (!view->wlr_surface) {
    return;
  }

  wl_list_remove (&view->link);
  wl_list_insert (&server->desktop->views, &view->link);
  phoc_view_damage_whole (view);

  PhocView *child;

  wl_list_for_each_reverse (child, &view->stack, parent_link)
  {
    seat_raise_view_stack (seat, child);
  }
}

void
phoc_seat_set_focus (PhocSeat *seat, PhocView *view)
{
  if (view && !phoc_seat_allow_input (seat, view->wlr_surface->resource)) {
    return;
  }

  // Make sure the view will be rendered on top of others, even if it's
  // already focused in this seat
  if (view != NULL) {
    PhocView *parent = view;
    // reorder stack
    while (parent->parent) {
      wl_list_remove (&parent->parent_link);
      wl_list_insert (&parent->parent->stack, &parent->parent_link);
      parent = parent->parent;
    }
    seat_raise_view_stack (seat, parent);
  }

  bool unfullscreen = true;

#ifdef PHOC_XWAYLAND
  if (view && view->type == ROOTS_XWAYLAND_VIEW) {
    struct roots_xwayland_surface *xwayland_surface =
      roots_xwayland_surface_from_view (view);
    if (xwayland_surface->xwayland_surface->override_redirect) {
      unfullscreen = false;
    }
  }
#endif

  if (view && unfullscreen) {
    PhocDesktop *desktop = view->desktop;
    PhocOutput *output;
    struct wlr_box box;
    view_get_box (view, &box);
    wl_list_for_each (output, &desktop->outputs, link) {
      if (output->fullscreen_view &&
          output->fullscreen_view != view &&
          wlr_output_layout_intersects (
            desktop->layout,
            output->wlr_output, &box)) {
        phoc_view_set_fullscreen (output->fullscreen_view,
                                  false, NULL);
      }
    }
  }

  PhocView *prev_focus = phoc_seat_get_focus (seat);

  if (view && view == prev_focus) {
    return;
  }

#ifdef PHOC_XWAYLAND
  if (view && view->type == ROOTS_XWAYLAND_VIEW) {
    struct roots_xwayland_surface *xwayland_surface =
      roots_xwayland_surface_from_view (view);
    if (!wlr_xwayland_or_surface_wants_focus (
          xwayland_surface->xwayland_surface)) {
      return;
    }
  }
#endif
  PhocSeatView *seat_view = NULL;
  if (view != NULL) {
    seat_view = phoc_seat_view_from_view (seat, view);
    if (seat_view == NULL) {
      return;
    }
  }

  seat->has_focus = false;

  // Deactivate the old view if it is not focused by some other seat
  if (prev_focus != NULL && !phoc_input_view_has_focus (seat->input, prev_focus)) {
    view_activate (prev_focus, false);
  }

  if (view == NULL) {
    seat->cursor->mode = PHOC_CURSOR_PASSTHROUGH;
    wlr_seat_keyboard_clear_focus (seat->seat);
    phoc_input_method_relay_set_focus (&seat->im_relay, NULL);
    return;
  }

  wl_list_remove (&seat_view->link);
  wl_list_insert (&seat->views, &seat_view->link);

  if (seat->focused_layer) {
    return;
  }

  view_activate (view, true);
  seat->has_focus = true;

  // An existing keyboard grab might try to deny setting focus, so cancel it
  wlr_seat_keyboard_end_grab (seat->seat);

  struct wlr_keyboard *keyboard = wlr_seat_get_keyboard (seat->seat);
  if (keyboard != NULL) {
    wlr_seat_keyboard_notify_enter (seat->seat, view->wlr_surface,
                                    keyboard->keycodes, keyboard->num_keycodes,
                                    &keyboard->modifiers);
    /* FIXME: Move this to a better place */
    PhocTabletPad *pad;
    wl_list_for_each (pad, &seat->tablet_pads, link) {
      if (pad->tablet) {
        wlr_tablet_v2_tablet_pad_notify_enter (pad->tablet_v2_pad, pad->tablet->tablet_v2, view->wlr_surface);
      }
    }
  } else {
    wlr_seat_keyboard_notify_enter (seat->seat, view->wlr_surface,
                                    NULL, 0, NULL);
  }

  phoc_cursor_update_focus (seat->cursor);
  phoc_input_method_relay_set_focus (&seat->im_relay, view->wlr_surface);
}

/*
 * Focus semantics of layer surfaces are somewhat detached from the normal focus
 * flow. For layers above the shell layer, for example, you cannot unfocus them.
 * You also cannot alt-tab between layer surfaces and shell surfaces.
 */
void
phoc_seat_set_focus_layer (PhocSeat                    *seat,
                           struct wlr_layer_surface_v1 *layer)
{
  PhocServer *server = phoc_server_get_default ();
  if (!layer) {
    if (seat->focused_layer) {
      seat->focused_layer = NULL;
      if (!wl_list_empty (&seat->views)) {
        // Focus first view
        PhocSeatView *first_seat_view = wl_container_of (
          seat->views.next, first_seat_view, link);
        phoc_seat_set_focus (seat, first_seat_view->view);
      } else {
        phoc_seat_set_focus (seat, NULL);
      }
      PhocOutput *output;
      wl_list_for_each (output, &server->desktop->outputs, link) {
        phoc_layer_shell_arrange (output);
      }
    }
    return;
  }
  struct wlr_keyboard *keyboard = wlr_seat_get_keyboard (seat->seat);

  if (!phoc_seat_allow_input (seat, layer->resource)) {
    return;
  }
  if (seat->has_focus) {
    PhocView *prev_focus = phoc_seat_get_focus (seat);
    wlr_seat_keyboard_clear_focus (seat->seat);
    view_activate (prev_focus, false);
  }
  seat->has_focus = false;
  if (layer->current.layer >= ZWLR_LAYER_SHELL_V1_LAYER_TOP) {
    seat->focused_layer = layer;
  }
  if (keyboard != NULL) {
    wlr_seat_keyboard_notify_enter (seat->seat, layer->surface,
                                    keyboard->keycodes, keyboard->num_keycodes,
                                    &keyboard->modifiers);
  } else {
    wlr_seat_keyboard_notify_enter (seat->seat, layer->surface,
                                    NULL, 0, NULL);
  }

  phoc_cursor_update_focus (seat->cursor);
  phoc_input_method_relay_set_focus (&seat->im_relay, layer->surface);
}

void
phoc_seat_set_exclusive_client (PhocSeat         *seat,
                                struct wl_client *client)
{
  if (!client) {
    seat->exclusive_client = client;
    // Triggers a refocus of the topmost surface layer if necessary
    phoc_layer_shell_update_focus ();
    return;
  }
  if (seat->focused_layer) {
    if (wl_resource_get_client (seat->focused_layer->resource) != client) {
      phoc_seat_set_focus_layer (seat, NULL);
    }
  }
  if (seat->has_focus) {
    PhocView *focus = phoc_seat_get_focus (seat);
    if (wl_resource_get_client (focus->wlr_surface->resource) != client) {
      phoc_seat_set_focus (seat, NULL);
    }
  }
  if (seat->seat->pointer_state.focused_client) {
    if (seat->seat->pointer_state.focused_client->client != client) {
      wlr_seat_pointer_clear_focus (seat->seat);
    }
  }
  struct timespec now;

  clock_gettime (CLOCK_MONOTONIC, &now);
  struct wlr_touch_point *point;

  wl_list_for_each (point, &seat->seat->touch_state.touch_points, link) {
    if (point->client->client != client) {
      wlr_seat_touch_point_clear_focus (seat->seat,
                                        now.tv_nsec / 1000, point->touch_id);
    }
  }
  seat->exclusive_client = client;
}

void
phoc_seat_cycle_focus (PhocSeat *seat)
{
  if (wl_list_empty (&seat->views)) {
    return;
  }

  PhocSeatView *first_seat_view = wl_container_of (
    seat->views.next, first_seat_view, link);

  if (!seat->has_focus) {
    phoc_seat_set_focus (seat, first_seat_view->view);
    return;
  }
  if (wl_list_length (&seat->views) < 2) {
    return;
  }

  // Focus the next view
  PhocSeatView *next_seat_view = wl_container_of (
    first_seat_view->link.next, next_seat_view, link);

  phoc_seat_set_focus (seat, next_seat_view->view);

  // Move the first view to the end of the list
  wl_list_remove (&first_seat_view->link);
  wl_list_insert (seat->views.prev, &first_seat_view->link);
}

void
phoc_seat_begin_move (PhocSeat *seat, PhocView *view)
{
  if (view->desktop->maximize)
    return;

  PhocCursor *cursor = seat->cursor;

  cursor->mode = PHOC_CURSOR_MOVE;
  if (seat->touch_id != -1) {
    wlr_cursor_warp (cursor->cursor, NULL, seat->touch_x, seat->touch_y);
  }
  cursor->offs_x = cursor->cursor->x;
  cursor->offs_y = cursor->cursor->y;
  struct wlr_box geom;

  view_get_geometry (view, &geom);
  if (view_is_maximized (view) || view_is_tiled (view)) {
    // calculate normalized (0..1) position of cursor in maximized window
    // and make it stay the same after restoring saved size
    double x = (cursor->cursor->x - view->box.x) / view->box.width;
    double y = (cursor->cursor->y - view->box.y) / view->box.height;
    cursor->view_x = cursor->cursor->x - x * view->saved.width;
    cursor->view_y = cursor->cursor->y - y * view->saved.height;
    view->saved.x = cursor->view_x;
    view->saved.y = cursor->view_y;
    view_restore (view);
  } else {
    cursor->view_x = view->box.x + geom.x * view->scale;
    cursor->view_y = view->box.y + geom.y * view->scale;
  }
  wlr_seat_pointer_clear_focus (seat->seat);

  phoc_seat_maybe_set_cursor (seat, PHOC_XCURSOR_MOVE);
}

void
phoc_seat_begin_resize (PhocSeat *seat, PhocView *view,
                        uint32_t edges)
{
  if (view->desktop->maximize || view_is_fullscreen (view))
    return;

  PhocCursor *cursor = seat->cursor;

  cursor->mode = PHOC_CURSOR_RESIZE;
  if (seat->touch_id != -1) {
    wlr_cursor_warp (cursor->cursor, NULL, seat->touch_x, seat->touch_y);
  }
  cursor->offs_x = cursor->cursor->x;
  cursor->offs_y = cursor->cursor->y;
  struct wlr_box geom;

  view_get_geometry (view, &geom);
  if (view_is_maximized (view) || view_is_tiled (view)) {
    view->saved.x = view->box.x + geom.x * view->scale;
    view->saved.y = view->box.y + geom.y * view->scale;
    view->saved.width = view->box.width;
    view->saved.height = view->box.height;
    view_restore (view);
  }

  cursor->view_x = view->box.x + geom.x * view->scale;
  cursor->view_y = view->box.y + geom.y * view->scale;
  struct wlr_box box;

  view_get_box (view, &box);
  cursor->view_width = box.width;
  cursor->view_height = box.height;
  cursor->resize_edges = edges;
  wlr_seat_pointer_clear_focus (seat->seat);

  const char *resize_name = wlr_xcursor_get_resize_name (edges);

  phoc_seat_maybe_set_cursor (seat, resize_name);
}

void
phoc_seat_end_compositor_grab (PhocSeat *seat)
{
  PhocCursor *cursor = seat->cursor;
  PhocView *view = phoc_seat_get_focus (seat);

  if (view == NULL) {
    return;
  }

  switch (cursor->mode) {
  case PHOC_CURSOR_MOVE:
    if (!view_is_fullscreen (view))
      view_move (view, cursor->view_x, cursor->view_y);
    break;
  case PHOC_CURSOR_RESIZE:
    view_move_resize (view, cursor->view_x, cursor->view_y, cursor->view_width, cursor->view_height);
    break;
  case PHOC_CURSOR_PASSTHROUGH:
    break;
  default:
    g_error ("Invalid cursor mode %d", cursor->mode);
  }

  cursor->mode = PHOC_CURSOR_PASSTHROUGH;
  phoc_cursor_update_focus (seat->cursor);
}

/**
 * phoc_seat_maybe_set_cursor:
 * @self: a PhocSeat
 * @name: (nullable): a cursor name or %NULL for the themes default cursor
 *
 * Show a cursor if the seat has pointer capabilities
 */
void
phoc_seat_maybe_set_cursor (PhocSeat *self, const char *name)
{
  if (phoc_seat_has_pointer (self) == FALSE) {
    wlr_cursor_set_image (self->cursor->cursor, NULL, 0, 0, 0, 0, 0, 0);
  } else {
    if (!name)
      name = self->cursor->default_xcursor;
    wlr_xcursor_manager_set_cursor_image (self->cursor->xcursor_manager,
                                          name, self->cursor->cursor);
  }
}

/**
 * phoc_seat_get_cursor:
 * @self: a PhocSeat
 *
 * Get the curent cursor
 *
 * Returns: (transfer none): The current cursor
 */
PhocCursor *
phoc_seat_get_cursor (PhocSeat *self)
{
  g_return_val_if_fail (self, NULL);

  return self->cursor;
}


static void
phoc_seat_constructed (GObject *object)
{
  PhocSeat *self = PHOC_SEAT(object);
  PhocServer *server = phoc_server_get_default ();

  G_OBJECT_CLASS (phoc_seat_parent_class)->constructed (object);

  self->seat = wlr_seat_create (server->wl_display, self->name);
  g_assert (self->seat);
  self->seat->data = self;

  phoc_seat_init_cursor (self);
  g_assert (self->cursor);

  phoc_input_method_relay_init (self, &self->im_relay);

  self->request_set_selection.notify =
    phoc_seat_handle_request_set_selection;
  wl_signal_add (&self->seat->events.request_set_selection,
                 &self->request_set_selection);
  self->request_set_primary_selection.notify =
    phoc_seat_handle_request_set_primary_selection;
  wl_signal_add (&self->seat->events.request_set_primary_selection,
                 &self->request_set_primary_selection);
  self->request_start_drag.notify = phoc_seat_handle_request_start_drag;
  wl_signal_add (&self->seat->events.request_start_drag,
                 &self->request_start_drag);
  self->start_drag.notify = phoc_seat_handle_start_drag;
  wl_signal_add (&self->seat->events.start_drag, &self->start_drag);
  self->destroy.notify = phoc_seat_handle_destroy;
  wl_signal_add (&self->seat->events.destroy, &self->destroy);
}


static void
phoc_seat_dispose (GObject *object)
{
  PhocSeat *self = PHOC_SEAT (object);

  g_clear_object (&self->cursor);

  G_OBJECT_CLASS (phoc_seat_parent_class)->dispose (object);
}


static void
phoc_seat_finalize (GObject *object)
{
  PhocSeat *self = PHOC_SEAT (object);

  g_clear_pointer (&self->input_mapping_settings, g_hash_table_destroy);
  phoc_seat_handle_destroy (&self->destroy, self->seat);
  wlr_seat_destroy (self->seat);
  g_clear_pointer (&self->name, g_free);

  G_OBJECT_CLASS (phoc_seat_parent_class)->finalize (object);
}


static void
phoc_seat_class_init (PhocSeatClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->get_property = phoc_seat_get_property;
  object_class->set_property = phoc_seat_set_property;
  object_class->constructed = phoc_seat_constructed;
  object_class->dispose = phoc_seat_dispose;
  object_class->finalize = phoc_seat_finalize;

  /**
   * PhocSeat:input:
   *
   * The %PhocInput that keeps track of all seats
   */
  props[PROP_INPUT] = g_param_spec_object ("input", "", "",
                                           PHOC_TYPE_INPUT,
                                           G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY);
  /**
   * PhocSeat:name:
   *
   * The name of this seat.
   */
  props[PROP_NAME] = g_param_spec_string ("name", "", "",
                                          NULL,
                                          G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY);

  g_object_class_install_properties (object_class, PROP_LAST_PROP, props);
}


static void
phoc_seat_init (PhocSeat *self)
{
  wl_list_init (&self->tablet_pads);
  wl_list_init (&self->switches);
  wl_list_init (&self->views);

  self->touch_id = -1;

  self->input_mapping_settings = g_hash_table_new_full (g_direct_hash,
                                                        g_direct_equal,
                                                        NULL,
                                                        g_object_unref);
}


PhocSeat *
phoc_seat_new (PhocInput *input, const char *name)
{
  return PHOC_SEAT (g_object_new (PHOC_TYPE_SEAT,
                                  "input", input,
                                  "name", name,
                                  NULL));
}


gboolean
phoc_seat_has_touch (PhocSeat *self)
{
  g_return_val_if_fail (PHOC_IS_SEAT (self), FALSE);

  g_assert (self->seat);
  return (self->seat->capabilities & WL_SEAT_CAPABILITY_TOUCH);
}


gboolean
phoc_seat_has_pointer (PhocSeat *self)
{
  g_return_val_if_fail (PHOC_IS_SEAT (self), FALSE);

  g_assert (self->seat);
  return (self->seat->capabilities & WL_SEAT_CAPABILITY_POINTER);
}


gboolean
phoc_seat_has_keyboard (PhocSeat *self)
{
  g_return_val_if_fail (PHOC_IS_SEAT (self), FALSE);

  g_assert (self->seat);
  return (self->seat->capabilities & WL_SEAT_CAPABILITY_KEYBOARD);
}
