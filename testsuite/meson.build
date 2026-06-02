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

agent_test_deps = [
  ptyxis_agent_deps,
]

# Container flow tests (GLib only, no heavy ptyxis deps)
container_tests = {
  'test-container-flow' : {},
}

# Widget tests (link against ghostty)
widget_tests = {
  'test-ghostty-widget' : {'sources': []},
}


# --- Widget/Config tests ---
widget_test_deps = [
  ptyxis_deps,
  dependency('gtk4', version: gtk_req),
]

foreach test, params: widget_tests
  extra = params.get('sources', [])
  test_exe = executable(test,
    ['@0@.c'.format(test), '../src/ptyxis-ghostty-widget.c'] + extra,
                 c_args: testsuite_c_args + ['-DGHOSTTY_STATIC', '-DGHOSTTY_TEST_BUILD'],
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

