project(
    'mctpwplus',
    'cpp',
    license: 'Apache-2.0',
    version: '0.1',
    meson_version: '>=1.1.1',
    default_options: ['warning_level=3', 'werror=false', 'cpp_std=c++23'],
)

build_examples = get_option('examples')
build_afmctp = get_option('afmctp')

boost_dep = dependency(
    'boost',
    required: false,
    version: '>=1.71',
    modules: ['context', 'coroutine'],
)
if not boost_dep.found()
    boost_proj = subproject('boost_1_83_0')
    boost_dep = boost_proj.get_variable('boost_dep').as_system()
endif
add_project_arguments(['-DBOOST_ASIO_NO_DEPRECATED'], language: 'cpp')

sdbusplus_dep = dependency('sdbusplus')
sdbusplus_partial_dep = sdbusplus_dep.partial_dependency(
    compile_args: false,
    link_args: true,
    includes: true,
    links: true,
)

phosphor_logging_dep = dependency('phosphor-logging')
phosphor_logging_partial_dep = phosphor_logging_dep.partial_dependency(
    compile_args: false,
    link_args: true,
    includes: true,
    links: true,
)

systemd = dependency('systemd', required: true)
systemd_system_unit_dir = systemd.get_pkgconfig_variable(
    'systemdsystemunitdir',
    define_variable: ['prefix', get_option('prefix')],
)

threads = dependency('threads')

if build_afmctp.enabled()
    src_files = ['afmctp-stack/mctp_wrapper.cpp', 'afmctp-stack/mctp_impl.cpp']
else
    src_files = ['mctp_wrapper.cpp', 'mctp_impl.cpp', 'socket_if.cpp']
endif

no_thread_flags = [
    '-DBOOST_ASIO_DISABLE_THREADS',
    '-DBOOST_NO_RTTI',
    '-DBOOST_NO_TYPEID',
]
no_thread_dep = declare_dependency(compile_args: no_thread_flags)

deps = [
    boost_dep,
    sdbusplus_partial_dep,
    phosphor_logging_partial_dep,
    systemd,
    threads,
]

deps_no_thread = [
    boost_dep,
    sdbusplus_partial_dep,
    phosphor_logging_partial_dep,
    systemd,
    no_thread_dep,
]

if build_afmctp.enabled()
    root_inc = include_directories('afmctp-stack')
else
    root_inc = include_directories('.')
endif

mctpwplus = shared_library(
    'mctpwplus',
    src_files,
    install: true,
    dependencies: deps,
    include_directories: root_inc,
    version: meson.project_version(),
)
mctpwplus_nothread = shared_library(
    'mctpwplus-nothread',
    src_files,
    install: true,
    dependencies: deps_no_thread,
    include_directories: root_inc,
    version: meson.project_version(),
)

mctpwplus_dep = declare_dependency(
    include_directories: root_inc,
    link_with: mctpwplus,
    dependencies: deps,
)
mctpwplus_nothread_dep = declare_dependency(
    include_directories: root_inc,
    link_with: mctpwplus_nothread,
    dependencies: deps_no_thread,
)

if build_afmctp.enabled()
    install_headers('afmctp-stack/mctp_wrapper.hpp')
else
    install_headers('mctp_wrapper.hpp')
endif


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
    requires: systemd,
)

pkg.generate(
    mctpwplus_nothread,
    name: meson.project_name() + '-nothread',
    version: meson.project_version(),
    description: 'C++ bindings for MCTP wrapper',
    requires: systemd,
    extra_cflags: no_thread_flags,
)

if (not meson.is_subproject() and get_option('tests').enabled())
    subdir('test')
endif
