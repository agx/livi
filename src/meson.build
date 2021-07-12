livi_sources = [
  'main.c',
  'livi-window.c',
  'livi-gst-paintable.c',
  'livi-gst-sink.c',
]

gstplayer_dep = dependency('gstreamer-player-1.0', version: '>= 1.12.3')
gstgl_dep = dependency('gstreamer-gl-1.0', version: '>= 1.12.3')

livi_deps = [
  dependency('gio-2.0', version: '>= 2.50'),
  dependency('gtk4', version: '>= 4.0'),
  dependency('gstreamer-1.0'),
  dependency('libadwaita-1', version: '>= 1.0.0-alpha.1'),
  cc.find_library('m', required: false),
  gstplayer_dep,
  gstgl_dep,
]

gnome = import('gnome')

livi_sources += gnome.compile_resources('livi-resources',
  'livi.gresource.xml',
  c_name: 'livi'
)

executable('livi', livi_sources,
  dependencies: livi_deps,
  install: true,
)
