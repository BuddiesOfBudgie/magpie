magpie_sources = [
    'main.cpp',
    'foreign_toplevel.cpp',
    'output.cpp',
    'server.cpp',
    'xwayland.cpp',
    'input/constraint.cpp',
    'input/cursor.cpp',
    'input/keyboard.cpp',
    'input/seat.cpp',
    'surface/layer.cpp',
    'surface/popup.cpp',
    'surface/view.cpp',
    'surface/xdg_view.cpp',
    'surface/xwayland_view.cpp',
    content_type_protocol,
    cursor_shape_protocol,
    xdg_shell_protocol,
    wlr_layer_shell_protocol,
    wlr_output_power_management_protocol,
    wlr_pointer_constraints_protocol,
]

exe = executable(
    'magpie-wm',
    sources: magpie_sources,
    dependencies: [dep_m, dep_wayland_server, dep_wlroots, dep_xcb, dep_xkbcommon],
    install: true
)
