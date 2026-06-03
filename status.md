# Project Status — Ptyxis Ghostty

Last updated: 2026-06-03 (rev 2)

## Summary

Ptyxis Ghostty is a fork of Ptyxis that replaces the VTE terminal backend with
[libghostty-vt](https://github.com/ghostty-org/ghostty) (the pure VT state
machine library). The project **builds and runs on Linux** with a Cairo+Pango
renderer driven directly by the libghostty-vt cell grid.

## What Works

| Component | Status | Notes |
|---|---|---|
| Meson build | ✅ | GCC 15 / Fedora 43 |
| libghostty-vt build | ✅ | `zig build -Doptimize=ReleaseFast` |
| VTE compat layer | ✅ | GTypes registered; key stubs delegate to real widget |
| Ghostty widget | ✅ | PTY, render state, key/mouse encoding, child lifecycle |
| Cairo+Pango renderer | ✅ | Cell backgrounds + text + cursor in single Cairo node |
| Application launch | ✅ | Window opens, terminal spawns shell, renders output |
| Shell exit handling | ✅ | `child-exited` signal closes/restarts tab per profile setting |
| Font scale (zoom) | ✅ | `Ctrl+Plus/Minus` remaps through real widget call |
| Window title | ✅ | OSC 0/2 title changes propagate to tab |
| Background color | ✅ | Window dressing reads real terminal background color |
| Agent (container) | ✅ | Agent PTY now wired into widget via attach_pty |
| Flatpak build (CI) | ✅ | Nightly Flatpak via GitHub Actions |
| Tests | ✅ | Type registration + widget tests pass |
| `just` recipes | ✅ | `just toolbox-run` and `just flatpak-dev` for quick iteration |
| Color palette | ✅ | Instantly synced from profile GSettings |
| Cursor & blinking | ✅ | Block/I-Beam/Underline shapes, cursor blink, and text blink fully supported |

## Architecture

The widget uses **libghostty-vt** (zero-dependency VT state machine) + a
custom GTK4/Cairo/Pango renderer, similar to what a future `libghostty-gtk4`
would be.

```
PTY (openpty + fork $SHELL)
  ↓ read bytes
ghostty_terminal_vt_write()   ← parses VT sequences, updates terminal state
ghostty_render_state_update()  ← computes dirty cell grid
GtkSnapshot → gtk_snapshot_append_cairo()
  ↓
  Cell loop: fill bg (Cairo rect) + text (Pango layout per cell)
  Cursor: filled block (focused) / hollow rect (unfocused)
```

### Container PTY wiring

`PtyxisTab::respawn` calls `vte_terminal_set_pty` → `ptyxis_terminal_set_pty`
→ `ptyxis_ghostty_widget_attach_pty(fd)`. This tears down the widget's
self-spawned shell, dups the agent's master fd, and sets up the I/O watch.
`ptyxis_application_spawn_async` then tells the agent to spawn the shell on
the slave side of that PTY — enabling container-aware spawning (toolbox, podman).

## What's Implemented (Corrected)

| Feature | Status | Notes |
|---|---|---|
| Container spawning | ✅ | Agent PTY wired via attach_pty; widget reads agent's shell |
| Text selection (basic) | ✅ | `has_selection` / `get_selected_text` work; mouse selection via ghostty API |
| Mouse-driven selection | ✅ | Left-click drag selects text; auto-copies to PRIMARY clipboard |
| Search (stub) | ✅ | `search_start/next/prev/end` implemented; basic next/prev navigation |
| Scrollback navigation | ✅ | Polls `GHOSTTY_TERMINAL_DATA_SCROLLBAR` in pty_readable_cb |
| CWD tracking | ✅ | OSC 7 polled in pty_readable_cb; `CWD_CHANGED` signal emitted |
| Input read-only mode | ✅ | `self->input_enabled` gates all key/mouse/scroll handlers |
| Shell integration | ⚠️ | Only in `ghostty_surface_*` (full terminal model); not available in VT-only widget |

## Known Issues & Future Work

| Feature | Status | Notes |
|---|---|---|
| GtkRevealer startup warning | ⚠️ | Cosmetic; size indicator fires before first layout |
| Text search UI | ⚠️ | Find bar integrated but search results not highlighted (stub highlight) |

## Build & Run

```bash
# One-time setup
just toolbox-setup

# Incremental build + run
just toolbox-run

# Flatpak
just flatpak-dev
```

### Manual run (after `just toolbox-build`):

```bash
glib-compile-schemas _prefix/share/glib-2.0/schemas/
LD_LIBRARY_PATH="subprojects/ghostty/zig-out/lib" \
XDG_DATA_DIRS="_prefix/share:$XDG_DATA_DIRS" \
GSETTINGS_SCHEMA_DIR="_prefix/share/glib-2.0/schemas" \
_prefix/bin/ptyxis
```

## Dependencies

| Dependency | Version | Notes |
|---|---|---|
| Meson | ≥ 1.0.0 | |
| GCC | 15.x | Tested on Fedora 43 |
| GTK4 | ≥ 4.14 | |
| libadwaita | ≥ 1.7 | |
| JSON-GLib | ≥ 1.6 | |
| libportal-gtk4 | | Linux only |
| libxml2 | | |
| Zig | **0.16.0** | Required for ghostty build |
| libghostty-vt | submodule | `zig build -Doptimize=ReleaseFast` |
| just | any | Optional; for build recipes |
