# Bug 2026-10-15T04:34:08Z

.mode list -limits off
.null NULL

.testcase 100
SELECT decimal_pow2(3);
.check '+8.0e+00'
.testcase 101
SELECT decimal_pow2(4294967299);
.check --glob 'value 4294967299 is too large: range limit -2147483648..+2147483647'
.testcase 102
SELECT decimal_pow2(-4294967293);
.check --glob 'value -4294967293 is too large: range limit -2147483648..+2147483647'
.testcase 110
SELECT length(decimal(1.23456,10000));
.check 51
.testcase 120
SELECT length(dtostr(3.14159, 100000));
.check 357
