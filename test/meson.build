gtest_dep = dependency('gtest', required:false)
if not gtest_dep.found()
    gtest_subproject = cmake.subproject('gtest')
    cm_gtest = gtest_subproject.dependency('gtest')
    cm_gtest_main = gtest_subproject.dependency('gtest_main')
    threads = dependency('threads')
    gtest_dep = declare_dependency(dependencies: [cm_gtest,
            cm_gtest_main, threads])
endif

test_src_files = ['test_create_objects']
ex_targets = []
foreach ex_src : test_src_files
    ex_src_files = [ex_src + '.cpp']
    ex_name = ex_src.replace('test_', '')
    ex_exe = executable(ex_name, ex_src_files, dependencies: [mctpwplus_nothread_dep, gtest_dep])
    ex_targets += ex_exe
    test( ex_src.replace('_', ' '), ex_exe, is_parallel : false)
endforeach
