/*
 * Copyright (C) 2020 Purism SPC
 * SPDX-License-Identifier: GPL-3.0+
 * Author: Guido Günther <agx@sigxcpu.org>
 */

#include "testlib.h"

static gboolean
test_client_xdg_shell_normal (PhocTestClientGlobals *globals, gpointer data)
{
  PhocTestXdgToplevelSurface *ls_green;

  ls_green = phoc_test_xdg_toplevel_new (globals, 0, 0, NULL, 0xFF00FF00);
  g_assert_nonnull (ls_green);
  phoc_assert_screenshot (globals, "test-xdg-shell-normal-1.png");

  phoc_test_xdg_toplevel_free (ls_green);
  phoc_assert_screenshot (globals, "empty.png");

  return TRUE;
}

static gboolean
test_client_xdg_shell_maximized (PhocTestClientGlobals *globals, gpointer data)
{
  PhocTestXdgToplevelSurface *ls_green;

  ls_green = phoc_test_xdg_toplevel_new (globals, 0, 0, NULL, 0xFF00FF00);
  g_assert_nonnull (ls_green);
  phoc_assert_screenshot (globals, "test-xdg-shell-maximized-1.png");

  phoc_test_xdg_toplevel_free (ls_green);
  phoc_assert_screenshot (globals, "empty.png");

  return TRUE;
}

static gboolean
test_client_xdg_shell_server_prepare (PhocServer *server, gpointer data)
{
  PhocDesktop *desktop = server->desktop;
  gboolean maximize = GPOINTER_TO_INT (data);

  g_assert_nonnull (desktop);
  phoc_desktop_set_auto_maximize (desktop, maximize);
  return TRUE;
}

static void
test_xdg_shell_normal (void)
{
  PhocTestClientIface iface = {
   .server_prepare = test_client_xdg_shell_server_prepare,
   .client_run     = test_client_xdg_shell_normal,
  };

  phoc_test_client_run (3, &iface, GINT_TO_POINTER (FALSE));
}

static void
test_xdg_shell_maximized (void)
{
  PhocTestClientIface iface = {
   .server_prepare = test_client_xdg_shell_server_prepare,
   .client_run     = test_client_xdg_shell_maximized,
  };

  phoc_test_client_run (3, &iface, GINT_TO_POINTER (TRUE));
}

gint
main (gint argc, gchar *argv[])
{
  g_test_init (&argc, &argv, NULL);

  g_test_add_func("/phoc/xdg-shell/simple", test_xdg_shell_normal);
  g_test_add_func("/phoc/xdg-shell/maximize", test_xdg_shell_maximized);
  return g_test_run();
}
