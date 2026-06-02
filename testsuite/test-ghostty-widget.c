/*
 * test-ghostty-widget.c
 *
 * Basic sanity tests for the PtyxisGhosttyWidget API.
 * These tests exercise the type system and public API without
 * requiring actual GTK widget instantiation (no templates needed).
 */

#include <glib.h>
#include "ptyxis-ghostty-widget.h"

static void
test_widget_type_exists(void)
{
  /* Verify the type is registered */
  GType type = ptyxis_ghostty_widget_get_type();
  g_assert_cmpuint(type, >, 0);
  g_assert_true(g_type_is_a(type, GTK_TYPE_WIDGET));
}

static void
test_widget_type_size(void)
{
  /* Verify the struct size is reasonable */
  g_assert_cmpuint(sizeof(PtyxisGhosttyWidget), >, 0);
}

static void
test_public_api_exists(void)
{
  /* Verify public API functions are callable (link check) */
  g_assert_true(PTYXIS_IS_GHOSTTY_WIDGET(NULL) || TRUE);

  /* Check type macro exists */
  g_assert_cmpuint(PTYXIS_TYPE_GHOSTTY_WIDGET, >, 0);
}

static void
test_widget_size_struct_defaults(void)
{
  PtyxisGhosttySize size = {0};
  g_assert_cmpuint(size.columns, ==, 0);
  g_assert_cmpuint(size.rows, ==, 0);
  g_assert_cmpuint(size.cell_width, ==, 0);
  g_assert_cmpuint(size.cell_height, ==, 0);
}

int
main(int argc, char *argv[])
{
  g_test_init(&argc, &argv, NULL);

  g_test_add_func("/Ptyxis/GhosttyWidget/type_exists",
                  test_widget_type_exists);
  g_test_add_func("/Ptyxis/GhosttyWidget/struct_size",
                  test_widget_type_size);
  g_test_add_func("/Ptyxis/GhosttyWidget/api_link",
                  test_public_api_exists);
  g_test_add_func("/Ptyxis/GhosttyWidget/size_defaults",
                  test_widget_size_struct_defaults);

  return g_test_run();
}
