/*
 * ptyxis-ghostty-config.h
 *
 * Maps PtyxisProfile and PtyxisSettings to ghostty_config_t.
 */

#pragma once

#include <ghostty.h>
#include "ptyxis-profile.h"
#include "ptyxis-settings.h"

G_BEGIN_DECLS

/**
 * ptyxis_ghostty_config_from_profile:
 * @profile: a PtyxisProfile (may be NULL)
 * @settings: a PtyxisSettings (may be NULL)
 *
 * Creates a ghostty_config_t populated from the Ptyxis profile and settings.
 * The returned config must be freed with ghostty_config_free().
 *
 * Returns: (transfer full): a new ghostty_config_t
 */
ghostty_config_t ptyxis_ghostty_config_from_profile(PtyxisProfile *profile,
                                                     PtyxisSettings *settings);

/**
 * ptyxis_ghostty_config_apply_to_surface:
 * @surface: the ghostty surface
 * @config: the config to apply
 *
 * Applies a ghostty config to a running surface.
 */
void ptyxis_ghostty_config_apply_to_surface(ghostty_surface_t surface,
                                             ghostty_config_t config);

/**
 * ptyxis_ghostty_config_from_palette:
 * @palette: a PtyxisPalette
 * @dark: whether to use dark palette variant
 *
 * Creates a ghostty palette from a Ptyxis palette face.
 * Fills the provided palette struct.
 */
void ptyxis_ghostty_config_palette_from_face(PtyxisPalette *palette,
                                              gboolean dark,
                                              ghostty_config_palette_s *out);

G_END_DECLS
