project(
    'waybar_workspace_buttons', 'c',
    version: '0.1.0',
    license: 'MIT',
    default_options: ['c_std=gnu11']
)

shared_library('workspace_buttons',
    ['src/workspace_buttons.c'],
    dependencies: [
        dependency('gtk+-3.0', version: ['>=3.22.0']),
        dependency('threads'),
    ],
    include_directories: include_directories('include'),
    name_prefix: '',
    install: false
)
