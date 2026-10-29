#!/usr/bin/python3
"""MDR-003: representation competition gate.

Arms (all wires carry the same 21B MDR1-style header; parametrized arms add
their param byte(s); C uses the P1 partial-tile policy won in MDR-002):
  A       raw db bytes
  B       identity                                (control)
  C       5D fold, full tiles; raw tail           (MDR-002 reference)
  D1 k    byte shuffle, stride k in {2,4,8,16}    (field-wise regularity)
  D2      bit shuffle (8x8 bit transpose)
  D3      SQLite page-aware header/payload split
  D4a k   fold + residual predictor,  lag k in {1,4,8}
  D4b k   predictor alone,            lag k in {1,4,8}   (attribution pair)
  D5      exhaustive selector: cheapest of the above + 2B meta (arm,param)

Sign convention (corrected per MDR-002 review): every "saving" printed as
baseline_minus_arm (positive = the new arm wins).

Gates in docs/mdr/MDR-003.md; fixtures split dev (MDR-002 corpus) vs heldout
(freshly built, different shapes) so nothing is tuned on seq_int.
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
from fold_gate import (  # noqa: E402  (reuse MDR-002 builders + helpers)
    sh, integrity_check, zstd, build_seq_int, build_rep_text, build_big_blob,
    build_sparse, build_rand_blob, build_snap, SNAP_A, SNAP_B_ADD, SNAP_C_UPD,
    make_db)

OUT = os.path.join(HERE, "out")
LEVELS = (1, 9, 19)
BLOCK = mdr.BLOCK
HDR_LEN = 21
STRIDES = (2, 4, 8, 16)
LAGS = (1, 4, 8)


# ------------------------------------------------------------- transforms
def shuffle_bytes(data, k):
    pad = (-len(data)) % k
    buf = bytes(data) + b"\x00" * pad
    m = len(buf) // k
    out = bytearray(len(buf))
    for i in range(k):
        out[i * m:(i + 1) * m] = buf[i::k]
    return bytes(out)


def unshuffle_bytes(out, k):
    m = len(out) // k
    orig = bytearray(len(out))
    for i in range(k):
        orig[i::k] = out[i * m:(i + 1) * m]
    return bytes(orig)


def bit_shuffle(data):
    pad = (-len(data)) % 8
    buf = bytes(data) + b"\x00" * pad
    out = bytearray(len(buf))
    for g in range(0, len(buf), 8):
        grp = buf[g:g + 8]
        for b in range(8):
            v = 0
            for j in range(8):
                v = (v << 1) | ((grp[j] >> b) & 1)
            out[g + b] = v
    return bytes(out)


def unbit_shuffle(out):
    buf = bytearray(len(out))
    for g in range(0, len(out), 8):
        for b in range(8):
            v = out[g + b]
            for j in range(8):
                buf[g + j] |= ((v >> (7 - j)) & 1) << b
    return bytes(buf)


def page_split(data):
    """Split each 4096-page into (header+cellptr region, payload)."""
    pad = (-len(data)) % 4096
    buf = data + b"\x00" * pad
    hss, H, P = [], bytearray(), bytearray()
    for off in range(0, len(buf), 4096):
        pg = buf[off:off + 4096]
        t = pg[0]
        ncells = struct.unpack(">H", pg[3:5])[0] if len(pg) >= 5 else 0
        hs = (12 if t in (2, 5) else 8 if t in (10, 13) else 0) + 2 * ncells
        hs = min(hs, 4096)
        hss.append(hs)
        H += pg[:hs]
        P += pg[hs:]
    meta = struct.pack("<H", len(hss)) + struct.pack("<%dH" % len(hss), *hss)
    return meta, bytes(H), bytes(P)


def page_join(meta, H, P, n):
    (npages,) = struct.unpack("<H", meta[:2])
    hss = struct.unpack("<%dH" % npages, meta[2:])
    hi, pi = 0, 0
    buf = bytearray()
    for hs in hss:
        buf += H[hi:hi + hs]
        hi += hs
        buf += P[pi:pi + 4096 - hs]
        pi += 4096 - hs
    return bytes(buf[:n])


def predict(data, k):
    head = data[:k]
    res = bytearray(len(data) - k)
    for i in range(k, len(data)):
        res[i - k] = (data[i] - data[i - k]) & 0xFF
    return head, bytes(res)


def unpredict(head, res, k):
    out = bytearray(head) + bytearray(len(res))
    for i in range(k, k + len(res)):
        out[i] = (res[i - k] + out[i - k]) & 0xFF
    return bytes(out)


# ------------------------------------------------------------- arm wiring
def body_C(data):
    """C arm body: 5D fold on full tiles, raw tail (P1 policy)."""
    ft = len(data) - len(data) % BLOCK
    return (mdr.fold(data[:ft]), data[ft:]) if ft else (b"", data[ft:])


def restore_C(body, n):
    ft = len(body) - len(body) % BLOCK
    head = mdr.unfold(body[:ft]) if ft else b""
    return (head + body[ft:])[:n]


def encode_arm(code, param, data):
    """BODY after the 21B header. encode_arm/decode_arm are inverses."""
    if code in ("A", "B"):
        return data
    if code == "C":
        head, tail = body_C(data)
        return head + tail
    if code == "D1":
        return bytes([param]) + shuffle_bytes(data, param)
    if code == "D2":
        return bit_shuffle(data)
    if code == "D3":
        meta, H, P = page_split(data)
        return meta + H + P
    if code == "D4a":
        folded, tail = body_C(data)
        plain = folded + tail
        head, res = predict(plain, param)
        return bytes([param]) + head + res
    if code == "D4b":
        head, res = predict(data, param)
        return bytes([param]) + head + res
    raise ValueError(code)


def decode_arm(code, param, body, n):
    if code in ("A", "B"):
        return body[:n]
    if code == "C":
        return restore_C(body, n)
    if code == "D1":
        return unshuffle_bytes(body[1:], param)[:n]
    if code == "D2":
        return unbit_shuffle(body)[:n]
    if code == "D3":
        meta_len = 2 + 2 * struct.unpack("<H", body[:2])[0]
        meta = body[:meta_len]
        rest = body[meta_len:]
        (npages,) = struct.unpack("<H", meta[:2])
        hss = struct.unpack("<%dH" % npages, meta[2:])
        H_total = sum(hss)
        return page_join(meta, rest[:H_total], rest[H_total:], n)[:n]
    if code in ("D4a", "D4b"):
        k = body[0]
        head, res = body[1:1 + k], body[1 + k:]
        plain = unpredict(head, res, k)
        return restore_C(plain, n) if code == "D4a" else plain[:n]
    raise ValueError(code)


def trial_arms():
    """(code, param) pairs the selector considers (D5 itself excluded)."""
    return ([("A", 0), ("B", 0), ("C", 0)]
            + [("D1", k) for k in STRIDES]
            + [("D2", 0), ("D3", 0)]
            + [("D4a", k) for k in LAGS]
            + [("D4b", k) for k in LAGS])


# --------------------------------------------------------------- fixtures
def xorshift_bytes(n, seed=0x9E3779B9):
    out = bytearray(n)
    s = seed
    for i in range(n):
        s ^= (s << 13) & 0xFFFFFFFF
        s ^= s >> 17
        s ^= (s << 5) & 0xFFFFFFFF
        out[i] = s & 0xFF
    return bytes(out)


def write_deterministic_blob_db(path):
    """SQLite db with seeded pseudo-random payloads (controlled distribution)."""
    body = xorshift_bytes(8 * 96 * 1024)
    rows = []
    for i in range(8):
        blob = body[i * 96 * 1024:(i + 1) * 96 * 1024]
        rows.append("(%d, x'%s')" % (i + 1, blob.hex()))
    sh(path, """PRAGMA page_size=4096;
CREATE TABLE b(id INTEGER PRIMARY KEY, blob BLOB);
INSERT INTO b VALUES %s;""" % ",".join(rows))


def build_ho_seq2(db):
    sh(db, """PRAGMA page_size=4096;
CREATE TABLE s2(id INTEGER PRIMARY KEY, x INTEGER, y INTEGER);
WITH RECURSIVE c(x) AS (SELECT 1 UNION ALL SELECT x+1 FROM c WHERE x<1000000)
INSERT INTO s2 SELECT x, (x*13)%99991, (x*7)%65521 FROM c;""")


def build_ho_text2(db):
    sh(db, """PRAGMA page_size=4096;
CREATE TABLE t2(id INTEGER PRIMARY KEY, s TEXT);
WITH RECURSIVE c(x) AS (SELECT 1 UNION ALL SELECT x+1 FROM c WHERE x<6000)
INSERT INTO t2 SELECT x, printf('%-40s','record-'||x) FROM c;""")


def build_ho_small3(db):
    sh(db, """PRAGMA page_size=4096;
CREATE TABLE m(id INTEGER PRIMARY KEY, note TEXT);
WITH RECURSIVE c(x) AS (SELECT 1 UNION ALL SELECT x+1 FROM c WHERE x<30)
INSERT INTO m SELECT x, 'note '||x FROM c;""")


HO_SNAP_A = """CREATE TABLE e2(id INTEGER PRIMARY KEY, name TEXT, val REAL);
WITH RECURSIVE c(x) AS (SELECT 1 UNION ALL SELECT x+1 FROM c WHERE x<12000)
INSERT INTO e2 SELECT x, printf('%-24s','entity-'||x), x*2.25 FROM c;"""
HO_SNAP_B_ADD = """WITH RECURSIVE c(x) AS (SELECT 12001 UNION ALL SELECT x+1
 FROM c WHERE x<16000)
INSERT INTO e2 SELECT x, printf('%-24s','entity-'||x), x*2.25 FROM c;"""
HO_SNAP_C_UPD = "UPDATE e2 SET val = val*3 WHERE id % 11 = 0;"


def _build(name, fn):
    path = os.path.join(OUT, name + ".db")
    if os.path.exists(path):
        os.unlink(path)
    fn(path)
    return (name, path)


def build_fixtures():
    """dev = MDR-002 corpus; heldout = fresh shapes + distribution controls."""
    dev, heldout = [], []
    for name, fn in [
        ("bench9", make_db),
        ("seq_int", build_seq_int),
        ("rep_text", build_rep_text),
        ("big_blob", build_big_blob),
        ("sparse", build_sparse),
        ("rand_blob", build_rand_blob),
        ("snap_a", lambda p: build_snap(p, SNAP_A)),
    ]:
        dev.append(_build(name, fn))
    for name, fn, base, sql in [
        ("snap_b", None, "snap_a", SNAP_B_ADD),
        ("snap_c", None, "snap_b", SNAP_C_UPD),
    ]:
        path = os.path.join(OUT, name + ".db")
        if os.path.exists(path):
            os.unlink(path)
        shutil.copyfile(os.path.join(OUT, base + ".db"), path)
        sh(path, sql)
        dev.append((name, path))
    for name, fn in [
        ("ho_seq2", build_ho_seq2),
        ("ho_text2", build_ho_text2),
        ("ho_blob2", write_deterministic_blob_db),
        ("ho_small3", build_ho_small3),
        ("ho_snap_a", lambda p: build_snap(p, HO_SNAP_A)),
    ]:
        heldout.append(_build(name, fn))
    for name, fn, base, sql in [
        ("ho_snap_b", None, "ho_snap_a", HO_SNAP_B_ADD),
        ("ho_snap_c", None, "ho_snap_b", HO_SNAP_C_UPD),
    ]:
        path = os.path.join(OUT, name + ".db")
        if os.path.exists(path):
            os.unlink(path)
        shutil.copyfile(os.path.join(OUT, base + ".db"), path)
        sh(path, sql)
        heldout.append((name, path))
    upath = os.path.join(OUT, "ho_uniform.bin")
    with open(upath, "wb") as f:
        f.write(xorshift_bytes(1 << 20))
    heldout.append(("ho_uniform", upath))
    return dev, heldout


# --------------------------------------------------------------- measure
def measure(name, path, check_integrity):
    with open(path, "rb") as f:
        data = f.read()
    n = len(data)
    arms = trial_arms()
    row = {"name": name, "raw_size": n,
           "raw_sha256": hashlib.sha256(data).hexdigest(), "levels": {}}

    t0 = time.perf_counter()
    bodies = {}
    for code, param in arms:
        bodies[(code, param)] = encode_arm(code, param, data)
    t_enc = time.perf_counter() - t0
    row["encode_transform_seconds"] = round(t_enc, 4)

    for level in LEVELS:
        lv = {}
        for (code, param), body in bodies.items():
            # A = raw + zstd, headerless (end-to-end baseline per spec);
            # every other arm = matched framing (21B header inside the stream)
            stream = body if code == "A" else HDR_LEN * b"\x00" + body
            blob, dt = zstd(stream, level)
            lv["%s%s" % (code, param if param else "")] = {
                "wire": len(body) if code == "A" else HDR_LEN + len(body),
                "zstd": len(blob), "zstd_seconds": round(dt, 5)}
        row["levels"][level] = lv

    row["roundtrip_exact"] = {}
    for code, param in arms:
        back = decode_arm(code, param, bodies[(code, param)], n)
        row["roundtrip_exact"]["%s%s" % (code, param if param else "")] = (back == data)

    if check_integrity:
        rp = os.path.join(OUT, "m3_restored.db")
        with open(rp, "wb") as f:
            f.write(decode_arm("C", 0, bodies[("C", 0)], n))
        row["integrity_C"] = integrity_check(rp)
        os.unlink(rp)

    row["d5"] = {}
    for level in LEVELS:
        # selector considers the FULL portfolio incl. A; meta = 2B charged
        best_key = min(row["levels"][level],
                       key=lambda k: row["levels"][level][k]["zstd"])
        row["d5"][level] = best_key
    return row


def main():
    os.makedirs(OUT, exist_ok=True)
    zstd(b"x" * 4096, 1)  # warmup
    dev, heldout = build_fixtures()
    rows = []
    for split, fixtures in (("dev", dev), ("heldout", heldout)):
        for name, path in fixtures:
            r = measure(name, path, check_integrity=False)
            r["split"] = split
            rows.append(r)
            print("measured", split, name, r["raw_size"], flush=True)

    # ---------------- gates
    g1 = all(v for r in rows for v in r["roundtrip_exact"].values())
    integ = {}
    for name in ("bench9", "ho_seq2", "ho_small3"):
        path = os.path.join(OUT, name + ".db")
        with open(path, "rb") as f:
            data = f.read()
        rp = os.path.join(OUT, "gi.db")
        with open(rp, "wb") as f:
            f.write(decode_arm("C", 0, encode_arm("C", 0, data), len(data)))
        integ[name] = integrity_check(rp)
        os.unlink(rp)
    g_int = all(v == "ok" for v in integ.values())

    def arm_bytes(r, level, key):
        if key == "A":
            return r["raw_size"]
        return r["levels"][level][key]["zstd"]

    def best(r, level, code):
        """Cheapest arm whose key starts with `code`. Key format: code+param."""
        cands = {k: arm_bytes(r, level, k) for k in r["levels"][level]
                 if k[:len(code)] == code}
        if not cands and code == "A":
            return "A", r["raw_size"]
        if not cands:
            return None, None
        k = min(cands, key=cands.get)
        return k, cands[k]

    # headline table at L19
    hdrs = ("fixture(L19)", "A", "B", "C", "B-C", "D1*", "B-D1*", "D2",
            "D3", "D4a*", "D4b*", "D5", "D5-A")
    print()
    print("%-13s %9s %9s %9s %8s %9s %8s %9s %9s %9s %9s %9s %9s" % hdrs)
    totals = dict.fromkeys(hdrs[1:], 0)
    for r in rows:
        _, d1 = best(r, 19, "D1")
        _, d4a = best(r, 19, "D4a")
        _, d4b = best(r, 19, "D4b")
        d5k = r["d5"][19]
        d5b = arm_bytes(r, 19, d5k) + 2
        A, B, C = arm_bytes(r, 19, "A"), arm_bytes(r, 19, "B"), arm_bytes(r, 19, "C")
        vals = (A, B, C, B - C, d1, B - d1, arm_bytes(r, 19, "D2"),
                arm_bytes(r, 19, "D3"), d4a, B - d4b, d5b, A - d5b)
        for h, v in zip(hdrs[1:], vals):
            totals[h] += v
        print("%-13s %9d %9d %9d %+8d %9d %+8d %9d %9d %9d %+8d %9d %+9d" %
              ((r["name"],) + vals))
    print("%-13s" % "TOTAL(all)", " ".join("%9d" % totals[h] for h in hdrs[1:]))

    # GC3: fold beats strongest conventional on heldout
    conv_codes = ("D1", "D2", "D3")
    g3_cases = []
    for r in rows:
        if r["split"] != "heldout":
            continue
        _, conv = min((best(r, 19, c) for c in conv_codes),
                      key=lambda kv: kv[1] if kv[1] is not None else 10**15)
        Cv = arm_bytes(r, 19, "C")
        if conv is not None and Cv < conv:
            g3_cases.append((r["name"], conv - Cv))

    # GC4: D5 == cheapest + 2 always
    g4 = True
    for r in rows:
        for level in LEVELS:
            mn = min(arm_bytes(r, level, k)
                     for k in r["levels"][level]) + 2
            d5 = arm_bytes(r, level, r["d5"][level]) + 2
            if d5 > mn:
                g4 = False

    # GC5 generalization: does C-beats-conventional transfer dev -> heldout?
    def wins(split):
        w = 0
        for r in rows:
            if r["split"] != split:
                continue
            _, conv = min((best(r, 19, c) for c in conv_codes),
                          key=lambda kv: kv[1] if kv[1] is not None else 10**15)
            if conv is not None and arm_bytes(r, 19, "C") < conv:
                w += 1
        return w
    dev_wins, ho_wins = wins("dev"), wins("heldout")

    verdict = "PASS" if (g1 and g_int and g3_cases and g4) else "FAIL"
    result = {
        "milestone": "MDR-003 representation competition",
        "gates": {
            "GC1_exact_every_variant": g1,
            "GC2_attribution": "per-fixture C/B/D1/D2/D3/D4a/D4b table + rows.json",
            "GC3_fold_beats_conventional_heldout": bool(g3_cases),
            "GC3_cases": sorted(g3_cases, key=lambda x: -x[1])[:10],
            "GC4_selector_min_plus_meta": g4,
            "GC5_dev_wins_vs_heldout_wins": [dev_wins, ho_wins],
            "integrity_C": integ,
        },
        "d5_selections": {r["name"]: r["d5"] for r in rows},
        "verdict": verdict,
    }
    out_path = os.path.join(OUT, "competition_result.json")
    with open(out_path, "w") as f:
        json.dump(result, f, indent=2, default=str)
    with open(out_path + ".rows.json", "w") as f:
        json.dump(rows, f, indent=2, default=str)
    print()
    print("gates: GC1=%s GCint=%s GC3=%s GC4=%s GC5 dev/ho=%d/%d" %
          (g1, g_int, bool(g3_cases), g4, dev_wins, ho_wins))
    print("verdict:", verdict)
    print("results:", out_path)
    return 0 if verdict == "PASS" else 1


if __name__ == "__main__":
    sys.exit(main())