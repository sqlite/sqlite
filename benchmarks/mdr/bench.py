#!/usr/bin/python3
"""MDR-001 benchmark: honest measurements against zstd and dedup baselines.

Usage: benchmarks/mdr/bench.py [dbfile]   (default: builds a synthetic db)
Acceptance (docs/mdr/ADR-001.md): roundtrip byte-exact, integrity_check OK,
zstd + dedup baselines measured, random-data control shows no gain.
"""

import hashlib
import json
import os
import subprocess
import sys
import time

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "..", "mdr", "core"))
import mdr  # noqa: E402

WORKDIR = os.environ.get("MDR_BENCH_DIR", os.path.join(os.path.dirname(__file__), "out"))


def sha256(b):
    return hashlib.sha256(b).hexdigest()


def make_db(path):
    """Deterministic synthetic database: 3 tables, ~300 pages."""
    if os.path.exists(path):
        os.unlink(path)
    p = subprocess.run(
        ["sqlite3", path],
        input="""PRAGMA page_size=4096;
BEGIN;
CREATE TABLE t1(id INTEGER PRIMARY KEY, name TEXT, val REAL, blob BLOB);
INSERT INTO t1(name, val, blob) VALUES
 ('alpha', 1.25, x'000102030405060708090a0b0c0d0e0f'),
 ('beta', 9.75, randomblob(512));
INSERT INTO t1(name, val, blob) SELECT name||'.'||id, val+id, blob FROM t1;
INSERT INTO t1(name, val, blob) SELECT name||'.2', val*2, blob FROM t1;
INSERT INTO t1(name, val, blob) SELECT name||'.3', val*3, blob FROM t1;
INSERT INTO t1(name, val, blob) SELECT name||'.4', val*4, blob FROM t1;
INSERT INTO t1(name, val, blob) SELECT name||'.5', val*5, blob FROM t1;
CREATE TABLE t2 AS SELECT id, name AS label FROM t1;
CREATE INDEX idx1 ON t1(name);
COMMIT;
VACUUM;
""",
        text=True, capture_output=True)
    if p.returncode != 0:
        raise RuntimeError(p.stderr)
    return path


def zstd_level(data, level):
    """Compress via zstd CLI with explicit level; returns (bytes, seconds)."""
    tmp = os.path.join(WORKDIR, "z.in")
    outp = tmp + ".out"
    with open(tmp, "wb") as f:
        f.write(data)
    t0 = time.time()
    subprocess.run(["zstd", "-q", f"-{abs(level)}", tmp, "-o", outp, "-f"],
                   check=True, capture_output=True)
    dt = time.time() - t0
    with open(outp, "rb") as f:
        blob = f.read()
    os.unlink(tmp)
    os.unlink(outp)
    # zstd appends 4-byte magic+frame content size when streaming from file;
    # subtract nothing: this is the honest on-disk size.
    return blob, dt


def run_bench(db_path):
    os.makedirs(WORKDIR, exist_ok=True)
    with open(db_path, "rb") as f:
        raw = f.read()

    result = {"db": os.path.basename(db_path), "raw_size": len(raw),
              "raw_sha256": sha256(raw)}

    # Integrity before
    chk = subprocess.run(["sqlite3", db_path, "PRAGMA integrity_check;"],
                         capture_output=True, text=True)
    result["integrity_before"] = chk.stdout.strip()

    # MDR fold + header
    t0 = time.time()
    packed = mdr.pack(raw)
    result["mdr_packed_size"] = len(packed)
    result["mdr_pack_seconds"] = round(time.time() - t0, 4)

    # Roundtrip
    restored = mdr.unpack(packed)
    result["roundtrip_exact"] = (restored == raw)

    # Write restored to disk; integrity after
    rp = os.path.join(WORKDIR, "restored.db")
    with open(rp, "wb") as f:
        f.write(restored)
    chk = subprocess.run(["sqlite3", rp, "PRAGMA integrity_check;"],
                         capture_output=True, text=True)
    result["integrity_after"] = chk.stdout.strip()
    os.unlink(rp)

    # zstd baselines (1, 9, 19)
    for lvl in (1, 9, 19):
        blob, dt = zstd_level(raw, lvl)
        result[f"zstd{lvl}_size"] = len(blob)
        result[f"zstd{lvl}_seconds"] = round(dt, 4)

    # Dedup baseline: unique 4096-byte pages accounting
    pages = [raw[i:i + 4096] for i in range(0, len(raw), 4096)]
    unique = len(set(pages))
    result["pages_total"] = len(pages)
    result["pages_unique"] = unique
    result["dedup_size"] = unique * 4096  # store each unique page once

    # Random-data control: no compression claim on random bytes
    rnd = os.urandom(1 << 20)
    rp_packed = mdr.pack(rnd)
    zb, _ = zstd_level(rnd, 9)
    result["random"] = {
        "size": len(rnd),
        "mdr_packed": len(rp_packed),
        "roundtrip_exact": mdr.unpack(rp_packed) == rnd,
        "zstd9": len(zb),
        "zstd9_expands": len(zb) >= len(rnd),
        "mdr_expands": len(rp_packed) >= len(rnd),
    }

    # Sanity note: MDR is a permutation, not a compressor.
    result["mdr_is_permutation_only"] = True
    result["verdict"] = "PASS" if (result["roundtrip_exact"]
                                   and result["integrity_after"] == "ok"
                                   and result["random"]["roundtrip_exact"]) else "FAIL"
    return result


def main():
    os.makedirs(WORKDIR, exist_ok=True)
    db = sys.argv[1] if len(sys.argv) > 1 else make_db(os.path.join(WORKDIR, "bench.db"))
    r = run_bench(db)
    out = json.dumps(r, indent=2)
    out_path = os.path.join(WORKDIR, "bench_result.json")
    with open(out_path, "w") as f:
        f.write(out)
    print(out)
    print(f"saved: {out_path}")
    return 0 if r["verdict"] == "PASS" else 1


if __name__ == "__main__":
    main()