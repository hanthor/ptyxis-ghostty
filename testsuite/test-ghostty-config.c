/*
 * test-ghostty-config.c
 *
 * Tests for the libghostty-vt terminal API.
 * Uses only symbols exported from libghostty-vt (no ghostty_config_* API).
 */

#include <glib.h>
#include <ghostty/vt.h>

static void
test_terminal_create_free(void)
{
  GhosttyTerminal terminal = NULL;
  GhosttyTerminalOptions opts = {.cols = 80, .rows = 24, .max_scrollback = 1000};
  GhosttyResult result;

  result = ghostty_terminal_new(NULL, &terminal, opts);
  g_assert_cmpint(result, ==, GHOSTTY_SUCCESS);
  g_assert_nonnull(terminal);

  ghostty_terminal_free(terminal);
}

static void
test_terminal_null_free(void)
{
  /* Freeing NULL must not crash */
  ghostty_terminal_free(NULL);
}

static void
test_terminal_reset(void)
{
  GhosttyTerminal terminal = NULL;
  GhosttyTerminalOptions opts = {.cols = 80, .rows = 24, .max_scrollback = 100};

  g_assert_cmpint(ghostty_terminal_new(NULL, &terminal, opts), ==, GHOSTTY_SUCCESS);
  g_assert_nonnull(terminal);

  /* Reset on live terminal should not crash */
  ghostty_terminal_reset(terminal);
  ghostty_terminal_reset(terminal);

  ghostty_terminal_free(terminal);
}

static void
test_terminal_reset_null(void)
{
  /* Reset on NULL is a no-op */
  ghostty_terminal_reset(NULL);
}

static void
test_terminal_resize(void)
{
  GhosttyTerminal terminal = NULL;
  GhosttyTerminalOptions opts = {.cols = 80, .rows = 24, .max_scrollback = 0};
  GhosttyResult result;

  g_assert_cmpint(ghostty_terminal_new(NULL, &terminal, opts), ==, GHOSTTY_SUCCESS);

  result = ghostty_terminal_resize(terminal, 120, 40, 8, 16);
  g_assert_cmpint(result, ==, GHOSTTY_SUCCESS);

  result = ghostty_terminal_resize(terminal, 80, 24, 8, 16);
  g_assert_cmpint(result, ==, GHOSTTY_SUCCESS);

  /* Resize to same size is a no-op (still succeeds) */
  result = ghostty_terminal_resize(terminal, 80, 24, 8, 16);
  g_assert_cmpint(result, ==, GHOSTTY_SUCCESS);

  ghostty_terminal_free(terminal);
}

static void
test_terminal_vt_write_ascii(void)
{
  GhosttyTerminal terminal = NULL;
  GhosttyTerminalOptions opts = {.cols = 80, .rows = 24, .max_scrollback = 100};

  g_assert_cmpint(ghostty_terminal_new(NULL, &terminal, opts), ==, GHOSTTY_SUCCESS);

  ghostty_terminal_vt_write(terminal, (const uint8_t *)"hello world\r\n", 13);
  ghostty_terminal_vt_write(terminal, (const uint8_t *)"", 0);

  ghostty_terminal_free(terminal);
}

static void
test_terminal_vt_write_escape_sequences(void)
{
  GhosttyTerminal terminal = NULL;
  GhosttyTerminalOptions opts = {.cols = 80, .rows = 24, .max_scrollback = 100};

  g_assert_cmpint(ghostty_terminal_new(NULL, &terminal, opts), ==, GHOSTTY_SUCCESS);

  ghostty_terminal_vt_write(terminal, (const uint8_t *)"\033[2J", 4);
  ghostty_terminal_vt_write(terminal, (const uint8_t *)"\033[1;1H", 6);
  ghostty_terminal_vt_write(terminal, (const uint8_t *)"\033[1;31mred text\033[0m", 19);
  ghostty_terminal_vt_write(terminal, (const uint8_t *)"\033]0;My Title\007", 13);

  ghostty_terminal_free(terminal);
}

static void
test_terminal_multiple_instances(void)
{
  GhosttyTerminal terms[4] = {NULL, NULL, NULL, NULL};
  GhosttyTerminalOptions opts = {.cols = 80, .rows = 24, .max_scrollback = 0};

  for (int i = 0; i < 4; i++)
    {
      g_assert_cmpint(ghostty_terminal_new(NULL, &terms[i], opts), ==, GHOSTTY_SUCCESS);
      g_assert_nonnull(terms[i]);
    }

  for (int i = 0; i < 4; i++)
    ghostty_terminal_free(terms[i]);
}

static void
test_terminal_scrollback_zero(void)
{
  GhosttyTerminal terminal = NULL;
  GhosttyTerminalOptions opts = {.cols = 80, .rows = 24, .max_scrollback = 0};

  g_assert_cmpint(ghostty_terminal_new(NULL, &terminal, opts), ==, GHOSTTY_SUCCESS);
  ghostty_terminal_vt_write(terminal, (const uint8_t *)"line1\r\nline2\r\nline3\r\n", 21);
  ghostty_terminal_free(terminal);
}

static void
test_terminal_scrollback_large(void)
{
  GhosttyTerminal terminal = NULL;
  GhosttyTerminalOptions opts = {.cols = 80, .rows = 24, .max_scrollback = 100000};

  g_assert_cmpint(ghostty_terminal_new(NULL, &terminal, opts), ==, GHOSTTY_SUCCESS);
  ghostty_terminal_free(terminal);
}

static void
test_terminal_min_dimensions(void)
{
  GhosttyTerminal terminal = NULL;
  GhosttyTerminalOptions opts = {.cols = 1, .rows = 1, .max_scrollback = 0};

  g_assert_cmpint(ghostty_terminal_new(NULL, &terminal, opts), ==, GHOSTTY_SUCCESS);
  ghostty_terminal_vt_write(terminal, (const uint8_t *)"x", 1);
  ghostty_terminal_free(terminal);
}

static void
test_terminal_wide_dimensions(void)
{
  GhosttyTerminal terminal = NULL;
  GhosttyTerminalOptions opts = {.cols = 512, .rows = 200, .max_scrollback = 0};

  g_assert_cmpint(ghostty_terminal_new(NULL, &terminal, opts), ==, GHOSTTY_SUCCESS);
  ghostty_terminal_free(terminal);
}

static void
test_focus_encode(void)
{
  char buf[16];
  size_t written = 0;
  GhosttyResult result;

  result = ghostty_focus_encode(GHOSTTY_FOCUS_GAINED, buf, sizeof(buf), &written);
  g_assert_cmpint(result, ==, GHOSTTY_SUCCESS);
  g_assert_cmpuint(written, >, 0);

  written = 0;
  result = ghostty_focus_encode(GHOSTTY_FOCUS_LOST, buf, sizeof(buf), &written);
  g_assert_cmpint(result, ==, GHOSTTY_SUCCESS);
  g_assert_cmpuint(written, >, 0);
}

static void
test_build_info(void)
{
  uint32_t version = 0;
  GhosttyResult result;

  result = ghostty_build_info(GHOSTTY_BUILD_INFO_VERSION_MAJOR, &version);
  /* If the field exists, it should return SUCCESS */
  g_assert_true(result == GHOSTTY_SUCCESS || result == GHOSTTY_INVALID_VALUE);
}

int
main(int argc, char *argv[])
{
  g_test_init(&argc, &argv, NULL);

  g_test_add_func("/Ptyxis/GhosttyVT/terminal_create_free",
                  test_terminal_create_free);
  g_test_add_func("/Ptyxis/GhosttyVT/terminal_null_free",
                  test_terminal_null_free);
  g_test_add_func("/Ptyxis/GhosttyVT/terminal_reset",
                  test_terminal_reset);
  g_test_add_func("/Ptyxis/GhosttyVT/terminal_reset_null",
                  test_terminal_reset_null);
  g_test_add_func("/Ptyxis/GhosttyVT/terminal_resize",
                  test_terminal_resize);
  g_test_add_func("/Ptyxis/GhosttyVT/terminal_vt_write_ascii",
                  test_terminal_vt_write_ascii);
  g_test_add_func("/Ptyxis/GhosttyVT/terminal_vt_write_escape_sequences",
                  test_terminal_vt_write_escape_sequences);
  g_test_add_func("/Ptyxis/GhosttyVT/terminal_multiple_instances",
                  test_terminal_multiple_instances);
  g_test_add_func("/Ptyxis/GhosttyVT/terminal_scrollback_zero",
                  test_terminal_scrollback_zero);
  g_test_add_func("/Ptyxis/GhosttyVT/terminal_scrollback_large",
                  test_terminal_scrollback_large);
  g_test_add_func("/Ptyxis/GhosttyVT/terminal_min_dimensions",
                  test_terminal_min_dimensions);
  g_test_add_func("/Ptyxis/GhosttyVT/terminal_wide_dimensions",
                  test_terminal_wide_dimensions);
  g_test_add_func("/Ptyxis/GhosttyVT/focus_encode",
                  test_focus_encode);
  g_test_add_func("/Ptyxis/GhosttyVT/build_info",
                  test_build_info);

  return g_test_run();
}
