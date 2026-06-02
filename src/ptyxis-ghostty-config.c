/*
 * ptyxis-ghostty-config.c
 *
 * Maps PtyxisProfile and PtyxisSettings to ghostty_config_t.
 */

#include "config.h"
#include "ptyxis-ghostty-config.h"

ghostty_config_t
ptyxis_ghostty_config_from_profile(PtyxisProfile *profile,
                                    PtyxisSettings *settings)
{
  ghostty_config_t config;
  ghostty_config_palette_s palette = {0};
  PtyxisPalette *ppal = NULL;
  gboolean dark;
  AdwStyleManager *style;
  PangoFontDescription *font_desc = NULL;
  const char *font_name = NULL;
  gboolean val_bool;
  int val_int;
  double val_double;
  ghostty_config_color_s color;

  config = ghostty_config_new();

  if (config == NULL)
    return NULL;

  /* Palette / colors */
  if (profile != NULL)
    {
      ppal = ptyxis_profile_dup_palette(profile);
      dark = adw_style_manager_get_dark(adw_style_manager_get_default());
      ptyxis_ghostty_config_palette_from_face(ppal, dark, &palette);

      ghostty_config_get(config, &palette, "palette", 7);
      g_clear_object(&ppal);
    }

  /* Font */
  if (settings != NULL)
    {
      font_desc = ptyxis_settings_dup_font_desc(settings);
      if (font_desc != NULL)
        {
          font_name = pango_font_description_get_family(font_desc);
          ghostty_config_get(config, &font_name, "font-family", 11);
          pango_font_description_free(font_desc);
        }
    }

  /* Opacity */
  if (profile != NULL)
    {
      val_double = ptyxis_profile_get_opacity(profile);
      opacity_as_int:;
      int opacity_pct = (int)(val_double * 100.0);
      ghostty_config_get(config, &opacity_pct, "background-opacity", 19);
    }

  /* Scrollback */
  if (profile != NULL)
    {
      val_bool = ptyxis_profile_get_limit_scrollback(profile);
      if (val_bool)
        {
          val_int = ptyxis_profile_get_scrollback_lines(profile);
          ghostty_config_get(config, &val_int, "scrollback-limit", 16);
        }
      else
        {
          val_int = 0; /* unlimited */
          ghostty_config_get(config, &val_int, "scrollback-limit", 16);
        }

      /* Scroll behavior */
      val_bool = ptyxis_profile_get_scroll_on_output(profile);
      ghostty_config_get(config, &val_bool, "scroll-on-output", 16);

      /* Cursor shape */
      if (settings != NULL)
        {
          PtyxisCursorShape shape = ptyxis_settings_get_cursor_shape(settings);
          const char *shape_str;
          switch (shape)
            {
            case PTYXIS_CURSOR_SHAPE_BLOCK:     shape_str = "block"; break;
            case PTYXIS_CURSOR_SHAPE_IBEAM:     shape_str = "bar"; break;
            case PTYXIS_CURSOR_SHAPE_UNDERLINE: shape_str = "underline"; break;
            default:                             shape_str = "block"; break;
            }
          ghostty_config_get(config, &shape_str, "cursor-shape", 14);

          /* Cursor blink */
          PtyxisCursorBlinkMode blink = ptyxis_settings_get_cursor_blink_mode(settings);
          const char *blink_str;
          switch (blink)
            {
            case PTYXIS_CURSOR_BLINK_SYSTEM: blink_str = "system"; break;
            case PTYXIS_CURSOR_BLINK_ON:     blink_str = "on"; break;
            case PTYXIS_CURSOR_BLINK_OFF:    blink_str = "off"; break;
            default:                          blink_str = "system"; break;
            }
          ghostty_config_get(config, &blink_str, "cursor-style", 14);
        }
    }

  /* Bell */
  if (settings != NULL)
    {
      val_bool = ptyxis_settings_get_audible_bell(settings);
      ghostty_config_get(config, &val_bool, "audible-bell", 14);

      val_bool = ptyxis_settings_get_visual_bell(settings);
      ghostty_config_get(config, &val_bool, "visual-bell", 13);
    }

  /* Word characters */
  if (settings != NULL)
    {
      g_autofree char *word_chars = ptyxis_settings_dup_word_char_exceptions(settings);
      if (word_chars != NULL && word_chars[0] != '\0')
        ghostty_config_get(config, &word_chars, "word-separators", 15);
    }

  /* Bold-is-bright */
  if (profile != NULL)
    {
      val_bool = ptyxis_profile_get_bold_is_bright(profile);
      ghostty_config_get(config, &val_bool, "bold-is-bright", 14);
    }

  /* Font size (default 12pt) */
  float font_size = 12.0f;
  if (settings != NULL && ptyxis_settings_get_use_system_font(settings))
    {
      /* System font - use monospace 12pt */
      font_size = 12.0f;
    }
  ghostty_config_get(config, &font_size, "font-size", 11);

  ghostty_config_finalize(config);

  return config;
}

void
ptyxis_ghostty_config_palette_from_face(PtyxisPalette *palette,
                                         gboolean dark,
                                         ghostty_config_palette_s *out)
{
  const PtyxisPaletteFace *face;

  g_return_if_fail(palette != NULL);
  g_return_if_fail(out != NULL);

  face = ptyxis_palette_get_face(palette, dark);
  if (face == NULL)
    return;

  memset(out, 0, sizeof(*out));

  for (guint i = 0; i < MIN(G_N_ELEMENTS(face->indexed), 256); i++)
    {
      out->colors[i] = (ghostty_config_color_s){
        .r = (uint8_t)(face->indexed[i].red * 255.0),
        .g = (uint8_t)(face->indexed[i].green * 255.0),
        .b = (uint8_t)(face->indexed[i].blue * 255.0),
      };
    }

  /* Standard 16 colors: 0-7 = normal, 8-15 = bright */
  out->colors[0] = (ghostty_config_color_s){
    .r = (uint8_t)(face->indexed[0].red * 255.0),
    .g = (uint8_t)(face->indexed[0].green * 255.0),
    .b = (uint8_t)(face->indexed[0].blue * 255.0),
  };
}

void
ptyxis_ghostty_config_apply_to_surface(ghostty_surface_t surface,
                                        ghostty_config_t config)
{
  g_return_if_fail(surface != NULL);
  g_return_if_fail(config != NULL);

  ghostty_surface_update_config(surface, config);
}
