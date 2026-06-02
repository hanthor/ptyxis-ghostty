/*
 * ptyxis-ghostty-widget.h
 *
 * GTK widget that wraps a ghostty_surface_t for embedding in Ptyxis.
 */

#pragma once

#include <gtk/gtk.h>
#include <ghostty.h>

G_BEGIN_DECLS

#define PTYXIS_TYPE_GHOSTTY_WIDGET (ptyxis_ghostty_widget_get_type())

G_DECLARE_FINAL_TYPE(PtyxisGhosttyWidget,
                     ptyxis_ghostty_widget,
                     PTYXIS,
                     GHOSTTY_WIDGET,
                     GtkWidget)

typedef struct {
  ghostty_surface_size_s size;
  guint                  columns;
  guint                  rows;
  guint                  cell_width;
  guint                  cell_height;
} PtyxisGhosttySize;

typedef void (*PtyxisGhosttyActionFunc)(ghostty_action_tag_e tag,
                                        const ghostty_action_u *action,
                                        gpointer user_data);

PtyxisGhosttyWidget *ptyxis_ghostty_widget_new(ghostty_app_t app,
                                               ghostty_surface_config_s *config,
                                               ghostty_config_t gconfig);

ghostty_surface_t ptyxis_ghostty_widget_get_surface(PtyxisGhosttyWidget *self);

void ptyxis_ghostty_widget_set_action_callback(PtyxisGhosttyWidget *self,
                                               PtyxisGhosttyActionFunc func,
                                               gpointer user_data);

void ptyxis_ghostty_widget_get_size(PtyxisGhosttyWidget *self,
                                    PtyxisGhosttySize *size);

void ptyxis_ghostty_widget_search_start(PtyxisGhosttyWidget *self,
                                        const char *needle);

void ptyxis_ghostty_widget_search_next(PtyxisGhosttyWidget *self);

void ptyxis_ghostty_widget_search_previous(PtyxisGhosttyWidget *self);

void ptyxis_ghostty_widget_search_end(PtyxisGhosttyWidget *self);

gboolean ptyxis_ghostty_widget_has_selection(PtyxisGhosttyWidget *self);

char *ptyxis_ghostty_widget_get_selected_text(PtyxisGhosttyWidget *self);

char *ptyxis_ghostty_widget_get_window_title(PtyxisGhosttyWidget *self);

void ptyxis_ghostty_widget_paste(PtyxisGhosttyWidget *self,
                                 const char *text);

void ptyxis_ghostty_widget_set_font_scale(PtyxisGhosttyWidget *self,
                                          double scale);

void ptyxis_ghostty_widget_set_config(PtyxisGhosttyWidget *self,
                                      ghostty_config_t config);

G_END_DECLS
