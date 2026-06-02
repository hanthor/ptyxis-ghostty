# Ptyxis + Ghostty Integration Design

*Step 3 deliverable: widget embedding strategy, PTY lifecycle, signal/event bridging, profile/theming mapping, container/toolbox integration path. No code yet.*

---

## 1. High-Level Architecture

### Current (VTE-based)
```
PtyxisTerminal : VteTerminal : GtkWidget
  ├── VtePty (agent-created, FD-passed)
  ├── VteRegex (URL matching in terminal.c static init)
  ├── VteTerminal vfuncs (setup_context_menu, selection_changed, char_size_changed)
  └── Signals (termprop-changed::*, contents-changed)
```

### Target (Ghostty-based)
```
PtyxisTerminal : GtkWidget                    ← no longer subclasses VteTerminal
  ├── ghostty_surface_t (owned, opaque)       ← core terminal engine
  ├── GhosttyActionDispatcher                 ← maps action_cb → Ptyxis signals
  ├── PtyxisUrlMatcher (new, PCRE2-based)     ← replaces VteRegex URL matching
  └── PtyxisTerminalIface (new, GObject interface) ← preserves API compat
```

### Design principle
**Wrap, don't inherit.** Ghostty's `ghostty_surface_t` is an opaque C type with a callback-driven API, not a GObject. We wrap it in a GTK widget that manages the lifecycle, input, rendering, and event dispatch. PtyxisTerminal becomes a plain GtkWidget that delegates to ghostty_surface_t.

---

## 2. Widget Embedding Strategy

### 2.1 PtyxisTerminal refactored as a GtkWidget wrapper

```
struct PtyxisTerminal {
  GtkWidget              parent;
  
  /* Ghostty core */
  ghostty_app_t          app;          // shared across all terminals
  ghostty_surface_t      surface;      // this terminal's surface
  ghostty_config_t       config;       // per-terminal config override
  
  /* Ptyxis heritage */
  PtyxisShortcuts       *shortcuts;
  PtyxisPalette         *palette;
  GHashTable            *custom_links;
  char                  *url;
  
  /* Widget chrome */
  GtkPopover            *popover;
  GtkWidget             *drop_highlight;
  GtkDropTargetAsync    *drop_target;
  GtkRevealer           *size_revealer;
  GtkLabel              *size_label;
  
  /* State tracking */
  guint                  n_columns;
  guint                  n_rows;
  guint                  cell_width;
  guint                  cell_height;
  gboolean               has_selection;    // polled from ghostty
  gboolean               input_enabled;
};
```

### 2.2 GTK Widget lifecycle → Ghostty lifecycle

| GTK vfunc | Ghostty action |
|---|---|
| `realize()` | `ghostty_surface_new(app, &config)` |
| `unrealize()` | `ghostty_surface_free(surface)` |
| `measure(orientation, for_size)` | `ghostty_surface_size(surface)` → cell dimensions |
| `size_allocate(width, height)` | `ghostty_surface_set_size(surface, width, height)` |
| `snapshot(snapshot)` | `ghostty_surface_draw(surface)` → render to GTK snapshot |

### 2.3 Input routing

| GTK event | Ghostty API |
|---|---|
| `GtkEventControllerKey` → key-pressed | `ghostty_surface_key(surface, key_event)` |
| `GtkGestureClick` → pressed/released | `ghostty_surface_mouse_button(surface, ...)` |
| `GtkEventControllerMotion` | `ghostty_surface_mouse_pos(surface, x, y, mods)` |
| `GtkEventControllerScroll` | `ghostty_surface_mouse_scroll(surface, dx, dy, mods)` |
| IME / compose | `ghostty_surface_text(surface, text, len)` |
| Paste from clipboard | `ghostty_surface_text(surface, clipboard_text, len)` |

### 2.4 Focus management

```
ghostty_surface_set_focus(surface, has_focus)
```
Called from `gtk_widget_grab_focus()` / `gtk_widget_has_focus()` transitions.

---

## 3. PTY Lifecycle

### 3.1 Current PTY flow (VTE)

```
1. PtyxisTab creates a VtePty via agent D-Bus
   agent → create_pty() → returns FD
   host → vte_pty_new_foreign_sync(fd) → VtePty
   
2. PtyxisTab sets PTY on VteTerminal
   vte_terminal_set_pty(terminal, pty)
   
3. PtyxisTab spawns shell via agent
   agent.spawn(container, profile, pty_fd, argv, env, cwd)
   agent → forks child, dup2(pty_slave_fd, stdin/stdout/stderr)
   
4. Terminal I/O flows through VteTerminal ↔ VtePty
```

### 3.2 Target PTY flow (Ghostty)

Ghostty manages its own PTY internally. The challenge: Ghostty's PTY is on the **host**, but the shell must run inside the **container**. 

**Required Ghostty API addition**: Add `pty_fd` field to `ghostty_surface_config_s` so the embedder can provide an externally-created PTY master FD.

```c
// Proposed addition to ghostty_surface_config_s:
struct ghostty_surface_config_s {
  // ... existing fields ...
  int pty_fd;           // -1 = let ghostty create PTY; >=0 = use this master FD
};
```

Then the flow becomes:

```
1. Agent creates PTY inside container (same as current)
   agent → create_pty() → returns master FD

2. Ghostty surface opens with that PTY
   ghostty_surface_new(app, &config) where config.pty_fd = master_fd
   ghostty reads/writes using this FD

3. Agent spawns shell connected to PTY slave
   agent.spawn(container, profile, pty_fd, argv, env, cwd)
   agent → forks child, dup2(pty_slave_fd, stdin/stdout/stderr)

4. Terminal I/O flows through ghostty_surface ↔ PTY FD
```

**Fallback** (if ghostty can't accept external PTY FDs):
Let ghostty create its PTY, then pass the **slave** FD to the agent for container spawning. This requires:
- `ghostty_surface_tty_name(surface)` → resolve to slave FD
- Pass slave FD to agent for child process
- Ghostty reads/writes master FD internally

### 3.3 PtyxisTab spawn flow (adapted)

```
ptyxis_tab_respawn(self):
  1. Resolve container (Podman/Distrobox/Toolbox/Host)
  2. Agent: create_pty() → master_fd
  3. Create ghostty config with pty_fd = master_fd
  4. ghostty_surface_new(app, &config) → surface
  5. Agent: spawn(container, profile, pty_fd, argv, env, cwd)
  6. Terminal is now live
```

**Key difference from VTE**: VTE decouples PTY creation and terminal attachment (`vte_terminal_set_pty`). With ghostty, PTY is set at surface creation time. This simplifies the flow: create PTY, create surface with that PTY, spawn shell, done.

---

## 4. Signal/Event Bridging

### 4.1 Ghostty action callback → Ptyxis signals

Ghostty delivers all terminal events through a **single callback**:

```c
bool ghostty_runtime_action_cb(
    ghostty_app_t app,
    ghostty_target_s target,
    ghostty_action_s action
);
```

We register a `GhosttyActionDispatcher` that receives every action and routes it to the appropriate Ptyxis signal or handler:

| Ghostty Action Tag | Ptyxis Signal / Handler |
|---|---|
| `GHOSTTY_ACTION_SET_TITLE` | Update tab title, emit notify |
| `GHOSTTY_ACTION_PWD` | Update `previous_working_directory_uri` |
| `GHOSTTY_ACTION_MOUSE_OVER_LINK` | `ptyxis_terminal_update_url_actions()` |
| `GHOSTTY_ACTION_MOUSE_SHAPE` | Set GTK cursor shape |
| `GHOSTTY_ACTION_COMMAND_FINISHED` | Emit `shell-precmd` signal |
| `GHOSTTY_ACTION_PROGRESS_REPORT` | Notify OSC 9;4 handler (tab progress) |
| `GHOSTTY_ACTION_CELL_SIZE` | Emit `grid-size-changed` signal |
| `GHOSTTY_ACTION_RENDER` | Queue GTK widget redraw |
| `GHOSTTY_ACTION_COLOR_CHANGE` | Update palette face |
| `GHOSTTY_ACTION_SCROLLBAR` | Update scroll position indicator |
| `GHOSTTY_ACTION_CLOSE_SURFACE` | Close tab / exit |
| `GHOSTTY_ACTION_DESKTOP_NOTIFICATION` | Forward to desktop notification |
| `GHOSTTY_ACTION_OPEN_URL` | `ptyxis_terminal_match_clicked()` handler |
| `GHOSTTY_ACTION_RING_BELL` | Trigger visual bell |

### 4.2 Clipboard bridge

Ghostty uses callback-driven clipboard:

```c
// In ghostty_runtime_config_s:
.read_clipboard_cb     → triggered when ghostty needs to read clipboard
.write_clipboard_cb    → triggered when ghostty needs to write clipboard
.confirm_read_clipboard_cb → OSC 52 read confirmation
```

Ptyxis maps these to GTK/GDK clipboard calls:
- `read_clipboard_cb` → `gdk_clipboard_read_text_async()`
- `write_clipboard_cb` → `gdk_clipboard_set_text()`

### 4.3 Selection tracking

VTE signals `selection_changed`. Ghostty doesn't signal this — we **poll**:

```c
// In ghostty_surface_draw() callback or periodic timer:
if (ghostty_surface_has_selection(surface) != self->had_selection) {
    self->had_selection = !self->had_selection;
    ptyxis_terminal_update_clipboard_actions(self);
}
```

### 4.4 Custom signal emitters preserved

PtyxisTerminal's custom signals — `match-clicked`, `grid-size-changed`, `shell-precmd`, `shell-preexec` — are **not** VTE signals. They're emitted from PtyxisTerminal code that responds to VTE events. We preserve these signals and their emission points, just replacing the VTE trigger with the ghostty action callback trigger.

### 4.5 VteTerminalClass vfuncs → Ghostty equivalents

| VteTerminalClass vfunc | Current behavior | Ghostty approach |
|---|---|---|
| `setup_context_menu` | Position popover at right-click coordinates | Capture mouse position from `ghostty_surface_mouse_pos()`, show popover on button-3 press |
| `selection_changed` | Update clipboard actions | Poll `ghostty_surface_has_selection()` or react to clipboard actions |
| `char_size_changed` | Recalculate grid dimensions | Read from `ghostty_surface_size()` after `GHOSTTY_ACTION_CELL_SIZE` |

---

## 5. Profile / Theming Mapping

### 5.1 PtyxisProfile → Ghostty config mapping

| PtyxisProfile property | Ghostty config key |
|---|---|
| `font-name` | `font-family` |
| `font-size` | `font-size` |
| `custom-command` | `command` (in surface config) |
| `default-container` | Set on PtyxisTab, not ghostty config |
| `palette` | `palette` (256-color array) |
| `background-color` | `background` |
| `foreground-color` | `foreground` |
| `cursor-shape` | `cursor-shape` |
| `cursor-blink-mode` | `cursor-style` |
| `scrollback-lines` | `scrollback-limit` |
| `audible-bell` | `audible-bell` |
| `scroll-on-output` | `scroll-on-output` |
| `scroll-on-keystroke` | Not directly in ghostty; Ptyxis handles at widget level |
| `opacity` | Applied via GTK widget opacity, not ghostty config |
| `word-char-exceptions` | `word-separators` |
| `bold-is-bright` | `bold-is-bright` |

### 5.2 PtyxisPalette → ghostty palette

Ptyxis uses `PtyxisPalette` (named themes like "GNOME", "Tango", plus 16-color custom profiles). These map to ghostty's 256-color `ghostty_config_palette_s`:

```c
void ptyxis_palette_to_ghostty(PtyxisPalette *palette, 
                                gboolean dark,
                                ghostty_config_palette_s *out) {
    const PtyxisPaletteFace *face = ptyxis_palette_get_face(palette, dark);
    // Ghostty palette colors[0..15] = standard ANSI
    // colors[16..255] = extended 6×6×6 cube + grayscale
    // For now, fill first 16 from Ptyxis indexed colors
    memcpy(out->colors, face->indexed, 16 * sizeof(ghostty_config_color_s));
}
```

### 5.3 Config application flow

```
Profile changed
  → ptyxis_profile_to_ghostty_config(profile, &config)
  → ghostty_surface_update_config(surface, config)
  → ghostty applies changes to running terminal
```

Dynamic changes (palette switch, font resize) go through `ghostty_surface_update_config()`. Surface-creation config (command, working_directory, pty_fd) is set at `ghostty_surface_new()` time.

---

## 6. Container / Toolbox Integration Path

### 6.1 Architecture preserved

The **agent** (ptyxis-agent) remains the sole component that knows about containers. Ghostty doesn't need container awareness.

```
┌─────────────────────────────────────────────────┐
│ Ptyxis (host)                                   │
│  ┌──────────────────────┐  ┌─────────────────┐ │
│  │ PtyxisTab            │  │ PtyxisClient     │ │
│  │  ├ PtyxisTerminal    │  │  (D-Bus to agent)│ │
│  │  │  └ ghostty_surface│  └────────┬────────┘ │
│  │  └ PtyxisProfile     │           │          │
│  └──────────────────────┘           │ D-Bus    │
└─────────────────────────────────────┼──────────┘
                                      │
┌─────────────────────────────────────┼──────────┐
│ ptyxis-agent (host or container)    │          │
│  ┌──────────────────────────────────┘          │
│  │ Podman/Distrobox/Toolbox/Host               │
│  │  ├ create_pty() → master FD                │
│  │  └ spawn(pty_fd, argv, env, cwd)           │
│  │      → forks child inside container        │
│  │      → dup2(slave_fd, stdin/stdout/stderr) │
│  └─────────────────────────────────────────────┘
│                                      │
│  Container shell process             │
│  ┌───────────────────────────────────┘
│  │ /bin/bash (or custom command)
│  │  stdin/stdout/stderr → PTY slave
│  └────────────────────────────────────
└──────────────────────────────────────
```

### 6.2 Container types preserved

| Container Type | Detection | Agent handling |
|---|---|---|
| **Host** | Default (no container) | Direct fork + PTY |
| **Podman** | `ptyxis-podman-provider.c` | `podman exec` or `podman run` with PTY |
| **Distrobox** | `ptyxis-distrobox-container.c` | `distrobox-enter` with PTY |
| **Toolbox** | `ptyxis-toolbox-container.c` | `toolbox run` with PTY |

### 6.3 Container spawn flow with Ghostty

```
1. PtyxisTab determines container from profile.default_container
2. Container object created (PodmanProvider → PodmanContainer, etc.)
3. Agent: container.create_pty() → master_fd
4. ghostty_surface_new(app, config) with config.pty_fd = master_fd
5. Agent: container.spawn(pty_fd, shell_argv, env, cwd)
   → container exec's shell, connects stdin/stdout/stderr to PTY slave
6. Terminal I/O: ghostty ↔ master_fd ↔ PTY ↔ slave_fd ↔ shell
```

### 6.4 Container termprops (CONTAINER_NAME, CONTAINER_RUNTIME)

In VTE, these are custom OSC sequences set by Ptyxis's shell integration scripts. With ghostty, we implement them as a custom action callback or as text processing in the PtyxisTerminal layer:

```c
// In terminal.c, after ghostty surface receives text from PTY:
// Parse custom OSC sequences: OSC 777;container;name;runtime ST
// These were previously handled by VTE's termprop system.
// With ghostty, we intercept them at the PtyxisTerminal level.

// Alternative: add custom OSC handler to ghostty action callback
// GHOSTTY_ACTION_CUSTOM_OSC with payload string
```

Ptyxis's shell integration scripts (`/etc/profile.d/vte.sh` or similar) emit OSC sequences that VTE interprets. The agent's shell profile sources integration scripts that emit these sequences. Ghostty doesn't have a termprop system, so we have two options:

**Option A (Recommended)**: Intercept OSC sequences in ghostty's action callback. Configure the action callback to deliver unknown OSC sequences to PtyxisTerminal, which parses container name/runtime from them.

**Option B**: Add a text-processing layer between ghostty and the PTY that intercepts and handles these OSC sequences.

### 6.5 Shell integration (precmd / preexec)

Ptyxis currently uses VTE's `termprop-changed::shell-precmd` and `termprop-changed::shell-preexec` signals. These are triggered by OSC sequences from shell integration scripts.

Ghostty provides `GHOSTTY_ACTION_COMMAND_FINISHED` which fires when a shell command completes. This is the ghostty equivalent of precmd.

For preexec, we need to detect when the shell starts executing a command. Ghostty doesn't directly expose this, so we'd need the shell integration scripts to emit an OSC sequence that ghostty's action callback delivers to us.

---

## 7. URL / Hyperlink Matching

### 7.1 Porting Ptyxis's URL regex system

Ptyxis registers URL regexes at class init time via `vte_terminal_match_add_regex()`. Ghostty handles URL detection internally and delivers results via `GHOSTTY_ACTION_MOUSE_OVER_LINK`.

**Design decision**: Use ghostty's built-in URL detection for standard URL highlighting. Ptyxis's `PtyxisCustomLink` (user-defined regex patterns) still works at the PtyxisTerminal level:

```
Mouse moves over terminal
  → ghostty_surface_mouse_pos(surface, x, y)
  → ghostty may deliver MOUSE_OVER_LINK action (built-in URL detection)
  → PtyxisTerminal also runs its own PtyxisCustomLink regexes at (x, y)
  → Union of results shown in UI
```

### 7.2 Click handling

```
Button press at (x, y)
  → If ghostty reported a URL at this position → navigate to URL
  → Else if PtyxisCustomLink matched → run custom link handler
  → Else → forward to ghostty for terminal-internal handling
```

---

## 8. Search

### 8.1 PtyxisFindBar integration

Ptyxis uses `vte_terminal_search_set_regex()` / `vte_terminal_search_find_next()` / `vte_terminal_search_find_previous()`.

Ghostty provides:
- `GHOSTTY_ACTION_START_SEARCH` with `ghostty_action_start_search_s{needle}`
- `GHOSTTY_ACTION_END_SEARCH`
- `GHOSTTY_ACTION_SEARCH_TOTAL` / `GHOSTTY_ACTION_SEARCH_SELECTED` for result counts

**Adaptation**: PtyxisFindBar calls ghostty search actions instead of VTE search functions. Search is string-based in ghostty (not regex), which is actually how PtyxisFindBar works — it passes the user's typed string directly.

---

## 9. Drag & Drop

PtyxisTerminal handles file drop via `GtkDropTargetAsync`. This is at the GTK widget level, independent of VTE. No ghostty changes needed — drops are handled by PtyxisTerminal's existing `ptyxis_terminal_drop_target_drop()` handler which feeds paths as text to the terminal.

---

## 10. Inspector

PtyxisInspector currently reads VTE version info. Adapt to read `ghostty_info()` output instead. Ghostty provides `ghostty_surface_inspector(surface)` for the built-in terminal inspector.

---

## 11. Files to Create / Modify

### New files
| File | Purpose |
|---|---|
| `src/ptyxis-ghostty-widget.c/h` | GTK widget wrapping ghostty_surface_t (rendering, input) |
| `src/ptyxis-ghostty-action.c/h` | Action dispatcher: ghostty action_cb → Ptyxis signals |
| `src/ptyxis-ghostty-config.c/h` | PtyxisProfile ↔ ghostty_config_t mapping |
| `src/ptyxis-ghostty-clipboard.c/h` | Ghostty clipboard callbacks backed by GTK clipboard |

### Modified files
| File | Changes |
|---|---|
| `src/ptyxis-terminal.c/h` | Remove VteTerminal inheritance; add ghostty_surface_t ownership; port all VTE calls to ghostty equivalents; preserve signal API |
| `src/ptyxis-tab.c` | Update spawn flow: ghostty PTY → agent spawn; port VTE calls |
| `src/ptyxis-client.c/h` | Port `vte_pty_new_foreign_sync` to pass raw FD to ghostty |
| `src/ptyxis-application.c/h` | Port `create_pty` to return raw FD; adapt spawn flow |
| `src/ptyxis-find-bar.c` | Port search from VTE regex to ghostty search actions |
| `src/ptyxis-inspector.c` | Port VTE version display to ghostty info |
| `src/ptyxis-settings.c/h` | Remove VTE-specific settings (cursor-shape enums now go to ghostty) |
| `src/ptyxis-custom-link.c` | Port from vte_terminal_match_check_at to own PCRE2 matching |
| `src/ptyxis-window.c` | Minor: terminal type references |
| `meson.build` | Replace vte dependency with ghostty dependency; add new source files |
| `subprojects/ghostty` | **Patch**: add `pty_fd` field to `ghostty_surface_config_s` |

---

## 12. Risks & Open Questions

| Risk | Mitigation |
|---|---|
| Ghostty C API doesn't accept external PTY FD | Patch ghostty to add `pty_fd` to surface config; fallback: ghostty creates PTY, agent uses slave |
| Ghostty rendering doesn't play well with GTK snapshot | Test early; may need to use `ghostty_surface_draw()` with OpenGL/Metal texture instead of CPU rendering |
| Custom OSC sequences for container termprops not supported | Implement OSC interception in action callback |
| Performance of polling `ghostty_surface_has_selection()` | Acceptable; checked on clipboard changes and periodic render |
| macOS Metal rendering vs Linux GTK OpenGL | Use `GHOSTTY_PLATFORM` detection; ghostty `set_display_id` for macOS |
