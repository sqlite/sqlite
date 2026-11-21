-- Demonstrates: negative zero survives REAL affinity, so an index on
-- a generated column gives the same answer as the original expression.
-- atan2(Y,-1.0) is -pi for Y=-0.0 and +pi for Y=+0.0.
.if capable('ENABLE_MATH_FUNCTIONS')
.mode --reset quote
CREATE TABLE t1(n, g REAL AS (CAST(n AS REAL)) VIRTUAL);
INSERT INTO t1(n) VALUES(CAST('-0' AS REAL)), (0.0);
CREATE INDEX t1g ON t1(g, n);

.testcase 100
SELECT atan2(CAST(n AS REAL), -1.0) FROM t1 NOT INDEXED ORDER BY rowid;
.check <<END
-3.1415926535897931
3.1415926535897931
END

.testcase 110
SELECT atan2(CAST(n AS REAL), -1.0) FROM t1 INDEXED BY t1g ORDER BY rowid;
.check <<END
-3.1415926535897931
3.1415926535897931
END

.testcase 120
SELECT atan2(g, -1.0) FROM t1 INDEXED BY t1g ORDER BY rowid;
.check <<END
-3.1415926535897931
3.1415926535897931
END

.testcase 130
CREATE TABLE t2(r REAL);
INSERT INTO t2 VALUES(-0.0), (0.0);
SELECT atan2(r, -1.0), typeof(r) FROM t2 ORDER BY rowid;
.check <<END
-3.1415926535897931,'real'
3.1415926535897931,'real'
END
.endif
