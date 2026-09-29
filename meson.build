project(
    'waybar_workspace_buttons', 'c',
    version: '1.4.0',
    license: 'MIT',
    default_options: ['c_std=gnu11']
)

# xdg-output: the module reads each bar's connector name straight from the
# compositor (see docs/decisions/2026-09-29-resolve-output-name-via-xdg-output.md).
wayland_client = dependency('wayland-client', version: '>=1.15')
wayland_protocols = dependency('wayland-protocols', version: '>=1.14')
wayland_scanner = find_program('wayland-scanner', native: true)
xdg_output_xml = wayland_protocols.get_variable(pkgconfig: 'pkgdatadir') / 'unstable' / 'xdg-output' / 'xdg-output-unstable-v1.xml'
xdg_output_src = [
    custom_target('xdg-output-client-header',
        input: xdg_output_xml,
        output: 'xdg-output-unstable-v1-client-protocol.h',
        command: [wayland_scanner, 'client-header', '@INPUT@', '@OUTPUT@']),
    custom_target('xdg-output-private-code',
        input: xdg_output_xml,
        output: 'xdg-output-unstable-v1-protocol.c',
        command: [wayland_scanner, 'private-code', '@INPUT@', '@OUTPUT@']),
]

shared_library('workspace_buttons',
    ['src/workspace_buttons.c', xdg_output_src],
    dependencies: [
        dependency('gtk+-3.0', version: ['>=3.22.0']),
        dependency('gtk-layer-shell-0'),
        dependency('threads'),
        wayland_client,
    ],
    include_directories: include_directories('include'),
    name_prefix: '',
    install: true,
    install_dir: get_option('libdir') / 'waybar'
)

install_man('man/workspace-zones.7')
