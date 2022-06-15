fs = import('fs')

cli11_dep = dependency('CLI11')

deps_no_thread += cli11_dep
deps_no_thread += mctpwplus_nothread_dep

r = run_command('find', '.', '-name', '*.cpp', check: true)
str_files = r.stdout().strip()
list_files = str_files.split('\n')

ex_targets = []
foreach ex_src : list_files
    ex_src_files = [ex_src]
    ex_name = fs.replace_suffix(fs.name(ex_src), '')
    ex_exe = executable(ex_name, ex_src_files, dependencies: deps_no_thread)
    ex_targets += ex_exe
endforeach