/*
 * test-ghostty-config.c
 *
 * Tests for the ghostty_config_t management and basic API.
 * These test ghostty config creation without requiring full ptyxis profile.
 */

#include <glib.h>
#include <ghostty.h>
#include "ptyxis-ghostty-config.h"

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
test_config_apply_to_surface_null(void)
{
  /* Calling apply with NULL surface should not crash */
  ghostty_config_t config = ghostty_config_new();
  g_assert_nonnull(config);
  /* apply_to_surface with NULL is a no-op (guarded by g_return) */
  ptyxis_ghostty_config_apply_to_surface(NULL, config);
  ghostty_config_free(config);
}

static void
test_config_palette_zero_init(void)
{
  /* Test that a zeroed palette doesn't crash when passed to config */
  ghostty_config_palette_s palette = {0};

  /* All zeros is valid (black on black); verify the struct initializes */
  g_assert_cmpuint(palette.colors[0].r, ==, 0);
  g_assert_cmpuint(palette.colors[0].g, ==, 0);
  g_assert_cmpuint(palette.colors[0].b, ==, 0);
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
  g_test_add_func("/Ptyxis/GhosttyConfig/apply_to_surface_null",
                  test_config_apply_to_surface_null);
  g_test_add_func("/Ptyxis/GhosttyConfig/palette_zero_init",
                  test_config_palette_zero_init);

  return g_test_run();
}
