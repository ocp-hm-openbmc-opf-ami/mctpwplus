project(
    'mctpwplus',
    'cpp',
    license: 'Apache-2.0',
    version: '0.1',
    default_options: [
        'warning_level=3',
        'werror=false',
        'cpp_std=c++20'
    ],
)

build_examples = get_option('examples')

boost_dep = declare_dependency(
    dependencies: dependency('boost', required: false, version : '>=1.71',  
    modules: [ 'context', 'coroutine']))

sdbusplus_dep = dependency('sdbusplus')
sdbusplus_partial_dep = sdbusplus_dep.partial_dependency(compile_args : false, 
     link_args: true, includes : true, links: true)

phosphor_logging_dep = dependency('phosphor-logging')
phosphor_logging_partial_dep = phosphor_logging_dep.partial_dependency(compile_args : false, 
     link_args: true, includes : true, links: true)

systemd = dependency('systemd', required: true)
systemd_system_unit_dir = systemd.get_pkgconfig_variable(
    'systemdsystemunitdir',
    define_variable: ['prefix', get_option('prefix')])

threads = dependency('threads')

src_files = ['mctp_wrapper.cpp', 'mctp_impl.cpp']
no_thread_flags = '-DBOOST_ASIO_DISABLE_THREADS'
no_thread_dep = declare_dependency(compile_args: no_thread_flags)

deps = [
    boost_dep,
    sdbusplus_partial_dep,
    phosphor_logging_partial_dep,
    systemd,
    threads
]

deps_no_thread = [
    boost_dep,
    sdbusplus_partial_dep,
    phosphor_logging_partial_dep,
    systemd,
    no_thread_dep
]

root_inc = include_directories('.')

mctpwplus = shared_library('mctpwplus', src_files, install : true, dependencies : deps, include_directories: root_inc, version: meson.project_version())
mctpwplus_nothread = shared_library('mctpwplus-nothread', src_files, install : true, dependencies : deps_no_thread, include_directories: root_inc, version: meson.project_version())

mctpwplus_dep = declare_dependency(include_directories: root_inc,
    link_with: mctpwplus,
    dependencies: deps
)
mctpwplus_nothread_dep = declare_dependency(include_directories: root_inc,
    link_with: mctpwplus_nothread,
    dependencies: deps_no_thread
)

install_headers('mctp_wrapper.hpp')

if build_examples.enabled()
    subdir('examples')
endif

# TODO Libs.private contains build directory paths. Remove them
pkg = import('pkgconfig')
pkg.generate(
    mctpwplus,
    name: meson.project_name(),
    version: meson.project_version(),
    description: 'C++ bindings for MCTP wrapper',
    requires: systemd
)

pkg.generate(
    mctpwplus_nothread,
    name: meson.project_name() + '-nothread',
    version: meson.project_version(),
    description: 'C++ bindings for MCTP wrapper',
    requires: systemd,
    extra_cflags: no_thread_flags,
)
