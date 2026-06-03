@smoke_suite @ptyxis
Feature: Ptyxis (ghostty-vt) smoke tests
  Validates that the PtyxisGhostty Flatpak launches, exposes its main controls
  via AT-SPI, and key terminal features work correctly.

  Regression coverage for widget wiring — these scenarios fail loudly if
  the ghostty-vt integration breaks during refactors.

  Background:
    * Start application "ptyxis" via "command"
    * Wait until window "Ptyxis" appears in "ptyxis"

  # ── Launch & main window ────────────────────────────────────────────────

  @launch
  Scenario: Application window appears with a terminal
    * Application "ptyxis" is running
    * Item "New Tab" "button" is "showing" in "ptyxis"

  @launch
  Scenario: Terminal widget is present in the window
    * Application "ptyxis" is running
    * Wait until "terminal" role appears in "ptyxis" within 5 seconds

  # ── Menu access ─────────────────────────────────────────────────────────

  @menu
  Scenario: Main menu opens
    * Left click "Main Menu" "toggle button" in "ptyxis"
    * Item "Main Menu" "menu" is "showing" in "ptyxis"

  @menu
  Scenario: Keyboard shortcut Ctrl+question does not crash
    * Key combo: "<Control>question"
    * Application "ptyxis" is running

  # ── Preferences dialog ──────────────────────────────────────────────────

  @preferences
  Scenario: Preferences dialog opens via Ctrl+comma
    * Key combo: "<Control>comma"
    * Wait until "Preferences" "dialog" appears in "ptyxis" within 5 seconds

  # ── Cursor style ─────────────────────────────────────────────────────────

  @cursor @preferences
  Scenario: Cursor style can be changed to I-Beam
    * Key combo: "<Control>comma"
    * Wait until "Preferences" "dialog" appears in "ptyxis" within 5 seconds
    * Application "ptyxis" is in cursor style "I-Beam"
    * Application "ptyxis" is running

  @cursor @preferences
  Scenario: Cursor style can be changed to Underline
    * Key combo: "<Control>comma"
    * Wait until "Preferences" "dialog" appears in "ptyxis" within 5 seconds
    * Application "ptyxis" is in cursor style "Underline"
    * Application "ptyxis" is running

  @cursor @preferences
  Scenario: Cursor style can be changed back to Block
    * Key combo: "<Control>comma"
    * Wait until "Preferences" "dialog" appears in "ptyxis" within 5 seconds
    * Application "ptyxis" is in cursor style "Block"
    * Application "ptyxis" is running

  # ── Find bar ─────────────────────────────────────────────────────────────

  @search
  Scenario: Find bar opens with Ctrl+Shift+F
    * Key combo: "<Control><Shift>f"
    * Wait until "Search" "text" appears in "ptyxis" within 5 seconds

  @search
  Scenario: Find bar closes with Escape
    * Key combo: "<Control><Shift>f"
    * Wait until "Search" "text" appears in "ptyxis" within 5 seconds
    * Key combo: "Escape"
    * Application "ptyxis" is running

  # ── New tab ──────────────────────────────────────────────────────────────

  @tabs
  Scenario: New tab opens with Ctrl+Shift+T
    * Key combo: "<Control><Shift>t"
    * Wait until tab count in "ptyxis" is at least 2 within 5 seconds

  # ── Input passthrough ────────────────────────────────────────────────────

  @input
  Scenario: Terminal accepts keyboard input without crashing
    * Focus terminal in "ptyxis"
    * Type text "echo hello_ptyxis_test" in "ptyxis"
    * Application "ptyxis" is running

  # ── Focus preservation ───────────────────────────────────────────────────

  @cursor @focus @regression
  Scenario: Terminal still accepts input after cursor style change via Preferences
    # Regression: non-modal prefs window did not return focus to terminal
    * Key combo: "<Control>comma"
    * Wait until "Preferences" "dialog" appears in "ptyxis" within 5 seconds
    * Application "ptyxis" is in cursor style "I-Beam"
    * Key combo: "<Alt>F4"
    * Wait until "Preferences" "dialog" is gone from "ptyxis" within 5 seconds
    * Focus terminal in "ptyxis"
    * Type text "echo focus_regression_ok" in "ptyxis"
    * Application "ptyxis" is running

  # ── Clean shutdown ───────────────────────────────────────────────────────

  @shutdown
  Scenario: Application exits cleanly via Ctrl+Q
    * Key combo: "<Control>q"
    * Wait until application "ptyxis" is gone within 5 seconds
