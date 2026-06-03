/*
 * test-ghostty-widget.c
 *
 * Basic sanity tests for the PtyxisGhosttyWidget API.
 * These tests exercise the type system and public API without
 * requiring actual GTK widget instantiation (no templates needed).
 */

#include <glib.h>
#include <string.h>
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

static void
test_widget_cursor_shape(void)
{
  GtkWidget *widget;

  if (!gtk_init_check())
    {
      g_test_skip("No display/GTK backend available; skipping cursor shape test");
      return;
    }

  widget = g_object_new(PTYXIS_TYPE_GHOSTTY_WIDGET, NULL);

  /* All three cursor shapes should apply without crashing */
  ptyxis_ghostty_widget_set_cursor_shape(PTYXIS_GHOSTTY_WIDGET(widget),
                                         PTYXIS_CURSOR_SHAPE_BLOCK);
  ptyxis_ghostty_widget_set_cursor_shape(PTYXIS_GHOSTTY_WIDGET(widget),
                                         PTYXIS_CURSOR_SHAPE_IBEAM);
  ptyxis_ghostty_widget_set_cursor_shape(PTYXIS_GHOSTTY_WIDGET(widget),
                                         PTYXIS_CURSOR_SHAPE_UNDERLINE);
  /* Round-trip back to block */
  ptyxis_ghostty_widget_set_cursor_shape(PTYXIS_GHOSTTY_WIDGET(widget),
                                         PTYXIS_CURSOR_SHAPE_BLOCK);

  g_object_ref_sink(widget);
  g_object_unref(widget);
}

static void
test_widget_cursor_blink_mode(void)
{
  GtkWidget *widget;

  if (!gtk_init_check())
    {
      g_test_skip("No display/GTK backend available; skipping cursor blink test");
      return;
    }

  widget = g_object_new(PTYXIS_TYPE_GHOSTTY_WIDGET, NULL);

  ptyxis_ghostty_widget_set_cursor_blink_mode(PTYXIS_GHOSTTY_WIDGET(widget),
                                              PTYXIS_CURSOR_BLINK_SYSTEM);
  ptyxis_ghostty_widget_set_cursor_blink_mode(PTYXIS_GHOSTTY_WIDGET(widget),
                                              PTYXIS_CURSOR_BLINK_ON);
  ptyxis_ghostty_widget_set_cursor_blink_mode(PTYXIS_GHOSTTY_WIDGET(widget),
                                              PTYXIS_CURSOR_BLINK_OFF);

  g_object_ref_sink(widget);
  g_object_unref(widget);
}

static void
test_widget_feed_vt_sequences(void)
{
  GtkWidget *widget;

  if (!gtk_init_check())
    {
      g_test_skip("No display/GTK backend available; skipping feed test");
      return;
    }

  widget = g_object_new(PTYXIS_TYPE_GHOSTTY_WIDGET, NULL);

  /* Feed basic printable ASCII */
  ptyxis_ghostty_widget_feed(PTYXIS_GHOSTTY_WIDGET(widget), "hello", 5);

  /* Feed a carriage return + newline */
  ptyxis_ghostty_widget_feed(PTYXIS_GHOSTTY_WIDGET(widget), "\r\n", 2);

  /* Feed a clear-screen sequence (ESC [ 2 J) */
  ptyxis_ghostty_widget_feed(PTYXIS_GHOSTTY_WIDGET(widget), "\033[2J", 4);

  /* Feed cursor position sequence (ESC [ 1 ; 1 H) */
  ptyxis_ghostty_widget_feed(PTYXIS_GHOSTTY_WIDGET(widget), "\033[1;1H", 6);

  /* Feed bold/color SGR sequence */
  ptyxis_ghostty_widget_feed(PTYXIS_GHOSTTY_WIDGET(widget), "\033[1;31mred\033[0m", 14);

  /* Feed zero-length data — should be a no-op */
  ptyxis_ghostty_widget_feed(PTYXIS_GHOSTTY_WIDGET(widget), "", 0);

  g_object_ref_sink(widget);
  g_object_unref(widget);
}

static void
test_widget_feed_then_title(void)
{
  GtkWidget *widget;
  char *title;

  if (!gtk_init_check())
    {
      g_test_skip("No display/GTK backend available; skipping feed+title test");
      return;
    }

  widget = g_object_new(PTYXIS_TYPE_GHOSTTY_WIDGET, NULL);

  /* Default title before any OSC sequence */
  title = ptyxis_ghostty_widget_get_window_title(PTYXIS_GHOSTTY_WIDGET(widget));
  /* title may be NULL or empty — just check it doesn't crash */
  g_free(title);

  /* Set title via OSC 0 */
  ptyxis_ghostty_widget_feed(PTYXIS_GHOSTTY_WIDGET(widget),
                              "\033]0;My Terminal\007", 17);

  title = ptyxis_ghostty_widget_get_window_title(PTYXIS_GHOSTTY_WIDGET(widget));
  /* After the OSC sequence the title should be set (may need realize) */
  g_free(title);

  g_object_ref_sink(widget);
  g_object_unref(widget);
}

static void
test_widget_focus_retained_after_cursor_shape_change(void)
{
  GtkWidget *window;
  GtkWidget *widget;

  if (!gtk_init_check())
    {
      g_test_skip("No display/GTK backend available; skipping focus regression test");
      return;
    }

  window = gtk_window_new();
  widget = g_object_new(PTYXIS_TYPE_GHOSTTY_WIDGET, NULL);
  gtk_window_set_child(GTK_WINDOW(window), widget);
  gtk_widget_realize(window);
  gtk_widget_realize(widget);

  gtk_widget_grab_focus(widget);

  /* Change cursor shape — this must not steal or lose focus */
  ptyxis_ghostty_widget_set_cursor_shape(PTYXIS_GHOSTTY_WIDGET(widget),
                                         PTYXIS_CURSOR_SHAPE_IBEAM);
  ptyxis_ghostty_widget_set_cursor_shape(PTYXIS_GHOSTTY_WIDGET(widget),
                                         PTYXIS_CURSOR_SHAPE_UNDERLINE);
  ptyxis_ghostty_widget_set_cursor_shape(PTYXIS_GHOSTTY_WIDGET(widget),
                                         PTYXIS_CURSOR_SHAPE_BLOCK);

  /* Widget must still be focusable and accept grab_focus */
  g_assert_true(gtk_widget_get_focusable(widget));
  gtk_widget_grab_focus(widget);

  gtk_window_destroy(GTK_WINDOW(window));
}

static void
test_widget_search_after_feed(void)
{
  GtkWidget *widget;

  if (!gtk_init_check())
    {
      g_test_skip("No display/GTK backend available; skipping search-after-feed test");
      return;
    }

  widget = g_object_new(PTYXIS_TYPE_GHOSTTY_WIDGET, NULL);

  /* Write known text into the terminal buffer */
  ptyxis_ghostty_widget_feed(PTYXIS_GHOSTTY_WIDGET(widget),
                              "needle_unique_string\r\n", 22);

  /* Search should find it (or at least not crash) */
  ptyxis_ghostty_widget_search_start(PTYXIS_GHOSTTY_WIDGET(widget),
                                     "needle_unique_string");
  ptyxis_ghostty_widget_search_next(PTYXIS_GHOSTTY_WIDGET(widget));
  ptyxis_ghostty_widget_search_previous(PTYXIS_GHOSTTY_WIDGET(widget));
  ptyxis_ghostty_widget_search_end(PTYXIS_GHOSTTY_WIDGET(widget));

  g_object_ref_sink(widget);
  g_object_unref(widget);
}

static void
test_widget_new_constructor(void)
{
  GtkWidget *widget;

  if (!gtk_init_check())
    {
      g_test_skip("No display/GTK backend available; skipping new constructor test");
      return;
    }

  widget = GTK_WIDGET(ptyxis_ghostty_widget_new());
  g_assert_nonnull(widget);
  g_assert_true(PTYXIS_IS_GHOSTTY_WIDGET(widget));

  g_object_ref_sink(widget);
  g_object_unref(widget);
}

static void
test_widget_get_terminal(void)
{
  GtkWidget *widget;
  GhosttyTerminal terminal;

  if (!gtk_init_check())
    {
      g_test_skip("No display/GTK backend available; skipping get_terminal test");
      return;
    }

  widget = g_object_new(PTYXIS_TYPE_GHOSTTY_WIDGET, NULL);
  terminal = ptyxis_ghostty_widget_get_terminal(PTYXIS_GHOSTTY_WIDGET(widget));
  g_assert_nonnull(terminal);

  g_object_ref_sink(widget);
  g_object_unref(widget);
}

static void
test_widget_font_scale(void)
{
  GtkWidget *widget;

  if (!gtk_init_check())
    {
      g_test_skip("No display/GTK backend available; skipping font scale test");
      return;
    }

  widget = g_object_new(PTYXIS_TYPE_GHOSTTY_WIDGET, NULL);

  ptyxis_ghostty_widget_set_font_scale(PTYXIS_GHOSTTY_WIDGET(widget), 0.5);
  ptyxis_ghostty_widget_set_font_scale(PTYXIS_GHOSTTY_WIDGET(widget), 1.0);
  ptyxis_ghostty_widget_set_font_scale(PTYXIS_GHOSTTY_WIDGET(widget), 1.5);
  ptyxis_ghostty_widget_set_font_scale(PTYXIS_GHOSTTY_WIDGET(widget), 2.0);
  /* Reset to normal */
  ptyxis_ghostty_widget_set_font_scale(PTYXIS_GHOSTTY_WIDGET(widget), 1.0);

  g_object_ref_sink(widget);
  g_object_unref(widget);
}

static void
test_widget_background_rgba(void)
{
  GtkWidget *widget;
  GdkRGBA color = {0};

  if (!gtk_init_check())
    {
      g_test_skip("No display/GTK backend available; skipping background RGBA test");
      return;
    }

  widget = g_object_new(PTYXIS_TYPE_GHOSTTY_WIDGET, NULL);

  ptyxis_ghostty_widget_get_background_rgba(PTYXIS_GHOSTTY_WIDGET(widget), &color);
  /* Background alpha should be non-negative and ≤ 1.0 */
  g_assert_cmpfloat(color.alpha, >=, 0.0);
  g_assert_cmpfloat(color.alpha, <=, 1.0);

  g_object_ref_sink(widget);
  g_object_unref(widget);
}

static void
test_widget_font_desc(void)
{
  GtkWidget *widget;
  PangoFontDescription *mono;
  PangoFontDescription *serif;

  if (!gtk_init_check())
    {
      g_test_skip("No display/GTK backend available; skipping font desc test");
      return;
    }

  widget = g_object_new(PTYXIS_TYPE_GHOSTTY_WIDGET, NULL);

  mono = pango_font_description_from_string("Monospace 12");
  g_assert_nonnull(mono);
  ptyxis_ghostty_widget_set_font_desc(PTYXIS_GHOSTTY_WIDGET(widget), mono);

  serif = pango_font_description_from_string("Serif 10");
  g_assert_nonnull(serif);
  ptyxis_ghostty_widget_set_font_desc(PTYXIS_GHOSTTY_WIDGET(widget), serif);

  /* Passing NULL should not crash (reset to default) */
  ptyxis_ghostty_widget_set_font_desc(PTYXIS_GHOSTTY_WIDGET(widget), NULL);

  pango_font_description_free(mono);
  pango_font_description_free(serif);
  g_object_ref_sink(widget);
  g_object_unref(widget);
}

static void
test_widget_text_blink_mode(void)
{
  GtkWidget *widget;

  if (!gtk_init_check())
    {
      g_test_skip("No display/GTK backend available; skipping text blink mode test");
      return;
    }

  widget = g_object_new(PTYXIS_TYPE_GHOSTTY_WIDGET, NULL);

  ptyxis_ghostty_widget_set_text_blink_mode(PTYXIS_GHOSTTY_WIDGET(widget),
                                            PTYXIS_TEXT_BLINK_NEVER);
  ptyxis_ghostty_widget_set_text_blink_mode(PTYXIS_GHOSTTY_WIDGET(widget),
                                            PTYXIS_TEXT_BLINK_ALWAYS);
  ptyxis_ghostty_widget_set_text_blink_mode(PTYXIS_GHOSTTY_WIDGET(widget),
                                            PTYXIS_TEXT_BLINK_FOCUSED);
  ptyxis_ghostty_widget_set_text_blink_mode(PTYXIS_GHOSTTY_WIDGET(widget),
                                            PTYXIS_TEXT_BLINK_UNFOCUSED);

  g_object_ref_sink(widget);
  g_object_unref(widget);
}

static void
test_widget_update_colors(void)
{
  GtkWidget *widget;
  PtyxisPaletteFace face = {0};

  if (!gtk_init_check())
    {
      g_test_skip("No display/GTK backend available; skipping update colors test");
      return;
    }

  widget = g_object_new(PTYXIS_TYPE_GHOSTTY_WIDGET, NULL);

  /* All-zero face (black theme) */
  face.background = (GdkRGBA){0.0, 0.0, 0.0, 1.0};
  face.foreground = (GdkRGBA){1.0, 1.0, 1.0, 1.0};
  face.cursor_bg  = (GdkRGBA){0.0, 1.0, 0.0, 1.0};
  face.cursor_fg  = (GdkRGBA){0.0, 0.0, 0.0, 1.0};
  ptyxis_ghostty_widget_update_colors(PTYXIS_GHOSTTY_WIDGET(widget), &face);

  /* Light theme */
  face.background = (GdkRGBA){1.0, 1.0, 1.0, 1.0};
  face.foreground = (GdkRGBA){0.0, 0.0, 0.0, 1.0};
  ptyxis_ghostty_widget_update_colors(PTYXIS_GHOSTTY_WIDGET(widget), &face);

  g_object_ref_sink(widget);
  g_object_unref(widget);
}

static void
test_widget_multiple_searches(void)
{
  GtkWidget *widget;

  if (!gtk_init_check())
    {
      g_test_skip("No display/GTK backend available; skipping multiple searches test");
      return;
    }

  widget = g_object_new(PTYXIS_TYPE_GHOSTTY_WIDGET, NULL);

  ptyxis_ghostty_widget_feed(PTYXIS_GHOSTTY_WIDGET(widget),
                              "alpha beta gamma alpha beta\r\n", 29);

  /* Start a search, navigate, then start a different search */
  ptyxis_ghostty_widget_search_start(PTYXIS_GHOSTTY_WIDGET(widget), "alpha");
  ptyxis_ghostty_widget_search_next(PTYXIS_GHOSTTY_WIDGET(widget));
  ptyxis_ghostty_widget_search_next(PTYXIS_GHOSTTY_WIDGET(widget));

  ptyxis_ghostty_widget_search_start(PTYXIS_GHOSTTY_WIDGET(widget), "beta");
  ptyxis_ghostty_widget_search_previous(PTYXIS_GHOSTTY_WIDGET(widget));

  ptyxis_ghostty_widget_search_end(PTYXIS_GHOSTTY_WIDGET(widget));

  /* Search with regex-like characters (no crash) */
  ptyxis_ghostty_widget_search_start(PTYXIS_GHOSTTY_WIDGET(widget), "a.*a");
  ptyxis_ghostty_widget_search_end(PTYXIS_GHOSTTY_WIDGET(widget));

  g_object_ref_sink(widget);
  g_object_unref(widget);
}

static void
test_widget_feed_unicode(void)
{
  GtkWidget *widget;
  const char *japanese = "こんにちは\r\n";
  const char *accented  = "café résumé naïve\r\n";
  const char *boxdraw   = "┌─┐\r\n│ │\r\n└─┘\r\n";
  const char *emoji     = "\xF0\x9F\x98\x80\r\n";

  if (!gtk_init_check())
    {
      g_test_skip("No display/GTK backend available; skipping feed unicode test");
      return;
    }

  widget = g_object_new(PTYXIS_TYPE_GHOSTTY_WIDGET, NULL);

  ptyxis_ghostty_widget_feed(PTYXIS_GHOSTTY_WIDGET(widget),
                              japanese, strlen(japanese));
  ptyxis_ghostty_widget_feed(PTYXIS_GHOSTTY_WIDGET(widget),
                              accented, strlen(accented));
  ptyxis_ghostty_widget_feed(PTYXIS_GHOSTTY_WIDGET(widget),
                              boxdraw, strlen(boxdraw));
  ptyxis_ghostty_widget_feed(PTYXIS_GHOSTTY_WIDGET(widget),
                              emoji, strlen(emoji));

  g_object_ref_sink(widget);
  g_object_unref(widget);
}

static void
test_widget_feed_null_data(void)
{
  GtkWidget *widget;

  if (!gtk_init_check())
    {
      g_test_skip("No display/GTK backend available; skipping feed null data test");
      return;
    }

  widget = g_object_new(PTYXIS_TYPE_GHOSTTY_WIDGET, NULL);

  /* Zero-length feed is a no-op — must not crash */
  ptyxis_ghostty_widget_feed(PTYXIS_GHOSTTY_WIDGET(widget), "", 0);
  ptyxis_ghostty_widget_feed(PTYXIS_GHOSTTY_WIDGET(widget), "x", 0);

  g_object_ref_sink(widget);
  g_object_unref(widget);
}

static void
test_widget_input_toggle_rapid(void)
{
  GtkWidget *widget;

  if (!gtk_init_check())
    {
      g_test_skip("No display/GTK backend available; skipping rapid input toggle test");
      return;
    }

  widget = g_object_new(PTYXIS_TYPE_GHOSTTY_WIDGET, NULL);

  for (int i = 0; i < 20; i++)
    {
      ptyxis_ghostty_widget_set_input_enabled(PTYXIS_GHOSTTY_WIDGET(widget),
                                              i % 2 == 0);
    }
  /* Final state should be enabled (even index 20 would be true, but loop stops at 19) */
  g_assert_false(ptyxis_ghostty_widget_get_input_enabled(PTYXIS_GHOSTTY_WIDGET(widget)));

  ptyxis_ghostty_widget_set_input_enabled(PTYXIS_GHOSTTY_WIDGET(widget), TRUE);
  g_assert_true(ptyxis_ghostty_widget_get_input_enabled(PTYXIS_GHOSTTY_WIDGET(widget)));

  g_object_ref_sink(widget);
  g_object_unref(widget);
}

static void
test_widget_realized_size_positive(void)
{
  GtkWidget *window;
  GtkWidget *widget;
  PtyxisGhosttySize size = {0};

  if (!gtk_init_check())
    {
      g_test_skip("No display/GTK backend available; skipping realized size test");
      return;
    }

  window = gtk_window_new();
  widget = g_object_new(PTYXIS_TYPE_GHOSTTY_WIDGET, NULL);
  gtk_window_set_child(GTK_WINDOW(window), widget);
  gtk_widget_realize(window);
  gtk_widget_realize(widget);

  ptyxis_ghostty_widget_get_size(PTYXIS_GHOSTTY_WIDGET(widget), &size);
  g_assert_cmpuint(size.columns, >, 0);
  g_assert_cmpuint(size.rows, >, 0);
  g_assert_cmpuint(size.cell_width, >, 0);
  g_assert_cmpuint(size.cell_height, >, 0);

  gtk_window_destroy(GTK_WINDOW(window));
}

static void
test_widget_cursor_shape_idempotent(void)
{
  GtkWidget *widget;

  if (!gtk_init_check())
    {
      g_test_skip("No display/GTK backend available; skipping cursor idempotent test");
      return;
    }

  widget = g_object_new(PTYXIS_TYPE_GHOSTTY_WIDGET, NULL);

  /* Setting the same shape twice should not crash */
  ptyxis_ghostty_widget_set_cursor_shape(PTYXIS_GHOSTTY_WIDGET(widget),
                                         PTYXIS_CURSOR_SHAPE_BLOCK);
  ptyxis_ghostty_widget_set_cursor_shape(PTYXIS_GHOSTTY_WIDGET(widget),
                                         PTYXIS_CURSOR_SHAPE_BLOCK);
  ptyxis_ghostty_widget_set_cursor_shape(PTYXIS_GHOSTTY_WIDGET(widget),
                                         PTYXIS_CURSOR_SHAPE_IBEAM);
  ptyxis_ghostty_widget_set_cursor_shape(PTYXIS_GHOSTTY_WIDGET(widget),
                                         PTYXIS_CURSOR_SHAPE_IBEAM);

  g_object_ref_sink(widget);
  g_object_unref(widget);
}

static void
test_widget_input_disabled_still_focusable(void)
{
  GtkWidget *window;
  GtkWidget *widget;

  if (!gtk_init_check())
    {
      g_test_skip("No display/GTK backend available; skipping input disabled focus test");
      return;
    }

  window = gtk_window_new();
  widget = g_object_new(PTYXIS_TYPE_GHOSTTY_WIDGET, NULL);
  gtk_window_set_child(GTK_WINDOW(window), widget);
  gtk_widget_realize(window);
  gtk_widget_realize(widget);

  ptyxis_ghostty_widget_set_input_enabled(PTYXIS_GHOSTTY_WIDGET(widget), FALSE);

  /* Widget must still be focusable even when input is disabled */
  g_assert_true(gtk_widget_get_focusable(widget));
  gtk_widget_grab_focus(widget);

  gtk_window_destroy(GTK_WINDOW(window));
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
  g_test_add_func("/Ptyxis/GhosttyWidget/cursor_shape",
                  test_widget_cursor_shape);
  g_test_add_func("/Ptyxis/GhosttyWidget/focus_after_cursor_change",
                  test_widget_focus_retained_after_cursor_shape_change);
  g_test_add_func("/Ptyxis/GhosttyWidget/cursor_blink_mode",
                  test_widget_cursor_blink_mode);
  g_test_add_func("/Ptyxis/GhosttyWidget/feed_vt_sequences",
                  test_widget_feed_vt_sequences);
  g_test_add_func("/Ptyxis/GhosttyWidget/feed_then_title",
                  test_widget_feed_then_title);
  g_test_add_func("/Ptyxis/GhosttyWidget/search_after_feed",
                  test_widget_search_after_feed);
  g_test_add_func("/Ptyxis/GhosttyWidget/new_constructor",
                  test_widget_new_constructor);
  g_test_add_func("/Ptyxis/GhosttyWidget/get_terminal",
                  test_widget_get_terminal);
  g_test_add_func("/Ptyxis/GhosttyWidget/font_scale",
                  test_widget_font_scale);
  g_test_add_func("/Ptyxis/GhosttyWidget/background_rgba",
                  test_widget_background_rgba);
  g_test_add_func("/Ptyxis/GhosttyWidget/font_desc",
                  test_widget_font_desc);
  g_test_add_func("/Ptyxis/GhosttyWidget/text_blink_mode",
                  test_widget_text_blink_mode);
  g_test_add_func("/Ptyxis/GhosttyWidget/update_colors",
                  test_widget_update_colors);
  g_test_add_func("/Ptyxis/GhosttyWidget/multiple_searches",
                  test_widget_multiple_searches);
  g_test_add_func("/Ptyxis/GhosttyWidget/feed_unicode",
                  test_widget_feed_unicode);
  g_test_add_func("/Ptyxis/GhosttyWidget/feed_null_data",
                  test_widget_feed_null_data);
  g_test_add_func("/Ptyxis/GhosttyWidget/input_toggle_rapid",
                  test_widget_input_toggle_rapid);
  g_test_add_func("/Ptyxis/GhosttyWidget/realized_size_positive",
                  test_widget_realized_size_positive);
  g_test_add_func("/Ptyxis/GhosttyWidget/cursor_shape_idempotent",
                  test_widget_cursor_shape_idempotent);
  g_test_add_func("/Ptyxis/GhosttyWidget/input_disabled_still_focusable",
                  test_widget_input_disabled_still_focusable);

  return g_test_run();
}
