# Project Status — Ptyxis Ghostty

Last updated: 2026-06-03

## Summary

Ptyxis Ghostty is a fork of Ptyxis that replaces the VTE terminal backend with
[libghostty](https://github.com/ghostty-org/ghostty). The project is in active
development and **compiles successfully on Linux**.

## What Works

| Component | Status | Notes |
|---|---|---|
| Meson build | ✅ | Compiles with GCC 15 on Fedora 43 |
| libghostty build | ✅ | Built with `zig build -Doptimize=ReleaseFast -Dapp-runtime=none` |
| VTE compat layer | ✅ | GTypes registered, stubs for all VTE API surface |
| Ghostty widget | ✅ | `PtyxisGhosttyWidget` type system, input routing, lifecycle |
| Config bridge | ✅ | PtyxisProfile/Settings → `ghostty_config_t` mapping |
| UI templates | ✅ | VTE-specific properties/signals removed from Blueprint |
| Application launch | ✅ | Reaches GTK window map, GPU initialization |
| Flatpak build (CI) | ✅ | Nightly Flatpak available via GitHub Actions |
| Agent (container) | ✅ | Compiles, PTY fd bridge in place |
| Tests | ✅ | Type registration tests pass; some integration tests skipped |

## What's Blocked

### Ghostty embedded runtime has no Linux PlatformTag

The ghostty library built with `-Dapp-runtime=none` (embedded mode) only
supports rendering platforms for **macOS** (`GHOSTTY_PLATFORM_MACOS=1`) and
**iOS** (`GHOSTTY_PLATFORM_IOS=2`). There is no Linux platform variant.

**Root cause:** In `src/apprt/embedded.zig`, `PlatformTag` enumeration:

```zig
pub const PlatformTag = enum(c_int) {
    macos = 1,
    ios = 2,
};
```

On Linux, `ghostty_surface_new()` calls `Platform.init(0, ...)` where tag 0
is explicitly reserved as invalid. There is no fallback path for headless or
GTK-based rendering in embedded mode.

**Impact:** `ptyxis_ghostty_widget_realize()` assertion fails:
```
Ptyxis:ERROR: ptyxis_ghostty_widget_realize: assertion failed: (self->surface != NULL)
```

**Resolution path:** Ghostty does support Linux via the GTK app runtime
(`-Dapp-runtime=gtk`), but this builds a full standalone application, not an
embeddable library. The ghostty project is working on `apprt/gtk-ng`
(tristan957/gtk-ng branch) which may provide Linux embedding support.

**Fallback plan:** If the gtk-ng branch stabilizes, switch to that. Otherwise,
contribute the Linux embedded platform support upstream to ghostty.

## Fixes Applied

### 1. GType registration for VTE compat enums (`ptyxis-compat.c`)

`VTE_TYPE_CURSOR_BLINK_MODE`, `VTE_TYPE_CURSOR_SHAPE`, `VTE_TYPE_ERASE_BINDING`,
and `VTE_TYPE_TEXT_BLINK_MODE` were defined as literal `0` in `ptyxis-compat.h`,
causing `g_param_spec_enum()` to crash with `G_TYPE_IS_ENUM` assertion failure.

**Fix:** Registered proper GTypes via `g_enum_register_static()` in a new
`src/ptyxis-compat.c` file. Updated `ptyxis-compat.h` with GType getter
declarations.

### 2. Ghostty header strict prototypes (`ghostty.h`)

`ghostty_config_new()` and `ghostty_surface_config_new()` were declared as
`fn()` (K&R-style) instead of `fn(void)`. The meson build uses
`-Werror=strict-prototypes`, causing compilation failure.

**Fix:** Patched `ghostty.h` to use `(void)` prototypes. Will upstream.

### 3. UI template VTE-specific properties

`ptyxis-tab.ui` and `ptyxis-terminal.ui` contained VTE-specific properties
and signals that don't exist on the ghostty-based `PtyxisTerminal`:

- Removed properties: `enable-fallback-scrolling`, `scroll-unit-is-pixels`
- Removed signals: `commit`, `notify::window-title`, `current-file-uri-changed`,
  `current-directory-uri-changed`, `decrease-font-size`, `increase-font-size`,
  `bell`, `termprop-changed::*`
- Removed property bindings: `scroll-on-keystroke`, `scroll-on-output`,
  `backspace-binding`, `delete-binding`, `cjk-ambiguous-width`, `bold-is-bright`
- Converted `context-menu` from VTE property to template child element

### 4. Ghostty library initialization (`main.c`)

`ghostty_init()` was never called, but `ghostty_config_new()` and
`ghostty_app_new()` require the Zig allocator (`state.alloc`) to be initialized.

**Fix:** Added `ghostty_init(argc, argv)` after `gtk_init()` in `main.c`.

### 5. Default config for ghostty widget (`ptyxis-terminal.c`)

`ptyxis_terminal_init()` passed `self->config` (NULL) to
`ptyxis_ghostty_widget_new()`, which then passed NULL config to
`ghostty_app_new()`, causing a segfault when `Config.clone()` dereferenced NULL.

**Fix:** Added `ghostty_config_new()` call to create a default config
before widget initialization.

### 6. Unused function warnings

Commented-out callbacks left unused function warnings that `-Werror` treated
as errors.

**Fix:** Added `-Wno-error=unused-function` to meson build args for the
transition period.

## Local Build Instructions

### Prerequisites

- Fedora 43+ toolbox (or equivalent with GCC 15, meson, GTK4, libadwaita)
- Zig 0.15.2 (exact version required; 0.16.0 will not work)
- Git with submodules

### Build Steps

```bash
# 1. Clone and init submodules
git clone git@github.com:hanthor/ptyxis-ghostty.git
cd ptyxis-ghostty
git submodule update --init --recursive

# 2. Build libghostty
cd subprojects/ghostty
/path/to/zig-0.15.2/zig build -Doptimize=ReleaseFast -Dapp-runtime=none
cd ../..

# 3. Set up ghostty install prefix
GHOSTTY_PREFIX="$HOME/.cache/ghostty-install"
mkdir -p "$GHOSTTY_PREFIX/lib" "$GHOSTTY_PREFIX/include/ghostty"
cp subprojects/ghostty/zig-out/lib/ghostty-internal.so "$GHOSTTY_PREFIX/lib/libghostty.so"
cp subprojects/ghostty/zig-out/include/ghostty.h "$GHOSTTY_PREFIX/include/"
cp -r subprojects/ghostty/zig-out/include/ghostty/* "$GHOSTTY_PREFIX/include/ghostty/"

# 4. Fix ghostty.h strict prototypes (one-time)
sed -i 's/ghostty_config_new();/ghostty_config_new(void);/' "$GHOSTTY_PREFIX/include/ghostty.h"
sed -i 's/ghostty_surface_config_new();/ghostty_surface_config_new(void);/' "$GHOSTTY_PREFIX/include/ghostty.h"

# 5. Build ptyxis
meson setup _build \
  -Ddevelopment=true \
  -Dlibc-compat=true \
  -Dghostty_prefix="$GHOSTTY_PREFIX" \
  -Dc_args="-Wno-error=unused-function"
meson compile -C _build

# 6. Run
cd _build/src
glib-compile-schemas .
LD_LIBRARY_PATH="$GHOSTTY_PREFIX/lib" \
GSETTINGS_SCHEMA_DIR="$PWD" \
./ptyxis
```

> **Note:** The app will launch its window but crash during surface creation
> due to the Linux PlatformTag limitation described above.

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
| Zig | **0.15.2** | Exact version required for ghostty |
| libghostty | submodule | Built with `-Dapp-runtime=none` |

## Next Steps

1. **Linux embedded platform support** — Either wait for `ghostty` upstream
   (`tristan957/gtk-ng` branch) or contribute a Linux rendering backend to
   the embedded runtime.

2. **Flatpak** — The Flatpak build works in CI. Once the surface creation
   issue is resolved, the Flatpak should launch successfully. The current
   Flatpak hits the same GType crash (fixed in this repo), then would hit
   the same PlatformTag limitation.

3. **Signal bridge completion** — ~20 VTE signals in `ptyxis-tab.c` are
   commented out. They need ghostty action callback equivalents.

4. **Property completion** — Properties like `scroll-on-keystroke`,
   `cursor-shape`, `audible-bell`, `font-desc`, `text-blink-mode` need
   to be bridged from `PtyxisSettings` through to `ghostty_config_t`.

5. **URL matching** — `PtyxisTerminal` uses VTE's `VteRegex` for URL
   detection. The design doc specifies a PCRE2-based `PtyxisUrlMatcher`
   replacement that hasn't been implemented yet.

6. **Search integration** — Ghostty's `ghostty_action_start_search_s`
   needs to be wired to Ptyxis's find bar.

## Known Warnings (non-fatal)

- `GObject-CRITICAL: signal 'contents-changed' is invalid` — VTE signal,
  not yet removed from ptyxis-tab-monitor.c
- `redefined VTE_PCRE2_*` — Both `ptyxis-util.h` and `ptyxis-compat.h`
  define these macros. Cleanup needed.
- `deprecated gtk_show_uri` — Should use `gtk_uri_launcher_launch`
- Multiple `-Wdeclaration-after-statement` — Ghostty config code uses
  C99 mixed declarations (harmless, suppressed)

## Archive

- Original VTE→Ghostty API audit: `docs/vte-ghostty-audit.md`
- Integration design document: `docs/integration-design.md`
