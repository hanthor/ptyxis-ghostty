# ptyxis-ghostty: Remaining Features Implementation Plan

## Corrected Status Table

Several items listed as ❌ in status.md are **already working**:

| Feature | True Status | Notes |
|---|---|---|
| Scrollback navigation | ✅ Done | `ptyxis-terminal.c` polls `GHOSTTY_TERMINAL_DATA_SCROLLBAR` |
| CWD tracking (OSC 7) | ✅ Done | Polled in `pty_readable_cb`; `CWD_CHANGED` signal emitted |
| Input read-only mode | ✅ Done | `self->input_enabled` gates all key/mouse/scroll handlers |
| `has_selection` / `get_selected_text` | ✅ Done | VT selection API fully wired |
| Mouse-driven text selection | ❌ Missing | Click handlers only encode mouse reports; never set a selection |
| Text search | ❌ Missing | All stubs empty; find bar still calls dead VTE APIs |
| Shell integration (precmd/preexec) | ❌ N/A | Only in `ghostty_surface_*` apprt model; not available in VT widget |
| `vte-terminal` CSS selectors | ⚠️ Bug | Widget CSS name is `terminal`; size indicator never styled correctly |

---

## Phase 1 — CSS Selector Fix

**Files**: `src/style.css`, `src/ptyxis-window-dressing.c`

Replace every `vte-terminal` CSS selector with `terminal` (the widget's registered CSS name).

---

## Phase 2 — Mouse-Driven Text Selection

**Files**: `src/ptyxis-ghostty-widget.c`

Use ghostty's `GhosttySelectionGesture` API from `ghostty/vt/selection.h`.

### New struct fields
```c
GhosttySelectionGesture     sel_gesture;
GhosttySelectionGestureEvent sel_press_event;
GhosttySelectionGestureEvent sel_drag_event;
GhosttySelectionGestureEvent sel_release_event;
gboolean                    sel_active;     /* dragging a selection */
```

### Pixel → grid ref helper
```c
static gboolean
pixel_to_grid_ref (PtyxisGhosttyWidget *self, double x, double y, GhosttyGridRef *out_ref)
{
  GhosttyPoint point;
  int col = (int)x / self->cell_width;
  int row = (int)y / self->cell_height;
  col = CLAMP(col, 0, self->cols - 1);
  row = CLAMP(row, 0, self->rows - 1);
  point.tag = GHOSTTY_POINT_TAG_VIEWPORT;
  point.value.coordinate.x = (uint16_t)col;
  point.value.coordinate.y = (uint32_t)row;
  return ghostty_terminal_grid_ref(self->terminal, point, out_ref) == GHOSTTY_SUCCESS;
}
```

### click_pressed_cb changes
- Left button, no mouse tracking mode active → run selection gesture PRESS event
- Set the resulting `GhosttySelection` as the terminal's active selection
- Otherwise → existing mouse-report path

### motion_cb changes
- If `sel_active` (left button held, selection in progress) → run DRAG event, update selection

### click_released_cb changes
- If `sel_active` → run RELEASE event, auto-copy to PRIMARY clipboard
- Clear `sel_active`

### PRIMARY clipboard copy
```c
GdkDisplay *display = gtk_widget_get_display(GTK_WIDGET(self));
GdkClipboard *primary = gdk_display_get_primary_clipboard(display);
char *text = ptyxis_ghostty_widget_get_selected_text(self);
if (text) { gdk_clipboard_set_text(primary, text); g_free(text); }
```

### Key press clears selection
When a non-modifier character key is typed to the terminal, clear the selection:
```c
ghostty_terminal_set(self->terminal, GHOSTTY_TERMINAL_OPT_SELECTION, NULL);
```

---

## Phase 3 — Text Search

**Files**: `src/ptyxis-ghostty-widget.c`, `src/ptyxis-terminal.c`, `src/ptyxis-terminal.h`, `src/ptyxis-find-bar.c`

### 3a. Widget search engine

No native search API exists in libghostty-vt. Strategy:
1. `ghostty_terminal_select_all()` → get a GhosttySelection covering all content
2. `ghostty_terminal_selection_format_alloc()` with PLAIN format → full text dump
3. GLib `g_regex_find()` or `strstr()` → find all UTF-8 byte offsets
4. Build a line-offset table from the dump to map byte offsets → (col, row) in viewport coords
5. `ghostty_terminal_grid_ref(VIEWPORT)` → GhosttyGridRef for start and end of match
6. Install match via `ghostty_terminal_set(GHOSTTY_TERMINAL_OPT_SELECTION, &sel)` + scroll to match

### New struct fields
```c
char    *search_needle;
GArray  *search_matches;   /* array of GhosttyPointCoordinate pairs (start_col, start_row) */
int      search_current;   /* index into search_matches */
guint    search_flags;     /* PCRE2 flags from find bar */
```

### 3b. PtyxisTerminal search bridge

Add to `ptyxis-terminal.h`:
```c
void ptyxis_terminal_search_find_next    (PtyxisTerminal *self);
void ptyxis_terminal_search_find_previous(PtyxisTerminal *self);
void ptyxis_terminal_search_set_regex    (PtyxisTerminal *self, const char *regex, guint flags);
void ptyxis_terminal_search_set_wrap_around(PtyxisTerminal *self, gboolean wrap);
```

These delegate to `ptyxis_ghostty_widget_search_*` on the inner widget.

### 3c. Find bar de-VTE-ification

Replace:
```c
vte_terminal_search_find_next(VTE_TERMINAL(self->terminal));
vte_terminal_search_set_regex(VTE_TERMINAL(self->terminal), regex, 0);
vte_terminal_search_set_wrap_around(VTE_TERMINAL(self->terminal), TRUE);
```
With:
```c
ptyxis_terminal_search_find_next(self->terminal);
ptyxis_terminal_search_set_regex(self->terminal, query, flags);
ptyxis_terminal_search_set_wrap_around(self->terminal, TRUE);
```

Remove `VteRegex` dependency from find bar.

---

## Phase 4 — Update status.md

Correct the feature table to reflect actual state after this implementation.

---

## Verification

```sh
cd /var/home/james/dev/ptyxis-ghostty-build
just build   # zero warnings required
```

Manual tests:
1. Click and drag in terminal → text highlighted
2. Double-click → word selected; triple-click → line selected
3. Middle-click elsewhere → pastes selected terminal text (PRIMARY clipboard)
4. Ctrl+Shift+F → search bar opens; type word → matches highlighted; next/prev cycles
5. Resize overlay appears correctly styled over the terminal
