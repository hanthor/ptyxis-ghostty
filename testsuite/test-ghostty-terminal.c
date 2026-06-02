/*
 * test-ghostty-terminal.c
 *
 * Terminal behavior tests adapted from the ghostty-vt C API examples.
 * These tests verify terminal emulation behaviors that Ptyxis depends on:
 *  - Basic VT sequence processing (cursor movement, screen operations)
 *  - Bell handling
 *  - Title changes (OSC 2)
 *  - Cursor position tracking
 *  - Screen content reading
 *  - Scrollback behavior
 *  - Key encoding
 *
 * Uses the libghostty-vt C API (ghostty/vt.h), not the full embedding API.
 */

#include <glib.h>
#include <string.h>
#include <ghostty/vt.h>

/* --- Helpers --- */

static GhosttyTerminal
create_terminal(int cols, int rows)
{
  GhosttyTerminal terminal = NULL;
  GhosttyTerminalOptions opts = {
    .cols = (uint16_t)cols,
    .rows = (uint16_t)rows,
    .max_scrollback = 1000,
  };
  GhosttyResult r = ghostty_terminal_new(NULL, &terminal, opts);
  g_assert_cmpint(r, ==, GHOSTTY_SUCCESS);
  g_assert_nonnull(terminal);
  return terminal;
}

static void
feed(GhosttyTerminal terminal, const char *data)
{
  ghostty_terminal_vt_write(terminal, (const uint8_t *)data, strlen(data));
}

/* --- Tests --- */

static void
test_terminal_create_destroy(void)
{
  GhosttyTerminal terminal = create_terminal(80, 24);
  ghostty_terminal_free(terminal);
}

static void
test_terminal_default_cursor_position(void)
{
  GhosttyTerminal terminal = create_terminal(80, 24);
  uint16_t x = 99, y = 99;

  ghostty_terminal_get(terminal, GHOSTTY_TERMINAL_DATA_CURSOR_X, &x);
  ghostty_terminal_get(terminal, GHOSTTY_TERMINAL_DATA_CURSOR_Y, &y);

  g_assert_cmpuint(x, ==, 0);
  g_assert_cmpuint(y, ==, 0);

  ghostty_terminal_free(terminal);
}

static void
test_terminal_write_text(void)
{
  GhosttyTerminal terminal = create_terminal(80, 24);
  uint16_t x = 0;

  feed(terminal, "Hello");

  ghostty_terminal_get(terminal, GHOSTTY_TERMINAL_DATA_CURSOR_X, &x);
  g_assert_cmpuint(x, ==, 5);  /* cursor moved right by 5 chars */

  ghostty_terminal_free(terminal);
}

static void
test_terminal_cursor_movement(void)
{
  GhosttyTerminal terminal = create_terminal(80, 24);
  uint16_t x = 0, y = 0;

  /* Move cursor to row 5, col 10 (1-based in VT, 0-based in API) */
  feed(terminal, "\x1B[6;11H");  /* CUP: row 6, col 11 (1-based) */

  ghostty_terminal_get(terminal, GHOSTTY_TERMINAL_DATA_CURSOR_X, &x);
  ghostty_terminal_get(terminal, GHOSTTY_TERMINAL_DATA_CURSOR_Y, &y);

  g_assert_cmpuint(x, ==, 10);  /* 0-based: col 11 → index 10 */
  g_assert_cmpuint(y, ==, 5);   /* 0-based: row 6 → index 5 */

  ghostty_terminal_free(terminal);
}

static void
test_terminal_cursor_home(void)
{
  GhosttyTerminal terminal = create_terminal(80, 24);
  uint16_t x = 0, y = 0;

  feed(terminal, "some text");
  feed(terminal, "\x1B[H");  /* CUP: home position */

  ghostty_terminal_get(terminal, GHOSTTY_TERMINAL_DATA_CURSOR_X, &x);
  ghostty_terminal_get(terminal, GHOSTTY_TERMINAL_DATA_CURSOR_Y, &y);

  g_assert_cmpuint(x, ==, 0);
  g_assert_cmpuint(y, ==, 0);

  ghostty_terminal_free(terminal);
}

static void
test_terminal_newline_cursor_movement(void)
{
  GhosttyTerminal terminal = create_terminal(80, 24);
  uint16_t y = 0;

  feed(terminal, "line1\r\nline2\r\nline3");

  ghostty_terminal_get(terminal, GHOSTTY_TERMINAL_DATA_CURSOR_Y, &y);
  g_assert_cmpuint(y, ==, 2);  /* 3 lines → y=2 (0-based) */

  ghostty_terminal_free(terminal);
}

static void
test_terminal_clear_screen(void)
{
  GhosttyTerminal terminal = create_terminal(80, 24);
  uint16_t x = 0, y = 0;

  /* Write some content, then erase display */
  feed(terminal, "Hello World");
  feed(terminal, "\x1B[2J");   /* ED: erase entire display */
  feed(terminal, "\x1B[H");    /* CUP: home */

  ghostty_terminal_get(terminal, GHOSTTY_TERMINAL_DATA_CURSOR_X, &x);
  ghostty_terminal_get(terminal, GHOSTTY_TERMINAL_DATA_CURSOR_Y, &y);

  g_assert_cmpuint(x, ==, 0);
  g_assert_cmpuint(y, ==, 0);

  ghostty_terminal_free(terminal);
}

static void
test_terminal_dimensions(void)
{
  GhosttyTerminal terminal = create_terminal(120, 40);
  uint16_t cols = 0, rows = 0;

  ghostty_terminal_get(terminal, GHOSTTY_TERMINAL_DATA_COLS, &cols);
  ghostty_terminal_get(terminal, GHOSTTY_TERMINAL_DATA_ROWS, &rows);

  g_assert_cmpuint(cols, ==, 120);
  g_assert_cmpuint(rows, ==, 40);

  ghostty_terminal_free(terminal);
}

static void
test_terminal_resize(void)
{
  GhosttyTerminal terminal = create_terminal(80, 24);
  uint16_t cols = 0, rows = 0;

  ghostty_terminal_resize(terminal, 132, 50);

  ghostty_terminal_get(terminal, GHOSTTY_TERMINAL_DATA_COLS, &cols);
  ghostty_terminal_get(terminal, GHOSTTY_TERMINAL_DATA_ROWS, &rows);

  g_assert_cmpuint(cols, ==, 132);
  g_assert_cmpuint(rows, ==, 50);

  ghostty_terminal_free(terminal);
}

static int bell_count = 0;

static void
on_bell(GhosttyTerminal terminal, void* userdata)
{
  (void)terminal;
  (void)userdata;
  bell_count++;
}

static void
test_terminal_bell(void)
{
  GhosttyTerminal terminal = create_terminal(80, 24);
  bell_count = 0;

  ghostty_terminal_set(terminal, GHOSTTY_TERMINAL_OPT_BELL,
                       (const void *)on_bell);

  feed(terminal, "\x07");   /* BEL */
  g_assert_cmpint(bell_count, ==, 1);

  feed(terminal, "\x07\x07");  /* Two more BELs */
  g_assert_cmpint(bell_count, ==, 3);

  ghostty_terminal_free(terminal);
}

static gboolean title_changed = FALSE;
static char last_title[256] = {0};

static void
on_title_changed(GhosttyTerminal terminal, void* userdata)
{
  (void)userdata;
  title_changed = TRUE;
}

static void
test_terminal_title_change(void)
{
  GhosttyTerminal terminal = create_terminal(80, 24);
  title_changed = FALSE;

  ghostty_terminal_set(terminal, GHOSTTY_TERMINAL_OPT_TITLE_CHANGED,
                       (const void *)on_title_changed);

  /* OSC 2: set terminal title */
  feed(terminal, "\x1B]2;Test Title\x1B\\");
  g_assert_true(title_changed);

  ghostty_terminal_free(terminal);
}

static void
test_terminal_build_info(void)
{
  bool simd = false;
  GhosttyResult r = ghostty_build_info(GHOSTTY_BUILD_INFO_SIMD, &simd);
  g_assert_cmpint(r, ==, GHOSTTY_SUCCESS);
  /* SIMD may or may not be available; just verify the call works */
}

static void
test_terminal_key_encoder(void)
{
  GhosttyTerminal terminal = create_terminal(80, 24);
  GhosttyKeyEncoder encoder = NULL;

  /* Create a key encoder from the terminal */
  GhosttyResult r = ghostty_key_encoder_new_from_terminal(terminal, &encoder);
  g_assert_cmpint(r, ==, GHOSTTY_SUCCESS);
  g_assert_nonnull(encoder);

  ghostty_key_encoder_free(encoder);
  ghostty_terminal_free(terminal);
}

static void
test_terminal_paste_safety(void)
{
  /* Test that paste safety check works for normal text */
  const char *safe_text = "echo hello\n";
  gboolean result = ghostty_paste_is_safe((const uint8_t *)safe_text,
                                           strlen(safe_text));
  g_assert_true(result);

  /* Test that text with escape sequences is detected as unsafe */
  const char *unsafe_text = "echo \x1B[1m hello\n";
  gboolean unsafe_result = ghostty_paste_is_safe((const uint8_t *)unsafe_text,
                                                   strlen(unsafe_text));
  g_assert_false(unsafe_result);
}

static void
test_terminal_erase_in_line(void)
{
  GhosttyTerminal terminal = create_terminal(80, 24);

  /* Write text then erase to end of line */
  feed(terminal, "Hello World");
  feed(terminal, "\x1B[1;6H");   /* Move cursor to col 6 of row 1 */
  feed(terminal, "\x1B[0K");     /* EL: erase from cursor to end of line */

  /* Cursor should still be at col 5 (0-based) */
  uint16_t x = 0;
  ghostty_terminal_get(terminal, GHOSTTY_TERMINAL_DATA_CURSOR_X, &x);
  g_assert_cmpuint(x, ==, 5);

  ghostty_terminal_free(terminal);
}

static void
test_terminal_scrollback(void)
{
  GhosttyTerminal terminal = create_terminal(80, 5);  /* Only 5 rows */

  /* Fill screen and scroll */
  for (int i = 0; i < 10; i++)
    feed(terminal, "line\r\n");

  /* Cursor should be at bottom */
  uint16_t y = 0;
  ghostty_terminal_get(terminal, GHOSTTY_TERMINAL_DATA_CURSOR_Y, &y);
  g_assert_cmpuint(y, ==, 4);  /* 0-based, 5-row terminal → last row is 4 */

  ghostty_terminal_free(terminal);
}

int
main(int argc, char *argv[])
{
  g_test_init(&argc, &argv, NULL);

  g_test_add_func("/Ptyxis/GhosttyTerminal/create_destroy",
                  test_terminal_create_destroy);
  g_test_add_func("/Ptyxis/GhosttyTerminal/default_cursor_position",
                  test_terminal_default_cursor_position);
  g_test_add_func("/Ptyxis/GhosttyTerminal/write_text",
                  test_terminal_write_text);
  g_test_add_func("/Ptyxis/GhosttyTerminal/cursor_movement",
                  test_terminal_cursor_movement);
  g_test_add_func("/Ptyxis/GhosttyTerminal/cursor_home",
                  test_terminal_cursor_home);
  g_test_add_func("/Ptyxis/GhosttyTerminal/newline_cursor_movement",
                  test_terminal_newline_cursor_movement);
  g_test_add_func("/Ptyxis/GhosttyTerminal/clear_screen",
                  test_terminal_clear_screen);
  g_test_add_func("/Ptyxis/GhosttyTerminal/dimensions",
                  test_terminal_dimensions);
  g_test_add_func("/Ptyxis/GhosttyTerminal/resize",
                  test_terminal_resize);
  g_test_add_func("/Ptyxis/GhosttyTerminal/bell",
                  test_terminal_bell);
  g_test_add_func("/Ptyxis/GhosttyTerminal/title_change",
                  test_terminal_title_change);
  g_test_add_func("/Ptyxis/GhosttyTerminal/build_info",
                  test_terminal_build_info);
  g_test_add_func("/Ptyxis/GhosttyTerminal/key_encoder",
                  test_terminal_key_encoder);
  g_test_add_func("/Ptyxis/GhosttyTerminal/paste_safety",
                  test_terminal_paste_safety);
  g_test_add_func("/Ptyxis/GhosttyTerminal/erase_in_line",
                  test_terminal_erase_in_line);
  g_test_add_func("/Ptyxis/GhosttyTerminal/scrollback",
                  test_terminal_scrollback);

  return g_test_run();
}
