fs = import('fs')

cli11_dep = dependency('cli11')
has_cli11 = meson.get_compiler('cpp').has_header_symbol(
    'CLI/CLI.hpp',
    'CLI::App',
    dependencies: cli11_dep,
    required: false)
if not has_cli11
    cli11_proj = subproject('cli11')
    assert(cli11_proj.found(), 'CLI11 is required')
    cli11_dep = cli11_proj.get_variable('CLI11_dep')
endif
deps_no_thread += mctpwplus_nothread_dep

r = run_command('find', '.', '-name', '*.cpp', check: true)
str_files = r.stdout().strip()
list_files = str_files.split('\n')

ex_targets = []
foreach ex_src : list_files
    ex_src_files = [ex_src]
    ex_name = fs.replace_suffix(fs.name(ex_src), '')
    ex_exe = executable(ex_name, ex_src_files, dependencies: [deps_no_thread, cli11_dep])
    ex_targets += ex_exe
endforeach
