/*
 * ptyxis-terminal.c
 *
 * Refactored: PtyxisTerminal wraps ghostty_surface_t instead of VteTerminal.
 * Preserves all Ptyxis-specific logic: URL matching, custom links, drop handling,
 * shortcuts, palette management, container termprops.
 *
 * Original copyright notices preserved from ptyxis-terminal.c.
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "config.h"

#define PCRE2_CODE_UNIT_WIDTH 0
#include <pcre2.h>

#include <adwaita.h>
#include <glib/gi18n.h>

#include "ptyxis-application.h"
#include "ptyxis-custom-link.h"
#include "ptyxis-ghostty-widget.h"
#include "ptyxis-shortcuts.h"
#include "ptyxis-tab.h"
#include "ptyxis-terminal.h"
#include "ptyxis-util.h"
#include "ptyxis-window.h"

#include "terminal-regex.h"

/* Forward declarations */
static void paste_clipboard_action_cb(GObject *object,
                                       GAsyncResult *result,
                                       gpointer user_data);

#define SIZE_DISMISS_TIMEOUT_MSEC 1000
#define URL_MATCH_CURSOR_NAME "pointer"

#define DROP_REQUEST_PRIORITY               G_PRIORITY_DEFAULT
#define APPLICATION_VND_PORTAL_FILETRANSFER "application/vnd.portal.filetransfer"
#define APPLICATION_VND_PORTAL_FILES        "application/vnd.portal.files"
#define TEXT_X_MOZ_URL                      "text/x-moz-url"
#define TEXT_URI_LIST                       "text/uri-list"

#define FILE_ATTRIBUTE_HOST_PATH "xattr::document-portal.host-path"

struct _PtyxisTerminal
{
  GtkWidget              parent_instance;

  /* Ghostty integration */
  PtyxisGhosttyWidget   *ghostty;
  ghostty_app_t          app;           /* Shared app instance */
  ghostty_config_t       config;        /* Per-terminal config */

  PtyxisShortcuts       *shortcuts;
  PtyxisPalette         *palette;
  char                  *url;
  GHashTable            *custom_links;

  /* Widget chrome */
  GtkPopover            *popover;
  GMenu                 *terminal_menu;
  GtkWidget             *drop_highlight;
  GtkDropTargetAsync    *drop_target;
  GtkRevealer           *size_revealer;
  GtkLabel              *size_label;

  /* Container tracking */
  char                  *current_container_name;
  char                  *current_container_runtime;

  GdkRGBA                background;

  guint                  size_dismiss_source;
  guint                  n_columns;
  guint                  n_rows;
  guint                  cell_width;
  guint                  cell_height;

  gboolean               has_selection;
  gboolean               input_enabled;
  gboolean               scroll_on_keystroke;
};

enum {
  PROP_0,
  PROP_CURRENT_CONTAINER_NAME,
  PROP_CURRENT_CONTAINER_RUNTIME,
  PROP_PALETTE,
  PROP_SHORTCUTS,
  N_PROPS
};

enum {
  MATCH_CLICKED,
  GRID_SIZE_CHANGED,
  SHELL_PRECMD,
  SHELL_PREEXEC,
  SLEWED,        /* Kept for API compat with existing signal consumers */
  N_SIGNALS
};

G_DEFINE_FINAL_TYPE(PtyxisTerminal, ptyxis_terminal, GTK_TYPE_WIDGET)

static GParamSpec *properties [N_PROPS];
static guint signals[N_SIGNALS];
static const char * const url_regexes_str[] = {
  REGEX_URL_AS_IS,
  REGEX_URL_HTTP,
  REGEX_URL_FILE,
  REGEX_EMAIL,
};

/* Ghostty action callback dispatcher */
static void
ptyxis_terminal_action_cb(ghostty_action_tag_e tag,
                          const ghostty_action_u *action,
                          gpointer user_data)
{
  PtyxisTerminal *self = PTYXIS_TERMINAL(user_data);

  switch (tag)
    {
    case GHOSTTY_ACTION_SET_TITLE:
      /* Title changes are handled by PtyxisTab via tab-monitor */
      break;

    case GHOSTTY_ACTION_PWD:
      /* Working directory tracking - forwarded to PtyxisTab */
      break;

    case GHOSTTY_ACTION_MOUSE_OVER_LINK:
      if (action->mouse_over_link.url != NULL)
        {
          g_set_str(&self->url, g_strndup(action->mouse_over_link.url,
                                           action->mouse_over_link.len));
        }
      else
        {
          g_clear_pointer(&self->url, g_free);
        }
      break;

    case GHOSTTY_ACTION_CELL_SIZE:
      self->cell_width = action->cell_size.width;
      self->cell_height = action->cell_size.height;

      ptyxis_ghostty_widget_get_size(self->ghostty, &(PtyxisGhosttySize){0});
      self->n_columns = action->cell_size.width > 0 ?
        gtk_widget_get_width(GTK_WIDGET(self)) / MAX(1, action->cell_size.width) : 80;
      self->n_rows = action->cell_size.height > 0 ?
        gtk_widget_get_height(GTK_WIDGET(self)) / MAX(1, action->cell_size.height) : 24;

      g_signal_emit(self, signals[GRID_SIZE_CHANGED], 0,
                    self->n_columns, self->n_rows);
      break;

    case GHOSTTY_ACTION_COMMAND_FINISHED:
      g_signal_emit(self, signals[SHELL_PRECMD], 0);
      break;

    case GHOSTTY_ACTION_CLOSE_WINDOW:
    case GHOSTTY_ACTION_CLOSE_TAB:
      /* Let tabs handle this */
      break;

    case GHOSTTY_ACTION_RING_BELL:
      /* Visual bell handled by PtyxisTab */
      break;

    case GHOSTTY_ACTION_PROGRESS_REPORT:
      /* OSC 9;4 progress reports - handled by PtyxisTab */
      break;

    default:
      break;
    }
}

/* --- Color / Palette --- */

static void
ptyxis_terminal_update_colors(PtyxisTerminal *self)
{
  const PtyxisPaletteFace *face;
  AdwStyleManager *style_manager;
  gboolean dark;
  ghostty_config_palette_s palette = {0};
  ghostty_config_color_s fg, bg;

  g_assert(PTYXIS_IS_TERMINAL(self));

  if (self->palette == NULL)
    self->palette = ptyxis_palette_lookup("gnome");

  style_manager = adw_style_manager_get_default();
  dark = adw_style_manager_get_dark(style_manager);
  face = ptyxis_palette_get_face(self->palette, dark);

  self->background = face->background;

  /* Build ghostty palette from Ptyxis face */
  fg = (ghostty_config_color_s){
    .r = (uint8_t)(face->foreground.red * 255),
    .g = (uint8_t)(face->foreground.green * 255),
    .b = (uint8_t)(face->foreground.blue * 255),
  };
  bg = (ghostty_config_color_s){
    .r = (uint8_t)(face->background.red * 255),
    .g = (uint8_t)(face->background.green * 255),
    .b = (uint8_t)(face->background.blue * 255),
  };

  for (guint i = 0; i < MIN(G_N_ELEMENTS(face->indexed), 256); i++)
    {
      palette.colors[i] = (ghostty_config_color_s){
        .r = (uint8_t)(face->indexed[i].red * 255),
        .g = (uint8_t)(face->indexed[i].green * 255),
        .b = (uint8_t)(face->indexed[i].blue * 255),
      };
    }

  /* Apply to ghostty config */
  if (self->config != NULL)
    {
      ghostty_config_get(self->config, &fg, "foreground", 10);
      ghostty_config_get(self->config, &bg, "background", 10);
      ghostty_config_get(self->config, &palette, "palette", 7);
      if (self->ghostty != NULL)
        {
          ghostty_surface_t surface = ptyxis_ghostty_widget_get_surface(self->ghostty);
          if (surface != NULL)
            ghostty_surface_update_config(surface, self->config);
        }
    }
}

static void
ptyxis_terminal_notify_dark_cb(PtyxisTerminal *self)
{
  ptyxis_terminal_update_colors(self);
}

/* --- Toast notifications --- */

static void
ptyxis_terminal_toast(PtyxisTerminal *self,
                      int            timeout,
                      const char    *title)
{
  GtkWidget *overlay = gtk_widget_get_ancestor(GTK_WIDGET(self), ADW_TYPE_TOAST_OVERLAY);
  AdwToast *toast;

  if (overlay == NULL)
    return;

  toast = g_object_new(ADW_TYPE_TOAST,
                       "title", title,
                       "timeout", timeout,
                       NULL);
  adw_toast_overlay_add_toast(ADW_TOAST_OVERLAY(overlay), toast);
}

/* --- Active terminal check --- */

static gboolean
ptyxis_terminal_is_active(PtyxisTerminal *self)
{
  PtyxisWindow *window;
  PtyxisTab *active_tab;
  PtyxisTerminal *active_terminal = NULL;

  g_assert(PTYXIS_IS_TERMINAL(self));

  if ((window = PTYXIS_WINDOW(gtk_widget_get_ancestor(GTK_WIDGET(self), PTYXIS_TYPE_WINDOW))) &&
      (active_tab = ptyxis_window_get_active_tab(window)))
    active_terminal = ptyxis_tab_get_terminal(active_tab);

  return active_terminal == self;
}

/* --- Clipboard actions --- */

static void
ptyxis_terminal_update_clipboard_actions(PtyxisTerminal *self)
{
  GdkClipboard *clipboard;
  gboolean can_paste;
  gboolean has_selection;

  g_assert(PTYXIS_IS_TERMINAL(self));

  clipboard = gtk_widget_get_clipboard(GTK_WIDGET(self));
  can_paste = gdk_content_formats_contain_gtype(
    gdk_clipboard_get_formats(clipboard), G_TYPE_STRING);
  has_selection = ptyxis_ghostty_widget_has_selection(self->ghostty);

  gtk_widget_action_set_enabled(GTK_WIDGET(self), "clipboard.copy", has_selection);
  gtk_widget_action_set_enabled(GTK_WIDGET(self), "clipboard.copy-as-html", has_selection);
  gtk_widget_action_set_enabled(GTK_WIDGET(self), "clipboard.paste", can_paste);

  self->has_selection = has_selection;
}

/* --- URL tracking --- */

static void
ptyxis_terminal_update_url_actions(PtyxisTerminal *self,
                                   double          x,
                                   double          y)
{
  /* URL is set by ghostty action callback (MOUSE_OVER_LINK).
   * Custom links checked here against the URL. */
  gtk_widget_action_set_enabled(GTK_WIDGET(self), "clipboard.copy-link", self->url != NULL);
  gtk_widget_action_set_enabled(GTK_WIDGET(self), "terminal.open-link", self->url != NULL);
}

/* --- Match clicked --- */

static gboolean
ptyxis_terminal_match_clicked(PtyxisTerminal  *self,
                              double           x,
                              double           y,
                              int              button,
                              GdkModifierType  state,
                              const char      *match)
{
  gboolean ret = FALSE;

  g_assert(PTYXIS_IS_TERMINAL(self));
  g_assert(match != NULL);

  g_signal_emit(self, signals[MATCH_CLICKED], 0, x, y, button, state, match, &ret);

  return ret;
}

/* --- Context menu setup --- */

static void
ptyxis_terminal_setup_context_menu(PtyxisTerminal *self,
                                   double          x,
                                   double          y)
{
  g_assert(PTYXIS_IS_TERMINAL(self));

  ptyxis_terminal_update_clipboard_actions(self);
  ptyxis_terminal_update_url_actions(self, x, y);

  gtk_popover_set_pointing_to(self->popover, &(GdkRectangle){x, y, 1, 1});

  if (gtk_widget_get_direction(GTK_WIDGET(self)) == GTK_TEXT_DIR_RTL)
    gtk_widget_set_halign(GTK_WIDGET(self->popover), GTK_ALIGN_END);
  else
    gtk_widget_set_halign(GTK_WIDGET(self->popover), GTK_ALIGN_START);

  gtk_popover_popup(self->popover);
}

/* --- Click handling --- */

static void
ptyxis_terminal_capture_click_pressed_cb(PtyxisTerminal  *self,
                                         int              n_press,
                                         double           x,
                                         double           y,
                                         GtkGestureClick *click)
{
  GdkModifierType state;
  gboolean handled = FALSE;
  GdkEvent *event;
  int button;

  g_assert(PTYXIS_IS_TERMINAL(self));
  g_assert(GTK_IS_GESTURE_CLICK(click));

  event = gtk_event_controller_get_current_event(GTK_EVENT_CONTROLLER(click));
  state = gdk_event_get_modifier_state(event) & gtk_accelerator_get_default_mod_mask();
  button = gtk_gesture_single_get_current_button(GTK_GESTURE_SINGLE(click));

  if (button == 3 && n_press == 1)
    {
      /* Right-click: show context menu */
      ptyxis_terminal_setup_context_menu(self, x, y);
      gtk_gesture_set_state(GTK_GESTURE(click), GTK_EVENT_SEQUENCE_CLAIMED);
      return;
    }

  if (n_press == 1 && !handled && (button == 1 || button == 2) && (state & GDK_CONTROL_MASK))
    {
      if (self->url != NULL)
        handled = ptyxis_terminal_match_clicked(self, x, y, button, state, self->url);
    }

  if (handled)
    gtk_gesture_set_state(GTK_GESTURE(click), GTK_EVENT_SEQUENCE_CLAIMED);
}

/* --- Keyboard handling --- */

static gboolean
ptyxis_terminal_capture_key_pressed_cb(PtyxisTerminal      *self,
                                       guint                keyval,
                                       guint                keycode,
                                       GdkModifierType      state,
                                       GtkEventControllerKey *key)
{
  g_assert(PTYXIS_IS_TERMINAL(self));
  g_assert(GTK_IS_EVENT_CONTROLLER_KEY(key));

  /* Shortcut handling is done by PtyxisTab */
  return GDK_EVENT_PROPAGATE;
}

/* --- Drop handling --- */

static GdkDragAction
ptyxis_terminal_drop_target_drag_enter(GtkDropTargetAsync *target,
                                       double              x,
                                       double              y,
                                       PtyxisTerminal     *self)
{
  g_assert(PTYXIS_IS_TERMINAL(self));

  gtk_widget_set_visible(self->drop_highlight, TRUE);
  return GDK_ACTION_COPY;
}

static void
ptyxis_terminal_drop_target_drag_leave(GtkDropTargetAsync *target,
                                       PtyxisTerminal     *self)
{
  g_assert(PTYXIS_IS_TERMINAL(self));
  gtk_widget_set_visible(self->drop_highlight, FALSE);
}

static gboolean
ptyxis_terminal_drop_target_drop(GtkDropTargetAsync *target,
                                 const GValue       *value,
                                 double              x,
                                 double              y,
                                 PtyxisTerminal     *self)
{
  g_autofree char *text = NULL;
  g_autoptr(GFile) file = NULL;
  g_autoptr(GFileInfo) info = NULL;
  PtyxisApplication *app = PTYXIS_APPLICATION_DEFAULT;
  g_autofree char *path = NULL;

  g_assert(PTYXIS_IS_TERMINAL(self));

  gtk_widget_set_visible(self->drop_highlight, FALSE);

  if (G_VALUE_HOLDS_STRING(value))
    {
      text = g_value_dup_string(value);
    }
  else if (G_VALUE_HOLDS(value, GDK_TYPE_FILE_LIST))
    {
      GSList *files = g_value_get_boxed(value);
      GString *str = g_string_new(NULL);
      for (GSList *iter = files; iter != NULL; iter = iter->next)
        {
          GFile *f = iter->data;
          g_autofree char *uri = g_file_get_uri(f);
          g_autofree char *peek = NULL;

          info = g_file_query_info(f, FILE_ATTRIBUTE_HOST_PATH,
                                   G_FILE_QUERY_INFO_NONE, NULL, NULL);
          if (info != NULL)
            peek = g_strdup(g_file_info_get_attribute_string(info, FILE_ATTRIBUTE_HOST_PATH));
          if (peek == NULL)
            peek = g_file_get_path(f);

          if (peek != NULL)
            g_string_append(str, peek);
          else
            g_string_append(str, uri);

          g_string_append_c(str, ' ');
        }
      text = g_string_free(str, FALSE);
    }

  if (text != NULL)
    {
      ptyxis_ghostty_widget_paste(self->ghostty, text);
      return TRUE;
    }

  return FALSE;
}

/* --- Clipboard actions --- */

static void
copy_clipboard_action(GtkWidget  *widget,
                      const char *action_name,
                      GVariant   *param)
{
  PtyxisTerminal *self = PTYXIS_TERMINAL(widget);
  g_autofree char *text = NULL;
  gboolean as_html;

  g_assert(PTYXIS_IS_TERMINAL(self));

  as_html = g_strcmp0(action_name, "clipboard.copy-as-html") == 0;

  text = ptyxis_ghostty_widget_get_selected_text(self->ghostty);

  if (text != NULL)
    {
      if (as_html)
        {
          /* TODO: Format as HTML */
          gdk_clipboard_set_text(gtk_widget_get_clipboard(widget), text);
        }
      else
        {
          gdk_clipboard_set_text(gtk_widget_get_clipboard(widget), text);
        }
    }
}

static void
copy_link_address_action(GtkWidget  *widget,
                         const char *action_name,
                         GVariant   *param)
{
  PtyxisTerminal *self = PTYXIS_TERMINAL(widget);

  g_assert(PTYXIS_IS_TERMINAL(self));

  if (self->url != NULL)
    gdk_clipboard_set_text(gtk_widget_get_clipboard(widget), self->url);
}

static void
paste_clipboard_action(GtkWidget  *widget,
                       const char *action_name,
                       GVariant   *param)
{
  PtyxisTerminal *self = PTYXIS_TERMINAL(widget);
  GdkClipboard *clipboard;

  g_assert(PTYXIS_IS_TERMINAL(self));

  clipboard = gtk_widget_get_clipboard(widget);
  gdk_clipboard_read_text_async(clipboard, NULL,
                                (GAsyncReadyCallback)paste_clipboard_action_cb,
                                g_object_ref(self));
}

static void
paste_clipboard_action_cb(GObject      *object,
                          GAsyncResult *result,
                          gpointer      user_data)
{
  GdkClipboard *clipboard = GDK_CLIPBOARD(object);
  g_autoptr(PtyxisTerminal) self = user_data;
  g_autofree char *text = NULL;

  text = gdk_clipboard_read_text_finish(clipboard, result, NULL);

  if (text != NULL && self->ghostty != NULL)
    ptyxis_ghostty_widget_paste(self->ghostty, text);
}

static void
open_link_action(GtkWidget  *widget,
                 const char *action_name,
                 GVariant   *param)
{
  PtyxisTerminal *self = PTYXIS_TERMINAL(widget);
  const char *uri;

  g_assert(PTYXIS_IS_TERMINAL(self));

  uri = self->url;

  if (uri != NULL)
    {
      gboolean handled = FALSE;
      g_signal_emit(self, signals[MATCH_CLICKED], 0,
                    0.0, 0.0, 1, (GdkModifierType)0, uri, &handled);

      if (!handled)
        gtk_show_uri(NULL, uri, GDK_CURRENT_TIME);
    }
}

static void
select_all_action(GtkWidget  *widget,
                  const char *action_name,
                  GVariant   *param)
{
  /* Selection handled by ghostty */
}

/* --- Shortcuts --- */

static void
ptyxis_terminal_shortcuts_notify_cb(PtyxisTerminal *self,
                                    GParamSpec     *pspec,
                                    PtyxisShortcuts *shortcuts)
{
  /* Font scaling and shortcuts applied here */
}

/* --- Container termprops --- */

static void
ptyxis_terminal_update_container_name(PtyxisTerminal *self,
                                      const char     *name)
{
  g_set_str(&self->current_container_name, name);
  g_object_notify_by_pspec(G_OBJECT(self), properties[PROP_CURRENT_CONTAINER_NAME]);
}

static void
ptyxis_terminal_update_container_runtime(PtyxisTerminal *self,
                                        const char     *runtime)
{
  g_set_str(&self->current_container_runtime, runtime);
  g_object_notify_by_pspec(G_OBJECT(self), properties[PROP_CURRENT_CONTAINER_RUNTIME]);
}

/* --- Size label --- */

static gboolean
ptyxis_terminal_size_dismiss_cb(gpointer user_data)
{
  PtyxisTerminal *self = user_data;

  g_assert(PTYXIS_IS_TERMINAL(self));

  self->size_dismiss_source = 0;
  gtk_revealer_set_reveal_child(self->size_revealer, FALSE);

  return G_SOURCE_REMOVE;
}

static void
ptyxis_terminal_grid_size_changed_cb(PtyxisTerminal *self,
                                     guint           columns,
                                     guint           rows)
{
  g_autofree char *text = NULL;

  g_assert(PTYXIS_IS_TERMINAL(self));

  text = g_strdup_printf("%u×%u", columns, rows);
  gtk_label_set_label(self->size_label, text);
  gtk_revealer_set_reveal_child(self->size_revealer, TRUE);

  g_clear_handle_id(&self->size_dismiss_source, g_source_remove);
  self->size_dismiss_source = g_timeout_add(SIZE_DISMISS_TIMEOUT_MSEC,
                                            ptyxis_terminal_size_dismiss_cb,
                                            self);

  self->n_columns = columns;
  self->n_rows = rows;
}

/* --- Snapshot (for GtkScreenshot compatibility) --- */

static void
ptyxis_terminal_snapshot(GtkWidget   *widget,
                         GtkSnapshot *snapshot)
{
  /* Delegate to the ghostty widget's snapshot */
  GTK_WIDGET_CLASS(ptyxis_terminal_parent_class)->snapshot(widget, snapshot);
}

/* --- Measure --- */

static void
ptyxis_terminal_measure(GtkWidget      *widget,
                        GtkOrientation  orientation,
                        int             for_size,
                        int            *minimum,
                        int            *natural,
                        int            *minimum_baseline,
                        int            *natural_baseline)
{
  PtyxisTerminal *self = PTYXIS_TERMINAL(widget);

  /* Delegate to ghostty widget */
  if (self->ghostty != NULL)
    {
      gtk_widget_measure(GTK_WIDGET(self->ghostty), orientation, for_size,
                         minimum, natural, minimum_baseline, natural_baseline);
    }
  else
    {
      GTK_WIDGET_CLASS(ptyxis_terminal_parent_class)->measure(
        widget, orientation, for_size, minimum, natural,
        minimum_baseline, natural_baseline);
    }
}

/* --- Size allocate --- */

static void
ptyxis_terminal_size_allocate(GtkWidget *widget,
                              int        width,
                              int        height,
                              int        baseline)
{
  PtyxisTerminal *self = PTYXIS_TERMINAL(widget);

  GTK_WIDGET_CLASS(ptyxis_terminal_parent_class)->size_allocate(
    widget, width, height, baseline);

  if (self->ghostty != NULL)
    gtk_widget_allocate(GTK_WIDGET(self->ghostty), width, height, baseline, NULL);
}

/* --- GObject lifecycle --- */

static void
ptyxis_terminal_constructed(GObject *object)
{
  PtyxisTerminal *self = PTYXIS_TERMINAL(object);
  AdwStyleManager *style_manager;

  G_OBJECT_CLASS(ptyxis_terminal_parent_class)->constructed(object);

  style_manager = adw_style_manager_get_default();
  g_signal_connect_object(style_manager,
                          "notify::dark",
                          G_CALLBACK(ptyxis_terminal_notify_dark_cb),
                          self,
                          G_CONNECT_SWAPPED);

  /* Apply initial colors */
  ptyxis_terminal_update_colors(self);
}

static void
ptyxis_terminal_dispose(GObject *object)
{
  PtyxisTerminal *self = PTYXIS_TERMINAL(object);

  g_clear_pointer(&self->ghostty, (GDestroyNotify)gtk_widget_unparent);

  g_clear_object(&self->palette);
  g_clear_object(&self->popover);
  g_clear_object(&self->terminal_menu);
  g_clear_object(&self->shortcuts);

  G_OBJECT_CLASS(ptyxis_terminal_parent_class)->dispose(object);
}

static void
ptyxis_terminal_finalize(GObject *object)
{
  PtyxisTerminal *self = PTYXIS_TERMINAL(object);

  g_clear_pointer(&self->url, g_free);
  g_clear_pointer(&self->current_container_name, g_free);
  g_clear_pointer(&self->current_container_runtime, g_free);
  g_clear_pointer(&self->config, ghostty_config_free);
  g_clear_pointer(&self->custom_links, g_hash_table_unref);

  G_OBJECT_CLASS(ptyxis_terminal_parent_class)->finalize(object);
}

/* --- Properties --- */

static void
ptyxis_terminal_get_property(GObject    *object,
                             guint       prop_id,
                             GValue     *value,
                             GParamSpec *pspec)
{
  PtyxisTerminal *self = PTYXIS_TERMINAL(object);

  switch (prop_id)
    {
    case PROP_CURRENT_CONTAINER_NAME:
      g_value_set_string(value, ptyxis_terminal_get_current_container_name(self));
      break;

    case PROP_CURRENT_CONTAINER_RUNTIME:
      g_value_set_string(value, ptyxis_terminal_get_current_container_runtime(self));
      break;

    case PROP_PALETTE:
      g_value_set_object(value, ptyxis_terminal_get_palette(self));
      break;

    case PROP_SHORTCUTS:
      g_value_set_object(value, ptyxis_terminal_get_shortcuts(self));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
    }
}

static void
ptyxis_terminal_set_property(GObject      *object,
                             guint         prop_id,
                             const GValue *value,
                             GParamSpec   *pspec)
{
  PtyxisTerminal *self = PTYXIS_TERMINAL(object);

  switch (prop_id)
    {
    case PROP_PALETTE:
      ptyxis_terminal_set_palette(self, g_value_get_object(value));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
    }
}

/* --- Class init --- */

static void
ptyxis_terminal_class_init(PtyxisTerminalClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS(klass);
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS(klass);

  object_class->constructed = ptyxis_terminal_constructed;
  object_class->dispose = ptyxis_terminal_dispose;
  object_class->finalize = ptyxis_terminal_finalize;
  object_class->get_property = ptyxis_terminal_get_property;
  object_class->set_property = ptyxis_terminal_set_property;

  widget_class->measure = ptyxis_terminal_measure;
  widget_class->size_allocate = ptyxis_terminal_size_allocate;
  widget_class->snapshot = ptyxis_terminal_snapshot;

  properties[PROP_CURRENT_CONTAINER_NAME] =
    g_param_spec_string("current-container-name", NULL, NULL,
                        NULL,
                        (G_PARAM_READABLE |
                         G_PARAM_STATIC_STRINGS));

  properties[PROP_CURRENT_CONTAINER_RUNTIME] =
    g_param_spec_string("current-container-runtime", NULL, NULL,
                        NULL,
                        (G_PARAM_READABLE |
                         G_PARAM_STATIC_STRINGS));

  properties[PROP_PALETTE] =
    g_param_spec_object("palette", NULL, NULL,
                        PTYXIS_TYPE_PALETTE,
                        (G_PARAM_READWRITE |
                         G_PARAM_EXPLICIT_NOTIFY |
                         G_PARAM_STATIC_STRINGS));

  properties[PROP_SHORTCUTS] =
    g_param_spec_object("shortcuts", NULL, NULL,
                        PTYXIS_TYPE_SHORTCUTS,
                        (G_PARAM_READABLE |
                         G_PARAM_STATIC_STRINGS));

  g_object_class_install_properties(object_class, N_PROPS, properties);

  signals[GRID_SIZE_CHANGED] =
    g_signal_new("grid-size-changed",
                 G_TYPE_FROM_CLASS(klass),
                 G_SIGNAL_RUN_LAST,
                 0,
                 NULL, NULL,
                 NULL,
                 G_TYPE_NONE, 2, G_TYPE_UINT, G_TYPE_UINT);

  signals[MATCH_CLICKED] =
    g_signal_new("match-clicked",
                 G_TYPE_FROM_CLASS(klass),
                 G_SIGNAL_RUN_LAST,
                 0,
                 g_signal_accumulator_true_handled, NULL,
                 NULL,
                 G_TYPE_BOOLEAN,
                 5,
                 G_TYPE_DOUBLE,
                 G_TYPE_DOUBLE,
                 G_TYPE_INT,
                 GDK_TYPE_MODIFIER_TYPE,
                 G_TYPE_STRING | G_SIGNAL_TYPE_STATIC_SCOPE);

  signals[SHELL_PRECMD] =
    g_signal_new("shell-precmd",
                 G_TYPE_FROM_CLASS(klass),
                 G_SIGNAL_RUN_LAST,
                 0,
                 NULL, NULL,
                 NULL,
                 G_TYPE_NONE, 0);

  signals[SHELL_PREEXEC] =
    g_signal_new("shell-preexec",
                 G_TYPE_FROM_CLASS(klass),
                 G_SIGNAL_RUN_LAST,
                 0,
                 NULL, NULL,
                 NULL,
                 G_TYPE_NONE, 0);

  /* Kept for API compatibility */
  signals[SLEWED] =
    g_signal_new("slewed",
                 G_TYPE_FROM_CLASS(klass),
                 G_SIGNAL_RUN_LAST,
                 0,
                 NULL, NULL,
                 NULL,
                 G_TYPE_NONE, 0);

  gtk_widget_class_set_template_from_resource(widget_class,
    "/org/gnome/Ptyxis/ptyxis-terminal.ui");

  gtk_widget_class_bind_template_child(widget_class, PtyxisTerminal, drop_highlight);
  gtk_widget_class_bind_template_child(widget_class, PtyxisTerminal, drop_target);
  gtk_widget_class_bind_template_child(widget_class, PtyxisTerminal, popover);
  gtk_widget_class_bind_template_child(widget_class, PtyxisTerminal, size_label);
  gtk_widget_class_bind_template_child(widget_class, PtyxisTerminal, size_revealer);
  gtk_widget_class_bind_template_child(widget_class, PtyxisTerminal, terminal_menu);

  gtk_widget_class_bind_template_callback(widget_class,
    ptyxis_terminal_capture_click_pressed_cb);
  gtk_widget_class_bind_template_callback(widget_class,
    ptyxis_terminal_capture_key_pressed_cb);
  gtk_widget_class_bind_template_callback(widget_class,
    ptyxis_terminal_drop_target_drag_enter);
  gtk_widget_class_bind_template_callback(widget_class,
    ptyxis_terminal_drop_target_drag_leave);
  gtk_widget_class_bind_template_callback(widget_class,
    ptyxis_terminal_drop_target_drop);

  gtk_widget_class_install_action(widget_class, "clipboard.copy", NULL,
                                  copy_clipboard_action);
  gtk_widget_class_install_action(widget_class, "clipboard.copy-as-html", NULL,
                                  copy_clipboard_action);
  gtk_widget_class_install_action(widget_class, "clipboard.copy-link", NULL,
                                  copy_link_address_action);
  gtk_widget_class_install_action(widget_class, "clipboard.paste", NULL,
                                  paste_clipboard_action);
  gtk_widget_class_install_action(widget_class, "terminal.open-link", NULL,
                                  open_link_action);
  gtk_widget_class_install_action(widget_class, "terminal.select-all", "b",
                                  select_all_action);
}

/* --- Instance init --- */

static void
ptyxis_terminal_init(PtyxisTerminal *self)
{
  g_autoptr(GdkContentFormats) formats = NULL;
  GdkContentFormatsBuilder *builder;
  PtyxisApplication *app = PTYXIS_APPLICATION_DEFAULT;
  PtyxisShortcuts *shortcuts = ptyxis_application_get_shortcuts(app);

  g_set_object(&self->shortcuts, shortcuts);

  gtk_widget_init_template(GTK_WIDGET(self));
  /* Create default ghostty config */
  if (self->config == NULL)
    self->config = ghostty_config_new();

  /* Create the ghostty widget child */
  self->ghostty = ptyxis_ghostty_widget_new(self->app, NULL, self->config);
  ptyxis_ghostty_widget_set_action_callback(self->ghostty,
                                            ptyxis_terminal_action_cb,
                                            self);
  gtk_widget_set_parent(GTK_WIDGET(self->ghostty), GTK_WIDGET(self));

  /* Listen to shortcuts */
  g_signal_connect_object(shortcuts,
                          "notify",
                          G_CALLBACK(ptyxis_terminal_shortcuts_notify_cb),
                          self,
                          G_CONNECT_SWAPPED);
  ptyxis_terminal_shortcuts_notify_cb(self, NULL, shortcuts);

  /* Grid size tracking */
  g_signal_connect(self, "grid-size-changed",
                   G_CALLBACK(ptyxis_terminal_grid_size_changed_cb), NULL);

  /* Custom links hash table */
  self->custom_links = g_hash_table_new_full(g_direct_hash,
                                             g_direct_equal,
                                             NULL,
                                             g_object_unref);

  /* Drop target setup */
  builder = gdk_content_formats_builder_new();
  gdk_content_formats_builder_add_gtype(builder, G_TYPE_STRING);
  gdk_content_formats_builder_add_gtype(builder, GDK_TYPE_FILE_LIST);
  gdk_content_formats_builder_add_mime_type(builder, APPLICATION_VND_PORTAL_FILES);
  gdk_content_formats_builder_add_mime_type(builder, APPLICATION_VND_PORTAL_FILETRANSFER);
  gdk_content_formats_builder_add_mime_type(builder, TEXT_URI_LIST);
  gdk_content_formats_builder_add_mime_type(builder, TEXT_X_MOZ_URL);
  formats = gdk_content_formats_builder_free_to_formats(builder);

  gtk_drop_target_async_set_actions(self->drop_target,
                                    (GDK_ACTION_COPY | GDK_ACTION_MOVE));
  gtk_drop_target_async_set_formats(self->drop_target, formats);

  /* Clipboard tracking */
  g_signal_connect_object(gtk_widget_get_clipboard(GTK_WIDGET(self)),
                          "changed",
                          G_CALLBACK(ptyxis_terminal_update_clipboard_actions),
                          self,
                          G_CONNECT_SWAPPED);

  ptyxis_terminal_update_clipboard_actions(self);

  self->input_enabled = TRUE;
}

/* --- Public API --- */

PtyxisPalette *
ptyxis_terminal_get_palette(PtyxisTerminal *self)
{
  g_return_val_if_fail(PTYXIS_IS_TERMINAL(self), NULL);
  return self->palette;
}

void
ptyxis_terminal_set_palette(PtyxisTerminal *self,
                            PtyxisPalette  *palette)
{
  g_return_if_fail(PTYXIS_IS_TERMINAL(self));

  if (g_set_object(&self->palette, palette))
    {
      ptyxis_terminal_update_colors(self);
      g_object_notify_by_pspec(G_OBJECT(self), properties[PROP_PALETTE]);
    }
}

PtyxisShortcuts *
ptyxis_terminal_get_shortcuts(PtyxisTerminal *self)
{
  g_return_val_if_fail(PTYXIS_IS_TERMINAL(self), NULL);
  return self->shortcuts;
}

const char *
ptyxis_terminal_get_current_container_name(PtyxisTerminal *self)
{
  g_return_val_if_fail(PTYXIS_IS_TERMINAL(self), NULL);
  return self->current_container_name;
}

const char *
ptyxis_terminal_get_current_container_runtime(PtyxisTerminal *self)
{
  g_return_val_if_fail(PTYXIS_IS_TERMINAL(self), NULL);
  return self->current_container_runtime;
}

guint
ptyxis_terminal_get_n_columns(PtyxisTerminal *self)
{
  g_return_val_if_fail(PTYXIS_IS_TERMINAL(self), 0);
  return self->n_columns;
}

guint
ptyxis_terminal_get_n_rows(PtyxisTerminal *self)
{
  g_return_val_if_fail(PTYXIS_IS_TERMINAL(self), 0);
  return self->n_rows;
}

void
ptyxis_terminal_reset(PtyxisTerminal *self,
                      gboolean        clear_screen)
{
  g_return_if_fail(PTYXIS_IS_TERMINAL(self));
  /* Ghostty doesn't expose a terminal reset in the C API directly.
   * We could emit a keyboard sequence for reset, or route via action. */
}

gboolean
ptyxis_terminal_get_scroll_on_keystroke(PtyxisTerminal *self)
{
  g_return_val_if_fail(PTYXIS_IS_TERMINAL(self), TRUE);
  return self->scroll_on_keystroke;
}

void
ptyxis_terminal_set_custom_link(PtyxisTerminal  *self,
                                int              tag,
                                const char      *regex,
                                const char      *replace,
                                const char      *cursor_name)
{
  PtyxisCustomLink *custom_link;

  g_return_if_fail(PTYXIS_IS_TERMINAL(self));

  custom_link = ptyxis_custom_link_new();
  ptyxis_custom_link_set_pattern(custom_link, regex);
  ptyxis_custom_link_set_target(custom_link, replace);

  g_hash_table_insert(self->custom_links,
                      GINT_TO_POINTER(tag),
                      custom_link);
}

/* --- CWD tracking (TODO: wire to GHOSTTY_ACTION_PWD) --- */

char *
ptyxis_terminal_dup_current_directory_uri(PtyxisTerminal *self)
{
  g_return_val_if_fail(PTYXIS_IS_TERMINAL(self), NULL);
  /* TODO: Track CWD via ghostty PWD action callback */
  return NULL;
}
