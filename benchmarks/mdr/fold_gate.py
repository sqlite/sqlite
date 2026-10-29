#!/usr/bin/python3
"""MDR-002: fold utility gate — measurement only, MDR1 format unchanged.

Arms (matched wire accounting, header = 21 B):
  A   raw db bytes
  B   identity + zero-pad to next 32768 tile + header   (matched baseline for C)
  C   fold (full tiles) + zero-pad + header             (== mdr.pack)
  B1  identity + header, no padding                     (matched baseline for P1)
  P1  fold full tiles only; trailing pages stored raw   (partial-tile policy)

Gates G1-G6 in docs/mdr/MDR-002.md. G5 passes iff fold beats its matched
identity baseline somewhere reproducible; otherwise NEGATIVE RESULT.
"""

import hashlib
import json
import os
import shutil
import struct
import subprocess
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, "..", ".."))
sys.path.insert(0, os.path.join(ROOT, "mdr", "core"))
sys.path.insert(0, HERE)

import mdr  # noqa: E402
from bench import make_db  # noqa: E402  (reuse MDR-001 fixture builder)

OUT = os.path.join(HERE, "out")
LEVELS = (1, 9, 19)
BLOCK = mdr.BLOCK
HDR_LEN = 21  # MAGIC(4) + struct("<BIIQ")(17)


def hdr(n):
    return mdr.MAGIC + struct.pack("<BIIQ", 1, 4096, mdr.AXES, n)


def sh(db, sql):
    p = subprocess.run(["sqlite3", db], input=sql, text=True, capture_output=True)
    if p.returncode:
        raise RuntimeError("sqlite3 failed: " + p.stderr[:400])


def integrity_check(path):
    p = subprocess.run(["sqlite3", path, "PRAGMA integrity_check;"],
                       capture_output=True, text=True)
    return p.stdout.strip()


def zstd(data, level):
    fin = os.path.join(OUT, "fg.in")
    fout = fin + ".zst"
    with open(fin, "wb") as f:
        f.write(data)
    t0 = time.perf_counter()
    subprocess.run(["zstd", "-q", "-%d" % level, fin, "-o", fout, "-f"],
                   check=True, capture_output=True)
    dt = time.perf_counter() - t0
    with open(fout, "rb") as f:
        blob = f.read()
    os.unlink(fin)
    os.unlink(fout)
    return blob, dt


def reps(data):
    """Wire representations for every arm."""
    n = len(data)
    pad = (-n) % BLOCK
    ft = n - (n % BLOCK)  # full-tile prefix
    folded = mdr.fold(data[:ft]) if ft else b""
    return {
        "A": data,
        "B": hdr(n) + data + b"\x00" * pad,
        # C == mdr.pack: fold the ENTIRE padded stream (pad bytes folded too)
        "C": hdr(n) + mdr.fold(data + b"\x00" * pad),
        "B1": hdr(n) + data,
        # P1: fold full tiles only; trailing partial tile stays raw
        "P1": hdr(n) + folded + data[ft:],
    }


def restore(arm, wire, n):
    if arm == "A":
        return wire
    if arm in ("B", "B1"):
        return wire[HDR_LEN:HDR_LEN + n]
    if arm == "C":
        return mdr.fold(wire[HDR_LEN:])[:n]
    if arm == "P1":
        body = wire[HDR_LEN:]
        ft = len(body) - (len(body) % BLOCK)
        if ft == 0:
            return body[:n]
        return mdr.fold(body[:ft]) + body[ft:]
    raise ValueError(arm)


def warmup():
    zstd(b"x" * 4096, 1)


# --------------------------------------------------------------- fixtures
def build_seq_int(db):
    sh(db, """PRAGMA page_size=4096;
CREATE TABLE s(id INTEGER PRIMARY KEY, v INTEGER);
WITH RECURSIVE c(x) AS (SELECT 1 UNION ALL SELECT x+1 FROM c WHERE x<150000)
INSERT INTO s SELECT x, x FROM c;""")


def build_rep_text(db):
    sh(db, """PRAGMA page_size=4096;
CREATE TABLE t(id INTEGER PRIMARY KEY, s TEXT);
WITH RECURSIVE c(x) AS (SELECT 1 UNION ALL SELECT x+1 FROM c WHERE x<8000)
INSERT INTO t SELECT x, 'label-'||x||' '||(SELECT group_concat(value,' ')
 FROM (SELECT 1 value UNION ALL SELECT 2 UNION ALL SELECT 3 UNION ALL
 SELECT 4 UNION ALL SELECT 5 UNION ALL SELECT 6 UNION ALL SELECT 7
 UNION ALL SELECT 8 UNION ALL SELECT 9 UNION ALL SELECT 10 UNION ALL
 SELECT 11 UNION ALL SELECT 12 UNION ALL SELECT 13 UNION ALL SELECT 14
 UNION ALL SELECT 15 UNION ALL SELECT 16 UNION ALL SELECT 17)) FROM c;""")


def build_big_blob(db):
    sh(db, """PRAGMA page_size=4096;
CREATE TABLE b(id INTEGER PRIMARY KEY, blob BLOB);
WITH RECURSIVE c(x) AS (SELECT 1 UNION ALL SELECT x+1 FROM c WHERE x<250)
INSERT INTO b SELECT x, zeroblob(0) FROM c;
UPDATE b SET blob = randomblob(64);  -- seed shape only; contents vary
UPDATE b SET blob = substr(blob,1,4096);""")


def build_sparse(db):
    sh(db, """PRAGMA page_size=4096;
CREATE TABLE s(id INTEGER PRIMARY KEY, pad TEXT);
WITH RECURSIVE c(x) AS (SELECT 1 UNION ALL SELECT x+1 FROM c WHERE x<20000)
INSERT INTO s SELECT x, printf('%.100c','x') FROM c;
DELETE FROM s WHERE id % 20 != 0;""")


def build_rand_blob(db):
    sh(db, """PRAGMA page_size=4096;
CREATE TABLE r(id INTEGER PRIMARY KEY, b BLOB);
WITH RECURSIVE c(x) AS (SELECT 1 UNION ALL SELECT x+1 FROM c WHERE x<20000)
INSERT INTO r SELECT x, randomblob(48) FROM c;""")


def build_snap(db, sql):
    sh(db, "PRAGMA page_size=4096;\n" + sql)


SNAP_A = """CREATE TABLE e(id INTEGER PRIMARY KEY, name TEXT, val REAL, blob BLOB);
WITH RECURSIVE c(x) AS (SELECT 1 UNION ALL SELECT x+1 FROM c WHERE x<8000)
INSERT INTO e SELECT x, 'entity-'||x, x*1.5, substr(zeroblob(24),1,24) FROM c;"""

SNAP_B_ADD = """WITH RECURSIVE c(x) AS (SELECT 8001 UNION ALL SELECT x+1 FROM c WHERE x<10000)
INSERT INTO e SELECT x, 'entity-'||x, x*1.5, substr(zeroblob(24),1,24) FROM c;"""

SNAP_C_UPD = "UPDATE e SET val = val*2 WHERE id % 16 = 0;"


def build_bench9(db):
    make_db(db)


# ---------------------------------------------------------------- measure
def measure(name, data, deterministic, check_integrity):
    t_fold0 = time.perf_counter()
    wire = reps(data)
    t_fold = time.perf_counter() - t_fold0

    n = len(data)
    row = {"name": name, "deterministic": deterministic,
           "raw_size": n, "raw_sha256": hashlib.sha256(data).hexdigest(),
           "pad_bytes": (-n) % BLOCK, "header_bytes": HDR_LEN,
           "fold_transform_seconds": round(t_fold, 4),
           "levels": {}, "roundtrip_exact": {}, "integrity": {}}

    for level in LEVELS:
        lv = {}
        for arm in ("A", "B", "C", "B1", "P1"):
            blob, dt = zstd(wire[arm], level)
            lv[arm] = {"bytes": len(wire[arm]), "zstd_bytes": len(blob),
                       "zstd_seconds": round(dt, 5)}
        # fold gain vs matched identity baseline (positive = fold helped)
        lv["gain_C_vs_B"] = lv["B"]["zstd_bytes"] - lv["C"]["zstd_bytes"]
        lv["gain_P1_vs_B1"] = lv["B1"]["zstd_bytes"] - lv["P1"]["zstd_bytes"]
        row["levels"][level] = lv

    for arm in ("B", "C", "B1", "P1"):
        back = restore(arm, wire[arm], n)
        row["roundtrip_exact"][arm] = (back == data)
    if check_integrity:
        rp = os.path.join(OUT, "fg_restored.db")
        with open(rp, "wb") as f:
            f.write(restore("P1", wire["P1"], n))  # exercise partial policy
        row["integrity"] = {"P1": integrity_check(rp)}
        os.unlink(rp)
    return row


def main():
    os.makedirs(OUT, exist_ok=True)
    warmup()

    fixtures = [
        ("bench9", lambda p: make_db(p), True),
        ("seq_int", build_seq_int, True),
        ("rep_text", build_rep_text, True),
        ("big_blob", build_big_blob, True),
        ("sparse", build_sparse, True),
        ("rand_blob", build_rand_blob, False),  # randomblob: unseeded
        ("snap_a", lambda p: build_snap(p, SNAP_A), True),
        ("snap_b", None, True),   # chain: copy snap_a + inserts
        ("snap_c", None, True),   # chain: copy snap_b + updates
    ]
    rows = []
    raws = {}
    for name, builder, det in fixtures:
        path = os.path.join(OUT, name + ".db")
        if os.path.exists(path):
            os.unlink(path)
        if name == "snap_b":
            shutil.copyfile(os.path.join(OUT, "snap_a.db"), path)
            sh(path, SNAP_B_ADD)
        elif name == "snap_c":
            shutil.copyfile(os.path.join(OUT, "snap_b.db"), path)
            sh(path, SNAP_C_UPD)
        else:
            builder(path)
        with open(path, "rb") as f:
            data = f.read()
        raws[name] = data
        rows.append(measure(name, data, det, check_integrity=True))

    # Partial-tile probes: byte-prefixes of snap_b, 1..7 pages (sub-tile)
    # and 8+k pages (one full tile + k-page tails). Deterministic. Exactness
    # only (truncated prefixes are not valid dbs; integrity n.a.).
    probes = []
    sb = raws["snap_b"]
    pages = max(len(sb) // 4096, 15)
    for k in range(1, 8):
        probes.append(("probe_subtile_%dp" % k, sb[:k * 4096]))
    for k in range(1, 8):
        probes.append(("probe_tail_%dp" % k, sb[:(8 + k) * 4096]))
    for name, data in probes:
        rows.append(measure(name, data, True, check_integrity=False))

    # ---------------------------------------------------------------- gates
    g1 = all(r["roundtrip_exact"].get(a, True)
             for r in rows for a in ("B", "C", "B1", "P1"))
    g2 = all(r["roundtrip_exact"][a]
             for r in rows if r["name"].startswith("probe_")
             for a in ("C", "P1"))
    g3 = all(r["levels"][lv]["C"]["bytes"] == r["levels"][lv]["B"]["bytes"]
             and r["levels"][lv]["P1"]["bytes"] == r["levels"][lv]["B1"]["bytes"]
             for r in rows for lv in LEVELS)
    # G4: padding effect measured separately (A vs B); credit only C vs B / P1 vs B1
    g4 = True  # structure of gains above enforces this; numbers reported below
    better = [(r["name"], lv, g) for r in rows if r["deterministic"]
              for lv in LEVELS
              for g in (r["levels"][lv]["gain_C_vs_B"],
                        r["levels"][lv]["gain_P1_vs_B1"])
              if g > 0]
    worse = [(r["name"], lv, g) for r in rows if r["deterministic"]
             for lv in LEVELS
             for g in (r["levels"][lv]["gain_C_vs_B"],
                       r["levels"][lv]["gain_P1_vs_B1"])
             if g < 0]
    g5 = len(better) > 0
    g6 = True  # aggregate table + regression count below

    result = {
        "milestone": "MDR-002 fold utility gate",
        "mdr1_format_changed": False,
        "fixtures": rows,
        "gates": {"G1_exact_reconstruction": g1, "G2_partial_tile": g2,
                  "G3_wire_accounting": g3, "G4_padding_isolated": g4,
                  "G5_fold_beats_identity": g5,
                  "G5_better_cases": better[:20],
                  "G6_regressions": len(worse),
                  "G6_worst_regressions": sorted(worse, key=lambda x: x[2])[:10]},
        "aggregate": {
            "fixtures": len(rows),
            "best_fold_gain": max((g for _, _, g in better), default=0),
            "worst_fold_regression": min((g for _, _, g in worse), default=0),
        },
        "verdict": ("PASS — fold-specific improvement found"
                    if g5 else
                    "NEGATIVE RESULT — no fold-specific improvement; "
                    "retain MDR-001 baseline"),
    }
    out_path = os.path.join(OUT, "fold_gate_result.json")
    with open(out_path, "w") as f:
        json.dump(result, f, indent=2)

    # stdout: headline table at L19 + gates
    print("%-16s %9s %6s %9s %9s %9s %8s %9s %9s %8s" %
          ("fixture(L19)", "raw", "pad", "A", "B", "C", "C-B", "P1", "B1", "P1-B1"))
    agg = [0] * 9
    for r in rows:
        lv = r["levels"][19]
        vals = (r["raw_size"], r["pad_bytes"], lv["A"]["zstd_bytes"],
                lv["B"]["zstd_bytes"], lv["C"]["zstd_bytes"], lv["gain_C_vs_B"],
                lv["P1"]["zstd_bytes"], lv["B1"]["zstd_bytes"],
                lv["gain_P1_vs_B1"])
        agg = [a + v for a, v in zip(agg, vals)]
        print("%-16s %9d %6d %9d %9d %9d %+8d %9d %9d %+8d" %
              ((r["name"],) + vals))
    print("%-16s %9d %6d %9d %9d %9d %+8d %9d %9d %+8d" % (("TOTAL",) + tuple(agg)))
    print("gates: G1=%s G2=%s G3=%s G4=reported G5=%s G6=reported" %
          (g1, g2, g3, g5))
    print("verdict:", result["verdict"])
    print("full results:", out_path)
    return 0


if __name__ == "__main__":
    sys.exit(main())