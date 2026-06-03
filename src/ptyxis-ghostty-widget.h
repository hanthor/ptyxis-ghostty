/*
 * ptyxis-ghostty-widget.h
 *
 * GTK4 terminal widget built on libghostty-vt.
 *
 * Manages terminal emulation state (via libghostty-vt), PTY lifecycle,
 * Cairo/Pango rendering, and input encoding.
 */

#pragma once

#include <gtk/gtk.h>
#include <ghostty/vt.h>

G_BEGIN_DECLS

#define PTYXIS_TYPE_GHOSTTY_WIDGET (ptyxis_ghostty_widget_get_type())

G_DECLARE_FINAL_TYPE(PtyxisGhosttyWidget,
                     ptyxis_ghostty_widget,
                     PTYXIS,
                     GHOSTTY_WIDGET,
                     GtkWidget)

typedef struct {
  guint columns;
  guint rows;
  guint cell_width;
  guint cell_height;
} PtyxisGhosttySize;

PtyxisGhosttyWidget *ptyxis_ghostty_widget_new         (void);

void                 ptyxis_ghostty_widget_get_size     (PtyxisGhosttyWidget *self,
                                                         PtyxisGhosttySize   *size);

gboolean             ptyxis_ghostty_widget_has_selection    (PtyxisGhosttyWidget *self);
char                *ptyxis_ghostty_widget_get_selected_text(PtyxisGhosttyWidget *self);
char                *ptyxis_ghostty_widget_get_window_title (PtyxisGhosttyWidget *self);

void                 ptyxis_ghostty_widget_paste         (PtyxisGhosttyWidget *self,
                                                          const char          *text);

void                 ptyxis_ghostty_widget_set_font_scale(PtyxisGhosttyWidget *self,
                                                          double               scale);

void                 ptyxis_ghostty_widget_feed           (PtyxisGhosttyWidget *self,
                                                           const char          *data,
                                                           gsize                length);

void                 ptyxis_ghostty_widget_get_background_rgba(PtyxisGhosttyWidget *self,
                                                               GdkRGBA             *out_color);

void                 ptyxis_ghostty_widget_set_input_enabled (PtyxisGhosttyWidget *self,
                                                              gboolean             enabled);
gboolean             ptyxis_ghostty_widget_get_input_enabled (PtyxisGhosttyWidget *self);

void                 ptyxis_ghostty_widget_search_start   (PtyxisGhosttyWidget *self,
                                                           const char          *needle);
void                 ptyxis_ghostty_widget_search_next    (PtyxisGhosttyWidget *self);
void                 ptyxis_ghostty_widget_search_previous(PtyxisGhosttyWidget *self);
void                 ptyxis_ghostty_widget_search_end     (PtyxisGhosttyWidget *self);

G_END_DECLS
