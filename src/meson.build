phoc_enum_headers = files(
  [
    'phosh-private.h',
    'layer-shell-effects.h',
    'event.h',
  ])
phoc_enum_sources = gnome.mkenums_simple(
  'phoc-enums',
  sources : phoc_enum_headers)

phoc_marshalers = gnome.genmarshal('phoc-marshalers',
  sources : 'phoc-marshalers.list',
  prefix : '_phoc_marshal',
  valist_marshallers : true)

sources = files(
  'cursor.c',
  'cursor.h',
  'desktop.c',
  'desktop.h',
  'event.c',
  'event.h',
  'gesture.h',
  'gesture.c',
  'gesture-drag.h',
  'gesture-drag.c',
  'gesture-single.h',
  'gesture-single.c',
  'gesture-swipe.h',
  'gesture-swipe.c',
  'gtk-shell.c',
  'gtk-shell.h',
  'input.c',
  'input.h',
  'input-device.c',
  'input-device.h',
  'keyboard.c',
  'keyboard.h',
  'keybindings.c',
  'keybindings.h',
  'layer-surface.c',
  'layer-surface.h',
  'layer_shell.c',
  'layer-shell-effects.h',
  'layer-shell-effects.c',
  'layers.h',
  'output.c',
  'output.h',
  'phosh-private.c',
  'phosh-private.h',
  'pointer.c',
  'pointer.h',
  'render.c',
  'render.h',
  'render-private.h',
  'seat.c',
  'seat.h',
  'server.c',
  'server.h',
  'settings.c',
  'settings.h',
  'switch.c',
  'switch.h',
  'tablet.c',
  'tablet.h',
  'text_input.c',
  'text_input.h',
  'touch.c',
  'touch.h',
  'utils.c',
  'utils.h',
  'view.c',
  'view.h',
  'virtual.c',
  'virtual.h',
  'xdg-activation-v1.c',
  'xdg-activation-v1.h',
  'xdg_shell.c',
  'xdg-surface.c',
  'xdg-surface.h',
)
libphoc_generated_sources = [
  phoc_enum_sources,
  phoc_marshalers,
  server_protos_headers,
  protos_sources,
]

phoc_deps = [
  input,
  drm,
  gio,
  glesv2,
  gnome_desktop,
  gsettings_desktop_schemas_dep,
  math,
  pixman,
  wayland_server,
  wlroots,
  xkbcommon,
]

if have_xwayland
  sources += ['xwayland.c',
              'xwayland-surface.c',
              'xwayland-surface.h',
	     ]
  phoc_deps += dependency('xcb')
endif

phoc_lib = static_library(
  'phoc',
  sources,
  libphoc_generated_sources,
  dependencies: phoc_deps,
  install: false,
)

libphoc_dep = declare_dependency(
  include_directories: [include_directories('.'), protocol_inc],
  link_with: phoc_lib,
  dependencies: phoc_deps,
  sources: libphoc_generated_sources)

if get_option('gtk_doc')
  phoc_gir_extra_args = [
    '--quiet',
  ]

  if have_xwayland
    phoc_gir_extra_args += '-DPHOC_XWAYLAND'
  endif

  phoc_gir = gnome.generate_gir(phoc_lib,
                 sources: sources,
               nsversion: '0',
               namespace: 'Phoc',
         export_packages: 'phoc-0',
           symbol_prefix: 'phoc',
       identifier_prefix: 'Phoc',
                includes: ['Gio-2.0'],
                 install: false,
              extra_args: phoc_gir_extra_args,
            dependencies: [phoc_deps, libphoc_dep],
   )
endif

executable(
  'phoc',
  sources: 'main.c',
  dependencies: libphoc_dep,
  install: true,
)
