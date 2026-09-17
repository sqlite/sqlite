# Developer Notes

## Measuring Test Coverage On Linux

On Mint Linux, as of 2025-12-02:

> ~~~
./configure --dev CFLAGS='-O0 -g -fprofile-arcs -ftest-coverage'
make clean testfixture
./testfixture test/qrf*.test
gcov -b -c testfixture-tclsqlite-ex.c
~~~

View results in tclsqlite-ex.c.gcov
