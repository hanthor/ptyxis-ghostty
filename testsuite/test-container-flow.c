/*
 * test-container-flow.c
 *
 * Tests for the container PTY lifecycle flow with ghostty backend.
 * These test the API layer, not actual container execution.
 */

#include <glib.h>
#include "ptyxis-client.h"
#include "ptyxis-application.h"
#include "ptyxis-profile.h"

static void
test_create_pty_fd_api(void)
{
  /* Test that the FD-based API exists and handles failure gracefully.
   * The client will fail because there's no agent running, but it
   * should return -1 cleanly (not crash). */
  PtyxisClient *client;
  g_autoptr(GError) error = NULL;
  int fd;

  client = ptyxis_client_new(FALSE, &error);
  /* Client may fail to create if no agent is running - that's OK */
  if (client == NULL)
    {
      g_test_skip("No ptyxis agent available for container test");
      return;
    }

  /* create_pty_fd should return -1 when no agent is connected */
  fd = ptyxis_client_create_pty_fd(client, &error);
  g_assert_cmpint(fd, ==, -1);
  g_assert_nonnull(error);

  g_object_unref(client);
}

static void
test_application_create_pty_fd(void)
{
  PtyxisApplication *app;
  g_autoptr(GError) error = NULL;
  int fd;

  app = PTYXIS_APPLICATION_DEFAULT;
  if (app == NULL)
    {
      g_test_skip("PtyxisApplication not initialized");
      return;
    }

  /* Should fail gracefully without agent */
  fd = ptyxis_application_create_pty_fd(app, &error);
  g_assert_cmpint(fd, ==, -1);
}

static void
test_profile_container_settings(void)
{
  PtyxisProfile *profile;
  g_autofree char *container = NULL;

  profile = ptyxis_profile_new("test-container-profile");
  g_assert_nonnull(profile);

  /* Default container should be NULL or empty */
  container = ptyxis_profile_dup_default_container(profile);
  g_assert_null(container);

  /* Set and get container */
  ptyxis_profile_set_default_container(profile, "fedora-toolbox:43");
  g_free(container);
  container = ptyxis_profile_dup_default_container(profile);
  g_assert_cmpstr(container, ==, "fedora-toolbox:43");

  g_object_unref(profile);
}

static void
test_profile_exit_action_restart(void)
{
  PtyxisProfile *profile;

  profile = ptyxis_profile_new("test-exit-profile");
  g_assert_nonnull(profile);

  /* Default exit action should be NONE */
  g_assert_cmpint(ptyxis_profile_get_exit_action(profile), ==,
                  PTYXIS_EXIT_ACTION_NONE);

  /* Set to RESTART */
  ptyxis_profile_set_exit_action(profile, PTYXIS_EXIT_ACTION_RESTART);
  g_assert_cmpint(ptyxis_profile_get_exit_action(profile), ==,
                  PTYXIS_EXIT_ACTION_RESTART);

  g_object_unref(profile);
}

int
main(int argc, char *argv[])
{
  g_test_init(&argc, &argv, NULL);

  g_test_add_func("/Ptyxis/Container/CreatePtyFdApi",
                  test_create_pty_fd_api);
  g_test_add_func("/Ptyxis/Container/AppCreatePtyFd",
                  test_application_create_pty_fd);
  g_test_add_func("/Ptyxis/Container/ProfileContainerSettings",
                  test_profile_container_settings);
  g_test_add_func("/Ptyxis/Container/ProfileExitAction",
                  test_profile_exit_action_restart);

  return g_test_run();
}
