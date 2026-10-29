#!/usr/bin/python3
"""MDR-001 archive: reversible page fold (Riemann index permutation) + pack/unpack.

Contract (docs/mdr/ADR-001.md): byte-exact restore, integrity_check preserved,
no compression claims outside measured benchmarks.
"""

import struct

MAGIC = b"MDR1"
AXES = 5
WIDTH = 8  # 8^5 = 32768 bytes per fold tile (8 pages of 4096)
BLOCK = WIDTH ** AXES


def _build_perm(n=WIDTH ** AXES):
    """Base-8 5-digit index reversal: out[PERM[i]] = page[i]. Involution."""
    perm = []
    for i in range(n):
        j = 0
        v = i
        for _ in range(AXES):
            j = (j << 3) | (v & 7)
            v >>= 3
        perm.append(j)
    return tuple(perm)


PERM = _build_perm()


def fold(page_or_bytes):
    """Apply the 5-axis transpose. Length must be a positive multiple of BLOCK."""
    perm = PERM
    n = len(page_or_bytes)
    assert n > 0 and n % BLOCK == 0, "fold requires data aligned to BLOCK (32768)"
    out = bytearray(n)
    for p in range(0, n, BLOCK):
        chunk = page_or_bytes[p:p + BLOCK]
        for i in range(BLOCK):
            out[p + perm[i]] = chunk[i]
    return bytes(out)


# Digit reversal applied twice is the identity, so unfold == fold.
unfold = fold


def pack(data: bytes, page_size: int = 4096) -> bytes:
    """Fold page-aligned data and wrap in an MDR1 header. Pads, trims on unpack."""
    if page_size != 4096:
        raise ValueError("MDR-001 supports the 4096 default page size only")
    pad = (-len(data)) % BLOCK  # BLOCK = 8^5 = 32768 (8 pages per fold tile)
    body = fold(data + b"\x00" * pad)
    return MAGIC + struct.pack("<BIIQ", 1, page_size, AXES, len(data)) + body


def unpack(archive: bytes) -> bytes:
    if archive[:4] != MAGIC:
        raise ValueError("not an MDR1 archive")
    version, page_size, axes, orig_len = struct.unpack("<BIIQ", archive[4:21])
    if version != 1 or page_size != 4096 or axes != AXES:
        raise ValueError("unsupported MDR archive header")
    return unfold(archive[21:])[:orig_len]


if __name__ == "__main__":
    import os
    # Self-check: exact roundtrip on padded/unpadded/edge sizes.
    for n in (1, BLOCK, BLOCK + 1, len(PERM)):
        blob = os.urandom(n)
        assert len(pack(blob)) == (n + BLOCK - 1) // BLOCK * BLOCK + 21, n
        assert unpack(pack(blob)) == blob, n
    # Involution check on real bytes (single 32768 tile).
    pg = os.urandom(BLOCK)
    assert fold(unfold(pg)) == pg
    # Permutation validity: PERM is a bijection on [0, BLOCK).
    assert sorted(PERM) == list(range(BLOCK))
    # Non-identity check: the fold actually moves bytes.
    seq = bytes((i * 7) % 256 for i in range(BLOCK))
    assert fold(seq) != seq
    print("mdr.selfcheck OK")