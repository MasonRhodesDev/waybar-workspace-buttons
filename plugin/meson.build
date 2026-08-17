project('workspace-zones', 'cpp',
  version: '0.3.0',
  default_options: ['buildtype=release'],
)

cpp_compiler = meson.get_compiler('cpp')
if cpp_compiler.has_argument('-std=c++26')
  add_global_arguments('-std=c++26', language: 'cpp')
elif cpp_compiler.has_argument('-std=c++23')
  add_global_arguments('-std=c++23', language: 'cpp')
else
  error('C++23-capable compiler required')
endif

add_global_arguments('-DWLR_USE_UNSTABLE', language: 'cpp')
add_global_arguments('-fno-gnu-unique', language: 'cpp')

shared_module(meson.project_name(), ['src/main.cpp'],
  dependencies: [
    dependency('hyprland'),
    dependency('pixman-1'),
    dependency('libdrm'),
  ],
  install: true,
  install_dir: get_option('libdir') / 'hyprland' / 'plugins',
)
