/*
 * ptyxis-compat.h
 *
 * VTE → Ghostty compatibility shims.
 * Included during the transition to stub out or redirect VTE API usage.
 */
#pragma once

#include <pango/pango.h>
#include "ptyxis-terminal.h"

/* Stub for VteRegex references. Ptyxis URL matching uses PCRE2 directly. */
typedef struct _VteRegex { int dummy; } VteRegex;
static inline void vte_regex_free(VteRegex *r) { g_free(r); }
G_DEFINE_AUTOPTR_CLEANUP_FUNC(VteRegex, vte_regex_free)

/* Stub for VtePty - PTY lifecycle managed by ghostty.
 * Defined as an opaque GObject type so g_autoptr works.
 * Stores the raw PTY master FD for container integration. */
#define VTE_TYPE_PTY (0)
typedef struct _VtePty { int fd; } VtePty;
static inline void vte_pty_free(VtePty *p) { if (p) { if (p->fd >= 0) close(p->fd); g_free(p); } }
G_DEFINE_AUTOPTR_CLEANUP_FUNC(VtePty, vte_pty_free)

/* Stub for VteEventContext */
typedef struct _VteEventContext VteEventContext;

/* Stub macros for VTE type checks */
#define VTE_IS_TERMINAL(t) PTYXIS_IS_TERMINAL(t)
#define VTE_TERMINAL(t)    ((void*)(t))  /* No longer needed; just pass through */
#define VTE_TERMINAL_CLASS(k) ((void*)(k))
#define VTE_TYPE_TERMINAL   PTYXIS_TYPE_TERMINAL
#define VTE_IS_PTY(p)       TRUE
/* Replacement enums for VTE cursor/erase/blink types */

GType ptyxis_erase_binding_get_type(void);
#define PTYXIS_TYPE_ERASE_BINDING (ptyxis_erase_binding_get_type())

typedef enum {
  PTYXIS_ERASE_AUTO = 0,
  PTYXIS_ERASE_ASCII_BACKSPACE = 1,
  PTYXIS_ERASE_ASCII_DELETE = 2,
  PTYXIS_ERASE_DELETE_SEQUENCE = 3,
  PTYXIS_ERASE_TTY = 4,
} PtyxisEraseBinding;
#define VteEraseBinding PtyxisEraseBinding
#define VTE_ERASE_AUTO PTYXIS_ERASE_AUTO
#define VTE_TYPE_ERASE_BINDING PTYXIS_TYPE_ERASE_BINDING

GType ptyxis_cursor_blink_mode_get_type(void);
#define PTYXIS_TYPE_CURSOR_BLINK_MODE (ptyxis_cursor_blink_mode_get_type())

typedef enum {
  PTYXIS_CURSOR_BLINK_SYSTEM = 0,
  PTYXIS_CURSOR_BLINK_ON = 1,
  PTYXIS_CURSOR_BLINK_OFF = 2,
} PtyxisCursorBlinkMode;
#define VteCursorBlinkMode PtyxisCursorBlinkMode
#define VTE_CURSOR_BLINK_SYSTEM PTYXIS_CURSOR_BLINK_SYSTEM
#define VTE_TYPE_CURSOR_BLINK_MODE PTYXIS_TYPE_CURSOR_BLINK_MODE

GType ptyxis_cursor_shape_get_type(void);
#define PTYXIS_TYPE_CURSOR_SHAPE (ptyxis_cursor_shape_get_type())

typedef enum {
  PTYXIS_CURSOR_SHAPE_BLOCK = 0,
  PTYXIS_CURSOR_SHAPE_IBEAM = 1,
  PTYXIS_CURSOR_SHAPE_UNDERLINE = 2,
} PtyxisCursorShape;
#define VteCursorShape PtyxisCursorShape
#define VTE_CURSOR_SHAPE_BLOCK PTYXIS_CURSOR_SHAPE_BLOCK
#define VTE_TYPE_CURSOR_SHAPE PTYXIS_TYPE_CURSOR_SHAPE

GType ptyxis_text_blink_mode_get_type(void);
#define PTYXIS_TYPE_TEXT_BLINK_MODE (ptyxis_text_blink_mode_get_type())

typedef enum {
  PTYXIS_TEXT_BLINK_NEVER = 0,
  PTYXIS_TEXT_BLINK_ALWAYS = 1,
  PTYXIS_TEXT_BLINK_FOCUSED = 2,
  PTYXIS_TEXT_BLINK_UNFOCUSED = 3,
} PtyxisTextBlinkMode;
#define VteTextBlinkMode PtyxisTextBlinkMode
#define VTE_TEXT_BLINK_ALWAYS PTYXIS_TEXT_BLINK_ALWAYS
#define VTE_TYPE_TEXT_BLINK_MODE PTYXIS_TYPE_TEXT_BLINK_MODE

/* VTE property IDs - stubs */
#define VTE_PROPERTY_ID_CONTAINER_NAME          1
#define VTE_PROPERTY_ID_CONTAINER_RUNTIME       2
#define VTE_PROPERTY_ID_CURRENT_DIRECTORY_URI    3
#define VTE_PROPERTY_ID_CURRENT_FILE_URI         4
#define VTE_PROPERTY_ID_PROGRESS_HINT            5
#define VTE_PROPERTY_ID_PROGRESS_VALUE           6

/* VTE progress hints */
#define VTE_PROGRESS_HINT_ACTIVE         0
#define VTE_PROGRESS_HINT_ERROR          1
#define VTE_PROGRESS_HINT_INDETERMINATE  2
#define VTE_PROGRESS_HINT_PAUSED         3

/* VTE format enums */
#define VTE_FORMAT_TEXT  0
#define VTE_FORMAT_HTML  1

/* VTE PCRE2 flags - used for URL regex matching */
#define VTE_PCRE2_MULTILINE  PCRE2_MULTILINE
#define VTE_PCRE2_CASELESS   PCRE2_CASELESS
#define VTE_PCRE2_UCP        PCRE2_UCP

/* VTE version (used in inspector) */
#define VTE_MAJOR_VERSION 0
#define VTE_MINOR_VERSION 0
#define VTE_MICRO_VERSION 0
#define VTE_VERSION       0
#define VTE_VERSION_NUMERIC 0

/* VTE termprops (shell integration OSC sequences) */
#define VTE_TERMPROP_SHELL_PRECMD     "shell-precmd"
#define VTE_TERMPROP_SHELL_PREEXEC    "shell-preexec"
#define VTE_TERMPROP_CONTAINER_NAME    "container-name"
#define VTE_TERMPROP_CONTAINER_RUNTIME "container-runtime"

/* Additional missing stubs */
static inline char *ptyxis_terminal_update_custom_links_list(void *t, void *p) { (void)t; (void)p; return NULL; }
static inline char *ptyxis_terminal_dup_current_file_uri(void *t) { (void)t; return NULL; }

static inline gboolean ptyxis_terminal_can_paste(void *t) { (void)t; return FALSE; }
static inline void ptyxis_terminal_paste(void *t) { (void)t; }
static inline void ptyxis_terminal_reset_for_size(void *t) { (void)t; }

/* Stub functions for VTE terminal API */
static inline void
vte_terminal_set_colors(void *t, void *fg, void *bg, void *palette, int n) { (void)t; (void)fg; (void)bg; (void)palette; (void)n; }

static inline void
vte_terminal_set_color_cursor(void *t, void *color) { (void)t; (void)color; }

static inline void
vte_terminal_set_color_cursor_foreground(void *t, void *color) { (void)t; (void)color; }

static inline void
vte_terminal_feed(void *t, const char *data, int len) { (void)t; (void)data; (void)len; }

static inline void
vte_terminal_set_pty(void *t, void *pty) { (void)t; (void)pty; }

static inline void *
vte_terminal_get_pty(void *t) { (void)t; return NULL; }

static inline void
vte_terminal_set_size(void *t, int cols, int rows) { (void)t; (void)cols; (void)rows; }

static inline void
vte_terminal_set_input_enabled(void *t, gboolean enabled) { (void)t; (void)enabled; }

static inline gboolean
vte_terminal_get_input_enabled(void *t) { (void)t; return TRUE; }

static inline void
vte_terminal_set_scrollback_lines(void *t, int lines) { (void)t; (void)lines; }

static inline guint
vte_terminal_get_column_count(void *t) { (void)t; return 80; }

static inline guint
vte_terminal_get_row_count(void *t) { (void)t; return 24; }

static inline gboolean
vte_terminal_get_has_selection(void *t) { (void)t; return FALSE; }

static inline void
vte_terminal_select_all(void *t) { (void)t; }

static inline void
vte_terminal_unselect_all(void *t) { (void)t; }

static inline void
vte_terminal_reset(void *t, gboolean clear, gboolean clear_screen) { (void)t; (void)clear; (void)clear_screen; }

static inline void
vte_terminal_paste_clipboard(void *t) { (void)t; }

static inline char *
vte_terminal_get_window_title(void *t) { (void)t; return NULL; }

static inline void
vte_terminal_set_font_scale(void *t, double scale) { (void)t; (void)scale; }

static inline void
vte_terminal_get_cursor_position(void *t, long *col, long *row) { (void)t; if(col) *col=0; if(row) *row=0; }

static inline void
vte_terminal_set_cell_width_scale(void *t, double scale) { (void)t; (void)scale; }

static inline void
vte_terminal_set_cell_height_scale(void *t, double scale) { (void)t; (void)scale; }

static inline void
vte_terminal_get_color_background_for_draw(void *t, void *color) { (void)t; (void)color; }

static inline gboolean
vte_terminal_get_scroll_on_keystroke(void *t) { (void)t; return TRUE; }

static inline char *
vte_terminal_get_text_selected(void *t, int format) { (void)t; (void)format; return NULL; }

static inline const PangoFontDescription *
vte_terminal_get_font(void *t) { (void)t; return NULL; }

static inline glong
vte_terminal_get_char_width(void *t) { (void)t; return 8; }

static inline glong
vte_terminal_get_char_height(void *t) { (void)t; return 16; }

static inline char *
vte_terminal_get_termprop_string_by_id(void *t, int id, int *len) { (void)t; (void)id; if(len) *len=0; return NULL; }

static inline char *
vte_terminal_get_termprop_string(void *t, const char *prop, int *len) { (void)t; (void)prop; if(len) *len=0; return NULL; }

static inline gboolean
vte_terminal_get_termprop_int_by_id(void *t, int id, gint64 *val) { (void)t; (void)id; if(val) *val=0; return TRUE; }

static inline gboolean
vte_terminal_get_termprop_uint_by_id(void *t, int id, guint64 *val) { (void)t; (void)id; if(val) *val=0; return TRUE; }

static inline char *
vte_terminal_ref_termprop_uri_by_id(void *t, int id) { (void)t; (void)id; return NULL; }

static inline char *
vte_terminal_check_hyperlink_at(void *t, double x, double y) { (void)t; (void)x; (void)y; return NULL; }

static inline char *
vte_terminal_check_match_at(void *t, double x, double y, int *tag) { (void)t; (void)x; (void)y; if(tag) *tag=0; return NULL; }

static inline int
vte_terminal_match_add_regex(void *t, void *regex, int flags) { (void)t; (void)regex; (void)flags; return 0; }

static inline void
vte_terminal_match_remove_all(void *t) { (void)t; }

static inline void
vte_terminal_match_set_cursor_name(void *t, int tag, const char *name) { (void)t; (void)tag; (void)name; }

static inline void
vte_terminal_search_set_regex(void *t, void *regex, int flags) { (void)t; (void)regex; (void)flags; }

static inline void
vte_terminal_search_set_wrap_around(void *t, gboolean wrap) { (void)t; (void)wrap; }

static inline void
vte_terminal_search_find_next(void *t) { (void)t; }

static inline void
vte_terminal_search_find_previous(void *t) { (void)t; }

static inline void
vte_terminal_set_word_char_exceptions(void *t, const char *exceptions) { (void)t; (void)exceptions; }

static inline void
vte_terminal_set_clear_background(void *t, gboolean clear) { (void)t; (void)clear; }

/* VTE Pty stubs — real FD storage for container integration */

static inline void *
vte_pty_new_foreign_sync(int fd, void *cancellable, void **error)
{
  (void)cancellable;
  if (error) *error = NULL;
  if (fd < 0) return NULL;
  VtePty *pty = g_new0(VtePty, 1);
  pty->fd = fd;
  return pty;
}

static inline int
vte_pty_get_fd(void *pty_ptr)
{
  VtePty *pty = (VtePty *)pty_ptr;
  if (pty == NULL) return -1;
  return pty->fd;
}

static inline void
vte_pty_set_utf8(void *pty_ptr, gboolean utf8, void **error)
{
  (void)pty_ptr; (void)utf8;
  if (error) *error = NULL;
}

/* VTE regex stubs */
static inline VteRegex *
vte_regex_new_for_match(const char *pattern, gsize len, guint32 flags, GError **error)
{ (void)pattern; (void)len; (void)flags; if(error) *error=NULL; return NULL; }

static inline VteRegex *
vte_regex_new_for_search(const char *pattern, gsize len, guint32 flags, GError **error)
{ (void)pattern; (void)len; (void)flags; if(error) *error=NULL; return NULL; }

static inline gboolean
vte_regex_jit(VteRegex *regex, guint32 flags, GError **error)
{ (void)regex; (void)flags; if(error) *error=NULL; return TRUE; }

static inline VteRegex *
vte_regex_ref(VteRegex *regex) { return regex; }

static inline void
vte_regex_unref(VteRegex *regex) { g_clear_pointer(&regex, vte_regex_free); }

static inline char *
vte_regex_substitute(VteRegex *regex, const char *subject, const char *replacement, guint32 flags, GError **error)
{ (void)regex; (void)subject; (void)replacement; (void)flags; if(error) *error=NULL; return NULL; }

/* VTE utilities */
static inline const char *
vte_get_features(void) { return "ghostty"; }

static inline int
vte_get_major_version(void) { return 0; }

static inline int
vte_get_minor_version(void) { return 0; }

static inline int
vte_get_micro_version(void) { return 0; }

static inline const char *
vte_sh_path(void) { return "/bin/sh"; }

/* VTE event context */
static inline void
vte_event_context_get_coordinates(const void *ctx, double *x, double *y) { (void)ctx; if(x) *x=0; if(y) *y=0; }
