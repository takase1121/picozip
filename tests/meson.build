test_files = [
    configure_file(input: 'test.txt', output: 'test.txt', copy: true, install: false),
    configure_file(input: 'test2.txt', output: 'test2.txt', copy: true, install: false),
]

tests_exe = executable(
    'picozip_tests',
    'tests.c',
    dependencies: [picozip_dep],
    extra_files: test_files
)
test('picozip_test', tests_exe)