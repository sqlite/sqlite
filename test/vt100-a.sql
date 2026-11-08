/*
** Run this script using the "sqlite3" command-line shell
** test test formatting of output text that contains
** vt100 escape sequences.
*/
.open :memory:
.mode box -escape off
CREATE TEMP TABLE t1(a,b,c);
INSERT INTO t1 VALUES
  ('one','twotwotwo','thirty-three'),
  (unistr('\u001b[91mRED\u001b[0m'),'fourfour','fifty-five'),
  ('six','seven','eighty-eight');
.testcase 100
SELECT * FROM t1;
.check <<END
╭─────┬───────────┬──────────────╮
│  a  │     b     │      c       │
╞═════╪═══════════╪══════════════╡
│ one │ twotwotwo │ thirty-three │
│ [91mRED[0m │ fourfour  │ fifty-five   │
│ six │ seven     │ eighty-eight │
╰─────┴───────────┴──────────────╯
END

.mode box -escape ascii
.testcase 200
SELECT * FROM t1;
.check <<END
╭────────────────┬───────────┬──────────────╮
│       a        │     b     │      c       │
╞════════════════╪═══════════╪══════════════╡
│ one            │ twotwotwo │ thirty-three │
│ ^[[91mRED^[[0m │ fourfour  │ fifty-five   │
│ six            │ seven     │ eighty-eight │
╰────────────────┴───────────┴──────────────╯
END

.testcase 300
.mode box -escape symbol
SELECT * FROM t1;
.check <<END
╭──────────────┬───────────┬──────────────╮
│      a       │     b     │      c       │
╞══════════════╪═══════════╪══════════════╡
│ one          │ twotwotwo │ thirty-three │
│ ␛[91mRED␛[0m │ fourfour  │ fifty-five   │
│ six          │ seven     │ eighty-eight │
╰──────────────┴───────────┴──────────────╯
END
