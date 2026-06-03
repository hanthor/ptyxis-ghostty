"""
Custom step definitions for the Ptyxis (ghostty-vt) smoke suite.

qecore.common_steps covers lifecycle and most UI actions. These custom steps
cover Ptyxis-specific affordances:
- Cursor style selection via the Preferences dialog
- Terminal widget focus and typed input
- Tab count assertions
- Waiting for specific AT-SPI roles to appear
"""
import subprocess
import time

from behave import step
from qecore.common_steps import *  # noqa: F401,F403


# ── Helpers ────────────────────────────────────────────────────────────────

def _find_by_role(app_node, role_name, timeout=10):
    """Return the first node with the given AT-SPI roleName, or None."""
    deadline = time.time() + timeout
    while time.time() < deadline:
        try:
            matches = app_node.findChildren(
                lambda n: n.roleName == role_name
            )
            if matches:
                return matches[0]
        except Exception:
            pass
        time.sleep(0.3)
    return None


def _find_by_name_and_role(app_node, name, role_name, timeout=10):
    """Return the first node matching name (substring) and role."""
    deadline = time.time() + timeout
    while time.time() < deadline:
        try:
            matches = app_node.findChildren(
                lambda n: n.roleName == role_name
                and name.lower() in (n.name or "").lower()
            )
            if matches:
                return matches[0]
        except Exception:
            pass
        time.sleep(0.3)
    return None


def _force_close_ptyxis(context):
    """Tear down ptyxis reliably without waiting on qecore's shortcut path."""
    subprocess.run(["pkill", "-x", "ptyxis"], check=False)
    deadline = time.time() + 5
    while time.time() < deadline:
        try:
            from dogtail.tree import root
            root.application("ptyxis")
        except Exception:
            break
        time.sleep(0.2)
    if getattr(context, "ptyxis", None) and hasattr(context.ptyxis, "instance"):
        context.ptyxis.instance = None


# ── Wait for AT-SPI role ───────────────────────────────────────────────────

@step('Wait until "{role}" role appears in "{app_id}" within {seconds:d} seconds')
def wait_for_role(context, role, app_id, seconds):
    """Wait up to `seconds` for any node with the given AT-SPI roleName."""
    app = getattr(context, app_id).instance
    deadline = time.time() + seconds
    while time.time() < deadline:
        try:
            matches = app.findChildren(lambda n: n.roleName == role)
            if matches:
                return
        except Exception:
            pass
        time.sleep(0.3)
    raise AssertionError(
        f"No node with role {role!r} appeared in {app_id!r} within {seconds}s"
    )


@step('Wait until "{name}" "{role}" appears in "{app_id}" within {seconds:d} seconds')
def wait_for_named_role(context, name, role, app_id, seconds):
    """Wait up to `seconds` for a node matching name (substring) and role."""
    app = getattr(context, app_id).instance
    node = _find_by_name_and_role(app, name, role, timeout=seconds)
    if node is None:
        raise AssertionError(
            f"No {role!r} containing {name!r} appeared in {app_id!r} within {seconds}s"
        )


# ── Cursor style ───────────────────────────────────────────────────────────

_CURSOR_STYLE_OPTIONS = {
    "block":     "Block",
    "i-beam":    "I-Beam",
    "underline": "Underline",
}


@step('Application "{app_id}" is in cursor style "{style}"')
def set_cursor_style(context, app_id, style):
    """Select the named cursor style in the open Preferences dialog.

    Assumes Preferences is already open (caller opened it before this step).
    Navigates to the Profile section and clicks the correct radio/combo option.
    """
    normalized = style.lower().replace("-", "").replace(" ", "")
    app = getattr(context, app_id).instance

    # The cursor style control is inside the profile editor. Try to find a
    # combo-box or radio-button labelled with each style name.
    deadline = time.time() + 10
    while time.time() < deadline:
        try:
            # Look for a combo-box that contains cursor style options
            combos = app.findChildren(
                lambda n: n.roleName in ("combo box", "list box", "menu")
                and any(
                    s.lower() in (n.name or "").lower()
                    for s in ("cursor", "style")
                )
            )
            for combo in combos:
                combo.click()
                time.sleep(0.2)
                items = app.findChildren(
                    lambda n: n.roleName in ("menu item", "list item")
                    and style.lower() in (n.name or "").lower()
                )
                if items:
                    items[0].click()
                    return

            # Alternatively look for a direct radio button with the style name
            radios = app.findChildren(
                lambda n: n.roleName == "radio button"
                and style.lower() in (n.name or "").lower()
            )
            if radios:
                radios[0].click()
                return

            # Or a push button / toggle with the style name
            btns = app.findChildren(
                lambda n: n.roleName in ("push button", "toggle button")
                and style.lower() in (n.name or "").lower()
            )
            if btns:
                btns[0].click()
                return
        except Exception:
            pass
        time.sleep(0.5)

    raise AssertionError(
        f"Could not find cursor style option {style!r} in Preferences dialog"
    )


# ── Tab count ──────────────────────────────────────────────────────────────

@step('Wait until tab count in "{app_id}" is at least {n:d} within {seconds:d} seconds')
def wait_for_tab_count(context, app_id, n, seconds):
    """Wait up to `seconds` for at least `n` tab nodes in the app."""
    app = getattr(context, app_id).instance
    deadline = time.time() + seconds
    while time.time() < deadline:
        try:
            tabs = app.findChildren(
                lambda node: node.roleName in ("page tab", "tab")
            )
            if len(tabs) >= n:
                return
        except Exception:
            pass
        time.sleep(0.3)
    raise AssertionError(
        f"Tab count did not reach {n} in {app_id!r} within {seconds}s"
    )


# ── Terminal input ─────────────────────────────────────────────────────────

@step('Focus terminal in "{app_id}"')
def focus_terminal(context, app_id):
    """Click the terminal widget to give it keyboard focus."""
    app = getattr(context, app_id).instance
    terminal = _find_by_role(app, "terminal", timeout=5)
    if terminal is None:
        # Fall back to the drawing area (ghostty-vt renders into a custom widget)
        terminal = _find_by_role(app, "drawing area", timeout=5)
    if terminal is None:
        raise AssertionError(f"No terminal widget found in {app_id!r}")
    terminal.click()


@step('Type text "{text}" in "{app_id}"')
def type_text(context, text, app_id):
    """Type a string into the currently-focused widget using dogtail typeText."""
    app = getattr(context, app_id).instance
    # dogtail.rawinput.typeText sends key events via AT-SPI
    try:
        import dogtail.rawinput
        dogtail.rawinput.typeText(text)
    except Exception as exc:
        raise AssertionError(f"Failed to type text in {app_id!r}: {exc}") from exc


# ── Widget gone ───────────────────────────────────────────────────────────

@step('Wait until "{name}" "{role}" is gone from "{app_id}" within {seconds:d} seconds')
def wait_until_widget_gone(context, name, role, app_id, seconds):
    """Wait for a named widget (by role+name substring) to disappear."""
    app = getattr(context, app_id).instance
    deadline = time.time() + seconds
    while time.time() < deadline:
        try:
            matches = app.findChildren(
                lambda n: n.roleName == role
                and name.lower() in (n.name or "").lower()
            )
            if not matches:
                return
        except Exception:
            return  # app gone = widget gone
        time.sleep(0.3)
    raise AssertionError(
        f"{role!r} named {name!r} was still present in {app_id!r} after {seconds}s"
    )


# ── Application gone ──────────────────────────────────────────────────────

@step('Wait until application "{app_id}" is gone within {seconds:d} seconds')
def wait_until_gone(context, app_id, seconds):
    """Wait for the AT-SPI process to disappear (clean shutdown)."""
    deadline = time.time() + seconds
    while time.time() < deadline:
        try:
            from dogtail.tree import root
            root.application(app_id)
            # If we get here without exception, it's still running
        except Exception:
            return  # Process gone — success
        time.sleep(0.3)
    raise AssertionError(
        f"Application {app_id!r} was still running after {seconds}s"
    )


# ── Diagnostics ───────────────────────────────────────────────────────────

@step('Dump AT-SPI tree of "{app_id}" to artifact')
def dump_tree(context, app_id, path="/tmp/ptyxis-tree.txt"):
    """Dump the full accessibility tree for debugging failed selectors."""
    app = getattr(context, app_id).instance
    out = []

    def walk(node, depth=0):
        try:
            out.append("  " * depth + f"[{node.roleName}] {node.name!r}")
            for child in node.children:
                walk(child, depth + 1)
        except Exception:
            pass

    walk(app)
    import os
    target = os.environ.get("DOGTAIL_ARTIFACTS", "/tmp") + f"/{app_id}-tree.txt"
    with open(target, "w") as f:
        f.write("\n".join(out))
    print(f"AT-SPI tree dumped to {target}")
