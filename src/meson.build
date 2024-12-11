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
gst_allocators_dep = dependency('gstreamer-allocators-1.0', version: gst_ver)

dmabuf_passthrough = false
if not get_option('dmabuf-passthrough').disabled()
  if gst_allocators_dep.version().version_compare('>= 1.23.1')
    dmabuf_passthrough = true
  endif
endif
config_h.set('HAVE_GSTREAMER_DRM', dmabuf_passthrough)

gtk4_dep = dependency(
  'gtk4',
  version: '>= 4.16.0',
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
  gst_allocators_dep,
  dependency('gstreamer-gl-1.0', version: gst_ver),
  dependency('gstreamer-play-1.0', version: gst_ver),
  dependency('libadwaita-1', version: '>= 1.4'),
  gtk4_dep,
  cc.find_library('m', required: false),
]

resource_data = configuration_data()
resource_data.set('APP_ID', app_id)
livi_resource = configure_file(
  input: 'livi.gresource.xml.in',
  output: 'livi.gresource.xml',
  configuration: resource_data,
)

livi_sources += gnome.compile_resources('livi-resources',
  livi_resource,
  c_name: 'livi',
  dependencies: appstream_file,
  source_dir: meson.current_build_dir())

executable('livi', livi_sources,
  dependencies: livi_deps,
  install: true,
)
