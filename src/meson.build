magpie_sources = [
    'main.cpp',
    'foreign_toplevel.cpp',
    'layer.cpp',
    'output.cpp',
    'popup.cpp',
    'server.cpp',
    'surface.cpp',
    'xdg_view.cpp',
    'xwayland.cpp',
    'xwayland_view.cpp',
    'input/cursor.cpp',
    'input/keyboard.cpp',
    'input/seat.cpp',
    xdg_shell_protocol,
    wlr_layer_shell_protocol,
]

exe = executable(
    'magpie',
    sources: magpie_sources,
    dependencies: [dep_m, dep_wayland_server, dep_wlroots, dep_xcb, dep_xkbcommon],
    install: true
)
