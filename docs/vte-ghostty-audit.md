# VTE → Ghostty API Audit

*Step 2 deliverable: documented inventory of every VTE type, function, signal, and property Ptyxis references, mapped to libghostty equivalents.*

---

## 1. Architecture Overview

**Ptyxis** inherits from VteTerminal (GObject):
```
PtyxisTerminal : VteTerminal : GtkWidget
```

**Ghostty** uses opaque C types with callback dispatch:
```
ghostty_app_t → ghostty_surface_t (renders to embedder-provided surface)
```

**Key implication**: PtyxisTerminal cannot simply inherit from ghostty_surface_t. We need a GTK widget wrapper (`PtyxisGhosttyWidget`) that owns a ghostty_surface_t and manages rendering, input, and size synchronization with the GTK widget hierarchy.

---

## 2. VTE Types Used by Ptyxis

| VTE Type | Files | Purpose | Ghostty Equivalent |
|---|---|---|---|
| `VteTerminal` | terminal.c (base class), tab.c, window.c, client.c, inspector.c, find-bar.c, custom-link.c, application.c | The terminal widget superclass | `ghostty_surface_t` — managed *by* a new GTK widget, not *as* the widget |
| `VtePty` | client.c, tab.c, application.c, session.c | PTY lifecycle (create, get fd, set utf8) | Internal to ghostty; managed via `ghostty_surface_config_s.command` and `ghostty_surface_tty_name()` |
| `VteRegex` | terminal.c (static init) | URL pattern matching and search regex | Not present in C API; Ptyxis manages URL matching itself via PCRE2; search uses `ghostty_action_start_search_s` |
| `VteEventContext` | terminal.c (setup_context_menu vfunc) | Context menu position coordinates | `ghostty_surface_mouse_pos()` tracks position; context menu triggered via action callback |

---

## 3. VTE Functions → Ghostty Mapping

### 3.1 Terminal Emulation & State

| VTE Function | Ghostty Equivalent | Notes |
|---|---|---|
| `vte_terminal_reset(terminal, clear, clear_screen)` | NOT YET EXPOSED | Terminal reset may need to be exposed or achieved via action dispatch |
| `vte_terminal_feed(terminal, data, len)` | `ghostty_surface_text(surf, data, len)` | Direct equivalent |
| `vte_terminal_feed_child(terminal, data, len)` | Same as above | Ghostty feeds text to the child process automatically |

### 3.2 Sizing & Grid

| VTE Function | Ghostty Equivalent | Notes |
|---|---|---|
| `vte_terminal_get_column_count(t)` | `ghostty_surface_size(surf).columns` | Via surface size query |
| `vte_terminal_get_row_count(t)` | `ghostty_surface_size(surf).rows` | Via surface size query |
| `vte_terminal_set_size(t, cols, rows)` | `ghostty_surface_set_size(surf, width_px, height_px)` | Pixel-based in Ghostty, not cell-based |
| `vte_terminal_get_char_width(t)` | `ghostty_surface_size(surf).cell_width_px` | Via surface size query |
| `vte_terminal_get_char_height(t)` | `ghostty_surface_size(surf).cell_height_px` | Via surface size query |
| `vte_terminal_set_cell_width_scale(t, scale)` | No direct equivalent | Font size managed via config |
| `vte_terminal_set_cell_height_scale(t, scale)` | No direct equivalent | Font size managed via config |
| `vte_terminal_set_font_scale(t, scale)` | No direct equivalent | Font size managed via config |

### 3.3 Cursor

| VTE Function | Ghostty Equivalent | Notes |
|---|---|---|
| `vte_terminal_get_cursor_position(t, &col, &row)` | NOT YET EXPOSED in C API | May need action-based query |

### 3.4 Colors & Appearance

| VTE Function | Ghostty Equivalent | Notes |
|---|---|---|
| `vte_terminal_set_colors(t, fg, bg, palette, n)` | `ghostty_config_palette_s` via `ghostty_surface_update_config()` | Config-based, not per-surface call |
| `vte_terminal_set_color_cursor(t, &color)` | `ghostty_action_color_change_s` with `GHOSTTY_ACTION_COLOR_KIND_CURSOR` | Action-based |
| `vte_terminal_set_color_cursor_foreground(t, &color)` | Same mechanism | Separated in action |
| `vte_terminal_get_color_background_for_draw(t, &bg)` | NOT YET EXPOSED in C API | Needed for snapshot/overlay rendering |
| `vte_terminal_set_clear_background(t, clear)` | No equivalent | Ghostty manages its own background |

### 3.5 Selection & Clipboard

| VTE Function | Ghostty Equivalent | Notes |
|---|---|---|
| `vte_terminal_get_has_selection(t)` | `ghostty_surface_has_selection(surf)` | Direct equivalent |
| `vte_terminal_select_all(t)` | Via action dispatch? | Not a direct C API call |
| `vte_terminal_unselect_all(t)` | Via action dispatch? | Not a direct C API call |
| `vte_terminal_get_text_selected(t, format)` | `ghostty_surface_read_selection(surf, &text)` | Reads as `ghostty_text_s` |
| `vte_terminal_paste_clipboard(t)` | Via clipboard callback in `ghostty_runtime_config_s` | Callback-driven |
| `vte_terminal_paste_text(t, text)` | Not directly exposed | Can feed text via `ghostty_surface_text()` |

### 3.6 Scrollback

| VTE Function | Ghostty Equivalent | Notes |
|---|---|---|
| `vte_terminal_set_scrollback_lines(t, lines)` | Config option via `ghostty_config_t` | Config-managed |
| `vte_terminal_get_scroll_on_keystroke(t)` | NOT YET EXPOSED | Ptyxis uses this to decide scroll-on-key behavior |

### 3.7 URL / Hyperlink Matching

| VTE Function | Ghostty Equivalent | Notes |
|---|---|---|
| `vte_terminal_check_hyperlink_at(t, x, y)` | `ghostty_action_mouse_over_link_s` via action callback | Delivered as action, not a query |
| `vte_terminal_check_match_at(t, x, y, &tag)` | NOT AVAILABLE | Ptyxis manages its own URL matches with `vte_terminal_match_add_regex()`; Ghostty doesn't expose a regex match API |
| `vte_terminal_match_add_regex(t, regex, flags)` | NOT AVAILABLE | Ptyxis registers URL patterns; Ghostty has its own URL detection |
| `vte_terminal_match_remove_all(t)` | NOT AVAILABLE | ` |
| `vte_terminal_match_set_cursor_name(t, tag, cursor)` | NOT AVAILABLE | ` |

### 3.8 Search

| VTE Function | Ghostty Equivalent | Notes |
|---|---|---|
| `vte_terminal_search_set_regex(t, regex, flags)` | `ghostty_action_start_search_s` with needle string | String-based in Ghostty, not regex |
| `vte_terminal_search_set_wrap_around(t, wrap)` | Config option? | |
| `vte_terminal_search_find_next(t)` | Via action dispatch | |
| `vte_terminal_search_find_previous(t)` | Via action dispatch | |

### 3.9 Input

| VTE Function | Ghostty Equivalent | Notes |
|---|---|---|
| `vte_terminal_get_input_enabled(t)` | No direct query | Would need to track state ourselves |
| `vte_terminal_set_input_enabled(t, enabled)` | `ghostty_surface_key(surf, key)` still works? | May need read-only mode action |

### 3.10 Window Title

| VTE Function | Ghostty Equivalent | Notes |
|---|---|---|
| `vte_terminal_get_window_title(t)` | `ghostty_action_set_title_s` via action callback | Delivered as action |

### 3.11 Font

| VTE Function | Ghostty Equivalent | Notes |
|---|---|---|
| `vte_terminal_get_font(t)` | NOT YET EXPOSED in C API | Needed for inspector |

### 3.12 Container / Termprops

| VTE Function | Ghostty Equivalent | Notes |
|---|---|---|
| `vte_terminal_get_termprop_string(t, prop, &len)` | NOT AVAILABLE | Container name/runtime are custom VTE extensions; need alternative approach for container integration |
| `vte_terminal_get_termprop_string_by_id(t, id, &len)` | NOT AVAILABLE | Same as above |
| `vte_terminal_get_termprop_int_by_id(t, id, &val)` | NOT AVAILABLE | Same as above |
| `vte_terminal_get_termprop_uint_by_id(t, id, &val)` | NOT AVAILABLE | Same as above |
| `vte_terminal_ref_termprop_uri_by_id(t, id)` | NOT AVAILABLE | Directory/file URI tracking |

### 3.13 Word Char Exceptions

| VTE Function | Ghostty Equivalent | Notes |
|---|---|---|
| `vte_terminal_set_word_char_exceptions(t, chars)` | Config option | Config-managed |

---

## 4. VTE PTY Functions

| VTE Function | Ghostty Equivalent | Notes |
|---|---|---|
| `vte_pty_new_foreign_sync(fd, cancellable, error)` | `ghostty_surface_new(app, &config)` with command set | Ghostty manages its own PTY |
| `vte_terminal_set_pty(t, pty)` | Handled via `ghostty_surface_config_s` | PTY assigned at surface creation |
| `vte_terminal_get_pty(t)` | `ghostty_surface_tty_name(surf)` | Returns PTY name string |
| `vte_pty_get_fd(pty)` | Not exposed directly | Internal to ghostty |
| `vte_pty_set_utf8(pty, utf8, error)` | Not needed | Ghostty handles UTF-8 |
| `VTE_IS_PTY(obj)` | N/A | No GType check needed |

---

## 5. VTE Signals Connected by Ptyxis

### 5.1 VteTerminalClass Virtual Functions (Overridden)

| VFunc | File | Ghostty Approach |
|---|---|---|
| `setup_context_menu` | terminal.c:245 | Capture via `ghostty_runtime_action_cb` — detect right-click and show custom popover |
| `selection_changed` | terminal.c (not shown but referenced in class_init) | Periodic polling of `ghostty_surface_has_selection()` or action callback |
| `char_size_changed` | terminal.c (class_init) | `GHOSTTY_ACTION_CELL_SIZE` action callback, or poll `ghostty_surface_size()` |

### 5.2 GObject Signals Connected

| Signal | File | Ghostty Approach |
|---|---|---|
| `termprop-changed::SHELL_PRECMD` | terminal.c:1456 | Map to `GHOSTTY_ACTION_COMMAND_FINISHED` or custom OSC handling |
| `termprop-changed::SHELL_PREEXEC` | terminal.c:1460 | Same as above |
| `termprop-changed::CONTAINER_NAME` | terminal.c:1464 | Custom container integration — no ghostty equivalent |
| `termprop-changed::CONTAINER_RUNTIME` | terminal.c:1468 | Custom container integration — no ghostty equivalent |
| `contents-changed` | tab.c:259 | Poll surface or use render action callback |

### 5.3 Custom Ptyxis Signals

| Signal | File | Ghostty Approach |
|---|---|---|
| `grid-size-changed` | terminal.c (custom signal) | Emit from `GHOSTTY_ACTION_CELL_SIZE` or `ghostty_surface_set_size` response |
| `match-clicked` | terminal.c (custom signal) | Emit from mouse handler after URL detection |
| `shell-precmd` | terminal.c (custom, emitted from termprop) | Emit from `GHOSTTY_ACTION_COMMAND_FINISHED` |
| `shell-preexec` | terminal.c (custom, emitted from termprop) | Same as above |

---

## 6. VTE Enums / Constants Used

| Constant | File(s) | Ghostty Equivalent |
|---|---|---|
| `VTE_CURSOR_BLINK_SYSTEM` | settings.c | Ghostty config option |
| `VTE_CURSOR_SHAPE_BLOCK` | settings.c | Ghostty config option |
| `VTE_ERASE_AUTO` | settings.c | Ghostty config option |
| `VTE_TEXT_BLINK_ALWAYS` | settings.c | Ghostty config option |
| `VTE_FORMAT_TEXT` / `VTE_FORMAT_HTML` | terminal.c (selection read) | `ghostty_text_s` struct |
| `VTE_PCRE2_MULTILINE` etc. | terminal.c (URL regex) | N/A — Ptyxis manages its own PCRE2 |
| `VTE_PROGRESS_HINT_*` | tab.c (OSC 9;4) | `ghostty_action_progress_report_s` |
| `VTE_PROPERTY_ID_CONTAINER_NAME` | terminal.c | Custom container integration |
| `VTE_PROPERTY_ID_CONTAINER_RUNTIME` | terminal.c | Custom container integration |
| `VTE_PROPERTY_ID_CURRENT_DIRECTORY_URI` | terminal.c | `ghostty_action_pwd_s` |
| `VTE_PROPERTY_ID_CURRENT_FILE_URI` | terminal.c | No direct equivalent |
| `VTE_PROPERTY_ID_PROGRESS_HINT` | tab.c | `ghostty_action_progress_report_s` |
| `VTE_PROPERTY_ID_PROGRESS_VALUE` | tab.c | `ghostty_action_progress_report_s` |
| `VTE_MAJOR/MINOR/MICRO_VERSION` | terminal.c (inspector) | `ghostty_info().version` |

---

## 7. Missing / Gap Summary

### Critical Gaps (no ghostty equivalent)
1. **Container termprops** (`CONTAINER_NAME`, `CONTAINER_RUNTIME`) — custom VTE extensions; must be reimplemented with container integration layer
2. **URL regex matching** (`vte_terminal_match_add_regex`, `vte_terminal_check_match_at`) — Ptyxis has its own URL patterns; must be ported to work without VTE regex API
3. **Cursor position query** (`vte_terminal_get_cursor_position`) — not in C API
4. **Background color for draw** (`vte_terminal_get_color_background_for_draw`) — needed for snapshot overlays

### Moderate Gaps (different mechanism)
5. **Font query** (`vte_terminal_get_font`) — not in C API; needed for inspector
6. **Input enabled toggle** (`vte_terminal_set_input_enabled`) — use read-only mode action
7. **Cell sizing scale** (`vte_terminal_set_cell_{width,height}_scale`) — font size managed differently
8. **Selection operations** (`vte_terminal_select_all`, `unselect_all`) — may need action dispatch

### Architectural Differences
9. **GObject inheritance replaced by widget wrapper** — PtyxisTerminal currently *is* a VteTerminal; new design wraps ghostty_surface_t
10. **Callback-driven actions replace signals** — all ghostty terminal events go through a single `action_cb` dispatcher
11. **Config-driven appearance** — colors, fonts, cursor are set via config objects, not per-surface calls
12. **PTY managed internally** — no `VtePty` object to pass around; ghostty creates its own PTY
