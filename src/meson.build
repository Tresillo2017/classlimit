classlimit_sources = [
  'main.c',
  'classlimit-application.c',
  'classlimit-window.c',
]

classlimit_deps = [
  dependency('gtk4'),
  dependency('libadwaita-1', version: '>= 1.4'),
  dependency('json-glib-1.0'),
]

classlimit_sources += gnome.compile_resources('classlimit-resources',
  'classlimit.gresource.xml',
  c_name: 'classlimit'
)

executable('classlimit', classlimit_sources,
  dependencies: classlimit_deps,
       install: true,
)
