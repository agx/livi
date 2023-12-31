livi_sources = [
  'main.c',
  'livi-application.c',
  'livi-controls.c',
  'livi-window.c',
  'livi-gst-paintable.c',
  'livi-gst-sink.c',
  'livi-url-processor.c',
]

gst_ver = '>= 1.22'

gstplay_dep = dependency('gstreamer-play-1.0', version: gst_ver)
gstgl_dep = dependency('gstreamer-gl-1.0', version: gst_ver)

livi_deps = [
  dependency('gio-2.0', version: '>= 2.50'),
  dependency('gtk4', version: '>= 4.12'),
  dependency('gstreamer-1.0', version: gst_ver),
  dependency('libadwaita-1', version: '>= 1.4'),
  cc.find_library('m', required: false),
  gstplay_dep,
  gstgl_dep,
]

gnome = import('gnome')

livi_sources += gnome.compile_resources('livi-resources',
  'livi.gresource.xml',
  c_name: 'livi',
  dependencies: appstream_file,
  source_dir: meson.current_build_dir(),
)

executable('livi', livi_sources,
  dependencies: livi_deps,
  install: true,
)
