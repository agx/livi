datadir = get_option('datadir')

config_data = configuration_data()
config_data.set('APP_ID', app_id)

desktop_file = i18n.merge_file(
  input: configure_file(
    input: 'org.sigxcpu.Livi.desktop.in.in',
    output: '@0@.desktop.in'.format(app_id),
    configuration: config_data,
  ),
  output: '@0@.desktop'.format(app_id),
  type: 'desktop',
  po_dir: '../po',
  install: true,
  install_dir: join_paths(datadir, 'applications')
)

desktop_utils = find_program('desktop-file-validate', required: false)
if desktop_utils.found()
  test('Validate desktop file', desktop_utils,
    args: [desktop_file]
  )
endif

appstream_file = i18n.merge_file(
  input: configure_file(
    input: 'org.sigxcpu.Livi.metainfo.xml.in.in',
    output: '@0@.metainfo.xml.in'.format(app_id),
    configuration: config_data,
  ),
  output: '@0@.metainfo.xml'.format(app_id),
  po_dir: '../po',
  install: true,
  install_dir: join_paths(get_option('datadir'), 'metainfo')
)

appstream_util = find_program('appstreamcli', required: false)
if appstream_util.found()
  test('Validate appstream file', appstream_util,
    args: ['validate', '--no-net', appstream_file]
  )
endif

install_data('org.sigxcpu.Livi.gschema.xml',
  install_dir: join_paths(get_option('datadir'), 'glib-2.0/schemas')
)

compile_schemas = find_program('glib-compile-schemas', required: false)
if compile_schemas.found()
  test('Validate schema file', compile_schemas,
    args: ['--strict', '--dry-run', meson.current_source_dir()]
  )
endif
compiled = gnome.compile_schemas(
  build_by_default: true
)

install_data('@0@.svg'.format(app_id),
  install_dir: join_paths(
    datadir,
    'icons',
    'hicolor',
    'scalable',
    'apps'
  )
)
