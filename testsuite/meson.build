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

# Container flow tests (link against main sources + agent IPC)
container_tests = {
  'test-container-flow' : {},
}

# Widget/Config tests (link against full ghostty)
widget_tests = {
  'test-ghostty-widget' : {},
  'test-ghostty-config' : {},
}

# Ghostty VT terminal behavior tests (link against ghostty-vt only)
vt_tests = {
  'test-ghostty-terminal' : {},
}

# --- Agent tests ---
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

# --- Widget/Config tests ---
widget_test_deps = [
  ptyxis_deps,
  dependency('gtk4', version: gtk_req),
]

foreach test, params: widget_tests
  test_exe = executable(test,
    ['@0@.c'.format(test), '../src/ptyxis-ghostty-widget.c', '../src/ptyxis-ghostty-config.c'],
                 c_args: testsuite_c_args + ['-DGHOSTTY_STATIC'],
           dependencies: widget_test_deps,
          link_args: ptyxis_link_args + ['-lstdc++'],
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

# --- Container flow tests (GLib only, no heavy ptyxis deps) ---
foreach test, params: container_tests
  test_exe = executable(test,
    ['@0@.c'.format(test)],
                 c_args: testsuite_c_args,
           dependencies: [dependency('glib-2.0')],
    include_directories: [
      include_directories('..'),
      include_directories('.'),
    ],
  )
  if not params.get('skip', false)
    test(test, test_exe, env: test_env)
  endif
endforeach

# --- Ghostty VT terminal behavior tests ---
# Links against libghostty-vt.a (lighter weight, VT-only library)
vt_test_link_args = [
  '-L' + ghty_libdir,
  '-l:libghostty-vt.a',
]

vt_test_deps = [
  dependency('glib-2.0'),
]

foreach test, params: vt_tests
  test_exe = executable(test,
    ['@0@.c'.format(test)],
                 c_args: testsuite_c_args,
           dependencies: vt_test_deps,
          link_args: vt_test_link_args,
    include_directories: [
      include_directories('..'),
      include_directories('.'),
      include_directories(ghty_include),
    ],
  )
  if not params.get('skip', false)
    test(test, test_exe, env: test_env)
  endif
endforeach
