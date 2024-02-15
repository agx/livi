subdir('dbus')

livi_sources = [
  'main.c',
  'livi-application.c',
  'livi-controls.c',
  'livi-mpris.c',
  'livi-window.c',
  'livi-recent-videos.c',
  'livi-gst-paintable.c',
  'livi-gst-sink.c',
  'livi-url-processor.c',
] + generated_dbus_sources

gst_ver = '>= 1.22'

gstplay_dep = dependency('gstreamer-play-1.0', version: gst_ver)
gstgl_dep = dependency('gstreamer-gl-1.0', version: gst_ver)

gtk4_dep = dependency(
  'gtk4',
  version: '>= 4.13.7',
  fallback: ['gtk4', 'gtk_dep'],
  default_options: [
    'introspection=disabled',
    'documentation=false',
    'gtk_doc=false',
    'build-demos=false',
    'build-testsuite=false',
    'build-examples=false',
    'build-tests=false',
  ],
)

livi_deps = [
  dependency('gio-2.0', version: '>= 2.50'),
  dependency('gstreamer-1.0', version: gst_ver),
  dependency('libadwaita-1', version: '>= 1.4'),
  gtk4_dep,
  cc.find_library('m', required: false),
  gstplay_dep,
  gstgl_dep,
]

livi_sources += gnome.compile_resources('livi-resources',
  'livi.gresource.xml',
  c_name: 'livi',
  dependencies: appstream_file,
  source_dir: meson.current_build_dir())

executable('livi', livi_sources,
  dependencies: livi_deps,
  install: true,
)
