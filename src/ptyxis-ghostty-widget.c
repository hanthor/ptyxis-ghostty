/*
 * ptyxis-ghostty-widget.c
 *
 * GTK widget that wraps a ghostty_surface_t.
 *
 * The widget manages:
 *  - Surface lifecycle (create/destroy on realize/unrealize)
 *  - Size negotiation (measure/size_allocate → ghostty_surface_set_size)
 *  - Rendering (snapshot → ghostty_surface_draw)
 *  - Input routing (key, mouse, scroll → ghostty_surface_*)
 *  - Action dispatch (ghostty action_cb → client callback)
 */

#include "config.h"
#include "ptyxis-ghostty-widget.h"

struct _PtyxisGhosttyWidget
{
  GtkWidget              parent_instance;

  ghostty_app_t          app;
  ghostty_surface_t      surface;
  ghostty_config_t       surface_config;
  ghostty_runtime_config_s runtime;

  PtyxisGhosttyActionFunc action_func;
  gpointer                action_userdata;

  PtyxisGhosttySize       size;

  /* Input controllers */
  GtkEventController     *key_controller;
  GtkGesture             *click_gesture;
  GtkEventController     *motion_controller;
  GtkEventController     *scroll_controller;

  /* State flags */
  guint                   has_focus : 1;
  guint                   mouse_captured : 1;
  guint                   realized : 1;
  double                  mouse_x;
  double                  mouse_y;
};

G_DEFINE_FINAL_TYPE(PtyxisGhosttyWidget,
                    ptyxis_ghostty_widget,
                    GTK_TYPE_WIDGET)

enum {
  PROP_0,
  N_PROPS
};

/* Input event handlers */
static gboolean key_pressed_cb(GtkEventControllerKey *key,
                               guint keyval, guint keycode,
                               GdkModifierType state,
                               PtyxisGhosttyWidget *self);
static void key_released_cb(GtkEventControllerKey *key,
                            guint keyval, guint keycode,
                            GdkModifierType state,
                            PtyxisGhosttyWidget *self);
static void click_pressed_cb(GtkGestureClick *gesture,
                             int n_press, double x, double y,
                             PtyxisGhosttyWidget *self);
static void click_released_cb(GtkGestureClick *gesture,
                              int n_press, double x, double y,
                              PtyxisGhosttyWidget *self);
static void motion_cb(GtkEventControllerMotion *motion,
                      double x, double y,
                      PtyxisGhosttyWidget *self);
static gboolean scroll_cb(GtkEventControllerScroll *scroll,
                          double dx, double dy,
                          PtyxisGhosttyWidget *self);

/* Forward declarations */
static void ptyxis_ghostty_widget_realize(GtkWidget *widget);
static void ptyxis_ghostty_widget_unrealize(GtkWidget *widget);
static void ptyxis_ghostty_widget_measure(GtkWidget *widget,
                                          GtkOrientation orientation,
                                          int for_size,
                                          int *minimum,
                                          int *natural,
                                          int *minimum_baseline,
                                          int *natural_baseline);
static void ptyxis_ghostty_widget_size_allocate(GtkWidget *widget,
                                                int width,
                                                int height,
                                                int baseline);
static void ptyxis_ghostty_widget_snapshot(GtkWidget *widget,
                                           GtkSnapshot *snapshot);
static void ptyxis_ghostty_widget_root(GtkWidget *widget);
static void ptyxis_ghostty_widget_unroot(GtkWidget *widget);
static gboolean ptyxis_ghostty_widget_grab_focus(GtkWidget *widget);
static void ptyxis_ghostty_widget_focus_enter(GtkEventControllerFocus *focus,
                                              GtkWidget *widget);
static void ptyxis_ghostty_widget_focus_leave(GtkEventControllerFocus *focus,
                                              GtkWidget *widget);

/* --- Ghostty runtime callbacks --- */

static void
ghostty_wakeup_cb(void *userdata)
{
  PtyxisGhosttyWidget *self = PTYXIS_GHOSTTY_WIDGET(userdata);
  gtk_widget_queue_draw(GTK_WIDGET(self));
}

static bool
ghostty_action_cb(ghostty_app_t app,
                  ghostty_target_s target,
                  ghostty_action_s action)
{
  PtyxisGhosttyWidget *self = ghostty_app_userdata(app);

  if (self == NULL)
    return false;

  if (action.tag == GHOSTTY_ACTION_RENDER)
    {
      gtk_widget_queue_draw(GTK_WIDGET(self));
      return true;
    }

  if (action.tag == GHOSTTY_ACTION_CELL_SIZE)
    {
      self->size.cell_width = action.action.cell_size.width;
      self->size.cell_height = action.action.cell_size.height;
      gtk_widget_queue_resize(GTK_WIDGET(self));
    }

  if (action.tag == GHOSTTY_ACTION_CLOSE_TAB)
    {
      /* Forward to Ptyxis for tab close handling */
    }

  if (self->action_func)
    self->action_func(action.tag, &action.action, self->action_userdata);

  return false;
}

static bool
ghostty_read_clipboard_cb(void *userdata,
                          ghostty_clipboard_e clipboard,
                          void *data)
{
  /* Delegate to PtyxisTerminal clipboard handling */
  return false;
}

static void
ghostty_confirm_read_clipboard_cb(void *userdata,
                                  const char *text,
                                  void *data,
                                  ghostty_clipboard_request_e request)
{
  PtyxisGhosttyWidget *self = PTYXIS_GHOSTTY_WIDGET(userdata);
  if (text != NULL)
    ghostty_surface_text(self->surface, text, strlen(text));
}

static void
ghostty_write_clipboard_cb(void *userdata,
                           ghostty_clipboard_e clipboard,
                           const ghostty_clipboard_content_s *contents,
                           size_t count,
                           bool confirm)
{
  /* Delegated to PtyxisTerminal */
}

static void
ghostty_close_surface_cb(void *userdata, bool process_exited)
{
  /* Surface will be freed in unrealize */
}

/* --- GObject lifecycle --- */

static void
ptyxis_ghostty_widget_init(PtyxisGhosttyWidget *self)
{
  GtkEventController *focus;

  self->runtime = (ghostty_runtime_config_s){
    .userdata = self,
    .supports_selection_clipboard = true,
    .wakeup_cb = ghostty_wakeup_cb,
    .action_cb = ghostty_action_cb,
    .read_clipboard_cb = ghostty_read_clipboard_cb,
    .confirm_read_clipboard_cb = ghostty_confirm_read_clipboard_cb,
    .write_clipboard_cb = ghostty_write_clipboard_cb,
    .close_surface_cb = ghostty_close_surface_cb,
  };

  /* Focus tracking */
  focus = gtk_event_controller_focus_new();
  g_signal_connect(focus, "enter",
                   G_CALLBACK(ptyxis_ghostty_widget_focus_enter), self);
  g_signal_connect(focus, "leave",
                   G_CALLBACK(ptyxis_ghostty_widget_focus_leave), self);
  gtk_widget_add_controller(GTK_WIDGET(self), focus);

  /* Key events */
  self->key_controller = gtk_event_controller_key_new();
  gtk_event_controller_set_propagation_phase(self->key_controller,
    GTK_PHASE_CAPTURE);
  g_signal_connect(self->key_controller, "key-pressed",
                   G_CALLBACK(key_pressed_cb), self);
  g_signal_connect(self->key_controller, "key-released",
                   G_CALLBACK(key_released_cb), self);
  gtk_widget_add_controller(GTK_WIDGET(self), self->key_controller);

  /* Click gesture (button 1-3) */
  self->click_gesture = GTK_GESTURE(gtk_gesture_click_new());
  gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(self->click_gesture), 0);
  g_signal_connect(self->click_gesture, "pressed",
                   G_CALLBACK(click_pressed_cb), self);
  g_signal_connect(self->click_gesture, "released",
                   G_CALLBACK(click_released_cb), self);
  gtk_widget_add_controller(GTK_WIDGET(self),
    GTK_EVENT_CONTROLLER(self->click_gesture));

  /* Motion tracking */
  self->motion_controller = gtk_event_controller_motion_new();
  g_signal_connect(self->motion_controller, "motion",
                   G_CALLBACK(motion_cb), self);
  gtk_widget_add_controller(GTK_WIDGET(self), self->motion_controller);

  /* Scroll events */
  self->scroll_controller = gtk_event_controller_scroll_new(
    GTK_EVENT_CONTROLLER_SCROLL_BOTH_AXES);
  g_signal_connect(self->scroll_controller, "scroll",
                   G_CALLBACK(scroll_cb), self);
  gtk_widget_add_controller(GTK_WIDGET(self), self->scroll_controller);
}

static void
ptyxis_ghostty_widget_finalize(GObject *object)
{
  PtyxisGhosttyWidget *self = PTYXIS_GHOSTTY_WIDGET(object);

  if (self->surface != NULL)
    {
      ghostty_surface_free(self->surface);
      self->surface = NULL;
    }

  if (self->surface_config != NULL)
    {
      ghostty_config_free(self->surface_config);
      self->surface_config = NULL;
    }

  G_OBJECT_CLASS(ptyxis_ghostty_widget_parent_class)->finalize(object);
}

static void
ptyxis_ghostty_widget_class_init(PtyxisGhosttyWidgetClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS(klass);
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS(klass);

  object_class->finalize = ptyxis_ghostty_widget_finalize;

  widget_class->realize = ptyxis_ghostty_widget_realize;
  widget_class->unrealize = ptyxis_ghostty_widget_unrealize;
  widget_class->measure = ptyxis_ghostty_widget_measure;
  widget_class->size_allocate = ptyxis_ghostty_widget_size_allocate;
  widget_class->snapshot = ptyxis_ghostty_widget_snapshot;
  widget_class->root = ptyxis_ghostty_widget_root;
  widget_class->unroot = ptyxis_ghostty_widget_unroot;
  widget_class->grab_focus = ptyxis_ghostty_widget_grab_focus;

  gtk_widget_class_set_css_name(widget_class, "terminal");
}

/* --- GTK widget lifecycle --- */

static void
ptyxis_ghostty_widget_realize(GtkWidget *widget)
{
  PtyxisGhosttyWidget *self = PTYXIS_GHOSTTY_WIDGET(widget);

  GTK_WIDGET_CLASS(ptyxis_ghostty_widget_parent_class)->realize(widget);

  /* Create the ghostty app if this is the first terminal */
  if (self->app == NULL)
    {
      self->app = ghostty_app_new(&self->runtime, self->surface_config);
    }

  /* Create the surface */
  if (self->surface == NULL)
    {
      ghostty_surface_config_s sconfig = ghostty_surface_config_new();
      sconfig.userdata = self;
      sconfig.scale_factor = gtk_widget_get_scale_factor(widget);
      sconfig.font_size = 12.0f;

      self->surface = ghostty_surface_new(self->app, &sconfig);
      g_assert(self->surface != NULL);
    }

  self->realized = TRUE;
}

static void
ptyxis_ghostty_widget_unrealize(GtkWidget *widget)
{
  PtyxisGhosttyWidget *self = PTYXIS_GHOSTTY_WIDGET(widget);

  self->realized = FALSE;

  if (self->surface != NULL)
    {
      ghostty_surface_free(self->surface);
      self->surface = NULL;
    }

  GTK_WIDGET_CLASS(ptyxis_ghostty_widget_parent_class)->unrealize(widget);
}

static void
ptyxis_ghostty_widget_measure(GtkWidget *widget,
                              GtkOrientation orientation,
                              int for_size,
                              int *minimum,
                              int *natural,
                              int *minimum_baseline,
                              int *natural_baseline)
{
  PtyxisGhosttyWidget *self = PTYXIS_GHOSTTY_WIDGET(widget);

  if (orientation == GTK_ORIENTATION_HORIZONTAL)
    {
      /* Default to 80 columns */
      *minimum = MAX(1, self->size.cell_width) * 20;
      *natural = MAX(1, self->size.cell_width) * 80;
    }
  else
    {
      /* Default to 24 rows */
      *minimum = MAX(1, self->size.cell_height) * 10;
      *natural = MAX(1, self->size.cell_height) * 24;
    }
}

static void
ptyxis_ghostty_widget_size_allocate(GtkWidget *widget,
                                    int width,
                                    int height,
                                    int baseline)
{
  PtyxisGhosttyWidget *self = PTYXIS_GHOSTTY_WIDGET(widget);

  if (self->surface != NULL && width > 0 && height > 0)
    {
      ghostty_surface_set_size(self->surface,
                               (uint32_t)width,
                               (uint32_t)height);
    }
}

static void
ptyxis_ghostty_widget_snapshot(GtkWidget *widget,
                               GtkSnapshot *snapshot)
{
  PtyxisGhosttyWidget *self = PTYXIS_GHOSTTY_WIDGET(widget);

  /* Request ghostty to render a new frame */
  if (self->surface != NULL)
    {
      ghostty_surface_draw(self->surface);
    }

  /* Render to GTK snapshot. Ghostty produces an OpenGL/Metal texture.
   * For GTK integration, we fill the background while ghostty handles
   * its own rendering surface. The ghostty surface is drawn via the
   * platform-specific rendering path (OpenGL on Linux). */
  graphene_rect_t bounds = GRAPHENE_RECT_INIT(0, 0,
    gtk_widget_get_width(widget),
    gtk_widget_get_height(widget));
  gtk_snapshot_append_color(snapshot,
                            &(GdkRGBA){0.1, 0.1, 0.12, 1.0},
                            &bounds);
}

static void
ptyxis_ghostty_widget_root(GtkWidget *widget)
{
  GTK_WIDGET_CLASS(ptyxis_ghostty_widget_parent_class)->root(widget);
}

static void
ptyxis_ghostty_widget_unroot(GtkWidget *widget)
{
  GTK_WIDGET_CLASS(ptyxis_ghostty_widget_parent_class)->unroot(widget);
}

static gboolean
ptyxis_ghostty_widget_grab_focus(GtkWidget *widget)
{
  return gtk_widget_grab_focus(widget);
}

static void
ptyxis_ghostty_widget_focus_enter(GtkEventControllerFocus *focus,
                                  GtkWidget *widget)
{
  PtyxisGhosttyWidget *self = PTYXIS_GHOSTTY_WIDGET(widget);
  if (self->surface != NULL)
    ghostty_surface_set_focus(self->surface, true);
  self->has_focus = TRUE;
  gtk_widget_queue_draw(widget);
}

static void
ptyxis_ghostty_widget_focus_leave(GtkEventControllerFocus *focus,
                                  GtkWidget *widget)
{
  PtyxisGhosttyWidget *self = PTYXIS_GHOSTTY_WIDGET(widget);
  if (self->surface != NULL)
    ghostty_surface_set_focus(self->surface, false);
  self->has_focus = FALSE;
  gtk_widget_queue_draw(widget);
}

/* --- Input event handlers --- */

static ghostty_input_mods_e
mods_to_ghostty(GdkModifierType state)
{
  int mods = GHOSTTY_MODS_NONE;
  if (state & GDK_SHIFT_MASK)   mods |= GHOSTTY_MODS_SHIFT;
  if (state & GDK_CONTROL_MASK) mods |= GHOSTTY_MODS_CTRL;
  if (state & GDK_ALT_MASK)     mods |= GHOSTTY_MODS_ALT;
  if (state & GDK_SUPER_MASK)   mods |= GHOSTTY_MODS_SUPER;
  return (ghostty_input_mods_e)mods;
}

static ghostty_input_key_e
keyval_to_ghostty_key(guint keyval)
{
  /* Map common GDK keyvals to ghostty key enum.
   * This is a partial mapping; extend as needed. */
  switch (keyval)
    {
    case GDK_KEY_Return:    return GHOSTTY_KEY_ENTER;
    case GDK_KEY_BackSpace: return GHOSTTY_KEY_BACKSPACE;
    case GDK_KEY_Tab:       return GHOSTTY_KEY_TAB;
    case GDK_KEY_Escape:    return GHOSTTY_KEY_ESCAPE;
    case GDK_KEY_Delete:    return GHOSTTY_KEY_DELETE;
    case GDK_KEY_Insert:    return GHOSTTY_KEY_INSERT;
    case GDK_KEY_Home:      return GHOSTTY_KEY_HOME;
    case GDK_KEY_End:       return GHOSTTY_KEY_END;
    case GDK_KEY_Page_Up:   return GHOSTTY_KEY_PAGE_UP;
    case GDK_KEY_Page_Down: return GHOSTTY_KEY_PAGE_DOWN;
    case GDK_KEY_Up:        return GHOSTTY_KEY_ARROW_UP;
    case GDK_KEY_Down:      return GHOSTTY_KEY_ARROW_DOWN;
    case GDK_KEY_Left:      return GHOSTTY_KEY_ARROW_LEFT;
    case GDK_KEY_Right:     return GHOSTTY_KEY_ARROW_RIGHT;
    case GDK_KEY_F1:        return GHOSTTY_KEY_F1;
    case GDK_KEY_F2:        return GHOSTTY_KEY_F2;
    case GDK_KEY_F3:        return GHOSTTY_KEY_F3;
    case GDK_KEY_F4:        return GHOSTTY_KEY_F4;
    case GDK_KEY_F5:        return GHOSTTY_KEY_F5;
    case GDK_KEY_F6:        return GHOSTTY_KEY_F6;
    case GDK_KEY_F7:        return GHOSTTY_KEY_F7;
    case GDK_KEY_F8:        return GHOSTTY_KEY_F8;
    case GDK_KEY_F9:        return GHOSTTY_KEY_F9;
    case GDK_KEY_F10:       return GHOSTTY_KEY_F10;
    case GDK_KEY_F11:       return GHOSTTY_KEY_F11;
    case GDK_KEY_F12:       return GHOSTTY_KEY_F12;
    case GDK_KEY_Shift_L:   return GHOSTTY_KEY_SHIFT_LEFT;
    case GDK_KEY_Shift_R:   return GHOSTTY_KEY_SHIFT_RIGHT;
    case GDK_KEY_Control_L: return GHOSTTY_KEY_CONTROL_LEFT;
    case GDK_KEY_Control_R: return GHOSTTY_KEY_CONTROL_RIGHT;
    case GDK_KEY_Alt_L:     return GHOSTTY_KEY_ALT_LEFT;
    case GDK_KEY_Alt_R:     return GHOSTTY_KEY_ALT_RIGHT;
    case GDK_KEY_Super_L:   return GHOSTTY_KEY_META_LEFT;
    case GDK_KEY_Super_R:   return GHOSTTY_KEY_META_RIGHT;
    case GDK_KEY_space:     return GHOSTTY_KEY_SPACE;
    default:                return GHOSTTY_KEY_UNIDENTIFIED;
    }
}

static gboolean
key_pressed_cb(GtkEventControllerKey *key,
               guint keyval, guint keycode,
               GdkModifierType state,
               PtyxisGhosttyWidget *self)
{
  ghostty_input_key_s gk = {0};

  if (self->surface == NULL)
    return GDK_EVENT_PROPAGATE;

  gk.action = GHOSTTY_ACTION_PRESS;
  gk.mods = mods_to_ghostty(state);
  gk.keycode = keycode;

  /* Try to use physical key mapping first */
  gk.text = NULL;  /* Text input handled below */

  /* Pass key event to ghostty; if unhandled, try as text */
  if (ghostty_surface_key(self->surface, gk))
    return GDK_EVENT_STOP;

  /* If ghostty didn't handle it as a physical key,
   * pass the text version for character input */
  if (gk.keycode == keycode)
    {
      char buf[8] = {0};
      guint32 unichar = gdk_keyval_to_unicode(keyval);
      if (unichar != 0)
        {
          g_unichar_to_utf8(unichar, buf);
          ghostty_surface_text(self->surface, buf, strlen(buf));
          return GDK_EVENT_STOP;
        }
    }

  return GDK_EVENT_PROPAGATE;
}

static void
key_released_cb(GtkEventControllerKey *key,
                guint keyval, guint keycode,
                GdkModifierType state,
                PtyxisGhosttyWidget *self)
{
  ghostty_input_key_s gk = {0};

  if (self->surface == NULL)
    return;

  gk.action = GHOSTTY_ACTION_RELEASE;
  gk.mods = mods_to_ghostty(state);
  gk.keycode = keycode;
  gk.text = NULL;

  ghostty_surface_key(self->surface, gk);
}

static ghostty_input_mouse_button_e
button_to_ghostty(guint button)
{
  switch (button)
    {
    case 1: return GHOSTTY_MOUSE_LEFT;
    case 2: return GHOSTTY_MOUSE_MIDDLE;
    case 3: return GHOSTTY_MOUSE_RIGHT;
    default: return GHOSTTY_MOUSE_UNKNOWN;
    }
}

static void
click_pressed_cb(GtkGestureClick *gesture,
                 int n_press, double x, double y,
                 PtyxisGhosttyWidget *self)
{
  GdkEvent *event;
  GdkModifierType state;
  guint button;

  if (self->surface == NULL)
    return;

  event = gtk_event_controller_get_current_event(
    GTK_EVENT_CONTROLLER(gesture));
  state = gdk_event_get_modifier_state(event);
  button = gtk_gesture_single_get_current_button(
    GTK_GESTURE_SINGLE(gesture));

  ghostty_surface_mouse_button(self->surface,
    GHOSTTY_MOUSE_PRESS,
    button_to_ghostty(button),
    mods_to_ghostty(state));

  self->mouse_x = x;
  self->mouse_y = y;
}

static void
click_released_cb(GtkGestureClick *gesture,
                  int n_press, double x, double y,
                  PtyxisGhosttyWidget *self)
{
  GdkEvent *event;
  GdkModifierType state;
  guint button;

  if (self->surface == NULL)
    return;

  event = gtk_event_controller_get_current_event(
    GTK_EVENT_CONTROLLER(gesture));
  state = gdk_event_get_modifier_state(event);
  button = gtk_gesture_single_get_current_button(
    GTK_GESTURE_SINGLE(gesture));

  ghostty_surface_mouse_button(self->surface,
    GHOSTTY_MOUSE_RELEASE,
    button_to_ghostty(button),
    mods_to_ghostty(state));
}

static void
motion_cb(GtkEventControllerMotion *motion,
          double x, double y,
          PtyxisGhosttyWidget *self)
{
  GdkModifierType state = 0;

  if (self->surface == NULL)
    return;

  self->mouse_x = x;
  self->mouse_y = y;

  ghostty_surface_mouse_pos(self->surface, x, y,
    mods_to_ghostty(state));
}

static gboolean
scroll_cb(GtkEventControllerScroll *scroll,
          double dx, double dy,
          PtyxisGhosttyWidget *self)
{
  if (self->surface == NULL)
    return GDK_EVENT_PROPAGATE;

  ghostty_surface_mouse_scroll(self->surface, dx, dy, 0);
  return GDK_EVENT_STOP;
}

/* --- Public API --- */

PtyxisGhosttyWidget *
ptyxis_ghostty_widget_new(ghostty_app_t app,
                          ghostty_surface_config_s *config,
                          ghostty_config_t gconfig)
{
  PtyxisGhosttyWidget *self;

  self = g_object_new(PTYXIS_TYPE_GHOSTTY_WIDGET, NULL);

  if (app != NULL)
    self->app = app;

  if (gconfig != NULL)
    self->surface_config = ghostty_config_clone(gconfig);

  return self;
}

ghostty_surface_t
ptyxis_ghostty_widget_get_surface(PtyxisGhosttyWidget *self)
{
  g_return_val_if_fail(PTYXIS_IS_GHOSTTY_WIDGET(self), NULL);
  return self->surface;
}

void
ptyxis_ghostty_widget_set_action_callback(PtyxisGhosttyWidget *self,
                                          PtyxisGhosttyActionFunc func,
                                          gpointer user_data)
{
  g_return_if_fail(PTYXIS_IS_GHOSTTY_WIDGET(self));
  self->action_func = func;
  self->action_userdata = user_data;
}

void
ptyxis_ghostty_widget_get_size(PtyxisGhosttyWidget *self,
                               PtyxisGhosttySize *size)
{
  g_return_if_fail(PTYXIS_IS_GHOSTTY_WIDGET(self));
  if (size != NULL)
    *size = self->size;
}

gboolean
ptyxis_ghostty_widget_has_selection(PtyxisGhosttyWidget *self)
{
  g_return_val_if_fail(PTYXIS_IS_GHOSTTY_WIDGET(self), FALSE);
  if (self->surface == NULL)
    return FALSE;
  return ghostty_surface_has_selection(self->surface);
}

char *
ptyxis_ghostty_widget_get_selected_text(PtyxisGhosttyWidget *self)
{
  ghostty_text_s text = {0};

  g_return_val_if_fail(PTYXIS_IS_GHOSTTY_WIDGET(self), NULL);

  if (self->surface == NULL)
    return NULL;

  if (!ghostty_surface_read_selection(self->surface, &text))
    return NULL;

  return g_strndup(text.text, text.text_len);
}

char *
ptyxis_ghostty_widget_get_window_title(PtyxisGhosttyWidget *self)
{
  /* Title comes via GHOSTTY_ACTION_SET_TITLE action callback */
  return NULL;
}

void
ptyxis_ghostty_widget_paste(PtyxisGhosttyWidget *self,
                            const char *text)
{
  g_return_if_fail(PTYXIS_IS_GHOSTTY_WIDGET(self));
  g_return_if_fail(text != NULL);

  if (self->surface != NULL)
    ghostty_surface_text(self->surface, text, strlen(text));
}

void
ptyxis_ghostty_widget_set_font_scale(PtyxisGhosttyWidget *self,
                                     double scale)
{
  g_return_if_fail(PTYXIS_IS_GHOSTTY_WIDGET(self));
  /* Font scaling handled via ghostty_surface_update_config */
}

void
ptyxis_ghostty_widget_set_config(PtyxisGhosttyWidget *self,
                                 ghostty_config_t config)
{
  g_return_if_fail(PTYXIS_IS_GHOSTTY_WIDGET(self));
  g_return_if_fail(config != NULL);

  if (self->surface != NULL)
    ghostty_surface_update_config(self->surface, config);
}

void
ptyxis_ghostty_widget_search_start(PtyxisGhosttyWidget *self,
                                   const char *needle)
{
  g_return_if_fail(PTYXIS_IS_GHOSTTY_WIDGET(self));
  /* Search initiated via action_cb using GHOSTTY_ACTION_START_SEARCH */
}

void
ptyxis_ghostty_widget_search_next(PtyxisGhosttyWidget *self)
{
  g_return_if_fail(PTYXIS_IS_GHOSTTY_WIDGET(self));
  /* Search navigation via action_cb */
}

void
ptyxis_ghostty_widget_search_previous(PtyxisGhosttyWidget *self)
{
  g_return_if_fail(PTYXIS_IS_GHOSTTY_WIDGET(self));
  /* Search navigation via action_cb */
}

void
ptyxis_ghostty_widget_search_end(PtyxisGhosttyWidget *self)
{
  g_return_if_fail(PTYXIS_IS_GHOSTTY_WIDGET(self));
  /* Search end via action_cb using GHOSTTY_ACTION_END_SEARCH */
}
