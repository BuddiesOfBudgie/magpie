magpie_sources = [
    'main.c',
    'input.c',
    'output.c',
    'popup.c',
    'server.c',
    'surface.c',
    'xdg_view.c',
    'xwayland.c',
    'xwayland_view.c',
    xdg_shell_protocol,
]

exe = executable(
    'magpie',
    sources: magpie_sources,
    dependencies: [dep_m, dep_wayland_server, dep_wlroots, dep_xcb, dep_xkbcommon],
    install: true
)
