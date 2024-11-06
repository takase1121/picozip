project(
    'picozip',
    'c',
    version: '1.0.0',
    license: 'MIT',
    meson_version: '>=0.50.0',
    default_options: ['c_std=c89'],
)

picozip_cargs = []
if not get_option('stdio')
    picozip_cargs += '-DPICOZIP_NO_STDIO'
endif

if not get_option('os_mtime')
    picozip_cargs += '-DPICOZIP_NO_OS_MTIME'
endif

install_headers('picozip.h')

picozip_inc = include_directories('.')
picozip_src = ['picozip.c']

picozip_lib = library(
    'picozip',
    picozip_src,
    c_args: picozip_cargs,
    include_directories: picozip_inc,
)

picozip_dep = declare_dependency(
    link_with: picozip_lib,
    compile_args: picozip_cargs,
    include_directories: picozip_inc,
)

if get_option('tests')
    subdir('tests')
endif