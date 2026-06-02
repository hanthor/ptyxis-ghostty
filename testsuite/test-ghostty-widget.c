/*
 * test-ghostty-widget.c
 *
 * Basic sanity tests for the PtyxisGhosttyWidget.
 * These are unit tests that don't require a running display server.
 */

#include <glib.h>
#include "ptyxis-ghostty-widget.h"

static void
test_widget_new(void)
{
  PtyxisGhosttyWidget *widget;

  widget = ptyxis_ghostty_widget_new(NULL, NULL, NULL);
  g_assert_nonnull(widget);
  g_assert_true(PTYXIS_IS_GHOSTTY_WIDGET(widget));

  g_object_unref(widget);
}

static void
test_widget_has_selection_false(void)
{
  PtyxisGhosttyWidget *widget;

  widget = ptyxis_ghostty_widget_new(NULL, NULL, NULL);
  g_assert_false(ptyxis_ghostty_widget_has_selection(widget));

  g_object_unref(widget);
}

static void
test_widget_get_surface_null(void)
{
  PtyxisGhosttyWidget *widget;

  widget = ptyxis_ghostty_widget_new(NULL, NULL, NULL);
  /* Surface is created on realize, so should be NULL in unit test */
  g_assert_null(ptyxis_ghostty_widget_get_surface(widget));

  g_object_unref(widget);
}

static void
test_widget_selected_text_null(void)
{
  PtyxisGhosttyWidget *widget;
  char *text;

  widget = ptyxis_ghostty_widget_new(NULL, NULL, NULL);
  text = ptyxis_ghostty_widget_get_selected_text(widget);
  g_assert_null(text);

  g_object_unref(widget);
}

static void
test_widget_get_size(void)
{
  PtyxisGhosttyWidget *widget;
  PtyxisGhosttySize size = {0};

  widget = ptyxis_ghostty_widget_new(NULL, NULL, NULL);
  ptyxis_ghostty_widget_get_size(widget, &size);
  /* Default size should be zero before any surface rendering */
  g_assert_cmpuint(size.columns, ==, 0);
  g_assert_cmpuint(size.rows, ==, 0);

  g_object_unref(widget);
}

static void
test_widget_action_callback(void)
{
  PtyxisGhosttyWidget *widget;
  static gboolean called = FALSE;

  called = FALSE;

  widget = ptyxis_ghostty_widget_new(NULL, NULL, NULL);

  /* Setting callback should not crash */
  ptyxis_ghostty_widget_set_action_callback(widget, NULL, NULL);
  ptyxis_ghostty_widget_set_action_callback(widget, NULL, &called);

  g_object_unref(widget);
}

int
main(int argc, char *argv[])
{
  g_test_init(&argc, &argv, NULL);

  /* GTK must be initialized before creating any GTK widgets */
  gtk_init();

  g_test_add_func("/Ptyxis/GhosttyWidget/new",
                  test_widget_new);
  g_test_add_func("/Ptyxis/GhosttyWidget/has_selection_false",
                  test_widget_has_selection_false);
  g_test_add_func("/Ptyxis/GhosttyWidget/get_surface_null",
                  test_widget_get_surface_null);
  g_test_add_func("/Ptyxis/GhosttyWidget/selected_text_null",
                  test_widget_selected_text_null);
  g_test_add_func("/Ptyxis/GhosttyWidget/get_size",
                  test_widget_get_size);
  g_test_add_func("/Ptyxis/GhosttyWidget/action_callback",
                  test_widget_action_callback);

  return g_test_run();
}
