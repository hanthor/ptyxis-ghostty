test_env = [
  'G_TEST_SRCDIR=@0@'.format(meson.current_source_dir()),
  'G_TEST_BUILDDIR=@0@'.format(meson.current_build_dir()),
  'G_DEBUG=gc-friendly',
  'GSETTINGS_BACKEND=memory',
  'MALLOC_CHECK_=2',
]

testsuite_c_args = [
  '-DG_LOG_DOMAIN="ptyxis"',
  '-DDISABLE_AGENT_ENTRY_POINT',
  '-DG_ENABLE_DEBUG',
  '-UG_DISABLE_ASSERT',
  '-UG_DISABLE_CAST_CHECKS',
]

# Agent tests (podman JSON parsing, etc.)
agent_tests = {
  'test-podman' : {},
}

# Container flow tests (need main source files)
container_tests = {
  'test-container-flow' : {},
}

agent_test_deps = [
  ptyxis_agent_deps,
]

foreach test, params: agent_tests
  test_exe = executable(test,
    ['@0@.c'.format(test)] + ptyxis_agent_sources,
                 c_args: testsuite_c_args,
           dependencies: agent_test_deps,
    include_directories: [
      include_directories('..'),
      include_directories('.'),
      include_directories('../agent'),
    ],
  )
  if not params.get('skip', false)
    test(test, test_exe, env: test_env)
  endif
endforeach

# Widget tests (link against ghostty and GTK)
widget_tests = {
  'test-ghostty-widget' : {},
  'test-ghostty-config' : {},
}

container_test_deps = [
  ptyxis_deps,
  dependency('gio-unix-2.0', version: glib_req),
]

widget_test_deps = [
  ptyxis_deps,
  dependency('gtk4', version: gtk_req),
]

foreach test, params: container_tests
  test_exe = executable(test,
    ['@0@.c'.format(test), '../src/ptyxis-client.c', '../src/ptyxis-application.c', '../src/ptyxis-profile.c', '../src/ptyxis-util.c'],
                 c_args: testsuite_c_args,
           dependencies: container_test_deps,
    include_directories: [
      include_directories('..'),
      include_directories('.'),
      include_directories('../src'),
      include_directories('../agent'),
    ],
  )
  if not params.get('skip', false)
    test(test, test_exe, env: test_env)
  endif
endforeach

foreach test, params: widget_tests
  test_exe = executable(test,
    ['@0@.c'.format(test), '../src/ptyxis-ghostty-widget.c', '../src/ptyxis-ghostty-config.c'],
                 c_args: testsuite_c_args + ['-DGHOSTTY_STATIC'],
           dependencies: widget_test_deps,
          link_args: ptyxis_link_args,
    include_directories: [
      include_directories('..'),
      include_directories('.'),
      include_directories('../src'),
      include_directories(ghty_include),
    ],
  )
  if not params.get('skip', false)
    test(test, test_exe, env: test_env)
  endif
endforeach
