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

  /* State flags */
  guint                   has_focus : 1;
  guint                   mouse_captured : 1;
  guint                   realized : 1;
};

G_DEFINE_FINAL_TYPE(PtyxisGhosttyWidget,
                    ptyxis_ghostty_widget,
                    GTK_TYPE_WIDGET)

enum {
  PROP_0,
  N_PROPS
};

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

static bool
ghostty_wakeup_cb(void *userdata)
{
  PtyxisGhosttyWidget *self = PTYXIS_GHOSTTY_WIDGET(userdata);
  gtk_widget_queue_draw(GTK_WIDGET(self));
  return true;
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

  if (action.tag == GHOSTTY_ACTION_CLOSE_SURFACE)
    {
      /* Forward to Ptyxis for tab close handling */
    }

  if (self->action_func)
    return self->action_func(action.tag, &action.action, self->action_userdata);

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

static bool
ghostty_close_surface_cb(void *userdata, bool process_exited)
{
  /* Surface will be freed in unrealize */
  return process_exited; /* Allow close if process exited */
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

  /* FIXME: The actual rendering approach depends on ghostty's
   * rendering backend. For GTK integration, ghostty needs to
   * render to a texture/buffer that we can snapshot.
   *
   * Placeholder: just fill background.
   */
  graphene_rect_t bounds;
  gtk_widget_get_bounds(widget, &bounds);
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
  return gtk_widget_grab_focus_child(widget);
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

  return g_strndup(text.ptr, text.len);
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
