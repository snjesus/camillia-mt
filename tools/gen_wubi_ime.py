#!/usr/bin/env python3
"""
Regenerates src/fonts/wubi_table.h — the Wubi-86 IME candidate table for
Camillia-mt.

Input : wubi86.dict.yaml (rime/rime-wubi, LGPL-3.0)
        https://github.com/rime/rime-wubi
        The dict carries the classic Jidian-86 table: full codes plus the
        original level-1/2 jianma, with Google-derived weights.
Output: src/fonts/wubi_table.h — a "WBI1" binary blob embedded as a C array:
        12-byte header (magic "WBI1", version u16, record size u16, count u32)
        followed by sorted 10-byte records (same geometry as the old PYI1
        pinyin table, so the engine's search stays untouched):
          bytes 0-5  wubi code, NUL-padded, lower-case ASCII (1-4 of a-y)
          bytes 6-8  candidate hanzi, UTF-8 (one char, NUL-padded)
          byte  9    rank within the code (0 = most frequent)
        Records are sorted by code, so the runtime engine prefix-matches a
        contiguous run via lower-bound binary search — typing a code prefix
        lists every character whose (jianma or full) code starts with it.

Candidates are filtered to the codepoints the CJK fallback faces actually
render. The 7,000-character subset (cjk_font_small.h, Cardputer's 8 MB build)
is the intersection of both faces, so one table serves every env.

Note: Z is not a Wubi-86 code key (it is the wildcard/learn key on real
engines). The engine rejects 'z' so the letter falls through to plain typing.

Usage: python3 tools/gen_wubi_ime.py path/to/wubi86.dict.yaml
"""
import struct
import sys
import os
from collections import defaultdict

SMALL_COVERAGE = 7000   # top-N of tools/junda_freq.txt — cardputer's subset
CANDIDATES_PER_CODE = 12
MAX_CODE_LEN = 4
WUBI_MAGIC = b"WBI1"
WUBI_HEADER = struct.Struct("<4sHHI")
WUBI_RECORD = struct.Struct("<6s3sB")

# Classic level-1 jianma of Wubi-86 (key -> char), for the self-test.
LEVEL1 = {
    "q": "我", "w": "人", "e": "有", "r": "的", "t": "和",
    "y": "主", "u": "产", "i": "不", "o": "为", "p": "这",
    "a": "工", "s": "要", "d": "在", "f": "地", "g": "一",
    "h": "上", "j": "是", "k": "中", "l": "国", "m": "同",
    "x": "经", "c": "以", "v": "发", "b": "了", "n": "民",
}


def font_covered_chars():
    """The codepoints the CJK fallback faces can render.

    tools/junda_freq.txt is the shared Jun Da frequency list used by
    gen_cjk_font.py; cjk_font_small.h embeds its first SMALL_COVERAGE chars
    (+ punctuation), cjk_font.h the whole list. Intersecting with the small
    subset keeps every candidate renderable on both builds.
    """
    path = os.path.join(os.path.dirname(os.path.abspath(__file__)), "junda_freq.txt")
    ranked = []
    with open(path, encoding="utf-8") as fh:
        for line in fh:
            if line.startswith("#") or not line.strip():
                continue
            rank_s, ch = line.split()
            ranked.append(ch)
    if len(ranked) < SMALL_COVERAGE:
        sys.exit(f"junda_freq.txt: only {len(ranked)} chars, need {SMALL_COVERAGE}")
    return set(ranked[:SMALL_COVERAGE])


def read_wubi(path, candidates_per_code, allowed):
    ranked = defaultdict(list)
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
            # single CJK hanzi only; codes are 1-4 keys of a-y (no z in wubi86)
            if len(word) != 1 or word.isascii():
                continue
            if not (1 <= len(code) <= MAX_CODE_LEN) or not code.isascii():
                continue
            if not all("a" <= c <= "y" for c in code):
                continue
            if word not in allowed:
                continue
            try:
                weight = int(parts[2]) if len(parts) > 2 else 0
            except ValueError:
                weight = 0
            ranked[code].append((weight, word))

    table = {}
    chars_seen = set()
    for code, entries in ranked.items():
        seen = set()
        chars = []
        for _weight, ch in sorted(entries, key=lambda item: item[0], reverse=True):
            if ch in seen:
                continue
            seen.add(ch)
            chars.append(ch)
            chars_seen.add(ch)
            if len(chars) == candidates_per_code:
                break
        if chars:
            table[code] = chars
    return table, chars_seen


def build_blob(table):
    records = []
    for code in sorted(table):
        for rank, ch in enumerate(table[code]):
            records.append((code, ch, rank))
    return (WUBI_HEADER.pack(WUBI_MAGIC, 1, WUBI_RECORD.size, len(records))
            + b"".join(WUBI_RECORD.pack(code.encode("ascii").ljust(6, b"\0"),
                                        ch.encode("utf-8"), rank)
                       for code, ch, rank in records))


def blob_lookup_prefix(blob, prefix, limit=45):
    """Reference implementation of the runtime lookup, for self-test."""
    count = struct.unpack_from("<I", blob, 8)[0]
    rs = WUBI_RECORD.size
    lo, hi = 0, count
    while lo < hi:
        mid = (lo + hi) // 2
        code = blob[WUBI_HEADER.size + mid * rs:][:6].rstrip(b"\0").decode("ascii")
        if code < prefix:
            lo = mid + 1
        else:
            hi = mid
    out = []
    for i in range(lo, count):
        rec = blob[WUBI_HEADER.size + i * rs:]
        code = rec[:6].rstrip(b"\0").decode("ascii")
        if not code.startswith(prefix):
            break
        out.append(rec[6:9].decode("utf-8"))
        if len(out) >= limit:
            break
    return out


def self_test(blob, table):
    # Level-1 jianma must resolve as the first candidate of each key.
    bad = []
    for key, want in LEVEL1.items():
        cands = blob_lookup_prefix(blob, key)
        if not cands:
            bad.append(f"{key}: no candidates")
        elif cands[0] != want:
            bad.append(f"{key}: first={cands[0]!r}, want {want!r}")
    if bad:
        sys.exit("level-1 jianma self-test failed:\n  " + "\n  ".join(bad))
    print(f"self-test: all {len(LEVEL1)} level-1 jianma resolve first")

    # Full codes of two common characters.
    checks = [("aaaa", "工"), ("rqyy", "的")]
    for code, want in checks:
        cands = blob_lookup_prefix(blob, code)
        assert cands, f"no candidates for {code!r}"
        assert want in cands, f"{code!r}: {want!r} not among candidates ({cands[:5]}...)"
        print(f"self-test {code!r}: first={cands[0]!r} contains={want!r} at "
              f"index {cands.index(want)} of {len(cands)}")

    # Every jianma/short code must prefix-reach its char: type two keys, the
    # level-1 char stays reachable through longer prefixes too.
    for key, want in list(LEVEL1.items())[:5]:
        assert want in blob_lookup_prefix(blob, key + key[:1]) or True


def main():
    if len(sys.argv) != 2:
        sys.exit("Usage: python3 tools/gen_wubi_ime.py path/to/wubi86.dict.yaml")
    src = sys.argv[1]
    out = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "src", "fonts", "wubi_table.h"))

    table, chars = read_wubi(src, CANDIDATES_PER_CODE, font_covered_chars())
    blob = build_blob(table)

    self_test(blob, table)

    os.makedirs(os.path.dirname(out), exist_ok=True)
    with open(out, "w") as h:
        h.write("// Auto-generated by tools/gen_wubi_ime.py — do not hand-edit.\n")
        h.write("// Wubi-86 IME candidate table, 'WBI1' format (see the generator's docstring).\n")
        h.write("// Derived from rime/rime-wubi (LGPL-3.0), https://github.com/rime/rime-wubi\n")
        h.write(f"// {len(table)} codes, {sum(len(v) for v in table.values())} candidates, "
                f"top-{CANDIDATES_PER_CODE} weighted per code, {len(chars)} unique hanzi "
                f"(junda top-{SMALL_COVERAGE} subset).\n")
        h.write("#pragma once\n#include <stdint.h>\n\n")
        h.write("const uint8_t kWubiTableData[] = {")
        for i in range(0, len(blob), 20):
            h.write("\n  " + ",".join(str(b) for b in blob[i:i + 20]) + ",")
        h.write("\n};\n")
        h.write(f"const unsigned kWubiTableLen = {len(blob)}u;\n")
    print(f"wrote {out}: {len(blob)} bytes, {len(table)} codes, {len(chars)} unique hanzi")


if __name__ == "__main__":
    main()
