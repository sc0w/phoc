/*
 * Copyright (C) 2019 Purism SPC
 * SPDX-License-Identifier: GPL-3.0+
 * Author: Guido Günther <agx@sigxcpu.org>
 */

#define G_LOG_DOMAIN "phoc-server"

#include "config.h"
#include "render.h"
#include "render-private.h"
#include "utils.h"
#include "seat.h"
#include "server.h"

#include <wlr/xwayland.h>

#include <errno.h>

static void phoc_server_initable_iface_init (GInitableIface *iface);

G_DEFINE_TYPE_WITH_CODE (PhocServer, phoc_server, G_TYPE_OBJECT,
			 G_IMPLEMENT_INTERFACE (G_TYPE_INITABLE, phoc_server_initable_iface_init));

typedef struct {
  GSource source;
  struct wl_display *display;
} WaylandEventSource;


static gboolean
wayland_event_source_prepare (GSource *base,
                              int     *timeout)
{
  WaylandEventSource *source = (WaylandEventSource *)base;

  *timeout = -1;

  wl_display_flush_clients (source->display);

  return FALSE;
}

static gboolean
wayland_event_source_dispatch (GSource     *base,
                               GSourceFunc callback,
                               void        *data)
{
  WaylandEventSource *source = (WaylandEventSource *)base;
  struct wl_event_loop *loop = wl_display_get_event_loop (source->display);

  wl_event_loop_dispatch (loop, 0);

  return TRUE;
}

static GSourceFuncs wayland_event_source_funcs = {
  wayland_event_source_prepare,
  NULL,
  wayland_event_source_dispatch,
  NULL
};

static GSource *
wayland_event_source_new (struct wl_display *display)
{
  WaylandEventSource *source;
  struct wl_event_loop *loop = wl_display_get_event_loop (display);

  source = (WaylandEventSource *) g_source_new (&wayland_event_source_funcs,
                                                sizeof (WaylandEventSource));
  g_source_set_name (&source->source, "[phoc] wayland source");
  source->display = display;
  g_source_add_unix_fd (&source->source,
                        wl_event_loop_get_fd (loop),
                        G_IO_IN | G_IO_ERR);

  return &source->source;
}

static void
phoc_wayland_init (PhocServer *self)
{
  GSource *wayland_event_source;

  wayland_event_source = wayland_event_source_new (self->wl_display);
  self->wl_source = g_source_attach (wayland_event_source, NULL);
}


static void
on_session_exit (GPid pid, gint status, PhocServer *self)
{
  g_autoptr(GError) err = NULL;

  g_return_if_fail (PHOC_IS_SERVER (self));
  g_spawn_close_pid (pid);
#if GLIB_CHECK_VERSION (2, 70, 0)
  if (g_spawn_check_wait_status (status, &err)) {
#else
  if (g_spawn_check_exit_status (status, &err)) {
#endif
    self->exit_status = 0;
  } else {
    if (err->domain ==  G_SPAWN_EXIT_ERROR)
      self->exit_status = err->code;
    else
      g_warning ("Session terminated: %s (%d)", err->message, self->exit_status);
  }
  if (!(self->debug_flags & PHOC_SERVER_DEBUG_FLAG_NO_QUIT))
    g_main_loop_quit (self->mainloop);
}


static void
on_child_setup (gpointer unused)
{
  sigset_t mask;

  /* phoc wants SIGUSR1 blocked due to wlroots/xwayland but we
     don't want to inherit that to childs */
  sigemptyset(&mask);
  sigaddset(&mask, SIGUSR1);
  sigprocmask(SIG_UNBLOCK, &mask, NULL);
}


static gboolean
phoc_startup_session_in_idle(PhocServer *self)
{
  GPid pid;
  g_autoptr(GError) err = NULL;
  gchar *cmd[] = { "/bin/sh", "-c", self->session, NULL };

  if (g_spawn_async (NULL, cmd, NULL,
		      G_SPAWN_DO_NOT_REAP_CHILD,
		      on_child_setup, self, &pid, &err)) {
    g_child_watch_add (pid, (GChildWatchFunc)on_session_exit, self);
  } else {
    g_warning ("Failed to launch session: %s", err->message);
    g_main_loop_quit (self->mainloop);
  }
  return FALSE;
}

static void
phoc_startup_session (PhocServer *server)
{
  gint id;

  id = g_idle_add ((GSourceFunc) phoc_startup_session_in_idle, server);
  g_source_set_name_by_id (id, "[phoc] phoc_startup_session");
}


static void
render_shield (PhocServer *self, PhocOutput *output, PhocRenderer *renderer)
{
  struct wlr_output *wlr_output = output->wlr_output;
  struct wlr_box box = { 0, 0, wlr_output->width, wlr_output->height };
  float color[4] = {0.0f, 0.0f, 0.0f, 1.0f};

  g_assert (PHOC_IS_RENDERER (renderer));

  color[3] = 1.0 - phoc_ease_in_cubic (self->fader_t);
  wlr_render_rect (wlr_output->renderer, &box, color, wlr_output->transform_matrix);

  if (self->fader_t >= 1.0f) {
    g_debug ("Shield fade done");
    g_clear_signal_handler (&self->render_shield_id, self->renderer);
  }
}


#define TICK 50
static void
damage_shield (PhocServer *self, PhocOutput *output, PhocRenderer *renderer)
{
  g_assert (PHOC_IS_RENDERER (renderer));
  g_assert (PHOC_IS_SERVER (self));

  phoc_output_damage_whole (output);
  self->fader_t += ((float)TICK) / 1000.0;

  if (self->fader_t > 1.0)
    self->fader_t = 1.0;

  if (self->fader_t >= 1.0f)
    g_clear_signal_handler (&self->damage_shield_id, self->renderer);
}


static void
on_shell_state_changed (PhocServer *self, GParamSpec *pspec, PhocPhoshPrivate *phosh)
{
  PhocPhoshPrivateShellState state;
  PhocOutput *output;

  g_assert (PHOC_IS_SERVER (self));
  g_assert (PHOC_IS_PHOSH_PRIVATE (phosh));

  state = phoc_phosh_private_get_shell_state (phosh);
  g_debug ("Shell state changed: %d", state);

  switch (state) {
  case PHOC_PHOSH_PRIVATE_SHELL_STATE_UP:
    if (self->render_shield_id) {
      self->damage_shield_id = g_signal_connect_object (self->renderer, "render-start",
                                                        G_CALLBACK (damage_shield),
                                                        self, G_CONNECT_SWAPPED);
    }
    break;
  case PHOC_PHOSH_PRIVATE_SHELL_STATE_UNKNOWN:
  default:
    /* TODO: prevent input without a shell attached */
    self->fader_t = 0.0f;
    self->render_shield_id = g_signal_connect_object (self->renderer, "render-end",
                                                      G_CALLBACK (render_shield),
                                                      self, G_CONNECT_SWAPPED);
    wl_list_for_each (output, &self->desktop->outputs, link)
      phoc_output_damage_whole (output);
  }
}


static gboolean
phoc_server_initable_init (GInitable    *initable,
			   GCancellable *cancellable,
			   GError      **error)
{
  PhocServer *self = PHOC_SERVER (initable);
  struct wlr_renderer *wlr_renderer;

  self->wl_display = wl_display_create();
  if (self->wl_display == NULL) {
    g_set_error (error,
                 G_FILE_ERROR, G_FILE_ERROR_FAILED,
		 "Could not create wayland display");
    return FALSE;
  }

  self->backend = wlr_backend_autocreate(self->wl_display);
  if (self->backend == NULL) {
    g_set_error (error,
                 G_FILE_ERROR, G_FILE_ERROR_FAILED,
		 "Could not create backend");
    return FALSE;
  }

  self->renderer = phoc_renderer_new (self->backend);
  if (self->renderer == NULL) {
    /* TODO */
  }
  wlr_renderer = phoc_renderer_get_wlr_renderer (self->renderer);

  self->data_device_manager = wlr_data_device_manager_create(self->wl_display);
  wlr_renderer_init_wl_display(wlr_renderer, self->wl_display);

  self->compositor = wlr_compositor_create(self->wl_display,
                                           wlr_renderer);

  return TRUE;
}


static void
phoc_server_initable_iface_init (GInitableIface *iface)
{
  iface->init = phoc_server_initable_init;
}


static void
phoc_server_dispose (GObject *object)
{
  PhocServer *self = PHOC_SERVER (object);

  if (self->backend) {
    wl_display_destroy_clients (self->wl_display);
    wlr_backend_destroy(self->backend);
    self->backend = NULL;
  }

  g_clear_signal_handler (&self->render_shield_id, self->renderer);
  g_clear_signal_handler (&self->damage_shield_id, self->renderer);
  g_clear_object (&self->renderer);

  G_OBJECT_CLASS (phoc_server_parent_class)->dispose (object);
}

static void
phoc_server_finalize (GObject *object)
{
  PhocServer *self = PHOC_SERVER (object);

  if (self->wl_source) {
    g_source_remove (self->wl_source);
    self->wl_source = 0;
  }
  g_clear_object (&self->input);
  g_clear_object (&self->desktop);
  g_clear_pointer (&self->session, g_free);

  if (self->inited) {
    g_unsetenv("WAYLAND_DISPLAY");
    self->inited = FALSE;
  }

  g_clear_pointer (&self->config, phoc_config_destroy);

  wl_display_destroy (self->wl_display);
  G_OBJECT_CLASS (phoc_server_parent_class)->finalize (object);
}


static void
phoc_server_class_init (PhocServerClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->finalize = phoc_server_finalize;
  object_class->dispose = phoc_server_dispose;
}

static void
phoc_server_init (PhocServer *self)
{
}

/**
 * phoc_server_get_default:
 *
 * Get the server singleton.
 *
 * Returns: (transfer none): The server singleton
 */
PhocServer *
phoc_server_get_default (void)
{
  static PhocServer *instance;

  if (G_UNLIKELY (instance == NULL)) {
    g_autoptr (GError) err = NULL;
    g_debug("Creating server");
    instance = g_initable_new (PHOC_TYPE_SERVER, NULL, &err, NULL);
    if (instance == NULL) {
      g_critical ("Failed to create server: %s", err->message);
      return NULL;
    }

    g_object_add_weak_pointer (G_OBJECT (instance), (gpointer *)&instance);
  }

  return instance;
}

/**
 * phoc_server_setup:
 *
 * Perform wayland server intialization: parse command line and config,
 * create the wayland socket, setup env vars.
 *
 * Returns: %TRUE on success, %FALSE otherwise
 */
gboolean
phoc_server_setup (PhocServer *self, const char *config_path,
		   const char *session, GMainLoop *mainloop,
                   PhocServerFlags flags,
		   PhocServerDebugFlags debug_flags)
{
  g_assert (!self->inited);

  self->config = phoc_config_create (config_path);
  if (!self->config) {
    /* phoc_config_create printed an error */
    return FALSE;
  }

  self->debug_flags = debug_flags;
  self->mainloop = mainloop;
  self->exit_status = 1;
  self->desktop = phoc_desktop_new (self->config);
  self->input = phoc_input_new ();
  self->session = g_strdup (session);
  self->mainloop = mainloop;
  self->flags = flags;

  const char *socket = wl_display_add_socket_auto(self->wl_display);
  if (!socket) {
    g_warning("Unable to open wayland socket: %s", strerror(errno));
    wlr_backend_destroy(self->backend);
    return FALSE;
  }

  g_print ("Running compositor on wayland display '%s'\n", socket);

  if (!wlr_backend_start(self->backend)) {
    g_warning("Failed to start backend");
    wlr_backend_destroy(self->backend);
    wl_display_destroy(self->wl_display);
    return FALSE;
  }

  g_setenv("WAYLAND_DISPLAY", socket, true);
#ifdef PHOC_XWAYLAND
  if (self->desktop->xwayland != NULL) {
    PhocSeat *xwayland_seat = phoc_input_get_seat(self->input, PHOC_CONFIG_DEFAULT_SEAT_NAME);
    wlr_xwayland_set_seat(self->desktop->xwayland, xwayland_seat->seat);
  }
#endif

  if (self->flags & PHOC_SERVER_FLAG_SHELL_MODE) {
    g_message ("Enabling shell mode");
    g_signal_connect_object (self->desktop->phosh,
                             "notify::shell-state",
                             G_CALLBACK (on_shell_state_changed),
                             self, G_CONNECT_SWAPPED);
    on_shell_state_changed (self, NULL, self->desktop->phosh);
  }

  phoc_wayland_init (self);
  if (self->session)
    phoc_startup_session (self);

  self->inited = TRUE;
  return TRUE;
}

/**
 * phoc_server_get_exit_status:
 *
 * Return the session's exit status. This is only meaningful
 * if the session has ended.
 *
 * Returns: The session's exit status.
 */
gint
phoc_server_get_session_exit_status (PhocServer *self)
{
  return self->exit_status;
}

/**
 * phoc_server_get_renderer:
 *
 * Returns: (transfer none): The renderer
 */
PhocRenderer *
phoc_server_get_renderer (PhocServer *self)
{
  g_assert (PHOC_IS_SERVER (self));

  return self->renderer;
}
