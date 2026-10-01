.load ./fossildelta
.mode qbox -quote relaxed
.testcase 100
SELECT op, a1 FROM delta_parse(delta_create(x'01',x'02'));
.check <<END
╭──────────┬──────────╮
│    op    │    a1    │
╞══════════╪══════════╡
│ SIZE     │        1 │
│ INSERT   │        1 │
│ CHECKSUM │ 33554432 │
╰──────────┴──────────╯
END

.testcase 110
CREATE TABLE t1(x,y,d);
INSERT INTO t1(x,y) VALUES
   (X'01', X'02'),
   (X'0101', X'0202'),
   (X'010101', X'020202');
UPDATE t1 SET d = delta_create(x,y);
SELECT x,op,a1,a2 FROM t1, delta_parse(d);
.check <<END
╭───────────┬──────────┬──────────┬───────────╮
│     x     │    op    │    a1    │    a2     │
╞═══════════╪══════════╪══════════╪═══════════╡
│ x'01'     │ SIZE     │        1 │ NULL      │
│ x'01'     │ INSERT   │        1 │ x'02'     │
│ x'01'     │ CHECKSUM │ 33554432 │ NULL      │
│ x'0101'   │ SIZE     │        2 │ NULL      │
│ x'0101'   │ INSERT   │        2 │ x'0202'   │
│ x'0101'   │ CHECKSUM │ 33685504 │ NULL      │
│ x'010101' │ SIZE     │        3 │ NULL      │
│ x'010101' │ INSERT   │        3 │ x'020202' │
│ x'010101' │ CHECKSUM │ 33686016 │ NULL      │
╰───────────┴──────────┴──────────┴───────────╯
END
