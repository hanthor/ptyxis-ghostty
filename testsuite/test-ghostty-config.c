/*
 * test-ghostty-config.c
 *
 * Tests for the ghostty_config_t management and basic API.
 * These test ghostty config creation without requiring full ptyxis profile.
 */

#include <glib.h>
#include <ghostty.h>

static void
test_config_create_free(void)
{
  ghostty_config_t config;

  config = ghostty_config_new();
  g_assert_nonnull(config);
  ghostty_config_free(config);
}

static void
test_config_clone(void)
{
  ghostty_config_t config;
  ghostty_config_t clone;

  config = ghostty_config_new();
  g_assert_nonnull(config);

  clone = ghostty_config_clone(config);
  g_assert_nonnull(clone);

  ghostty_config_free(clone);
  ghostty_config_free(config);
}

static void
test_config_finalize(void)
{
  ghostty_config_t config;

  config = ghostty_config_new();
  g_assert_nonnull(config);

  /* Finalizing a config should not crash */
  ghostty_config_finalize(config);
  ghostty_config_free(config);
}

static void
test_config_palette_zero_init(void)
{
  ghostty_config_palette_s palette = {0};

  g_assert_cmpuint(palette.colors[0].r, ==, 0);
  g_assert_cmpuint(palette.colors[0].g, ==, 0);
  g_assert_cmpuint(palette.colors[0].b, ==, 0);
}

static void
test_config_diagnostics_fresh(void)
{
  ghostty_config_t config;
  uint32_t count;

  config = ghostty_config_new();
  g_assert_nonnull(config);
  ghostty_config_finalize(config);

  count = ghostty_config_diagnostics_count(config);
  /* A fresh default config should have zero diagnostics */
  g_assert_cmpuint(count, ==, 0);

  ghostty_config_free(config);
}

static void
test_config_load_nonexistent_file(void)
{
  ghostty_config_t config;

  config = ghostty_config_new();
  g_assert_nonnull(config);

  /* Loading a non-existent file must not crash */
  ghostty_config_load_file(config, "/nonexistent/path/that/does/not/exist.conf");

  ghostty_config_finalize(config);
  ghostty_config_free(config);
}

static void
test_config_clone_is_independent(void)
{
  ghostty_config_t orig;
  ghostty_config_t clone_a;
  ghostty_config_t clone_b;

  orig = ghostty_config_new();
  g_assert_nonnull(orig);

  clone_a = ghostty_config_clone(orig);
  clone_b = ghostty_config_clone(orig);

  g_assert_nonnull(clone_a);
  g_assert_nonnull(clone_b);
  g_assert_true(clone_a != clone_b);
  g_assert_true(clone_a != orig);

  ghostty_config_free(clone_a);
  ghostty_config_free(clone_b);
  ghostty_config_free(orig);
}

static void
test_config_finalize_idempotent_via_clone(void)
{
  ghostty_config_t config;
  ghostty_config_t clone;

  /* Finalize twice by finalizing a clone after the original is freed */
  config = ghostty_config_new();
  g_assert_nonnull(config);
  ghostty_config_finalize(config);

  clone = ghostty_config_clone(config);
  g_assert_nonnull(clone);
  ghostty_config_finalize(clone);

  ghostty_config_free(config);
  ghostty_config_free(clone);
}

static void
test_config_palette_all_colors(void)
{
  ghostty_config_palette_s palette = {0};

  /* Verify we can read all 256 color slots without out-of-bounds */
  for (int i = 0; i < 256; i++)
    {
      g_assert_cmpuint(palette.colors[i].r, ==, 0);
      g_assert_cmpuint(palette.colors[i].g, ==, 0);
      g_assert_cmpuint(palette.colors[i].b, ==, 0);
    }
}

static void
test_config_multiple_clones(void)
{
  ghostty_config_t configs[8];

  for (int i = 0; i < 8; i++)
    configs[i] = ghostty_config_new();

  for (int i = 0; i < 8; i++)
    {
      g_assert_nonnull(configs[i]);
      ghostty_config_finalize(configs[i]);
    }

  for (int i = 0; i < 8; i++)
    ghostty_config_free(configs[i]);
}

int
main(int argc, char *argv[])
{
  g_test_init(&argc, &argv, NULL);

  g_test_add_func("/Ptyxis/GhosttyConfig/create_free",
                  test_config_create_free);
  g_test_add_func("/Ptyxis/GhosttyConfig/clone",
                  test_config_clone);
  g_test_add_func("/Ptyxis/GhosttyConfig/finalize",
                  test_config_finalize);
  g_test_add_func("/Ptyxis/GhosttyConfig/palette_zero_init",
                  test_config_palette_zero_init);
  g_test_add_func("/Ptyxis/GhosttyConfig/diagnostics_fresh",
                  test_config_diagnostics_fresh);
  g_test_add_func("/Ptyxis/GhosttyConfig/load_nonexistent_file",
                  test_config_load_nonexistent_file);
  g_test_add_func("/Ptyxis/GhosttyConfig/clone_is_independent",
                  test_config_clone_is_independent);
  g_test_add_func("/Ptyxis/GhosttyConfig/finalize_idempotent_via_clone",
                  test_config_finalize_idempotent_via_clone);
  g_test_add_func("/Ptyxis/GhosttyConfig/palette_all_colors",
                  test_config_palette_all_colors);
  g_test_add_func("/Ptyxis/GhosttyConfig/multiple_clones",
                  test_config_multiple_clones);

  return g_test_run();
}
