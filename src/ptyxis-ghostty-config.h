/*
 * ptyxis-ghostty-config.h
 *
 * Stub: config bridge replaced by libghostty-vt terminal options.
 */

#pragma once

#include "ptyxis-profile.h"
#include "ptyxis-settings.h"

G_BEGIN_DECLS

void ptyxis_ghostty_config_apply_to_surface (gpointer surface,
                                             gpointer config);

G_END_DECLS
