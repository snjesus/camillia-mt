#!/usr/bin/env python3
"""
Regenerates src/fonts/wubi_table.h — the Wubi-86 IME candidate table for
Camillia-mt.

v2 upgrade (WBI2, compressed with raw-DEFLATE):
  Input : wubi86.dict.yaml (rime/rime-wubi, LGPL-3.0)
          https://github.com/rime/rime-wubi
  Output: src/fonts/wubi_table.h
          A "WBI2" blob: 32-byte header + raw-DEFLATE compressed payload.
          On startup the engine decompresses the payload into PSRAM and
          builds a view over the variable-length records.

WBI2 layout (after decompression):
  Offset  Size  Field
    0      4    magic "WBI2"
    4      2    version == 2
    6      2    flags (reserved, 0)
    8      4    recordCount
   12      4    recordsByteSize  (total bytes of variable-length records)
   16      4    indexOffset      (bytes from payload start to index array)
   20      4    indexCount       (index entry count: ceil(recordCount/32) + 1)
   24      4    compType         1 = raw DEFLATE (decompress with wbits=-15)
   28      4    reserved         0

  Immediately after the 32-byte header (in the decompressed payload at byte
  32 onwards) come the variable-length records, sorted by code so a prefix
  matches a contiguous run found by binary search over the 32-records index.

  Record:
    bytes 0-3  code, NUL-padded, lower-case ASCII (wubi-86 1-4 keys a-y)
    byte  4    utf8Len : number of UTF-8 bytes of the candidate (3..27 for
                         1..9 chars, CJK chars are 3 bytes each)
    bytes 5..5+utf8Len-1   candidate text, UTF-8
    byte  last   rank : sort order within the same code (0 = most frequent)

  Index (at payload[indexOffset .. indexOffset + 4*indexCount - 1]):
    uint32_t indexCount entries, index[i] = byte offset from payload start
    (i.e. from the WBI2 header) of record #(i*32). index[indexCount-1] is
    the end offset (= 32 + recordsByteSize) so record ranges are
    [index[i], index[i+1]).

Compression (compType=1):
  raw deflate: compress the 32-byte header + records + index as a single
  flat buffer with zlib.compressobj(9, DEFLATED, wbits=-15). Runtime calls
  tinfl_decompress() (miniz, raw-mode) to recover the payload into PSRAM.

Usage: python3 tools/gen_wubi_ime.py path/to/wubi86.dict.yaml
"""
import os
import re
import struct
import subprocess
import sys
import tempfile
import zlib
from collections import defaultdict

SMALL_COVERAGE = 7000   # junda top-N that cjk_font_small.h ships
CANDIDATES_PER_CODE = 8   # cap each code: people almost never flip past #8
MAX_CODE_LEN = 4
MAX_PHRASE_LEN = 9   # accept 1..9 CJK chars (wubi86 phrases up to 9 chars)
INDEX_BLOCK = 32     # one 32-bit index entry every 32 records
TWO_WORD_GLOBAL_CAP = 30000   # top-30k 2-word phrases by Rime weight

WBI_MAGIC = b"WBI2"
WBI_VERSION = 2
COMP_DEFLATE_RAW = 1
HEADER_SIZE = 32
# layout: magic[4] ver[2] flags[2] count[4] recSz[4] idxOff[4] idxN[4] compT[4] reserved[4]
HEADER = struct.Struct("<4sHHIIIIII")
assert HEADER.size == HEADER_SIZE

LEVEL1 = {
    "q": "我", "w": "人", "e": "有", "r": "的", "t": "和",
    "y": "主", "u": "产", "i": "不", "o": "为", "p": "这",
    "a": "工", "s": "要", "d": "在", "f": "地", "g": "一",
    "h": "上", "j": "是", "k": "中", "l": "国", "m": "同",
    "x": "经", "c": "以", "v": "发", "b": "了", "n": "民",
}


def font_covered_chars():
    path = os.path.join(os.path.dirname(os.path.abspath(__file__)), "junda_freq.txt")
    ranked = []
    with open(path, encoding="utf-8") as fh:
        for line in fh:
            if line.startswith("#") or not line.strip():
                continue
            _rank_s, ch = line.split()
            ranked.append(ch)
    if len(ranked) < SMALL_COVERAGE:
        sys.exit(f"junda_freq.txt: only {len(ranked)} chars, need {SMALL_COVERAGE}")
    return set(ranked[:SMALL_COVERAGE])


def read_dict(path, allowed_chars):
    """Return (singles: code -> list[(weight, char)],
               two_word_entries: list[(weight, word, code)],
               longer_codes: code -> list[(weight, word)] with len>=3)."""
    singles = defaultdict(list)   # code -> [(weight, word)]
    two_word = []                 # [(weight, word, code)]
    longer = defaultdict(list)    # code -> [(weight, word)]
    in_entries = False
    with open(path, encoding="utf-8") as fh:
        for raw in fh:
            if not in_entries:
                if raw.strip() == "...":
                    in_entries = True
                continue
            if not raw or raw.startswith("#"):
                continue
            parts = raw.rstrip("\n").split("\t")
            if len(parts) < 2:
                continue
            word, code = parts[0].strip(), parts[1].strip().lower()
            if not re.fullmatch(r"[a-y]{1,4}", code):
                continue
            if not (1 <= len(word) <= MAX_PHRASE_LEN):
                continue
            # All CJK chars must be renderable; ASCII-only entries skipped.
            ascii_only = all(ord(ch) < 128 for ch in word)
            if ascii_only:
                continue
            if any((ord(ch) >= 128 and ch not in allowed_chars) for ch in word):
                continue
            try:
                weight = int(parts[2]) if len(parts) > 2 else 0
            except ValueError:
                weight = 0
            if len(word) == 1:
                singles[code].append((weight, word))
            elif len(word) == 2:
                two_word.append((weight, word, code))
            else:
                longer[code].append((weight, word))
    return singles, two_word, longer


def build_table(singles, two_word, longer):
    """Merge buckets, per-code dedupe & cap, produce {code: [word,...]} sorted
    by original Rime rank (descending, so highest weight first)."""
    out = defaultdict(list)
    for code, arr in singles.items():
        for _w, word in sorted(arr, key=lambda x: -x[0]):
            if word not in out[code]:
                out[code].append(word)
    two_word.sort(key=lambda x: -x[0])
    taken = 0
    for _w, word, code in two_word:
        if taken >= TWO_WORD_GLOBAL_CAP:
            break
        if word in out[code]:
            continue
        out[code].append(word)
        taken += 1
    for code, arr in longer.items():
        for _w, word in sorted(arr, key=lambda x: -x[0]):
            if word not in out[code]:
                out[code].append(word)
    # Cap every code's list
    kept = {}
    for code, words in out.items():
        words = words[:CANDIDATES_PER_CODE]
        if words:
            kept[code] = words
    return kept, taken


def build_payload(table):
    """Returns (payload bytes, record_count). payload includes header."""
    # Encode records in sorted code order.
    records_buf = bytearray()
    index_offsets = []
    count = 0
    for code in sorted(table):
        for rank, word in enumerate(table[code]):
            if count % INDEX_BLOCK == 0:
                index_offsets.append(HEADER_SIZE + len(records_buf))
            code_b = code.encode("ascii").ljust(4, b"\x00")
            utf8 = word.encode("utf-8")
            if len(utf8) > 0xFF:
                # cap at 255 bytes per utf8 field (phrases cap at 9 chars -> 27 bytes)
                continue
            rec = code_b + bytes([len(utf8)]) + utf8 + bytes([min(rank, 0xFF)])
            records_buf.extend(rec)
            count += 1
    index_offsets.append(HEADER_SIZE + len(records_buf))  # end sentinel
    records_size = len(records_buf)
    index_count = len(index_offsets)
    index_offset = HEADER_SIZE + records_size
    header = HEADER.pack(
        WBI_MAGIC, WBI_VERSION, 0,
        count,
        records_size,
        index_offset,
        index_count,
        COMP_DEFLATE_RAW,
        0,  # reserved
    )
    payload = header + bytes(records_buf) + struct.pack(f"<{index_count}I", *index_offsets)
    assert len(payload) == HEADER_SIZE + records_size + index_count * 4
    return payload, count


def deflate_raw(buf):
    co = zlib.compressobj(9, zlib.DEFLATED, wbits=-15, memLevel=9)
    return co.compress(buf) + co.flush()


def inflate_raw(compressed, out_size):
    dco = zlib.decompressobj(wbits=-15)
    out = dco.decompress(compressed) + dco.flush()
    if len(out) != out_size:
        raise RuntimeError(f"roundtrip size mismatch {len(out)} vs {out_size}")
    return out


def payload_lookup_prefix(payload, prefix, limit=45):
    """Reference runtime lookup: binary search through the 32-block index,
    then linear scan within the matching block(s). Collects unique strings,
    just like the C engine will."""
    header = HEADER.unpack(payload[:HEADER_SIZE])
    magic, _ver, _flags, count, rec_sz, index_off, idx_count, _ct, _res = header
    assert magic == WBI_MAGIC
    index = struct.unpack(f"<{idx_count}I", payload[index_off:index_off + idx_count*4])

    def rec_at(off):
        code = bytes(payload[off:off+4]).rstrip(b"\x00").decode("ascii")
        ulen = payload[off+4]
        text = bytes(payload[off+5:off+5+ulen]).decode("utf-8")
        rank = payload[off+5+ulen]
        return code, text, rank

    # Block-level binary search: find first block whose FIRST record code >= prefix.
    n_blocks = idx_count - 1
    lo, hi = 0, n_blocks
    while lo < hi:
        mid = (lo + hi) // 2
        off = index[mid]
        rec_code, _, _ = rec_at(off)
        if rec_code < prefix:
            lo = mid + 1
        else:
            hi = mid
    # Scan from block lo forward. May have to peek one block back (because
    # the previous block might end with a record that starts with prefix).
    start_block = max(0, lo - 1)
    cands = []
    seen = set()
    plen = len(prefix)
    for b in range(start_block, n_blocks):
        b_lo = index[b]
        b_hi = index[b+1]
        off = b_lo
        while off < b_hi and len(cands) < limit:
            code, text, _rank = rec_at(off)
            if code < prefix and code[:plen] != prefix:
                off += 6 + (payload[off+4] & 0xFF)
                continue
            if not code.startswith(prefix):
                # Sorted: past prefix. One more record's code could still
                # share prefix if it's in same block; end as soon as we pass.
                if code[:plen] != prefix:
                    return cands
            off += 6 + (payload[off+4] & 0xFF)
            if text in seen:
                continue
            seen.add(text)
            cands.append(text)
        if len(cands) >= limit:
            break
    return cands


def self_test(payload, table):
    hdr = HEADER.unpack(payload[:HEADER_SIZE])
    print(f"self-test: decompressed payload {len(payload)/1024:.1f} KB, "
          f"{hdr[3]:,} records")
    # Level-1 jianma first-char test
    bad = []
    for key, want in LEVEL1.items():
        cands = payload_lookup_prefix(payload, key)
        if not cands:
            bad.append(f"{key}: no candidates")
        elif cands[0] != want:
            bad.append(f"{key}: first={cands[0]!r}, want {want!r}")
    if bad:
        sys.exit("level-1 jianma self-test FAILED:\n  " + "\n  ".join(bad))
    print(f"self-test: all {len(LEVEL1)} level-1 jianma first-candidate OK")

    # Sample full codes & 2-word phrases
    checks = [
        ("aaaa", "工"),
        ("rqyy", "的"),
        ("khlg", "中国"),   # classic 2-word
        ("wqvb", "你好"),   # 2-word
        ("uxyi", "北京"),   # 2-word
    ]
    for code, want in checks:
        cands = payload_lookup_prefix(payload, code)
        if not cands or want not in cands:
            sys.exit(f"self-test FAILED: {code} -> {cands[:5]} missing {want!r}")
        print(f"self-test {code!r}: {want!r} @index {cands.index(want)}/{len(cands)}  first={cands[0]!r}")

    # Every prefix of level-1 codes still resolves the L1 char somewhere in its
    # list (so the 2-key jianma doesn't displace the 1-key list head).
    miss = 0
    for key, want in LEVEL1.items():
        for ext in ("", key[:1]):
            cand = payload_lookup_prefix(payload, key + ext)
            if want not in cand:
                miss += 1
    print(f"self-test: level-1 reach-through extra prefixes: {2*len(LEVEL1)-miss}/{2*len(LEVEL1)} pass")


def write_c_header(out_path, compressed, raw_size, record_count, comp_note):
    os.makedirs(os.path.dirname(out_path), exist_ok=True)
    # Write the C header. Compressed bytes as a uint8_t array. Also expose the
    # raw (decompressed) size constant for the engine malloc.
    with open(out_path, "w") as h:
        h.write("// Auto-generated by tools/gen_wubi_ime.py — do not hand-edit.\n")
        h.write("// Wubi-86 IME candidate table, 'WBI2' format with raw-DEFLATE compression.\n")
        h.write("// Layout: 32-byte WBI2 header describes a decompressed view (records +\n")
        h.write("// 32-record index) that the engine inflates into PSRAM at boot.\n")
        h.write("// Derived from rime/rime-wubi (LGPL-3.0), https://github.com/rime/rime-wubi\n")
        h.write(f"// {comp_note}\n")
        h.write("#pragma once\n#include <stdint.h>\n\n")
        h.write(f"const unsigned kWubiTableRawSize = {raw_size}u;\n")
        h.write(f"const unsigned kWubiTableRecordCount = {record_count}u;\n")
        h.write("const uint8_t kWubiTableCompressed[] = {")
        for i in range(0, len(compressed), 20):
            h.write("\n  " + ",".join(str(b) for b in compressed[i:i+20]) + ",")
        h.write("\n};\n")
        h.write(f"const unsigned kWubiTableCompressedLen = {len(compressed)}u;\n")


def main():
    if len(sys.argv) != 2:
        sys.exit("Usage: python3 tools/gen_wubi_ime.py path/to/wubi86.dict.yaml")
    src = sys.argv[1]
    out = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "src", "fonts", "wubi_table.h"))

    allowed = font_covered_chars()
    singles, two_word, longer = read_dict(src, allowed)
    table, two_word_taken = build_table(singles, two_word, longer)

    total_records = sum(len(v) for v in table.values())
    unique_chars = {w for ws in table.values() for w in ws if len(w) == 1}
    phrase_records = sum(1 for ws in table.values() for w in ws if len(w) >= 2)
    print(f"built table: {len(table)} unique codes, {total_records:,} records "
          f"({len(unique_chars)} unique singles, {phrase_records:,} phrase entries)")
    print(f"  2-word phrases: {two_word_taken:,} (cap {TWO_WORD_GLOBAL_CAP})")

    payload, count = build_payload(table)
    assert count == total_records

    raw_size = len(payload)
    compressed = deflate_raw(payload)
    ratio = len(compressed) / raw_size * 100
    print(f"payload: {raw_size:,} bytes ({raw_size/1024:.1f} KB) -> "
          f"deflate-9: {len(compressed):,} bytes ({len(compressed)/1024:.1f} KB, {ratio:.1f}%)")

    # Roundtrip decompression check.
    got = inflate_raw(compressed, raw_size)
    if got != payload:
        sys.exit("deflate/inflate roundtrip mismatch")
    print("self-test: deflate <-> inflate roundtrip OK")

    self_test(payload, table)

    comp_note = (f"{total_records:,} records, {len(unique_chars)} unique hanzi "
                 f"(junda top-{SMALL_COVERAGE}), top-{CANDIDATES_PER_CODE}/code, "
                 f"2-word global cap {TWO_WORD_GLOBAL_CAP}, "
                 f"raw {raw_size/1024:.0f} KB, comp {len(compressed)/1024:.0f} KB ({ratio:.1f}%)")
    write_c_header(out, compressed, raw_size, total_records, comp_note)
    print(f"wrote {out}: {len(compressed)/1024:.1f} KB compressed, {raw_size/1024:.1f} KB raw")


if __name__ == "__main__":
    main()
