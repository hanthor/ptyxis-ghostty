/*
 * ptyxis-compat.c
 *
 * GType registration for VTE compatibility types.
 * VtePty is a real GObject so g_set_object/g_clear_object work on it.
 */

#include "ptyxis-compat.h"
#include <unistd.h>

/* ---- VtePty GObject ---- */

G_DEFINE_TYPE (VtePty, vte_pty, G_TYPE_OBJECT)

static void
vte_pty_finalize (GObject *object)
{
  VtePty *self = VTE_PTY (object);
  if (self->fd >= 0)
    {
      close (self->fd);
      self->fd = -1;
    }
  G_OBJECT_CLASS (vte_pty_parent_class)->finalize (object);
}

static void
vte_pty_class_init (VtePtyClass *klass)
{
  G_OBJECT_CLASS (klass)->finalize = vte_pty_finalize;
}

static void
vte_pty_init (VtePty *self)
{
  self->fd = -1;
}

GType
ptyxis_cursor_blink_mode_get_type(void)
{
  static GType type = 0;
  static const GEnumValue values[] = {
    { PTYXIS_CURSOR_BLINK_SYSTEM, "PTYXIS_CURSOR_BLINK_SYSTEM", "system" },
    { PTYXIS_CURSOR_BLINK_ON,     "PTYXIS_CURSOR_BLINK_ON",     "on" },
    { PTYXIS_CURSOR_BLINK_OFF,    "PTYXIS_CURSOR_BLINK_OFF",    "off" },
    { 0, NULL, NULL },
  };
  if (G_UNLIKELY(type == 0))
    type = g_enum_register_static("PtyxisCursorBlinkMode", values);
  return type;
}

GType
ptyxis_cursor_shape_get_type(void)
{
  static GType type = 0;
  static const GEnumValue values[] = {
    { PTYXIS_CURSOR_SHAPE_BLOCK,     "PTYXIS_CURSOR_SHAPE_BLOCK",     "block" },
    { PTYXIS_CURSOR_SHAPE_IBEAM,     "PTYXIS_CURSOR_SHAPE_IBEAM",     "ibeam" },
    { PTYXIS_CURSOR_SHAPE_UNDERLINE, "PTYXIS_CURSOR_SHAPE_UNDERLINE", "underline" },
    { 0, NULL, NULL },
  };
  if (G_UNLIKELY(type == 0))
    type = g_enum_register_static("PtyxisCursorShape", values);
  return type;
}

GType
ptyxis_erase_binding_get_type(void)
{
  static GType type = 0;
  static const GEnumValue values[] = {
    { PTYXIS_ERASE_AUTO,              "PTYXIS_ERASE_AUTO",              "auto" },
    { PTYXIS_ERASE_ASCII_BACKSPACE,   "PTYXIS_ERASE_ASCII_BACKSPACE",   "ascii-backspace" },
    { PTYXIS_ERASE_ASCII_DELETE,      "PTYXIS_ERASE_ASCII_DELETE",      "ascii-delete" },
    { PTYXIS_ERASE_DELETE_SEQUENCE,   "PTYXIS_ERASE_DELETE_SEQUENCE",   "delete-sequence" },
    { PTYXIS_ERASE_TTY,               "PTYXIS_ERASE_TTY",               "tty" },
    { 0, NULL, NULL },
  };
  if (G_UNLIKELY(type == 0))
    type = g_enum_register_static("PtyxisEraseBinding", values);
  return type;
}

GType
ptyxis_text_blink_mode_get_type(void)
{
  static GType type = 0;
  static const GEnumValue values[] = {
    { PTYXIS_TEXT_BLINK_NEVER,      "PTYXIS_TEXT_BLINK_NEVER",      "never" },
    { PTYXIS_TEXT_BLINK_ALWAYS,     "PTYXIS_TEXT_BLINK_ALWAYS",     "always" },
    { PTYXIS_TEXT_BLINK_FOCUSED,    "PTYXIS_TEXT_BLINK_FOCUSED",    "focused" },
    { PTYXIS_TEXT_BLINK_UNFOCUSED,  "PTYXIS_TEXT_BLINK_UNFOCUSED",  "unfocused" },
    { 0, NULL, NULL },
  };
  if (G_UNLIKELY(type == 0))
    type = g_enum_register_static("PtyxisTextBlinkMode", values);
  return type;
}
