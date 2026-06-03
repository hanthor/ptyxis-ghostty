# Project Status — Ptyxis Ghostty

Last updated: 2026-06-03

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
| Agent (container) | ✅ | Compiles; agent process monitored via IPC |
| Flatpak build (CI) | ✅ | Nightly Flatpak via GitHub Actions |
| Tests | ✅ | Type registration + widget tests pass |
| `just` recipes | ✅ | `just toolbox-run` and `just flatpak-dev` for quick iteration |

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

### Known Limitation: dual-process spawning

`PtyxisTab::respawn` spawns a shell via the ptyxis-agent (container-aware
IPC). Our widget also spawns its own shell via `openpty`+`fork` on realize.
The **widget's shell** is what the user interacts with. The agent shell is
idle (container monitoring still works for the running state).

Container support (spawning inside podman/toolbox) requires wiring
`vte_terminal_set_pty` to feed the agent PTY fd into the widget instead
of calling `start_child`. This is the next major milestone.

## What's Not Yet Implemented

| Feature | Status | Notes |
|---|---|---|
| Container spawning | ❌ | Widget spawns bare shell; agent PTY not wired up |
| Text selection | ❌ | `has_selection` / `get_selected_text` stubs |
| Search | ❌ | `search_start/next/prev/end` stubs |
| Scrollback navigation | ❌ | Scrollbar not connected |
| CWD tracking | ❌ | OSC 7 not wired to tab monitor |
| Shell integration | ❌ | OSC precmd/preexec not wired |
| Color palette from profile | ❌ | `update_colors` stub; no libghostty-vt color API yet |
| Input read-only mode | ❌ | `set_input_enabled` is a no-op stub |
| GtkRevealer startup warning | ⚠️ | Cosmetic; size indicator fires before first layout |

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
