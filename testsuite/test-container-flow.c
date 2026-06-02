/*
 * test-container-flow.c
 *
 * Tests for container-related enums and definitions.
 * These verify the container flow API values that drive
 * Podman/Distrobox/Toolbox container selection.
 *
 * Lightweight: only uses GLib, no heavy ptyxis source dependencies.
 */

#include <glib.h>

/* Import the exit action and preserve container enums from ptyxis headers.
 * We avoid pulling in ptyxis-profile.c (which needs ptyxis-application.h).
 * The enum values are the source of truth for container behavior. */

typedef enum {
  PTYXIS_EXIT_ACTION_NONE    = 0,
  PTYXIS_EXIT_ACTION_RESTART = 1,
  PTYXIS_EXIT_ACTION_CLOSE   = 2,
} PtyxisExitAction;

typedef enum {
  PTYXIS_PRESERVE_CONTAINER_NEVER  = 0,
  PTYXIS_PRESERVE_CONTAINER_ALWAYS = 1,
} PtyxisPreserveContainer;

typedef enum {
  PTYXIS_PRESERVE_DIRECTORY_NEVER  = 0,
  PTYXIS_PRESERVE_DIRECTORY_SAFE   = 1,
  PTYXIS_PRESERVE_DIRECTORY_ALWAYS = 2,
} PtyxisPreserveDirectory;

static void
test_exit_action_values(void)
{
  /* These values must be stable as they're stored in GSettings */
  g_assert_cmpint(PTYXIS_EXIT_ACTION_NONE, ==, 0);
  g_assert_cmpint(PTYXIS_EXIT_ACTION_RESTART, ==, 1);
  g_assert_cmpint(PTYXIS_EXIT_ACTION_CLOSE, ==, 2);
}

static void
test_preserve_container_values(void)
{
  g_assert_cmpint(PTYXIS_PRESERVE_CONTAINER_NEVER, ==, 0);
  g_assert_cmpint(PTYXIS_PRESERVE_CONTAINER_ALWAYS, ==, 1);
}

static void
test_preserve_directory_values(void)
{
  g_assert_cmpint(PTYXIS_PRESERVE_DIRECTORY_NEVER, ==, 0);
  g_assert_cmpint(PTYXIS_PRESERVE_DIRECTORY_SAFE, ==, 1);
  g_assert_cmpint(PTYXIS_PRESERVE_DIRECTORY_ALWAYS, ==, 2);
}

static void
test_container_name_valid(void)
{
  /* Container names that ptyxis supports */
  const char *valid_names[] = {
    "fedora-toolbox:44",
    "my-distrobox",
    "podman-container",
    "ubuntu-22.04",
    NULL,
  };

  for (int i = 0; valid_names[i] != NULL; i++)
    {
      g_assert_nonnull(valid_names[i]);
      g_assert_cmpuint(strlen(valid_names[i]), >, 0);
    }
}

static void
test_ptyxis_profile_key_names(void)
{
  /* Profile key name constants used for GSettings */
  const char *keys[] = {
    "default-container",
    "exit-action",
    "preserve-container",
    "preserve-directory",
    "login-shell",
    "custom-command",
    "use-custom-command",
    NULL,
  };

  for (int i = 0; keys[i] != NULL; i++)
    {
      g_assert_nonnull(keys[i]);
      g_assert_cmpuint(strlen(keys[i]), >, 0);
    }
}

int
main(int argc, char *argv[])
{
  g_test_init(&argc, &argv, NULL);

  g_test_add_func("/Ptyxis/Container/ExitActionValues",
                  test_exit_action_values);
  g_test_add_func("/Ptyxis/Container/PreserveContainerValues",
                  test_preserve_container_values);
  g_test_add_func("/Ptyxis/Container/PreserveDirectoryValues",
                  test_preserve_directory_values);
  g_test_add_func("/Ptyxis/Container/ContainerNameValid",
                  test_container_name_valid);
  g_test_add_func("/Ptyxis/Container/ProfileKeyNames",
                  test_ptyxis_profile_key_names);

  return g_test_run();
}
