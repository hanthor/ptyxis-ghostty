"""
Ptyxis (ghostty-vt) smoke-test environment — qecore TestSandbox.

Driven by behave with the GUI exposed over AT-SPI via dogtail. Runnable inside
`qecore-headless --session-type wayland` on CI, or locally in a GNOME Wayland
session.

The application under test is the dev.hanthor.PtyxisGhostty Flatpak installed
via `just flatpak-install`.
"""
import sys
import traceback

from qecore.sandbox import TestSandbox
from qecore.common_steps import *  # noqa: F401,F403


APP_ID = "dev.hanthor.PtyxisGhostty"
A11Y_APP_NAME = "ptyxis"


def before_all(context) -> None:
    try:
        context.sandbox = TestSandbox("ptyxis", context=context)
        context.sandbox.attach_faf = False
        context.sandbox.production = False

        context.ptyxis = context.sandbox.get_flatpak(
            flatpak_id=[APP_ID],
            a11y_app_name=A11Y_APP_NAME,
        )
        context.ptyxis.exit_shortcut = "<Ctrl>q"
    except Exception as error:
        print(f"Environment error: before_all: {error}")
        context.failed_setup = traceback.format_exc()
        raise RuntimeError("Smoke environment initialization failed") from error


def before_scenario(context, scenario) -> None:
    try:
        context.sandbox.before_scenario(context, scenario)
    except Exception:
        context.embed("text/plain", traceback.format_exc(), "Before Scenario Error")
        sys.exit(1)


def after_scenario(context, scenario) -> None:
    context.sandbox.after_scenario(context, scenario)
