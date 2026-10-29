#!/usr/bin/python3
"""MDR2 archive codec: adaptive lossless archival compression for SQLite files.

MDR-004. Portfolio per segment (exhaustive trial, cheapest wire wins):
  0 stored      (raw fallback; guarantees no expansion)
  1 zstd        (raw bytes)
  2 D1<stride>  byte-shuffle stride + zstd   (stride in 1..255)
  3 D2          bit shuffle + zstd
  4 D3          SQLite page-aware split + zstd
5D fold is NOT in the portfolio (MDR-003: 0/8 held-out). Research only.

Wire format (all little-endian):
  header 22B  "MDR2" | ver=2 | flags | src_len u64 | seg_size u32 | nseg u32
  segment[]   codec u8 | param u8 | comp_len u32 | crc32(stored) u32 | bytes
  footer 48B  "M2DN" | nseg u32 | src_len u64 | sha256(source) 32B

Unpacker rejects: wrong magic/version, unknown codec, truncation, crc
mismatch, segment overrun, footer/segment-count/source-length mismatch.
A failed write never leaves a complete-looking archive: the footer is
written last, after fsync of the body.

Restore contracts (docs/mdr/MDR-003.md, ADR-004):
  byte-exact  -- pack reads the file bytes; unpack reproduces them exactly.
  backup      -- pack --backup captures a consistent snapshot via the
                 SQLite backup API; the snapshot need not equal a
                 concurrently captured raw file byte-for-byte.

Usage:
  mdrc pack   in.sqlite -o out.mdr [--backup] [--segment-size BYTES]
  mdrc unpack in.mdr -o restored.sqlite
  mdrc verify in.mdr [--full]
"""

import argparse
import hashlib
import os
import sqlite3
import struct
import subprocess
import sys
import tempfile
import zlib

MAGIC_HDR = b"MDR2"
MAGIC_FTR = b"M2DN"
VERSION = 2
HDR_LEN = 22
FTR_LEN = 48
DEFAULT_SEG = 8 << 20
PAGE = 4096
LEVEL_FLAG = {1: "-1", 9: "-9", 19: "-19"}  # pinned zstd levels, archive uses -19


class MdrError(Exception):
    pass


# ------------------------------------------------------------ transforms
def _pad(buf, k):
    return buf + b"\x00" * ((-len(buf)) % k)


def shuffle_bytes(data, k):
    buf = _pad(bytes(data), k)
    m = len(buf) // k
    out = bytearray(len(buf))
    for i in range(k):
        out[i * m:(i + 1) * m] = buf[i::k]
    return bytes(out)


def unshuffle_bytes(out, k):
    orig = bytearray(len(out))
    for i in range(k):
        orig[i::k] = out[i * len(out) // k:(i + 1) * len(out) // k]
    return bytes(orig)


def bit_shuffle(data):
    buf = _pad(bytes(data), 8)
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
    pad = (-len(data)) % PAGE
    buf = data + b"\x00" * pad
    hss, H, P = [], bytearray(), bytearray()
    for off in range(0, len(buf), PAGE):
        pg = buf[off:off + PAGE]
        t = pg[0]
        ncells = struct.unpack(">H", pg[3:5])[0] if len(pg) >= 5 else 0
        hs = (12 if t in (2, 5) else 8 if t in (10, 13) else 0) + 2 * ncells
        hs = min(hs, PAGE)
        hss.append(hs)
        H += pg[:hs]
        P += pg[hs:]
    meta = struct.pack("<H", len(hss)) + struct.pack("<%dH" % len(hss), *hss)
    return meta, bytes(H), bytes(P)


def page_join(meta, H, P, pad_total):
    (npages,) = struct.unpack("<H", meta[:2])
    hss = struct.unpack("<%dH" % npages, meta[2:])
    hi = pi = 0
    buf = bytearray()
    for hs in hss:
        buf += H[hi:hi + hs]
        hi += hs
        buf += P[pi:pi + PAGE - hs]
        pi += PAGE - hs
    assert len(buf) == pad_total
    return bytes(buf)


# ------------------------------------------------------------ zstd helper
def zstd_bytes(data, level=19):
    """Compress via zstd CLI. Returns compressed bytes."""
    exe = _zstd_tmpdir()
    fin = os.path.join(exe, "z.in")
    fout = fin + ".zst"
    with open(fin, "wb") as f:
        f.write(data)
    try:
        subprocess.run(["zstd", "-q", "-%d" % level, fin, "-o", fout, "-f"],
                       check=True, capture_output=True)
        with open(fout, "rb") as f:
            return f.read()
    finally:
        for p in (fin, fout):
            if os.path.exists(p):
                os.unlink(p)


def zstd_decompress(blob):
    exe = _zstd_tmpdir()
    fin = os.path.join(exe, "d.zst")
    fout = fin + ".out"
    with open(fin, "wb") as f:
        f.write(blob)
    try:
        subprocess.run(["zstd", "-q", "-d", fin, "-o", fout, "-f"],
                       check=True, capture_output=True)
        with open(fout, "rb") as f:
            return f.read()
    finally:
        for p in (fin, fout):
            if os.path.exists(p):
                os.unlink(p)


TMPDIR = None


def _zstd_tmpdir():
    global TMPDIR
    if TMPDIR is None:
        TMPDIR = tempfile.mkdtemp(prefix="mdrc.")
    return TMPDIR


# ---------------------------------------------------- segment codec trial
def encode_segment(codec, param, data):
    """Returns stored bytes for this segment (post-transform, pre-crc)."""
    if codec == 0:
        return bytes(data)
    if codec == 1:
        return zstd_bytes(data)
    if codec == 2:
        return zstd_bytes(shuffle_bytes(data, param))
    if codec == 3:
        return zstd_bytes(bit_shuffle(data))
    if codec == 4:
        meta, H, P = page_split(data)
        return zstd_bytes(meta + H + P)
    raise MdrError("unknown codec %d" % codec)


def decode_segment(codec, param, stored, out_len):
    """Inverse of encode_segment; returns exactly out_len bytes."""
    if codec == 0:
        if len(stored) != out_len:
            raise MdrError("stored segment length mismatch")
        return stored
    data = zstd_decompress(stored)
    if codec == 1:
        pass
    elif codec == 2:
        data = unshuffle_bytes(data, param)
    elif codec == 3:
        data = unbit_shuffle(data)
    elif codec == 4:
        meta_len = 2 + 2 * struct.unpack("<H", data[:2])[0]
        meta = data[:meta_len]
        rest = data[meta_len:]
        (npages,) = struct.unpack("<H", meta[:2])
        if npages == 0 or len(rest) < 2 * npages or npages > out_len + PAGE:
            raise MdrError("bad D3 metadata")
        hss = struct.unpack("<%dH" % npages, meta[2:])
        H_total = sum(hss)
        if H_total > len(rest) or npages * PAGE != len(rest):
            raise MdrError("bad D3 split lengths")
        data = page_join(meta, rest[:H_total], rest[H_total:], pad_total=out_len + (-out_len % PAGE))
    else:
        raise MdrError("unknown codec %d" % codec)
    return data[:out_len]


def select_and_encode(data, strides=(4, 16), level=19):
    """Trial-encode every portfolio codec; cheapest wire (len+overhead) wins.

    Segment overhead charged: codec+param+crc = 10 bytes/segment.
    Returns (codec, param, stored).
    """
    OVH = 10
    candidates = [(0, 0), (1, 0), (3, 0), (4, 0)] + [(2, k) for k in strides]
    best = None
    for codec, param in candidates:
        if codec == 2 and param > len(data):
            continue  # degenerate stride > data
        stored = encode_segment(codec, param, data)
        cost = len(stored) + OVH
        if best is None or cost < best[0]:
            best = (cost, codec, param, stored)
    _, codec, param, stored = best
    return codec, param, stored


# ----------------------------------------------------------------- pack
def backup_snapshot(db_path):
    """Consistent snapshot via the SQLite backup API; returns bytes."""
    src = sqlite3.connect("file:%s?mode=ro" % db_path, uri=True)
    try:
        dst = sqlite3.connect(":memory:")
        with dst:
            src.backup(dst)
        return dst.serialize() if hasattr(dst, "serialize") else None
    finally:
        src.close()
        if 'dst' in dir():
            pass


def _backup_bytes(db_path):
    src = sqlite3.connect("file:%s?mode=ro" % db_path, uri=True)
    path = os.path.join(_zstd_tmpdir(), "backup.db")
    try:
        if os.path.exists(path):
            os.unlink(path)
        dst = sqlite3.connect(path)
        with dst:
            src.backup(dst)
        dst.close()
    finally:
        src.close()
    with open(path, "rb") as f:
        data = f.read()
    os.unlink(path)
    return data


def pack(src_path, out_path, segment_size=DEFAULT_SEG, use_backup=False,
         progress=None):
    if segment_size < PAGE:
        raise MdrError("segment-size must be >= %d" % PAGE)
    if use_backup:
        data = _backup_bytes(src_path)
    else:
        with open(src_path, "rb") as f:
            data = f.read()
    src_len = len(data)
    nseg = max(1, (src_len + segment_size - 1) // segment_size)
    body_blocks = []
    out = {"segments": []}
    for i in range(nseg):
        chunk = data[i * segment_size:(i + 1) * segment_size]
        codec, param, stored = select_and_encode(chunk)
        seg = struct.pack("<BBII", codec, param, len(stored),
                          zlib.crc32(stored) & 0xFFFFFFFF) + stored
        body_blocks.append(seg)
        out["segments"].append({
            "index": i, "src_bytes": len(chunk), "codec": codec,
            "param": param, "stored": len(stored),
            "wire": len(seg)})
        if progress:
            progress(i + 1, nseg)
    # Assemble; footer LAST so a failed write can't look complete.
    header = struct.pack("<4sBBQII", MAGIC_HDR, VERSION, 1 if use_backup else 0,
                         src_len, segment_size, nseg)
    body = b"".join(body_blocks)
    footer = struct.pack("<4sIQ", MAGIC_FTR, nseg, src_len) + \
        hashlib.sha256(data if not use_backup else _sha_bytes(src_path, data)).digest()
    # NOTE: for --backup, sha is of the snapshot bytes actually archived.
    fd = os.open(out_path + ".part", os.O_WRONLY | os.O_CREAT | os.O_TRUNC, 0o644)
    try:
        os.write(fd, header)
        for blk in body_blocks:
            os.write(fd, blk)
        os.fsync(fd)
        os.write(fd, footer)
        os.fsync(fd)
    finally:
        os.close(fd)
    os.rename(out_path + ".part", out_path)
    out["src_len"] = src_len
    out["nseg"] = nseg
    out["archive"] = len(header) + len(body) + len(footer)
    out["overhead"] = out["archive"] - sum(s["stored"] for s in out["segments"])
    return out


def _sha_bytes(_p, data):
    return data


# --------------------------------------------------------------- unpack
def unpack(archive_path, out_path):
    with open(archive_path, "rb") as f:
        blob = f.read()
    if len(blob) < HDR_LEN + FTR_LEN:
        raise MdrError("truncated archive")
    magic, ver, flags, src_len, seg_size, nseg = struct.unpack("<4sBBQII", blob[:HDR_LEN])
    if magic != MAGIC_HDR:
        raise MdrError("not an MDR2 archive")
    if ver != VERSION:
        raise MdrError("unsupported MDR version %d" % ver)
    fmagic, fnseg, fsrc_len = struct.unpack("<4sIQ", blob[-FTR_LEN:-FTR_LEN + 16])
    fsha = blob[-32:]
    if fmagic != MAGIC_FTR:
        raise MdrError("archive footer missing/corrupt (incomplete write?)")
    if fnseg != nseg or fsrc_len != src_len:
        raise MdrError("footer/header mismatch")
    # Segment walk, bounds checked.
    body_end = len(blob) - FTR_LEN
    off = HDR_LEN
    chunks = []
    for i in range(nseg):
        if off + 10 > body_end:
            raise MdrError("truncated segment table at %d" % i)
        codec, param, clen, crc = struct.unpack("<BBII", blob[off:off + 10])
        off += 10
        if off + clen > body_end:
            raise MdrError("segment %d overruns archive" % i)
        stored = blob[off:off + clen]
        off += clen
        if codec not in (0, 1, 2, 3, 4):
            raise MdrError("unsupported codec %d" % codec)
        if zlib.crc32(stored) & 0xFFFFFFFF != crc:
            raise MdrError("segment %d crc mismatch" % i)
        src_end = min((i + 1) * seg_size, src_len)
        out_len = src_end - i * seg_size
        if out_len <= 0:
            raise MdrError("segment %d claims empty span" % i)
        chunks.append(decode_segment(codec, param, stored, out_len))
    if off != body_end:
        raise MdrError("segment bytes do not fill archive body")
    data = b"".join(chunks)
    if len(data) != src_len:
        raise MdrError("decoded length mismatch")
    if hashlib.sha256(data).digest() != fsha:
        raise MdrError("source sha256 mismatch — archive corrupt")
    fd = os.open(out_path + ".part", os.O_WRONLY | os.O_CREAT | os.O_TRUNC, 0o644)
    try:
        os.write(fd, data)
        os.fsync(fd)
    finally:
        os.close(fd)
    os.rename(out_path + ".part", out_path)
    return {"src_len": src_len, "nseg": nseg}


def verify(archive_path, full=False):
    """Structural verify; --full decodes and checks sha256 (already in unpack)."""
    with open(archive_path, "rb") as f:
        blob = f.read()
    if len(blob) < HDR_LEN + FTR_LEN:
        raise MdrError("truncated archive")
    magic, ver, flags, src_len, seg_size, nseg = struct.unpack("<4sBBQII", blob[:HDR_LEN])
    if magic != MAGIC_HDR or ver != VERSION:
        raise MdrError("not a supported MDR2 archive")
    fmagic, fnseg, fsrc_len = struct.unpack("<4sIQ", blob[-FTR_LEN:-FTR_LEN + 16])
    if fmagic != MAGIC_FTR or fnseg != nseg or fsrc_len != src_len:
        raise MdrError("footer corrupt or mismatched")
    body_end = len(blob) - FTR_LEN
    off = HDR_LEN
    segs = []
    for i in range(nseg):
        codec, param, clen, crc = struct.unpack("<BBII", blob[off:off + 10])
        off += 10
        if off + clen > body_end:
            raise MdrError("segment %d overruns archive" % i)
        stored = blob[off:off + clen]
        off += clen
        if codec not in (0, 1, 2, 3, 4):
            raise MdrError("unsupported codec %d in segment %d" % (codec, i))
        if zlib.crc32(stored) & 0xFFFFFFFF != crc:
            raise MdrError("segment %d crc mismatch" % i)
        segs.append({"index": i, "codec": codec, "param": param,
                     "stored": clen})
    if off != body_end:
        raise MdrError("body length mismatch")
    info = {"ok": True, "src_len": src_len, "nseg": nseg,
            "seg_size": seg_size, "segments": segs}
    if full:
        # decode all segments and integrity_check the restored db
        import tempfile
        tmp = os.path.join(_zstd_tmpdir(), "verify.db")
        unpack(archive_path, tmp)
        p = subprocess.run(["sqlite3", tmp, "PRAGMA integrity_check;"],
                           capture_output=True, text=True)
        info["integrity_check"] = p.stdout.strip()
        os.unlink(tmp)
        info["full"] = True
    return info


# ------------------------------------------------------------------ CLI
def cmd_pack(args):
    rep = pack(args.input, args.output, segment_size=args.segment_size,
               use_backup=args.backup)
    if args.report:
        import json
        with open(args.report, "w") as f:
            json.dump(rep, f, indent=2)
    src = rep["src_len"]
    print("packed %d bytes -> %d bytes (%d segments, overhead %d)" %
          (src, rep["archive"], rep["nseg"], rep["overhead"]))
    return 0


def cmd_unpack(args):
    rep = unpack(args.input, args.output)
    print("unpacked %d bytes from %d segments -> %s" %
          (rep["src_len"], rep["nseg"], args.output))
    return 0


def cmd_verify(args):
    rep = verify(args.input, full=args.full)
    print("archive OK: %d segments, source %d bytes" %
          (rep["nseg"], rep["src_len"]))
    if args.full:
        print("integrity_check:", rep.get("integrity_check"))
    return 0


def main(argv=None):
    ap = argparse.ArgumentParser(prog="mdrc", description=__doc__)
    sub = ap.add_subparsers(dest="cmd", required=True)
    pp = sub.add_parser("pack")
    pp.add_argument("input")
    pp.add_argument("-o", "--output", required=True)
    pp.add_argument("--backup", action="store_true",
                    help="archive a consistent snapshot via SQLite backup API")
    pp.add_argument("--segment-size", type=int, default=DEFAULT_SEG)
    pp.add_argument("--report")
    pp.set_defaults(fn=cmd_pack)
    up = sub.add_parser("unpack")
    up.add_argument("input")
    up.add_argument("-o", "--output", required=True)
    up.set_defaults(fn=cmd_unpack)
    vp = sub.add_parser("verify")
    vp.add_argument("input")
    vp.add_argument("--full", action="store_true")
    vp.set_defaults(fn=cmd_verify)
    args = ap.parse_args(argv)
    try:
        return args.fn(args)
    except MdrError as e:
        print("mdrc: error: %s" % e, file=sys.stderr)
        return 2


if __name__ == "__main__":
    sys.exit(main())