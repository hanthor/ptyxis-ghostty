project_dir  := justfile_directory()
build_dir    := project_dir / "_build"
prefix_dir   := project_dir / "_prefix"
ghostty_dir  := project_dir / "subprojects/ghostty"
ghostty_lib  := ghostty_dir / "zig-out/lib"
container    := "finupdate"
flatpak_id   := "dev.hanthor.PtyxisGhostty"
flatpak_repo := project_dir / "_flatpak-repo"
flatpak_bdir := project_dir / "_flatpak-build"

# List available recipes
default:
    @just --list

# ── Toolbox (finupdate) ────────────────────────────────────────────────────────

# Build libghostty-vt inside the toolbox
toolbox-build-ghostty:
    toolbox run --container {{container}} \
        bash -c "cd '{{ghostty_dir}}' && zig build -Doptimize=ReleaseFast"

# Configure the meson build inside the toolbox (run once or after option changes)
toolbox-setup: toolbox-build-ghostty
    toolbox run --container {{container}} \
        bash -c "cd '{{project_dir}}' && meson setup '{{build_dir}}' \
            --prefix='{{prefix_dir}}' \
            -Ddevelopment=true \
            -Dlibc-compat=true \
            -Dc_args='-Wno-error=unused-function' \
            --wipe"

# Compile and install into _prefix (so ptyxis-agent ends up in _prefix/libexec)
toolbox-build:
    toolbox run --container {{container}} \
        bash -c "ninja -C '{{build_dir}}' && meson install -C '{{build_dir}}' --quiet"

# Run the installed binary with correct lib and schema paths
toolbox-run: toolbox-build
    LD_LIBRARY_PATH="{{ghostty_lib}}" \
    XDG_DATA_DIRS="{{prefix_dir}}/share:$XDG_DATA_DIRS" \
    GSETTINGS_SCHEMA_DIR="{{prefix_dir}}/share/glib-2.0/schemas" \
    '{{prefix_dir}}/bin/ptyxis'

# Run the test suite inside the toolbox
toolbox-test: toolbox-build
    toolbox run --container {{container}} \
        bash -c "cd '{{project_dir}}' && \
            LD_LIBRARY_PATH='{{ghostty_lib}}' \
            meson test -C '{{build_dir}}' --print-errorlogs"

# ── Flatpak ───────────────────────────────────────────────────────────────────

# Build the Flatpak (local, no network after first run if deps are cached)
flatpak-build:
    flatpak-builder \
        --force-clean \
        --user \
        --install-deps-from=gnome-nightly \
        --repo='{{flatpak_repo}}' \
        '{{flatpak_bdir}}' \
        '{{project_dir}}/dev.hanthor.PtyxisGhostty.json'

# Install the locally-built Flatpak for the current user
flatpak-install: flatpak-build
    flatpak --user remote-add --no-gpg-verify --if-not-exists \
        ptyxis-ghostty-local '{{flatpak_repo}}'
    flatpak --user install --reinstall -y \
        ptyxis-ghostty-local {{flatpak_id}}

# Run the installed Flatpak
flatpak-run:
    flatpak run {{flatpak_id}}

# Build, install, and run in one step
flatpak-dev: flatpak-install flatpak-run
