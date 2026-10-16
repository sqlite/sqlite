.testcase 100
/* Unterminated SQL comment */
SELECT 1; -- hi

/* SQL comment terminted by ; */
SELECT 2; -- hi ;

/* A C-style commend across multiple lines */
SELECT 3; /* hi
and more
and more */

/* SQL text keeps growing */
SELECT 4
; SELECT 5
; SELECT 6
; SELECT 7;

/* CREATE TRIGGER */
CREATE TRIGGER xyz
  SELECT 8;
  SELECT 9;
  -- END;
END;
.check <<END
1 line: SELECT 1; -- hi ... 
1 line: SELECT 2; -- hi ; ... 
3 lines: SELECT 3; /* hi\nand more\nand more */
4 lines: SELECT 4\n; SELECT 5 ... SELECT 6\n; SELECT 7;
5 lines: CREATE TRIGGER xyz\n ... CT 9;\n  -- END;\nEND;
END
