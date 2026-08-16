# 2009 February 27
#
# The author disclaims copyright to this source code.  In place of
# a legal notice, here is a blessing:
#
#    May you do good and not evil.
#    May you find forgiveness for yourself and forgive others.
#    May you share freely, never taking more than you give.
#
#***********************************************************************
#
# $Id: temptrigger.test,v 1.3 2009/04/15 13:07:19 drh Exp $

set testdir [file dirname $argv0]
source $testdir/tester.tcl
set testprefix temptrigger

ifcapable {!trigger || !shared_cache} { finish_test ; return }

# Test cases:
#
#   temptrigger-1.*: Shared cache problem.
#   temptrigger-2.*: A similar shared cache problem.
#   temptrigger-3.*: Attached database problem.
#

#-------------------------------------------------------------------------
# Test case temptrigger-1.* demonstrates a problem with temp triggers
# in shared-cache mode. If process 1 connections to a shared-cache and
# creates a temp trigger, the temp trigger is linked into the shared-cache
# schema. If process 2 reloads the shared-cache schema from disk, then
# it does not recreate the temp trigger belonging to process 1. From the
# point of view of process 1, the temp trigger just disappeared.
# 
#   temptrigger-1.1: In shared cache mode, create a table in the main 
#                    database and add a temp trigger to it.
#
#   temptrigger-1.2: Check that the temp trigger is correctly fired. Check
#                    that the temp trigger is not fired by statements
#                    executed by a second connection connected to the 
#                    same shared cache.
#
#   temptrigger-1.3: Using the second connection to the shared-cache, cause
#                    the shared-cache schema to be reloaded.
#
#   temptrigger-1.4: Check that the temp trigger is still fired correctly.
#
#   temptrigger-1.5: Check that the temp trigger can be dropped without error.
#
db close
set ::enable_shared_cache [sqlite3_enable_shared_cache]
sqlite3_enable_shared_cache 1

sqlite3 db test.db
sqlite3 db2 test.db

do_test temptrigger-1.1 {
  execsql {
    CREATE TABLE t1(a, b);
    CREATE TEMP TABLE tt1(a, b);
    CREATE TEMP TRIGGER tr1 AFTER INSERT ON t1 BEGIN
      INSERT INTO tt1 VALUES(new.a, new.b);
    END;
  }
} {}

do_test temptrigger-1.2.1 {
  execsql { INSERT INTO t1 VALUES(1, 2) }
  execsql { SELECT * FROM t1 }
} {1 2}
do_test temptrigger-1.2.2 {
  execsql { SELECT * FROM tt1 }
} {1 2}
do_test temptrigger-1.2.3 {
  execsql { INSERT INTO t1 VALUES(3, 4) } db2
  execsql { SELECT * FROM t1 }
} {1 2 3 4}
do_test temptrigger-1.2.4 {
  execsql { SELECT * FROM tt1 }
} {1 2}

# Cause the shared-cache schema to be reloaded.
#
do_test temptrigger-1.3 {
  execsql { BEGIN; CREATE TABLE t3(a, b); ROLLBACK; } db2
} {}

do_test temptrigger-1.4 {
  execsql { INSERT INTO t1 VALUES(5, 6) }
  execsql { SELECT * FROM tt1 }
} {1 2 5 6}

do_test temptrigger-1.5 {
  # Before the bug was fixed, the following 'DROP TRIGGER' hit an 
  # assert if executed.
  #execsql { DROP TRIGGER tr1 }
} {}

catch {db close}
catch {db2 close}

#-------------------------------------------------------------------------
# Tests temptrigger-2.* are similar to temptrigger-1.*, except that
# temptrigger-2.3 simply opens and closes a connection to the shared-cache.
# It does not do anything special to cause the schema to be reloaded.
# 
do_test temptrigger-2.1 {
  sqlite3 db test.db
  execsql {
    DELETE FROM t1;
    CREATE TEMP TABLE tt1(a, b);
    CREATE TEMP TRIGGER tr1 AFTER INSERT ON t1 BEGIN
      INSERT INTO tt1 VALUES(new.a, new.b);
    END;
  }
} {}
do_test temptrigger-2.2 {
  execsql {
    INSERT INTO t1 VALUES(10, 20);
    SELECT * FROM tt1;
  }
} {10 20}
do_test temptrigger-2.3 {
  sqlite3 db2 test.db
  db2 close
} {}
do_test temptrigger-2.4 {
  execsql {
    INSERT INTO t1 VALUES(30, 40);
    SELECT * FROM tt1;
  }
} {10 20 30 40}
do_test temptrigger-2.5 {
  #execsql { DROP TRIGGER tr1 }
} {}

catch {db close}
catch {db2 close}
sqlite3_enable_shared_cache $::enable_shared_cache

#-------------------------------------------------------------------------
# Test case temptrigger-3.* demonstrates a problem with temp triggers
# on tables located in attached databases. At one point when SQLite reloaded 
# the schema of an attached database (because some other connection had 
# changed the schema cookie) it was not re-creating temp triggers attached 
# to tables located within the attached database.
# 
#   temptrigger-3.1: Attach database 'test2.db' to connection [db]. Add a
#                    temp trigger to a table in 'test2.db'.
#
#   temptrigger-3.2: Check that the temp trigger is correctly fired.
#
#   temptrigger-3.3: Update the schema of 'test2.db' using an external
#                    connection. This forces [db] to reload the 'test2.db'
#                    schema. Check that the temp trigger is still fired
#                    correctly.
#
#   temptrigger-3.4: Check that the temp trigger can be dropped without error.
# 
do_test temptrigger-3.1 {
  catch { forcedelete test2.db test2.db-journal }
  catch { forcedelete test.db test.db-journal }
  sqlite3 db test.db 
  sqlite3 db2 test2.db 
  execsql { CREATE TABLE t2(a, b) } db2
  execsql {
    ATTACH 'test2.db' AS aux;
    CREATE TEMP TABLE tt2(a, b);
    CREATE TEMP TRIGGER tr2 AFTER INSERT ON aux.t2 BEGIN
      INSERT INTO tt2 VALUES(new.a, new.b);
    END;
  }
} {}

do_test temptrigger-3.2.1 {
  execsql { 
    INSERT INTO aux.t2 VALUES(1, 2);
    SELECT * FROM aux.t2;
  }
} {1 2}
do_test temptrigger-3.2.2 {
  execsql { SELECT * FROM tt2 }
} {1 2}

do_test temptrigger-3.3.1 {
  execsql { CREATE TABLE t3(a, b) } db2
  execsql { 
    INSERT INTO aux.t2 VALUES(3, 4);
    SELECT * FROM aux.t2;
  }
} {1 2 3 4}
do_test temptrigger-3.3.2 {
  execsql { SELECT * FROM tt2 }
} {1 2 3 4}

do_test temptrigger-3.4 {
  # Before the bug was fixed, the following 'DROP TRIGGER' hit an 
  # assert if executed.
  #execsql { DROP TRIGGER tr2 }
} {}

catch { db close }
catch { db2 close }


#-------------------------------------------------------------------------
# Test that creating a temp table after a temp trigger on the same name
# has been created is an error.
#
reset_db
do_execsql_test 4.0 {
  CREATE TABLE t1(x);
  CREATE TEMP TRIGGER tr1 BEFORE INSERT ON t1 BEGIN
    SELECT 1,2,3;
  END;
}

do_execsql_test 4.1 {
  CREATE TEMP TABLE t1(x);
}

#-------------------------------------------------------------------------
# Test that no harm is done if the table a temp trigger is attached to is
# deleted by an external connection.
#
reset_db
do_execsql_test 5.0 {
  CREATE TABLE t1(x);
  CREATE TEMP TRIGGER tr1 BEFORE INSERT ON t1 BEGIN SELECT 1,2,3; END;
}

do_test 5.1 {
  sqlite3 db2 test.db
  execsql { DROP TABLE t1 } db2
} {}

do_execsql_test 5.2 {
  SELECT * FROM sqlite_master;
  SELECT * FROM temp.sqlite_master;
} {
  trigger tr1 t1 0 
  {CREATE TRIGGER tr1 BEFORE INSERT ON t1 BEGIN SELECT 1,2,3; END}
}
db2 close

#-------------------------------------------------------------------------
# Check that if a second connection creates a table in an attached database
# with the same name as a table in the main database that has a temp
# trigger attached to it nothing goes awry.
#
reset_db
forcedelete test.db2

do_execsql_test 6.0 {
  CREATE TABLE t1(x);
  CREATE TEMP TRIGGER tr1 BEFORE INSERT ON t1 BEGIN 
    SELECT raise(ABORT, 'error'); 
  END;
  ATTACH 'test.db2' AS aux;
}

do_test 6.1 {
  sqlite3 db2 test.db2
  execsql { CREATE TABLE t1(a, b, c); } db2
} {}

do_execsql_test 6.2 {
  SELECT type,name,tbl_name,sql FROM aux.sqlite_master;
  INSERT INTO aux.t1 VALUES(1,2,3);
} {
  table t1 t1 {CREATE TABLE t1(a, b, c)}
}

do_catchsql_test 6.3 {
  INSERT INTO main.t1 VALUES(1);
} {1 error}
db2 close

#-------------------------------------------------------------------------
reset_db
forcedelete test.db2

do_execsql_test 7.0 {
  CREATE TABLE m1(a, b);
  ATTACH 'test.db2' AS aux;
  CREATE TABLE aux.a1(c, d);
}

do_execsql_test 7.1 {
  CREATE TEMP TRIGGER tr1 AFTER INSERT ON m1 BEGIN
    INSERT INTO a1 VALUES(new.a, new.b);
  END;

  INSERT INTO m1 VALUES(5, 6);
  SELECT * FROM aux.a1;
} {5 6}

do_execsql_test 7.2 {
  CREATE TABLE a1(e, f);
  INSERT INTO m1 VALUES(7, 8);
}

do_execsql_test 7.3.1 { SELECT * FROM main.a1 } {7 8}
do_execsql_test 7.3.2 { SELECT * FROM  aux.a1 } {5 6}

do_execsql_test 7.4 {
  DROP TRIGGER tr1;
  CREATE TEMP TRIGGER tr1 AFTER INSERT ON m1 BEGIN
    INSERT INTO a1 SELECT d, c FROM aux.a1;
  END;

  DELETE FROM aux.a1;
  DELETE FROM main.a1;
  INSERT INTO aux.a1 VALUES('hello', 'world');
}

do_execsql_test 7.5 {
  INSERT INTO m1 VALUES(9, 10);
  SELECT * FROM main.a1;
} {world hello}

do_catchsql_test 7.6 {
  DROP TRIGGER tr1;
  CREATE TRIGGER tr1 AFTER INSERT ON m1 BEGIN
    INSERT INTO a1 SELECT d, c FROM aux.a1;
  END;
} {1 {trigger tr1 cannot reference objects in database aux}}

#-------------------------------------------------------------------------
# Check that temp triggers may INSERT/UPDATE/DELETE to fully qualified
# table names.
reset_db
forcedelete {*}[glob -nocomplain *mj*]
forcedelete test.db2
do_execsql_test 8.0 {
  ATTACH 'test.db2' AS aux;
  CREATE TABLE t1(a, b);
  CREATE TABLE t2(c, d);
  CREATE TABLE aux.t1(e, f);
  CREATE TABLE aux.t2(g, h);
}

do_catchsql_test 8.1.1 {
  CREATE TRIGGER tr1 AFTER INSERT ON t2 BEGIN
    INSERT INTO aux.t1 VALUES(new.c, new.d);
  END;
} {1 {qualified table names are not allowed on INSERT, UPDATE, and DELETE statements within triggers}}

do_execsql_test 8.1.2 {
  CREATE TEMP TRIGGER tr1 AFTER INSERT ON t2 BEGIN
    INSERT INTO aux.t1 VALUES(new.c, new.d);
  END;

  INSERT INTO main.t2 VALUES('x', 'y');
  SELECT * FROM aux.t1;
} {x y}

do_execsql_test 8.1.3 { SELECT * FROM t1 } {}

do_catchsql_test 8.2.1 {
  CREATE TRIGGER aux.tr2 AFTER UPDATE ON aux.t1 BEGIN
    UPDATE main.t2 SET c=new.e, d=new.f;
  END;
} {1 {qualified table names are not allowed on INSERT, UPDATE, and DELETE statements within triggers}}

do_execsql_test 8.2.2 {
  CREATE TEMP TRIGGER tr2 AFTER UPDATE ON aux.t1 BEGIN
    UPDATE main.t2 SET c=new.e, d=new.f;
  END;

  UPDATE aux.t1 SET e=1, f=2;
  SELECT * FROM t2;
} {1 2}

do_execsql_test 8.2.3 { SELECT * FROM aux.t2 } {}

do_catchsql_test 8.3.1 {
  CREATE TRIGGER tr3 AFTER DELETE ON t2 BEGIN
    DELETE FROM aux.t1;
  END;
} {1 {qualified table names are not allowed on INSERT, UPDATE, and DELETE statements within triggers}}

do_execsql_test 8.3.2 {
  INSERT INTO main.t1 VALUES('a', 'b');
  CREATE TEMP TRIGGER tr3 AFTER DELETE ON t2 BEGIN
    DELETE FROM aux.t1;
  END;

  DELETE FROM main.t2;
  SELECT * FROM aux.t1;
} {}

do_execsql_test 8.3.3 { SELECT * FROM t1 } {a b}

#-------------------------------------------------------------------------
reset_db
set nDb 8
do_test 9.0 {
  for {set ii 0} {$ii < $nDb} {incr ii} {
    db eval "ATTACH ':memory:' AS db$ii"
    db eval "CREATE TABLE db$ii.tbl(a, b, c)"
  }

  for {set ii 0} {$ii < ($nDb-1)} {incr ii} {
    set jj [expr $ii+1]
    db eval "
      CREATE TEMP TRIGGER tr$ii AFTER INSERT ON db$ii.tbl BEGIN
        INSERT INTO db$jj.tbl VALUES(new.b, new.c, new.a);
      END;
    "
  }
} {}

do_execsql_test 9.1 { INSERT INTO db0.tbl VALUES('a', 'b', 'c'); }
do_execsql_test 9.1.1 { SELECT * FROM db0.tbl } {a b c}
do_execsql_test 9.1.2 { SELECT * FROM db1.tbl } {b c a}
do_execsql_test 9.1.3 { SELECT * FROM db2.tbl } {c a b}
do_execsql_test 9.1.1 { SELECT * FROM db3.tbl } {a b c}
do_execsql_test 9.1.2 { SELECT * FROM db4.tbl } {b c a}
do_execsql_test 9.1.3 { SELECT * FROM db5.tbl } {c a b}
do_execsql_test 9.1.1 { SELECT * FROM db6.tbl } {a b c}
do_execsql_test 9.1.2 { SELECT * FROM db7.tbl } {b c a}

do_test 9.2 {
  for {set ii 0} {$ii < ($nDb-1)} {incr ii} {
    set jj [expr $ii+1]
    db eval "
      CREATE TEMP TRIGGER tru$ii AFTER UPDATE ON db$ii.tbl BEGIN
        UPDATE db$jj.tbl SET a=new.b, b=new.c, c=new.a;
      END;
    "
  }
} {}

do_execsql_test 9.3 { UPDATE db0.tbl SET a=1, b=2, c=3 }
do_execsql_test 9.3.1 { SELECT * FROM db0.tbl } {1 2 3}
do_execsql_test 9.3.2 { SELECT * FROM db1.tbl } {2 3 1}
do_execsql_test 9.3.3 { SELECT * FROM db2.tbl } {3 1 2}
do_execsql_test 9.3.1 { SELECT * FROM db3.tbl } {1 2 3}
do_execsql_test 9.3.2 { SELECT * FROM db4.tbl } {2 3 1}
do_execsql_test 9.3.3 { SELECT * FROM db5.tbl } {3 1 2}
do_execsql_test 9.3.1 { SELECT * FROM db6.tbl } {1 2 3}
do_execsql_test 9.3.2 { SELECT * FROM db7.tbl } {2 3 1}

do_test 9.4 {
  for {set ii 0} {$ii < ($nDb-1)} {incr ii} {
    set jj [expr $ii+1]
    db eval "
      CREATE TEMP TRIGGER trd$ii BEFORE DELETE ON db$ii.tbl BEGIN
        DELETE FROM db$jj.tbl;
      END;
    "
  }
} {}

do_execsql_test 9.5 { DELETE FROM db0.tbl }
do_execsql_test 9.5.1 { SELECT * FROM db0.tbl } {}
do_execsql_test 9.5.2 { SELECT * FROM db1.tbl } {}
do_execsql_test 9.5.3 { SELECT * FROM db2.tbl } {}
do_execsql_test 9.5.1 { SELECT * FROM db3.tbl } {}
do_execsql_test 9.5.2 { SELECT * FROM db4.tbl } {}
do_execsql_test 9.5.3 { SELECT * FROM db5.tbl } {}
do_execsql_test 9.5.1 { SELECT * FROM db6.tbl } {}
do_execsql_test 9.5.2 { SELECT * FROM db7.tbl } {}

finish_test
