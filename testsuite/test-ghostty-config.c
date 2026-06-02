/*
 * test-ghostty-config.c
 *
 * Tests for the PtyxisProfile → ghostty_config_t mapping.
 */

#include <glib.h>
#include "ptyxis-ghostty-config.h"
#include "ptyxis-profile.h"
#include "ptyxis-settings.h"
#include "ptyxis-palette.h"

static void
test_config_from_null(void)
{
  ghostty_config_t config;

  /* Should not crash with NULL inputs */
  config = ptyxis_ghostty_config_from_profile(NULL, NULL);
  g_assert_nonnull(config);
  ghostty_config_free(config);
}

static void
test_config_from_profile(void)
{
  PtyxisProfile *profile;
  ghostty_config_t config;

  profile = ptyxis_profile_new("test-profile-uuid");
  g_assert_nonnull(profile);

  ptyxis_profile_set_label(profile, "Test Profile");
  ptyxis_profile_set_opacity(profile, 0.85);
  ptyxis_profile_set_scrollback_lines(profile, 10000);
  ptyxis_profile_set_scroll_on_output(profile, TRUE);

  config = ptyxis_ghostty_config_from_profile(profile, NULL);
  g_assert_nonnull(config);

  /* Config should be valid */
  ghostty_config_free(config);
  g_object_unref(profile);
}

static void
test_config_palette(void)
{
  PtyxisPalette *palette;
  ghostty_config_palette_s gpalette = {0};

  palette = ptyxis_palette_lookup("gnome");
  g_assert_nonnull(palette);

  ptyxis_ghostty_config_palette_from_face(palette, FALSE, &gpalette);

  /* Basic sanity: first color should not be pure black in all channels */
  g_assert_cmpint(gpalette.colors[0].r + gpalette.colors[0].g +
                  gpalette.colors[0].b, >, 0);

  g_object_unref(palette);
}

static void
test_config_palette_dark(void)
{
  PtyxisPalette *palette;
  ghostty_config_palette_s gpalette = {0};

  palette = ptyxis_palette_lookup("gnome");
  g_assert_nonnull(palette);

  ptyxis_ghostty_config_palette_from_face(palette, TRUE, &gpalette);

  /* Dark palette should have non-zero colors */
  g_assert_cmpint(gpalette.colors[0].r + gpalette.colors[0].g +
                  gpalette.colors[0].b, >, 0);

  g_object_unref(palette);
}

static void
test_config_with_settings(void)
{
  PtyxisSettings *settings;
  ghostty_config_t config;

  settings = ptyxis_settings_new();
  g_assert_nonnull(settings);

  /* Set some settings values */
  ptyxis_settings_set_audible_bell(settings, TRUE);
  ptyxis_settings_set_visual_bell(settings, FALSE);
  ptyxis_settings_set_cursor_shape(settings, PTYXIS_CURSOR_SHAPE_BLOCK);
  ptyxis_settings_set_cursor_blink_mode(settings, PTYXIS_CURSOR_BLINK_SYSTEM);

  config = ptyxis_ghostty_config_from_profile(NULL, settings);
  g_assert_nonnull(config);

  ghostty_config_free(config);
  g_object_unref(settings);
}

int
main(int argc, char *argv[])
{
  g_test_init(&argc, &argv, NULL);

  g_test_add_func("/Ptyxis/GhosttyConfig/from_null",
                  test_config_from_null);
  g_test_add_func("/Ptyxis/GhosttyConfig/from_profile",
                  test_config_from_profile);
  g_test_add_func("/Ptyxis/GhosttyConfig/palette",
                  test_config_palette);
  g_test_add_func("/Ptyxis/GhosttyConfig/palette_dark",
                  test_config_palette_dark);
  g_test_add_func("/Ptyxis/GhosttyConfig/with_settings",
                  test_config_with_settings);

  return g_test_run();
}
