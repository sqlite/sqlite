# 2025-12-15
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

set testdir [file dirname $argv0]
source $testdir/tester.tcl
set testprefix bestindexF

ifcapable !vtab {
  finish_test
  return
}


proc vtab_command {method args} {
  switch -- $method {
    xConnect {
      return "CREATE TABLE t1(a, b, c)"
    }

    xBestIndex {
      set hdl [lindex $args 0]
      set ::vtab_orderby [$hdl orderby]
      set ::vtab_distinct [$hdl distinct]

      if {$::vtab_orderby == "{column 0 desc 0} {column 1 desc 0}"
       || $::vtab_orderby == "{column 0 desc 0}"
      } {
        return [list orderby 1]
      }

      return ""
    }

    xFilter {
      set sql {
        SELECT 1, 1, 'a', 555
          UNION ALL
        SELECT 2, 1, 'a', NULL
          UNION ALL
        SELECT 3, 1, 'b', 'text'
          UNION ALL
        SELECT 4, 2, 'a', 3.14
          UNION ALL
        SELECT 5, 2, 'b', 0
      }
      return [list sql $sql]
    }
  }

  return {}
}

register_tcl_module db

do_execsql_test 1.0 {
  CREATE VIRTUAL TABLE t1 USING tcl(vtab_command)
}

proc uses_idxinsert {sql} {
  return [expr [lsearch [db eval "explain $sql"] IdxInsert]>=0]
}
proc do_idxinsert_test {tn sql res} {
  set uses [uses_idxinsert $sql]
  uplevel [list do_execsql_test $tn "SELECT $uses ; $sql" $res]
}

do_idxinsert_test 1.1.1 {
  SELECT DISTINCT a, b FROM t1 
} {0    1 a 1 b 2 a 2 b}

do_test 1.1.2 {
  list $::vtab_distinct $::vtab_orderby
} {2 {{column 0 desc 0} {column 1 desc 0}}}

do_execsql_test 1.3 {
  CREATE TABLE t0(c0);
  INSERT INTO t0 VALUES(0);
  INSERT INTO t0 VALUES(1);
}

do_idxinsert_test 1.4.1 {
  SELECT DISTINCT t0.c0 FROM t1, t0 ORDER BY t1.a;
} {1    0 1}

do_test 1.4.2 {
  list $::vtab_distinct $::vtab_orderby
} {3 {{column 0 desc 0}}}

#-------------------------------------------------------------------------
#
reset_db
proc vtab_command {method args} {
  switch -- $method {
    xConnect {
      return "CREATE TABLE t1(a, b, c)"
    }

    xBestIndex {
      set hdl [lindex $args 0]
      set ::vtab_orderby  [$hdl orderby]
      set ::vtab_distinct [$hdl distinct]

      # Set idxNum to 1 if DISTINCT is to be used in xFilter.
      #
      set idxStr [list ""]
      if {$::vtab_distinct==2 || $::vtab_distinct==3} {
        set idxStr [list DISTINCT]
      }

      set orderby 0
      if {$::vtab_orderby == "{column 0 desc 1}" 
       || $::vtab_orderby == "{column 0 desc 0}"
      } {
        set orderby 1
        if {$::vtab_distinct==1 || $::vtab_distinct==2} {
          lappend idxStr "ORDER BY ((a+2)%5)"
        } else {
          set sort "ORDER BY a"
          if {$::vtab_orderby == "{column 0 desc 1}"} {
            append sort " DESC"
          }
          lappend idxStr $sort
        }
      } else {
        lappend idxStr ""
      }

      return [list orderby $orderby idxstr $idxStr]
      return ""
    }

    xFilter {
      set idxstr [lindex $args 1]

      set distinct [lindex $idxstr 0]
      set orderby  [lindex $idxstr 1]
      set sql "
        SELECT $distinct 0, a, b FROM real_t1 $orderby
      "
      return [list sql $sql]
    }
  }

  return {}
}

do_execsql_test 2.0 {
  CREATE TABLE real_t1(a, b);

  INSERT INTO real_t1 VALUES (1, 'a');
  INSERT INTO real_t1 VALUES (2, 'a');
  INSERT INTO real_t1 VALUES (1, 'a');

  INSERT INTO real_t1 VALUES (2, 'b');
  INSERT INTO real_t1 VALUES (1, 'b');
  INSERT INTO real_t1 VALUES (2, 'b');

  INSERT INTO real_t1 VALUES (3, 'a');
  INSERT INTO real_t1 VALUES (4, 'b');
  INSERT INTO real_t1 VALUES (3, 'a');

  INSERT INTO real_t1 VALUES (4, 'b');
  INSERT INTO real_t1 VALUES (3, 'a');
  INSERT INTO real_t1 VALUES (4, 'b');
}

register_tcl_module db
do_execsql_test 2.0 {
  CREATE VIRTUAL TABLE t1 USING tcl(vtab_command)
}

do_execsql_test 2.1 {
  SELECT a, b FROM t1
} {
  1 a 2 a 1 a 
  2 b 1 b 2 b 
  3 a 4 b 3 a 
  4 b 3 a 4 b
}

# This is like do_execsql_test, except one value is prepended to the
# expected result - the P4 (idxStr) of the VFilter opcode. It is an error 
# if $sql generates two or more VFilter instructions.
#
proc do_vtabsorter_test {tn sql expect} {
  set vm [db eval "EXPLAIN $sql"]

  set ii [lsearch $vm VFilter]
  set ::res [lindex $vm [expr $ii+4]]

  set ::idx [expr [lsearch $vm IdxInsert]>=0]

  set iSort [lsearch $vm SorterSort]
  if {$iSort>=0} {
    error "query is using sorter"
  }

  uplevel [list do_test $tn.0 { set ::idx } [lindex $expect 0]]
  uplevel [list do_test $tn.1 { set ::res } [lindex $expect 1]]
  uplevel [list do_execsql_test $tn.2 $sql [lrange $expect 2 end]]
}

do_vtabsorter_test 2.2 {
  SELECT a, b FROM t1
} { 0 "{} {}"
  1 a 2 a 1 a 
  2 b 1 b 2 b 
  3 a 4 b 3 a 
  4 b 3 a 4 b
}

do_vtabsorter_test 2.3 {
  SELECT DISTINCT a FROM t1
} { 0 "DISTINCT {ORDER BY ((a+2)%5)}"
  3 4 1 2
}

do_vtabsorter_test 2.4 {
  SELECT DISTINCT a FROM t1 ORDER BY a
} { 0 "DISTINCT {ORDER BY a}"
  1 2 3 4
}

do_vtabsorter_test 2.5 {
  SELECT DISTINCT a FROM t1 ORDER BY a DESC
} { 0 "DISTINCT {ORDER BY a DESC}"
  4 3 2 1
}

do_vtabsorter_test 2.6 {
  SELECT a FROM t1 ORDER BY a
} { 0 "{} {ORDER BY a}"
  1 1 1
  2 2 2
  3 3 3
  4 4 4
}

do_vtabsorter_test 2.7 {
  SELECT a FROM t1 ORDER BY a DESC
} { 0 "{} {ORDER BY a DESC}"
  4 4 4
  3 3 3
  2 2 2
  1 1 1
}

do_vtabsorter_test 2.8 {
  SELECT a, count(*) FROM t1 GROUP BY a ORDER BY a
} { 0 "{} {ORDER BY a}"
  1 3
  2 3
  3 3
  4 3
}

do_vtabsorter_test 2.9 {
  SELECT a, count(*) FROM t1 GROUP BY a ORDER BY a DESC
} { 0 "{} {ORDER BY a DESC}"
  4 3
  3 3
  2 3
  1 3
}

do_vtabsorter_test 2.10 {
  SELECT a, count(*) FROM t1 GROUP BY a
} { 0 "{} {ORDER BY ((a+2)%5)}"
  3 3
  4 3
  1 3
  2 3
}

do_vtabsorter_test 2.11 {
  SELECT DISTINCT a, count(*) FROM t1 GROUP BY a
} { 1 "{} {ORDER BY ((a+2)%5)}"
  3 3
  4 3
  1 3
  2 3
}

finish_test

