/*
 * ptyxis-ghostty-widget.c
 *
 * GTK4 terminal widget using libghostty-vt for terminal emulation.
 *
 * Architecture:
 *   - libghostty-vt: VT parsing, terminal state, key/mouse encoding
 *   - forkpty + GLib I/O watch: PTY and child process management
 *   - Cairo + Pango (via GTK snapshot): cell rendering
 */

#include "config.h"
#include "ptyxis-ghostty-widget.h"

#include <errno.h>
#include <pty.h>
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>
#include <glib-unix.h>

#define DEFAULT_COLS         80
#define DEFAULT_ROWS         24
#define DEFAULT_SCROLLBACK   10000
#define DEFAULT_FONT_DESC    "Monospace 11"

/* ---- signals ---- */
enum {
  TITLE_CHANGED,
  BELL,
  CHILD_EXITED,
  GRID_SIZE_CHANGED,
  CONTENTS_CHANGED,
  N_SIGNALS
};

static guint signals[N_SIGNALS];

struct _PtyxisGhosttyWidget
{
  GtkWidget parent_instance;

  /* libghostty-vt */
  GhosttyTerminal    terminal;
  GhosttyRenderState render_state;
  GhosttyKeyEncoder  key_encoder;
  GhosttyMouseEncoder mouse_encoder;

  /* PTY */
  int   pty_fd;
  pid_t child_pid;
  guint pty_watch_id;
  guint child_watch_id;

  /* Rendering */
  PangoFontDescription *font_desc;
  double font_scale;
  int    cell_width;
  int    cell_height;

  /* Terminal dimensions */
  int cols;
  int rows;

  /* Terminal state */
  char    *title;
  gboolean has_focus;

  /* Selection */
  gboolean has_selection;

  /* Input controllers */
  GtkEventController *key_controller;
  GtkGesture         *click_gesture;
  GtkEventController *motion_controller;
  GtkEventController *scroll_controller;
  GtkEventController *focus_controller;

  /* Mouse state */
  double mouse_x;
  double mouse_y;
};

G_DEFINE_FINAL_TYPE(PtyxisGhosttyWidget,
                    ptyxis_ghostty_widget,
                    GTK_TYPE_WIDGET)

/* ---- forward declarations ---- */
static void ptyxis_ghostty_widget_realize   (GtkWidget *widget);
static void ptyxis_ghostty_widget_unrealize (GtkWidget *widget);
static void ptyxis_ghostty_widget_snapshot  (GtkWidget *widget, GtkSnapshot *snapshot);
static void ptyxis_ghostty_widget_measure   (GtkWidget *widget, GtkOrientation orientation,
                                             int for_size, int *minimum, int *natural,
                                             int *minimum_baseline, int *natural_baseline);
static void ptyxis_ghostty_widget_size_allocate(GtkWidget *widget, int width, int height,
                                                int baseline);
static gboolean ptyxis_ghostty_widget_grab_focus(GtkWidget *widget);

static void update_cell_metrics (PtyxisGhosttyWidget *self);
static void resize_terminal     (PtyxisGhosttyWidget *self, int width, int height);
static void start_child         (PtyxisGhosttyWidget *self);

/* ---- terminal effect callbacks ---- */

static void
on_terminal_write_pty(GhosttyTerminal terminal, void *userdata,
                      const uint8_t *data, size_t len)
{
  PtyxisGhosttyWidget *self = PTYXIS_GHOSTTY_WIDGET(userdata);
  (void)terminal;
  if (self->pty_fd < 0)
    return;
  write(self->pty_fd, data, len);
}

static void
on_terminal_bell(GhosttyTerminal terminal, void *userdata)
{
  PtyxisGhosttyWidget *self = PTYXIS_GHOSTTY_WIDGET(userdata);
  (void)terminal;
  g_signal_emit(self, signals[BELL], 0);
}

static void
on_terminal_title_changed(GhosttyTerminal terminal, void *userdata)
{
  PtyxisGhosttyWidget *self = PTYXIS_GHOSTTY_WIDGET(userdata);
  GhosttyString str = {0};
  ghostty_terminal_get(terminal, GHOSTTY_TERMINAL_DATA_TITLE, &str);
  g_free(self->title);
  self->title = str.len > 0 ? g_strndup(str.ptr, str.len) : NULL;
  g_signal_emit(self, signals[TITLE_CHANGED], 0, self->title);
}

/* ---- PTY I/O ---- */

static gboolean
pty_readable_cb(int fd, GIOCondition condition, gpointer user_data)
{
  PtyxisGhosttyWidget *self = PTYXIS_GHOSTTY_WIDGET(user_data);
  uint8_t buf[4096];
  ssize_t n;

  if (condition & (G_IO_HUP | G_IO_ERR))
    {
      self->pty_watch_id = 0;
      return G_SOURCE_REMOVE;
    }

  n = read(fd, buf, sizeof(buf));
  if (n <= 0)
    {
      self->pty_watch_id = 0;
      return G_SOURCE_REMOVE;
    }

  ghostty_terminal_vt_write(self->terminal, buf, (size_t)n);
  ghostty_render_state_update(self->render_state, self->terminal);

  /* Sync key encoder modes after terminal state changes */
  ghostty_key_encoder_setopt_from_terminal(self->key_encoder, self->terminal);
  ghostty_mouse_encoder_setopt_from_terminal(self->mouse_encoder, self->terminal);

  g_signal_emit(self, signals[CONTENTS_CHANGED], 0);
  gtk_widget_queue_draw(GTK_WIDGET(self));
  return G_SOURCE_CONTINUE;
}

static void
child_watch_cb(GPid pid, gint status, gpointer user_data)
{
  PtyxisGhosttyWidget *self = PTYXIS_GHOSTTY_WIDGET(user_data);
  self->child_watch_id = 0;
  self->child_pid = -1;
  g_signal_emit(self, signals[CHILD_EXITED], 0, status);
}

/* ---- cell metrics ---- */

static void
update_cell_metrics(PtyxisGhosttyWidget *self)
{
  PangoLayout *layout;
  PangoFontDescription *desc;
  int w, h;

  layout = gtk_widget_create_pango_layout(GTK_WIDGET(self), "M");
  desc = pango_font_description_copy(self->font_desc);
  pango_font_description_set_size(desc,
    pango_font_description_get_size(self->font_desc) * self->font_scale);
  pango_layout_set_font_description(layout, desc);
  pango_layout_get_pixel_size(layout, &w, &h);
  pango_font_description_free(desc);
  g_object_unref(layout);

  self->cell_width  = MAX(1, w);
  self->cell_height = MAX(1, h);
}

/* ---- resize ---- */

static void
resize_terminal(PtyxisGhosttyWidget *self, int width, int height)
{
  int new_cols, new_rows;

  if (self->cell_width <= 0 || self->cell_height <= 0)
    return;

  new_cols = MAX(1, width  / self->cell_width);
  new_rows = MAX(1, height / self->cell_height);

  if (new_cols == self->cols && new_rows == self->rows)
    return;

  self->cols = new_cols;
  self->rows = new_rows;

  ghostty_terminal_resize(self->terminal,
                          (uint16_t)new_cols, (uint16_t)new_rows,
                          (uint32_t)self->cell_width,
                          (uint32_t)self->cell_height);

  if (self->pty_fd >= 0)
    {
      struct winsize ws = {
        .ws_row = (unsigned short)new_rows,
        .ws_col = (unsigned short)new_cols,
        .ws_xpixel = (unsigned short)(new_cols * self->cell_width),
        .ws_ypixel = (unsigned short)(new_rows * self->cell_height),
      };
      ioctl(self->pty_fd, TIOCSWINSZ, &ws);
    }

  g_signal_emit(self, signals[GRID_SIZE_CHANGED], 0,
                (guint)new_cols, (guint)new_rows,
                (guint)self->cell_width, (guint)self->cell_height);
}

/* ---- child launch ---- */

static void
start_child(PtyxisGhosttyWidget *self)
{
  int master_fd = -1, slave_fd = -1;
  struct winsize ws;
  pid_t pid;
  const char *shell;
  char *argv[2];

  ws.ws_col    = (unsigned short)self->cols;
  ws.ws_row    = (unsigned short)self->rows;
  ws.ws_xpixel = (unsigned short)(self->cols * self->cell_width);
  ws.ws_ypixel = (unsigned short)(self->rows * self->cell_height);

  if (openpty(&master_fd, &slave_fd, NULL, NULL, &ws) < 0)
    {
      g_warning("openpty failed: %s", g_strerror(errno));
      return;
    }

  pid = fork();
  if (pid < 0)
    {
      g_warning("fork failed: %s", g_strerror(errno));
      close(master_fd);
      close(slave_fd);
      return;
    }

  if (pid == 0)
    {
      /* child */
      setsid();
      if (ioctl(slave_fd, TIOCSCTTY, 0) < 0)
        _exit(1);
      close(master_fd);
      dup2(slave_fd, STDIN_FILENO);
      dup2(slave_fd, STDOUT_FILENO);
      dup2(slave_fd, STDERR_FILENO);
      if (slave_fd > STDERR_FILENO)
        close(slave_fd);

      shell = g_getenv("SHELL");
      if (shell == NULL || shell[0] == '\0')
        shell = "/bin/bash";

      argv[0] = (char *)shell;
      argv[1] = NULL;
      execvp(shell, argv);
      _exit(127);
    }

  /* parent */
  close(slave_fd);
  self->pty_fd    = master_fd;
  self->child_pid = pid;

  self->pty_watch_id = g_unix_fd_add(master_fd, G_IO_IN | G_IO_HUP | G_IO_ERR,
                                      pty_readable_cb, self);
  self->child_watch_id = g_child_watch_add(pid, child_watch_cb, self);
}

/* ---- GtkWidget virtuals ---- */

static void
ptyxis_ghostty_widget_realize(GtkWidget *widget)
{
  PtyxisGhosttyWidget *self = PTYXIS_GHOSTTY_WIDGET(widget);

  GTK_WIDGET_CLASS(ptyxis_ghostty_widget_parent_class)->realize(widget);

  update_cell_metrics(self);

  self->cols = DEFAULT_COLS;
  self->rows = DEFAULT_ROWS;

  ghostty_terminal_resize(self->terminal,
                          (uint16_t)self->cols, (uint16_t)self->rows,
                          (uint32_t)self->cell_width,
                          (uint32_t)self->cell_height);

  start_child(self);
}

static void
ptyxis_ghostty_widget_unrealize(GtkWidget *widget)
{
  PtyxisGhosttyWidget *self = PTYXIS_GHOSTTY_WIDGET(widget);

  if (self->pty_watch_id != 0)
    {
      g_source_remove(self->pty_watch_id);
      self->pty_watch_id = 0;
    }

  if (self->child_watch_id != 0)
    {
      g_source_remove(self->child_watch_id);
      self->child_watch_id = 0;
    }

  if (self->pty_fd >= 0)
    {
      close(self->pty_fd);
      self->pty_fd = -1;
    }

  if (self->child_pid > 0)
    {
      kill(self->child_pid, SIGTERM);
      self->child_pid = -1;
    }

  GTK_WIDGET_CLASS(ptyxis_ghostty_widget_parent_class)->unrealize(widget);
}

/* ---- Rendering ---- */

static GdkRGBA
color_rgb_to_rgba(GhosttyColorRgb c)
{
  return (GdkRGBA){
    .red   = c.r / 255.0f,
    .green = c.g / 255.0f,
    .blue  = c.b / 255.0f,
    .alpha = 1.0f,
  };
}

static void
ptyxis_ghostty_widget_snapshot(GtkWidget *widget, GtkSnapshot *snapshot)
{
  PtyxisGhosttyWidget *self = PTYXIS_GHOSTTY_WIDGET(widget);
  int width  = gtk_widget_get_width(widget);
  int height = gtk_widget_get_height(widget);

  if (self->cell_width <= 0 || self->cell_height <= 0)
    return;

  GhosttyRenderStateColors colors = GHOSTTY_INIT_SIZED(GhosttyRenderStateColors);
  ghostty_render_state_colors_get(self->render_state, &colors);

  GdkRGBA bg_default = color_rgb_to_rgba(colors.background);

  /* Full-widget background as a fast color node */
  gtk_snapshot_append_color(snapshot, &bg_default,
    &GRAPHENE_RECT_INIT(0, 0, width, height));

  /* Single Cairo node for ALL cell drawing: backgrounds, text, cursor.
   * Everything must go into Cairo — appending GTK color nodes after this
   * node would overdraw the text. */
  graphene_rect_t full_bounds = GRAPHENE_RECT_INIT(0, 0, width, height);
  cairo_t *cr = gtk_snapshot_append_cairo(snapshot, &full_bounds);

  PangoLayout *layout = pango_cairo_create_layout(cr);
  PangoFontDescription *fdesc = pango_font_description_copy(self->font_desc);
  pango_font_description_set_size(fdesc,
    pango_font_description_get_size(self->font_desc) * self->font_scale);
  pango_layout_set_font_description(layout, fdesc);
  pango_font_description_free(fdesc);

  /* Get font metrics for vertical baseline offset */
  PangoFontMetrics *metrics = pango_context_get_metrics(
    pango_layout_get_context(layout), fdesc, NULL);
  int ascent_px = PANGO_PIXELS(pango_font_metrics_get_ascent(metrics));
  pango_font_metrics_unref(metrics);

  GhosttyRenderStateRowIterator row_iter = NULL;
  ghostty_render_state_row_iterator_new(NULL, &row_iter);
  ghostty_render_state_get(self->render_state,
                           GHOSTTY_RENDER_STATE_DATA_ROW_ITERATOR,
                           &row_iter);

  GhosttyRenderStateRowCells cells = NULL;
  ghostty_render_state_row_cells_new(NULL, &cells);

  int row_y = 0;

  while (ghostty_render_state_row_iterator_next(row_iter))
    {
      int cell_top = row_y * self->cell_height;

      ghostty_render_state_row_get(row_iter,
                                   GHOSTTY_RENDER_STATE_ROW_DATA_CELLS,
                                   &cells);

      int col_x = 0;

      while (ghostty_render_state_row_cells_next(cells))
        {
          int cell_left = col_x * self->cell_width;

          GhosttyStyle style = GHOSTTY_INIT_SIZED(GhosttyStyle);
          ghostty_render_state_row_cells_get(cells,
                                             GHOSTTY_RENDER_STATE_ROW_CELLS_DATA_STYLE,
                                             &style);

          GhosttyColorRgb fg_c, bg_c;
          ghostty_render_state_row_cells_get(cells,
                                             GHOSTTY_RENDER_STATE_ROW_CELLS_DATA_FG_COLOR,
                                             &fg_c);
          ghostty_render_state_row_cells_get(cells,
                                             GHOSTTY_RENDER_STATE_ROW_CELLS_DATA_BG_COLOR,
                                             &bg_c);

          GdkRGBA fg = color_rgb_to_rgba(fg_c);
          GdkRGBA bg = color_rgb_to_rgba(bg_c);

          /* Cell background in Cairo (same layer as text, correct z-order) */
          if (bg.red   != bg_default.red   ||
              bg.green != bg_default.green ||
              bg.blue  != bg_default.blue)
            {
              cairo_set_source_rgba(cr, bg.red, bg.green, bg.blue, bg.alpha);
              cairo_rectangle(cr, cell_left, cell_top,
                              self->cell_width, self->cell_height);
              cairo_fill(cr);
            }

          uint32_t grapheme_len = 0;
          ghostty_render_state_row_cells_get(cells,
                                             GHOSTTY_RENDER_STATE_ROW_CELLS_DATA_GRAPHEMES_LEN,
                                             &grapheme_len);

          if (grapheme_len > 0)
            {
              uint8_t utf8_buf[32] = {0};
              GhosttyBuffer gbuf = { .ptr = utf8_buf, .cap = sizeof(utf8_buf) - 1, .len = 0 };
              ghostty_render_state_row_cells_get(cells,
                                                 GHOSTTY_RENDER_STATE_ROW_CELLS_DATA_GRAPHEMES_UTF8,
                                                 &gbuf);
              utf8_buf[gbuf.len] = '\0';

              PangoAttrList *attrs = pango_attr_list_new();

              if (style.bold)
                pango_attr_list_insert(attrs,
                  pango_attr_weight_new(PANGO_WEIGHT_BOLD));
              if (style.italic)
                pango_attr_list_insert(attrs,
                  pango_attr_style_new(PANGO_STYLE_ITALIC));
              if (style.underline != GHOSTTY_SGR_UNDERLINE_NONE)
                pango_attr_list_insert(attrs,
                  pango_attr_underline_new(PANGO_UNDERLINE_SINGLE));
              if (style.strikethrough)
                pango_attr_list_insert(attrs,
                  pango_attr_strikethrough_new(TRUE));

              pango_layout_set_attributes(layout, attrs);
              pango_attr_list_unref(attrs);
              pango_layout_set_text(layout, (const char *)utf8_buf, -1);

              /* Position text so the baseline sits correctly within the cell */
              cairo_move_to(cr, cell_left, cell_top + ascent_px - PANGO_PIXELS(pango_layout_get_baseline(layout)));
              cairo_set_source_rgba(cr, fg.red, fg.green, fg.blue, fg.alpha);
              pango_cairo_show_layout(cr, layout);
            }

          col_x++;
        }

      row_y++;
    }

  /* Cursor — drawn in Cairo so it layers correctly over text */
  bool cursor_visible = false;
  ghostty_render_state_get(self->render_state,
                           GHOSTTY_RENDER_STATE_DATA_CURSOR_VISIBLE,
                           &cursor_visible);
  bool cursor_in_viewport = false;
  ghostty_render_state_get(self->render_state,
                           GHOSTTY_RENDER_STATE_DATA_CURSOR_VIEWPORT_HAS_VALUE,
                           &cursor_in_viewport);

  if (cursor_visible && cursor_in_viewport)
    {
      uint16_t cx = 0, cy = 0;
      ghostty_render_state_get(self->render_state,
                               GHOSTTY_RENDER_STATE_DATA_CURSOR_VIEWPORT_X, &cx);
      ghostty_render_state_get(self->render_state,
                               GHOSTTY_RENDER_STATE_DATA_CURSOR_VIEWPORT_Y, &cy);

      GdkRGBA cursor_color = {1.0, 1.0, 1.0, 0.85};
      if (colors.cursor_has_value)
        cursor_color = color_rgb_to_rgba(colors.cursor);

      if (self->has_focus)
        {
          /* Filled block cursor */
          cairo_set_source_rgba(cr,
            cursor_color.red, cursor_color.green,
            cursor_color.blue, cursor_color.alpha);
          cairo_rectangle(cr,
            cx * self->cell_width, cy * self->cell_height,
            self->cell_width, self->cell_height);
          cairo_fill(cr);
        }
      else
        {
          /* Hollow cursor when unfocused */
          cairo_set_source_rgba(cr,
            cursor_color.red, cursor_color.green,
            cursor_color.blue, 0.6);
          cairo_set_line_width(cr, 1.0);
          cairo_rectangle(cr,
            cx * self->cell_width + 0.5, cy * self->cell_height + 0.5,
            self->cell_width - 1.0, self->cell_height - 1.0);
          cairo_stroke(cr);
        }
    }

  GhosttyRenderStateDirty clean = GHOSTTY_RENDER_STATE_DIRTY_FALSE;
  ghostty_render_state_set(self->render_state,
                           GHOSTTY_RENDER_STATE_OPTION_DIRTY, &clean);

  ghostty_render_state_row_cells_free(cells);
  ghostty_render_state_row_iterator_free(row_iter);

  g_object_unref(layout);
  cairo_destroy(cr);
}

static void
ptyxis_ghostty_widget_measure(GtkWidget      *widget,
                               GtkOrientation  orientation,
                               int             for_size,
                               int            *minimum,
                               int            *natural,
                               int            *minimum_baseline,
                               int            *natural_baseline)
{
  PtyxisGhosttyWidget *self = PTYXIS_GHOSTTY_WIDGET(widget);

  if (orientation == GTK_ORIENTATION_HORIZONTAL)
    *minimum = *natural = self->cell_width  * DEFAULT_COLS;
  else
    *minimum = *natural = self->cell_height * DEFAULT_ROWS;

  *minimum_baseline = *natural_baseline = -1;
}

static void
ptyxis_ghostty_widget_size_allocate(GtkWidget *widget,
                                     int        width,
                                     int        height,
                                     int        baseline)
{
  PtyxisGhosttyWidget *self = PTYXIS_GHOSTTY_WIDGET(widget);

  GTK_WIDGET_CLASS(ptyxis_ghostty_widget_parent_class)
    ->size_allocate(widget, width, height, baseline);

  if (self->terminal != NULL)
    resize_terminal(self, width, height);
}

static gboolean
ptyxis_ghostty_widget_grab_focus(GtkWidget *widget)
{
  PtyxisGhosttyWidget *self = PTYXIS_GHOSTTY_WIDGET(widget);
  self->has_focus = TRUE;
  gtk_widget_queue_draw(widget);
  return GTK_WIDGET_CLASS(ptyxis_ghostty_widget_parent_class)->grab_focus(widget);
}

/* ---- Input ---- */

static GhosttyMods
gdk_mods_to_ghostty(GdkModifierType state)
{
  GhosttyMods mods = 0;
  if (state & GDK_SHIFT_MASK)   mods |= GHOSTTY_MODS_SHIFT;
  if (state & GDK_CONTROL_MASK) mods |= GHOSTTY_MODS_CTRL;
  if (state & GDK_ALT_MASK)     mods |= GHOSTTY_MODS_ALT;
  if (state & GDK_SUPER_MASK)   mods |= GHOSTTY_MODS_SUPER;
  if (state & GDK_LOCK_MASK)    mods |= GHOSTTY_MODS_CAPS_LOCK;
  return mods;
}

static GhosttyKey
keyval_to_ghostty_key(guint keyval)
{
  switch (keyval)
    {
    case GDK_KEY_Return:        return GHOSTTY_KEY_ENTER;
    case GDK_KEY_KP_Enter:      return GHOSTTY_KEY_NUMPAD_ENTER;
    case GDK_KEY_BackSpace:     return GHOSTTY_KEY_BACKSPACE;
    case GDK_KEY_Tab:
    case GDK_KEY_ISO_Left_Tab:  return GHOSTTY_KEY_TAB;
    case GDK_KEY_Escape:        return GHOSTTY_KEY_ESCAPE;
    case GDK_KEY_Delete:        return GHOSTTY_KEY_DELETE;
    case GDK_KEY_Insert:        return GHOSTTY_KEY_INSERT;
    case GDK_KEY_Home:          return GHOSTTY_KEY_HOME;
    case GDK_KEY_End:           return GHOSTTY_KEY_END;
    case GDK_KEY_Page_Up:       return GHOSTTY_KEY_PAGE_UP;
    case GDK_KEY_Page_Down:     return GHOSTTY_KEY_PAGE_DOWN;
    case GDK_KEY_Up:            return GHOSTTY_KEY_ARROW_UP;
    case GDK_KEY_Down:          return GHOSTTY_KEY_ARROW_DOWN;
    case GDK_KEY_Left:          return GHOSTTY_KEY_ARROW_LEFT;
    case GDK_KEY_Right:         return GHOSTTY_KEY_ARROW_RIGHT;
    case GDK_KEY_F1:            return GHOSTTY_KEY_F1;
    case GDK_KEY_F2:            return GHOSTTY_KEY_F2;
    case GDK_KEY_F3:            return GHOSTTY_KEY_F3;
    case GDK_KEY_F4:            return GHOSTTY_KEY_F4;
    case GDK_KEY_F5:            return GHOSTTY_KEY_F5;
    case GDK_KEY_F6:            return GHOSTTY_KEY_F6;
    case GDK_KEY_F7:            return GHOSTTY_KEY_F7;
    case GDK_KEY_F8:            return GHOSTTY_KEY_F8;
    case GDK_KEY_F9:            return GHOSTTY_KEY_F9;
    case GDK_KEY_F10:           return GHOSTTY_KEY_F10;
    case GDK_KEY_F11:           return GHOSTTY_KEY_F11;
    case GDK_KEY_F12:           return GHOSTTY_KEY_F12;
    case GDK_KEY_Shift_L:       return GHOSTTY_KEY_SHIFT_LEFT;
    case GDK_KEY_Shift_R:       return GHOSTTY_KEY_SHIFT_RIGHT;
    case GDK_KEY_Control_L:     return GHOSTTY_KEY_CONTROL_LEFT;
    case GDK_KEY_Control_R:     return GHOSTTY_KEY_CONTROL_RIGHT;
    case GDK_KEY_Alt_L:         return GHOSTTY_KEY_ALT_LEFT;
    case GDK_KEY_Alt_R:         return GHOSTTY_KEY_ALT_RIGHT;
    case GDK_KEY_Super_L:       return GHOSTTY_KEY_META_LEFT;
    case GDK_KEY_Super_R:       return GHOSTTY_KEY_META_RIGHT;
    case GDK_KEY_space:         return GHOSTTY_KEY_SPACE;
    case GDK_KEY_a: case GDK_KEY_A: return GHOSTTY_KEY_A;
    case GDK_KEY_b: case GDK_KEY_B: return GHOSTTY_KEY_B;
    case GDK_KEY_c: case GDK_KEY_C: return GHOSTTY_KEY_C;
    case GDK_KEY_d: case GDK_KEY_D: return GHOSTTY_KEY_D;
    case GDK_KEY_e: case GDK_KEY_E: return GHOSTTY_KEY_E;
    case GDK_KEY_f: case GDK_KEY_F: return GHOSTTY_KEY_F;
    case GDK_KEY_g: case GDK_KEY_G: return GHOSTTY_KEY_G;
    case GDK_KEY_h: case GDK_KEY_H: return GHOSTTY_KEY_H;
    case GDK_KEY_i: case GDK_KEY_I: return GHOSTTY_KEY_I;
    case GDK_KEY_j: case GDK_KEY_J: return GHOSTTY_KEY_J;
    case GDK_KEY_k: case GDK_KEY_K: return GHOSTTY_KEY_K;
    case GDK_KEY_l: case GDK_KEY_L: return GHOSTTY_KEY_L;
    case GDK_KEY_m: case GDK_KEY_M: return GHOSTTY_KEY_M;
    case GDK_KEY_n: case GDK_KEY_N: return GHOSTTY_KEY_N;
    case GDK_KEY_o: case GDK_KEY_O: return GHOSTTY_KEY_O;
    case GDK_KEY_p: case GDK_KEY_P: return GHOSTTY_KEY_P;
    case GDK_KEY_q: case GDK_KEY_Q: return GHOSTTY_KEY_Q;
    case GDK_KEY_r: case GDK_KEY_R: return GHOSTTY_KEY_R;
    case GDK_KEY_s: case GDK_KEY_S: return GHOSTTY_KEY_S;
    case GDK_KEY_t: case GDK_KEY_T: return GHOSTTY_KEY_T;
    case GDK_KEY_u: case GDK_KEY_U: return GHOSTTY_KEY_U;
    case GDK_KEY_v: case GDK_KEY_V: return GHOSTTY_KEY_V;
    case GDK_KEY_w: case GDK_KEY_W: return GHOSTTY_KEY_W;
    case GDK_KEY_x: case GDK_KEY_X: return GHOSTTY_KEY_X;
    case GDK_KEY_y: case GDK_KEY_Y: return GHOSTTY_KEY_Y;
    case GDK_KEY_z: case GDK_KEY_Z: return GHOSTTY_KEY_Z;
    case GDK_KEY_0:                 return GHOSTTY_KEY_DIGIT_0;
    case GDK_KEY_1:                 return GHOSTTY_KEY_DIGIT_1;
    case GDK_KEY_2:                 return GHOSTTY_KEY_DIGIT_2;
    case GDK_KEY_3:                 return GHOSTTY_KEY_DIGIT_3;
    case GDK_KEY_4:                 return GHOSTTY_KEY_DIGIT_4;
    case GDK_KEY_5:                 return GHOSTTY_KEY_DIGIT_5;
    case GDK_KEY_6:                 return GHOSTTY_KEY_DIGIT_6;
    case GDK_KEY_7:                 return GHOSTTY_KEY_DIGIT_7;
    case GDK_KEY_8:                 return GHOSTTY_KEY_DIGIT_8;
    case GDK_KEY_9:                 return GHOSTTY_KEY_DIGIT_9;
    case GDK_KEY_minus:             return GHOSTTY_KEY_MINUS;
    case GDK_KEY_equal:             return GHOSTTY_KEY_EQUAL;
    case GDK_KEY_bracketleft:       return GHOSTTY_KEY_BRACKET_LEFT;
    case GDK_KEY_bracketright:      return GHOSTTY_KEY_BRACKET_RIGHT;
    case GDK_KEY_backslash:         return GHOSTTY_KEY_BACKSLASH;
    case GDK_KEY_semicolon:         return GHOSTTY_KEY_SEMICOLON;
    case GDK_KEY_apostrophe:        return GHOSTTY_KEY_QUOTE;
    case GDK_KEY_grave:             return GHOSTTY_KEY_BACKQUOTE;
    case GDK_KEY_comma:             return GHOSTTY_KEY_COMMA;
    case GDK_KEY_period:            return GHOSTTY_KEY_PERIOD;
    case GDK_KEY_slash:             return GHOSTTY_KEY_SLASH;
    default:                        return GHOSTTY_KEY_UNIDENTIFIED;
    }
}

static void
encode_and_write_key(PtyxisGhosttyWidget *self,
                     GhosttyKeyAction     action,
                     guint                keyval,
                     GdkModifierType      state,
                     const char          *utf8_text)
{
  GhosttyKeyEvent key_event = NULL;
  char buf[128];
  size_t written = 0;

  if (self->pty_fd < 0)
    return;

  ghostty_key_event_new(NULL, &key_event);
  ghostty_key_event_set_action(key_event, action);
  ghostty_key_event_set_key(key_event, keyval_to_ghostty_key(keyval));
  ghostty_key_event_set_mods(key_event, gdk_mods_to_ghostty(state));

  if (utf8_text && utf8_text[0] != '\0')
    ghostty_key_event_set_utf8(key_event, utf8_text, strlen(utf8_text));

  ghostty_key_encoder_encode(self->key_encoder, key_event,
                              buf, sizeof(buf), &written);

  if (written > 0)
    write(self->pty_fd, buf, written);

  ghostty_key_event_free(key_event);
}

static gboolean
key_pressed_cb(GtkEventControllerKey *key,
               guint                  keyval,
               guint                  keycode,
               GdkModifierType        state,
               PtyxisGhosttyWidget   *self)
{
  (void)keycode;
  char utf8[8] = {0};
  guint32 unichar;

  /* Get the Unicode character for this key press, excluding control chars */
  unichar = gdk_keyval_to_unicode(keyval);
  if (unichar >= 0x20 && unichar != 0x7f)
    g_unichar_to_utf8(unichar, utf8);

  encode_and_write_key(self, GHOSTTY_KEY_ACTION_PRESS, keyval, state, utf8);
  return GDK_EVENT_STOP;
}

static void
key_released_cb(GtkEventControllerKey *key,
                guint                  keyval,
                guint                  keycode,
                GdkModifierType        state,
                PtyxisGhosttyWidget   *self)
{
  (void)keycode;
  encode_and_write_key(self, GHOSTTY_KEY_ACTION_RELEASE, keyval, state, NULL);
}

static void
focus_enter_cb(GtkEventControllerFocus *focus,
               PtyxisGhosttyWidget     *self)
{
  (void)focus;
  self->has_focus = TRUE;
  gtk_widget_queue_draw(GTK_WIDGET(self));
}

static void
focus_leave_cb(GtkEventControllerFocus *focus,
               PtyxisGhosttyWidget     *self)
{
  (void)focus;
  self->has_focus = FALSE;
  gtk_widget_queue_draw(GTK_WIDGET(self));
}

static void
click_pressed_cb(GtkGestureClick     *gesture,
                 int                  n_press,
                 double               x,
                 double               y,
                 PtyxisGhosttyWidget *self)
{
  (void)n_press;
  GhosttyMouseEvent mevent = NULL;
  GdkEvent *gdk_event;
  GdkModifierType state;
  guint button;
  char buf[64];
  size_t written = 0;

  if (self->pty_fd < 0)
    return;

  gdk_event = gtk_event_controller_get_current_event(GTK_EVENT_CONTROLLER(gesture));
  state     = gdk_event_get_modifier_state(gdk_event);
  button    = gtk_gesture_single_get_current_button(GTK_GESTURE_SINGLE(gesture));

  ghostty_mouse_event_new(NULL, &mevent);
  ghostty_mouse_event_set_action(mevent, GHOSTTY_MOUSE_ACTION_PRESS);
  ghostty_mouse_event_set_button(mevent, (GhosttyMouseButton)button);
  ghostty_mouse_event_set_mods(mevent, gdk_mods_to_ghostty(state));
  ghostty_mouse_event_set_position(mevent, (GhosttyMousePosition){ .x = (float)x, .y = (float)y });

  ghostty_mouse_encoder_encode(self->mouse_encoder, mevent,
                                buf, sizeof(buf), &written);
  if (written > 0)
    write(self->pty_fd, buf, written);

  ghostty_mouse_event_free(mevent);

  self->mouse_x = x;
  self->mouse_y = y;
}

static void
click_released_cb(GtkGestureClick     *gesture,
                  int                  n_press,
                  double               x,
                  double               y,
                  PtyxisGhosttyWidget *self)
{
  (void)n_press;
  GhosttyMouseEvent mevent = NULL;
  GdkEvent *gdk_event;
  GdkModifierType state;
  guint button;
  char buf[64];
  size_t written = 0;

  if (self->pty_fd < 0)
    return;

  gdk_event = gtk_event_controller_get_current_event(GTK_EVENT_CONTROLLER(gesture));
  state     = gdk_event_get_modifier_state(gdk_event);
  button    = gtk_gesture_single_get_current_button(GTK_GESTURE_SINGLE(gesture));

  ghostty_mouse_event_new(NULL, &mevent);
  ghostty_mouse_event_set_action(mevent, GHOSTTY_MOUSE_ACTION_RELEASE);
  ghostty_mouse_event_set_button(mevent, (GhosttyMouseButton)button);
  ghostty_mouse_event_set_mods(mevent, gdk_mods_to_ghostty(state));
  ghostty_mouse_event_set_position(mevent, (GhosttyMousePosition){ .x = (float)x, .y = (float)y });

  ghostty_mouse_encoder_encode(self->mouse_encoder, mevent,
                                buf, sizeof(buf), &written);
  if (written > 0)
    write(self->pty_fd, buf, written);

  ghostty_mouse_event_free(mevent);
}

static void
motion_cb(GtkEventControllerMotion *motion,
          double                    x,
          double                    y,
          PtyxisGhosttyWidget      *self)
{
  (void)motion;
  self->mouse_x = x;
  self->mouse_y = y;
}

static gboolean
scroll_cb(GtkEventControllerScroll *scroll,
          double                    dx,
          double                    dy,
          PtyxisGhosttyWidget      *self)
{
  (void)scroll;
  GhosttyMouseEvent mevent = NULL;
  char buf[64];
  size_t written = 0;

  if (self->pty_fd < 0)
    return GDK_EVENT_PROPAGATE;

  /* Map scroll to synthetic button 4 (up) / 5 (down) press events */
  GhosttyMouseButton scroll_button = dy < 0 ? GHOSTTY_MOUSE_BUTTON_FOUR
                                             : GHOSTTY_MOUSE_BUTTON_FIVE;

  ghostty_mouse_event_new(NULL, &mevent);
  ghostty_mouse_event_set_action(mevent, GHOSTTY_MOUSE_ACTION_PRESS);
  ghostty_mouse_event_set_button(mevent, scroll_button);
  ghostty_mouse_event_set_position(mevent,
    (GhosttyMousePosition){ .x = (float)self->mouse_x, .y = (float)self->mouse_y });

  ghostty_mouse_encoder_encode(self->mouse_encoder, mevent,
                                buf, sizeof(buf), &written);
  if (written > 0)
    write(self->pty_fd, buf, written);

  ghostty_mouse_event_free(mevent);
  return GDK_EVENT_STOP;
}

/* ---- GObject lifecycle ---- */

static void
ptyxis_ghostty_widget_init(PtyxisGhosttyWidget *self)
{
  GhosttyTerminalOptions opts = {
    .cols           = DEFAULT_COLS,
    .rows           = DEFAULT_ROWS,
    .max_scrollback = DEFAULT_SCROLLBACK,
  };

  self->pty_fd    = -1;
  self->child_pid = -1;
  self->font_scale = 1.0;
  self->cols = DEFAULT_COLS;
  self->rows = DEFAULT_ROWS;

  /* Default font */
  self->font_desc = pango_font_description_from_string(DEFAULT_FONT_DESC);

  /* Terminal */
  ghostty_terminal_new(NULL, &self->terminal, opts);

  /* Register effect callbacks */
  ghostty_terminal_set(self->terminal, GHOSTTY_TERMINAL_OPT_USERDATA, self);
  ghostty_terminal_set(self->terminal, GHOSTTY_TERMINAL_OPT_WRITE_PTY,
                       on_terminal_write_pty);
  ghostty_terminal_set(self->terminal, GHOSTTY_TERMINAL_OPT_BELL,
                       on_terminal_bell);
  ghostty_terminal_set(self->terminal, GHOSTTY_TERMINAL_OPT_TITLE_CHANGED,
                       on_terminal_title_changed);

  /* Render state */
  ghostty_render_state_new(NULL, &self->render_state);

  /* Key encoder */
  ghostty_key_encoder_new(NULL, &self->key_encoder);

  /* Mouse encoder */
  ghostty_mouse_encoder_new(NULL, &self->mouse_encoder);

  /* Focus tracking */
  self->focus_controller = gtk_event_controller_focus_new();
  g_signal_connect(self->focus_controller, "enter",
                   G_CALLBACK(focus_enter_cb), self);
  g_signal_connect(self->focus_controller, "leave",
                   G_CALLBACK(focus_leave_cb), self);
  gtk_widget_add_controller(GTK_WIDGET(self), self->focus_controller);

  /* Key events */
  self->key_controller = gtk_event_controller_key_new();
  gtk_event_controller_set_propagation_phase(self->key_controller,
                                              GTK_PHASE_CAPTURE);
  g_signal_connect(self->key_controller, "key-pressed",
                   G_CALLBACK(key_pressed_cb), self);
  g_signal_connect(self->key_controller, "key-released",
                   G_CALLBACK(key_released_cb), self);
  gtk_widget_add_controller(GTK_WIDGET(self), self->key_controller);

  /* Click gesture */
  self->click_gesture = GTK_GESTURE(gtk_gesture_click_new());
  gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(self->click_gesture), 0);
  g_signal_connect(self->click_gesture, "pressed",
                   G_CALLBACK(click_pressed_cb), self);
  g_signal_connect(self->click_gesture, "released",
                   G_CALLBACK(click_released_cb), self);
  gtk_widget_add_controller(GTK_WIDGET(self),
                            GTK_EVENT_CONTROLLER(self->click_gesture));

  /* Motion */
  self->motion_controller = gtk_event_controller_motion_new();
  g_signal_connect(self->motion_controller, "motion",
                   G_CALLBACK(motion_cb), self);
  gtk_widget_add_controller(GTK_WIDGET(self), self->motion_controller);

  /* Scroll */
  self->scroll_controller = gtk_event_controller_scroll_new(
    GTK_EVENT_CONTROLLER_SCROLL_BOTH_AXES);
  g_signal_connect(self->scroll_controller, "scroll",
                   G_CALLBACK(scroll_cb), self);
  gtk_widget_add_controller(GTK_WIDGET(self), self->scroll_controller);

  gtk_widget_set_focusable(GTK_WIDGET(self), TRUE);
}

static void
ptyxis_ghostty_widget_dispose(GObject *object)
{
  PtyxisGhosttyWidget *self = PTYXIS_GHOSTTY_WIDGET(object);

  if (self->pty_watch_id != 0)
    {
      g_source_remove(self->pty_watch_id);
      self->pty_watch_id = 0;
    }

  if (self->child_watch_id != 0)
    {
      g_source_remove(self->child_watch_id);
      self->child_watch_id = 0;
    }

  if (self->pty_fd >= 0)
    {
      close(self->pty_fd);
      self->pty_fd = -1;
    }

  if (self->child_pid > 0)
    {
      kill(self->child_pid, SIGTERM);
      self->child_pid = -1;
    }

  G_OBJECT_CLASS(ptyxis_ghostty_widget_parent_class)->dispose(object);
}

static void
ptyxis_ghostty_widget_finalize(GObject *object)
{
  PtyxisGhosttyWidget *self = PTYXIS_GHOSTTY_WIDGET(object);

  g_clear_pointer(&self->title,     g_free);
  g_clear_pointer(&self->font_desc, pango_font_description_free);

  if (self->key_encoder != NULL)
    ghostty_key_encoder_free(self->key_encoder);

  if (self->mouse_encoder != NULL)
    ghostty_mouse_encoder_free(self->mouse_encoder);

  if (self->render_state != NULL)
    ghostty_render_state_free(self->render_state);

  if (self->terminal != NULL)
    ghostty_terminal_free(self->terminal);

  G_OBJECT_CLASS(ptyxis_ghostty_widget_parent_class)->finalize(object);
}

static void
ptyxis_ghostty_widget_class_init(PtyxisGhosttyWidgetClass *klass)
{
  GObjectClass   *object_class = G_OBJECT_CLASS(klass);
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS(klass);

  object_class->dispose  = ptyxis_ghostty_widget_dispose;
  object_class->finalize = ptyxis_ghostty_widget_finalize;

  widget_class->realize       = ptyxis_ghostty_widget_realize;
  widget_class->unrealize     = ptyxis_ghostty_widget_unrealize;
  widget_class->snapshot      = ptyxis_ghostty_widget_snapshot;
  widget_class->measure       = ptyxis_ghostty_widget_measure;
  widget_class->size_allocate = ptyxis_ghostty_widget_size_allocate;
  widget_class->grab_focus    = ptyxis_ghostty_widget_grab_focus;

  gtk_widget_class_set_css_name(widget_class, "terminal");

  signals[TITLE_CHANGED] = g_signal_new("title-changed",
    G_TYPE_FROM_CLASS(klass), G_SIGNAL_RUN_LAST,
    0, NULL, NULL, NULL, G_TYPE_NONE, 1, G_TYPE_STRING);

  signals[BELL] = g_signal_new("bell",
    G_TYPE_FROM_CLASS(klass), G_SIGNAL_RUN_LAST,
    0, NULL, NULL, NULL, G_TYPE_NONE, 0);

  signals[CHILD_EXITED] = g_signal_new("child-exited",
    G_TYPE_FROM_CLASS(klass), G_SIGNAL_RUN_LAST,
    0, NULL, NULL, NULL, G_TYPE_NONE, 1, G_TYPE_INT);

  signals[GRID_SIZE_CHANGED] = g_signal_new("grid-size-changed",
    G_TYPE_FROM_CLASS(klass), G_SIGNAL_RUN_LAST,
    0, NULL, NULL, NULL, G_TYPE_NONE, 4,
    G_TYPE_UINT, G_TYPE_UINT, G_TYPE_UINT, G_TYPE_UINT);

  signals[CONTENTS_CHANGED] = g_signal_new("contents-changed",
    G_TYPE_FROM_CLASS(klass), G_SIGNAL_RUN_LAST,
    0, NULL, NULL, NULL, G_TYPE_NONE, 0);
}

/* ---- Public API ---- */

PtyxisGhosttyWidget *
ptyxis_ghostty_widget_new(void)
{
  return g_object_new(PTYXIS_TYPE_GHOSTTY_WIDGET, NULL);
}

void
ptyxis_ghostty_widget_get_size(PtyxisGhosttyWidget *self,
                                PtyxisGhosttySize   *size)
{
  g_return_if_fail(PTYXIS_IS_GHOSTTY_WIDGET(self));
  if (size == NULL)
    return;
  size->columns     = (guint)self->cols;
  size->rows        = (guint)self->rows;
  size->cell_width  = (guint)self->cell_width;
  size->cell_height = (guint)self->cell_height;
}

gboolean
ptyxis_ghostty_widget_has_selection(PtyxisGhosttyWidget *self)
{
  GhosttySelection sel = GHOSTTY_INIT_SIZED(GhosttySelection);
  GhosttyResult result;

  g_return_val_if_fail(PTYXIS_IS_GHOSTTY_WIDGET(self), FALSE);

  result = ghostty_terminal_get(self->terminal,
                                 GHOSTTY_TERMINAL_DATA_SELECTION, &sel);
  return (result == GHOSTTY_SUCCESS);
}

char *
ptyxis_ghostty_widget_get_selected_text(PtyxisGhosttyWidget *self)
{
  g_return_val_if_fail(PTYXIS_IS_GHOSTTY_WIDGET(self), NULL);
  /* TODO: implement via ghostty_terminal formatter */
  return NULL;
}

char *
ptyxis_ghostty_widget_get_window_title(PtyxisGhosttyWidget *self)
{
  g_return_val_if_fail(PTYXIS_IS_GHOSTTY_WIDGET(self), NULL);
  return g_strdup(self->title);
}

void
ptyxis_ghostty_widget_paste(PtyxisGhosttyWidget *self,
                             const char          *text)
{
  g_return_if_fail(PTYXIS_IS_GHOSTTY_WIDGET(self));
  g_return_if_fail(text != NULL);

  if (self->pty_fd >= 0)
    write(self->pty_fd, text, strlen(text));
}

void
ptyxis_ghostty_widget_set_font_scale(PtyxisGhosttyWidget *self,
                                      double               scale)
{
  g_return_if_fail(PTYXIS_IS_GHOSTTY_WIDGET(self));

  self->font_scale = MAX(0.25, scale);
  update_cell_metrics(self);

  if (gtk_widget_get_realized(GTK_WIDGET(self)))
    {
      resize_terminal(self,
                      gtk_widget_get_width(GTK_WIDGET(self)),
                      gtk_widget_get_height(GTK_WIDGET(self)));
      gtk_widget_queue_draw(GTK_WIDGET(self));
    }
}

void
ptyxis_ghostty_widget_search_start(PtyxisGhosttyWidget *self,
                                    const char          *needle)
{
  (void)needle;
  g_return_if_fail(PTYXIS_IS_GHOSTTY_WIDGET(self));
  /* TODO: implement search via formatter + text scan */
}

void
ptyxis_ghostty_widget_search_next(PtyxisGhosttyWidget *self)
{
  g_return_if_fail(PTYXIS_IS_GHOSTTY_WIDGET(self));
}

void
ptyxis_ghostty_widget_search_previous(PtyxisGhosttyWidget *self)
{
  g_return_if_fail(PTYXIS_IS_GHOSTTY_WIDGET(self));
}

void
ptyxis_ghostty_widget_search_end(PtyxisGhosttyWidget *self)
{
  g_return_if_fail(PTYXIS_IS_GHOSTTY_WIDGET(self));
}
