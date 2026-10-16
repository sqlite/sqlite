# 2026-10-14
#
# Compile the CLI with -DSQLITE_ENABLE_SANDBOX_FUNCTION then run this
# script to verify the sandbox() function.
#
CREATE TABLE t1(a,b,c,d);
INSERT INTO t1 VALUES(1,2,3,4),('a','b','c','d'),(9,8,7,6);
CREATE INDEX t1a ON t1(a);
CREATE INDEX t1bc ON t1(b,c);
CREATE TABLE t2(x,y,z);
INSERT INTO t2 values(1,2,3),('x','y','z'),(1,0,-1);

.testcase 100
SELECT * FROM t1, t2 WHERE sandbox(12345, a=x, 12346);
.check --glob 'sandbox violation'
.testcase 110
SELECT * FROM t1, t2 WHERE sandbox(1, (SELECT 1)=x, 1);
.check --glob 'sandbox violation'
.testcase 120
SELECT * FROM t1, t2 WHERE sandbox($abc, a=x, $abc);
.check --glob 'sandbox violation'
.testcase 130
SELECT * FROM t1, t2 WHERE sandbox(2, a=x, 2, 4);
.check --glob 'sandbox violation'
.testcase 140
SELECT * FROM t1, t2 WHERE sandbox(2, a=x) AND a=x;
.check --glob 'sandbox violation'
.testcase 150
SELECT * FROM t1, t2 WHERE sandbox() AND a=x;
.check --glob 'sandbox violation'
.testcase 160
SELECT * FROM t1, t2 WHERE sandbox(0,a=x,0) AND sandbox(0,b=y,0);
.check --glob 'sandbox violation'
.testcase 170
SELECT * FROM t1, t2 WHERE sandbox(0,a=x,0) AND sandbox(1,b=y,1);
.check --glob ''

.testcase 200
.eqp on
.mode list --colsep , --rowsep ";"
SELECT a, x FROM t1, t2 WHERE sandbox(3, b<>11 AND a=x AND y<>22, 3);
.check <<END
QUERY PLAN
|--SCAN t2
`--SEARCH t1 USING INDEX t1a (a=?)
1,1;1,1;
END

.testcase 210
.eqp off
.mode list --colsep ,
SELECT count(*) FROM pragma_compile_options
 WHERE compile_options LIKE '%sandbox%';
.check 1
