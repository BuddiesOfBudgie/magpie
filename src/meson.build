magpie_sources = [
    'main.cpp',
    'foreign_toplevel.cpp',
    'output.cpp',
    'server.cpp',
    'xwayland.cpp',
    'input/constraint.cpp',
    'input/cursor.cpp',
    'input/keyboard.cpp',
    'input/keyboard_shortcuts_subscriber.cpp',
    'input/seat.cpp',
    'protocols/budgie_keyboard_shortcuts.cpp',
    'surface/layer.cpp',
    'surface/popup.cpp',
    'surface/subsurface.cpp',
    'surface/view.cpp',
    'surface/xdg_view.cpp',
    'surface/xwayland_view.cpp',
    budgie_keyboard_shortcuts_protocol,
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
    dependencies: [dep_argparse, dep_m, dep_wayland_server, dep_wlroots, dep_xcb, dep_xkbcommon],
    cpp_args: '-DPROJECT_VERSION="' + meson.project_version() + '"',
    install: true
)

glxgears = find_program('glxgears', required: false)
if glxgears.found()
    test('glxgears', exe, args: ['--debug', '--kiosk', 'sh -c "timeout 5 glxgears"'])
endif

vkgears = find_program('vkgears', required: false)
if vkgears.found()
    test('vkgears', exe, args: ['--debug', '--kiosk', 'sh -c "timeout 5 vkgears"'])
endif
