dbus_inc = include_directories('.')

# DBus client interfaces
generated_dbus_sources = []

dbus_prefix = 'LiviDBus'

generated_dbus_sources += gnome.gdbus_codegen('livi-mpris-dbus',
                                              'org.mpris.MediaPlayer2.xml',
                                              interface_prefix: 'org.mpris',
                                              namespace: dbus_prefix)
