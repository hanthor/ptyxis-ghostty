/*
 * ptyxis-terminal.h
 *
 * Refactored: PtyxisTerminal no longer inherits VteTerminal.
 * It is now a plain GtkWidget that delegates terminal emulation to ghostty_surface_t.
 */

#pragma once

#include <gtk/gtk.h>
#include <adwaita.h>

#include "ptyxis-palette.h"
#include "ptyxis-shortcuts.h"

G_BEGIN_DECLS

/* Forward declaration; full definition in ptyxis-compat.h */
typedef struct _VtePty VtePty;

#define PTYXIS_TYPE_TERMINAL (ptyxis_terminal_get_type())

G_DECLARE_FINAL_TYPE(PtyxisTerminal,
                     ptyxis_terminal,
                     PTYXIS,
                     TERMINAL,
                     GtkWidget)

PtyxisPalette  *ptyxis_terminal_get_palette        (PtyxisTerminal *self);
void            ptyxis_terminal_set_palette        (PtyxisTerminal *self,
                                                    PtyxisPalette  *palette);
PtyxisShortcuts *ptyxis_terminal_get_shortcuts     (PtyxisTerminal *self);
const char     *ptyxis_terminal_get_current_container_name   (PtyxisTerminal *self);
const char     *ptyxis_terminal_get_current_container_runtime(PtyxisTerminal *self);
guint           ptyxis_terminal_get_n_columns       (PtyxisTerminal *self);
guint           ptyxis_terminal_get_n_rows          (PtyxisTerminal *self);
void            ptyxis_terminal_reset               (PtyxisTerminal *self,
                                                     gboolean        clear_screen);
gboolean        ptyxis_terminal_get_scroll_on_keystroke(PtyxisTerminal *self);
char           *ptyxis_terminal_dup_current_directory_uri(PtyxisTerminal *self);
void            ptyxis_terminal_set_custom_link     (PtyxisTerminal  *self,
                                                     int              tag,
                                                     const char      *regex,
                                                     const char      *replace,
                                                     const char      *cursor_name);

/* Called from VTE compat stubs */
void            ptyxis_terminal_set_font_scale      (PtyxisTerminal *self, double scale);
double          ptyxis_terminal_get_font_scale      (PtyxisTerminal *self);
void            ptyxis_terminal_feed                (PtyxisTerminal *self,
                                                     const char     *data,
                                                     gssize          length);
char           *ptyxis_terminal_get_window_title    (PtyxisTerminal *self);
void            ptyxis_terminal_get_background_rgba (PtyxisTerminal *self,
                                                     GdkRGBA        *out_color);
void            ptyxis_terminal_set_input_enabled   (PtyxisTerminal *self, gboolean enabled);
gboolean        ptyxis_terminal_get_input_enabled   (PtyxisTerminal *self);

void            ptyxis_terminal_set_pty             (PtyxisTerminal *self, VtePty *pty);
VtePty         *ptyxis_terminal_get_pty             (PtyxisTerminal *self);

void            ptyxis_terminal_set_font_desc       (PtyxisTerminal *self, const PangoFontDescription *font_desc);
const PangoFontDescription *ptyxis_terminal_get_font_desc(PtyxisTerminal *self);
void            ptyxis_terminal_set_cursor_shape    (PtyxisTerminal *self, int shape);
int             ptyxis_terminal_get_cursor_shape    (PtyxisTerminal *self);
void            ptyxis_terminal_set_cursor_blink_mode(PtyxisTerminal *self, int mode);
int             ptyxis_terminal_get_cursor_blink_mode(PtyxisTerminal *self);
void            ptyxis_terminal_set_text_blink_mode (PtyxisTerminal *self, int mode);
int             ptyxis_terminal_get_text_blink_mode (PtyxisTerminal *self);

void            ptyxis_terminal_search_find_next    (PtyxisTerminal *self);
void            ptyxis_terminal_search_find_previous(PtyxisTerminal *self);
void            ptyxis_terminal_search_set_regex    (PtyxisTerminal *self, const char *regex, guint flags);
void            ptyxis_terminal_search_set_wrap_around(PtyxisTerminal *self, gboolean wrap);

G_END_DECLS
