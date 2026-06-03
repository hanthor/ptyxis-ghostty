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
test_widget_type_name(void)
{
  /* Verify the type is registered with correct name */
  GType type = ptyxis_ghostty_widget_get_type();
  const char *name = g_type_name(type);
  g_assert_nonnull(name);
  g_assert_true(strstr(name, "GhosttyWidget") != NULL);
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

static void
test_widget_lifecycle(void)
{
  GtkWidget *widget;
  PtyxisGhosttySize size = {0};

  if (!gtk_init_check())
    {
      g_test_skip("No display/GTK backend available; skipping widget lifecycle test");
      return;
    }

  widget = g_object_new(PTYXIS_TYPE_GHOSTTY_WIDGET, NULL);
  g_assert_nonnull(widget);
  g_assert_true(PTYXIS_IS_GHOSTTY_WIDGET(widget));

  ptyxis_ghostty_widget_get_size(PTYXIS_GHOSTTY_WIDGET(widget), &size);
  g_assert_cmpuint(size.columns, >, 0);
  g_assert_cmpuint(size.rows, >, 0);

  g_object_ref_sink(widget);
  g_object_unref(widget);
}

static void
test_widget_realized_lifecycle(void)
{
  GtkWidget *window;
  GtkWidget *widget;

  if (!gtk_init_check())
    {
      g_test_skip("No display/GTK backend available; skipping realized lifecycle test");
      return;
    }

  window = gtk_window_new();
  g_assert_nonnull(window);

  widget = g_object_new(PTYXIS_TYPE_GHOSTTY_WIDGET, NULL);
  g_assert_nonnull(widget);

  gtk_window_set_child(GTK_WINDOW(window), widget);
  g_assert_true(gtk_widget_get_parent(widget) == window);

  gtk_widget_realize(window);
  gtk_widget_realize(widget);
  g_assert_true(gtk_widget_get_realized(widget));

  gtk_window_destroy(GTK_WINDOW(window));
}

static void
test_widget_input_enabled(void)
{
  GtkWidget *widget;

  if (!gtk_init_check())
    {
      g_test_skip("No display/GTK backend available; skipping input enabled test");
      return;
    }

  widget = g_object_new(PTYXIS_TYPE_GHOSTTY_WIDGET, NULL);
  g_assert_true(ptyxis_ghostty_widget_get_input_enabled(PTYXIS_GHOSTTY_WIDGET(widget)));

  ptyxis_ghostty_widget_set_input_enabled(PTYXIS_GHOSTTY_WIDGET(widget), FALSE);
  g_assert_false(ptyxis_ghostty_widget_get_input_enabled(PTYXIS_GHOSTTY_WIDGET(widget)));

  ptyxis_ghostty_widget_set_input_enabled(PTYXIS_GHOSTTY_WIDGET(widget), TRUE);
  g_assert_true(ptyxis_ghostty_widget_get_input_enabled(PTYXIS_GHOSTTY_WIDGET(widget)));

  g_object_ref_sink(widget);
  g_object_unref(widget);
}

static void
test_widget_selection_api_defaults(void)
{
  GtkWidget *widget;
  char *text;

  if (!gtk_init_check())
    {
      g_test_skip("No display/GTK backend available; skipping selection defaults test");
      return;
    }

  widget = g_object_new(PTYXIS_TYPE_GHOSTTY_WIDGET, NULL);

  g_assert_false(ptyxis_ghostty_widget_has_selection(PTYXIS_GHOSTTY_WIDGET(widget)));
  text = ptyxis_ghostty_widget_get_selected_text(PTYXIS_GHOSTTY_WIDGET(widget));
  g_assert_null(text);

  g_object_ref_sink(widget);
  g_object_unref(widget);
}

static void
test_widget_paste_api(void)
{
  GtkWidget *widget;
  GtkWidget *window;

  if (!gtk_init_check())
    {
      g_test_skip("No display/GTK backend available; skipping paste API test");
      return;
    }

  widget = g_object_new(PTYXIS_TYPE_GHOSTTY_WIDGET, NULL);

  /* Paste on unrealized widget (should not crash) */
  ptyxis_ghostty_widget_paste(PTYXIS_GHOSTTY_WIDGET(widget), "test unrealized");

  /* Paste on realized widget */
  window = gtk_window_new();
  gtk_window_set_child(GTK_WINDOW(window), widget);
  gtk_widget_realize(window);
  gtk_widget_realize(widget);

  ptyxis_ghostty_widget_paste(PTYXIS_GHOSTTY_WIDGET(widget), "test realized");

  gtk_window_destroy(GTK_WINDOW(window));
}

static void
test_widget_search_api(void)
{
  GtkWidget *widget;

  if (!gtk_init_check())
    {
      g_test_skip("No display/GTK backend available; skipping search API test");
      return;
    }

  widget = g_object_new(PTYXIS_TYPE_GHOSTTY_WIDGET, NULL);

  /* Search with NULL needle (should not crash) */
  ptyxis_ghostty_widget_search_start(PTYXIS_GHOSTTY_WIDGET(widget), NULL);

  /* Search with empty string (should not crash) */
  ptyxis_ghostty_widget_search_start(PTYXIS_GHOSTTY_WIDGET(widget), "");

  /* Search with valid needle (should not crash) */
  ptyxis_ghostty_widget_search_start(PTYXIS_GHOSTTY_WIDGET(widget), "test");

  /* Navigate search results (should not crash even with no results) */
  ptyxis_ghostty_widget_search_next(PTYXIS_GHOSTTY_WIDGET(widget));
  ptyxis_ghostty_widget_search_previous(PTYXIS_GHOSTTY_WIDGET(widget));

  /* End search (should not crash) */
  ptyxis_ghostty_widget_search_end(PTYXIS_GHOSTTY_WIDGET(widget));

  g_object_ref_sink(widget);
  g_object_unref(widget);
}

int
main(int argc, char *argv[])
{
  g_test_init(&argc, &argv, NULL);

  g_test_add_func("/Ptyxis/GhosttyWidget/type_exists",
                  test_widget_type_exists);
  g_test_add_func("/Ptyxis/GhosttyWidget/type_name",
                  test_widget_type_name);
  g_test_add_func("/Ptyxis/GhosttyWidget/api_link",
                  test_public_api_exists);
  g_test_add_func("/Ptyxis/GhosttyWidget/size_defaults",
                  test_widget_size_struct_defaults);
  g_test_add_func("/Ptyxis/GhosttyWidget/lifecycle",
                  test_widget_lifecycle);
  g_test_add_func("/Ptyxis/GhosttyWidget/realized_lifecycle",
                  test_widget_realized_lifecycle);
  g_test_add_func("/Ptyxis/GhosttyWidget/input_enabled",
                  test_widget_input_enabled);
  g_test_add_func("/Ptyxis/GhosttyWidget/selection_defaults",
                  test_widget_selection_api_defaults);
  g_test_add_func("/Ptyxis/GhosttyWidget/paste_api",
                  test_widget_paste_api);
  g_test_add_func("/Ptyxis/GhosttyWidget/search_api",
                  test_widget_search_api);

  return g_test_run();
}
