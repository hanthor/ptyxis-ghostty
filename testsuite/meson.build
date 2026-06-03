test_env = [
  'G_TEST_SRCDIR=@0@'.format(meson.current_source_dir()),
  'G_TEST_BUILDDIR=@0@'.format(meson.current_build_dir()),
  'G_DEBUG=gc-friendly',
  'GSETTINGS_BACKEND=memory',
  'MALLOC_CHECK_=2',
  # Suppress a11y bus errors in headless CI (fatal in GTK4/Fedora 43)
  'GTK_A11Y=none',
  # Force X11 backend so Xvfb is used; avoids Wayland/XDG_RUNTIME_DIR issues
  'GDK_BACKEND=x11',
  # Software rendering for headless CI (meson test doesn't inherit container env)
  'LIBGL_ALWAYS_SOFTWARE=1',
  'GALLIUM_DRIVER=llvmpipe',
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

# Container flow tests (GLib only)
container_tests = {
  'test-container-flow' : {},
}

# Widget tests (link against ghostty, need widget source)
widget_tests = {
  'test-ghostty-widget' : {'sources': []},
}

# Ghostty API tests (link against ghostty, no widget source needed)
ghostty_api_tests = {
  'test-ghostty-config' : {'sources': []},
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

# --- Container flow tests ---
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

# --- Widget tests ---
widget_test_deps = [
  ptyxis_deps,
  dependency('gtk4', version: gtk_req),
]

foreach test, params: widget_tests
  extra = params.get('sources', [])
  test_exe = executable(test,
    ['@0@.c'.format(test), '../src/ptyxis-ghostty-widget.c'] + extra,
                 c_args: testsuite_c_args + ['-DGHOSTTY_STATIC', '-DGHOSTTY_TEST_BUILD', '-Wno-error=strict-prototypes'],
           dependencies: widget_test_deps,
          link_args: ptyxis_link_args,
    include_directories: [
      include_directories('..'),
      include_directories('.'),
      include_directories('../src'),
      include_directories(ghty_vt_include),
    ],
  )
  if not params.get('skip', false)
    test(test, test_exe, env: test_env)
  endif
endforeach

# --- Ghostty API tests ---
foreach test, params: ghostty_api_tests
  extra = params.get('sources', [])
  test_exe = executable(test,
    ['@0@.c'.format(test)] + extra,
                 c_args: testsuite_c_args + ['-DGHOSTTY_STATIC', '-Wno-error=strict-prototypes'],
           dependencies: widget_test_deps,
          link_args: ptyxis_link_args,
    include_directories: [
      include_directories('..'),
      include_directories('.'),
      include_directories('../src'),
      include_directories(ghty_vt_include),
    ],
  )
  if not params.get('skip', false)
    test(test, test_exe, env: test_env)
  endif
endforeach
