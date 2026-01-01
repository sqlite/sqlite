#!sqlite3
/*
** Run this script using "sqlite3" to confirm that the command-line
** shell properly handles the output of double-width characters.
**
** https://sqlite.org/forum/forumpost/008ac80276
*/
.testcase 100
.mode box
CREATE TABLE data(word TEXT, description TEXT);
INSERT INTO data VALUES('〈οὐκέτι〉','Greek without dblwidth <...>');
SELECT * FROM data;
.check <<END
╭────────────┬──────────────────────────────╮
│    word    │         description          │
╞════════════╪══════════════════════════════╡
│ 〈οὐκέτι〉 │ Greek without dblwidth <...> │
╰────────────┴──────────────────────────────╯
END

.testcase 200
.mode table
SELECT * FROM data;
.check <<END
+------------+------------------------------+
|    word    |         description          |
+------------+------------------------------+
| 〈οὐκέτι〉 | Greek without dblwidth <...> |
+------------+------------------------------+
END

.testcase 300
.mode qbox
SELECT * FROM data;
.check <<END
╭──────────────┬────────────────────────────────╮
│     word     │          description           │
╞══════════════╪════════════════════════════════╡
│ '〈οὐκέτι〉' │ 'Greek without dblwidth <...>' │
╰──────────────┴────────────────────────────────╯
END

.testcase 400
.mode column
SELECT * FROM data;
.check <<END
   word             description
----------  ----------------------------
〈οὐκέτι〉  Greek without dblwidth <...>
END
