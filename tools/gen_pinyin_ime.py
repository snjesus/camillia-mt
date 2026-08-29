#!/usr/bin/env python3
"""
Regenerates src/fonts/pinyin_table.h — the full-pinyin IME candidate table
for Camillia-mt, adapted from the Meshtastic-CardputerADV-CN asset generator
(MIT, (c) hansgao0422), which in turn compiles its table from the Rime
pinyin-simp dictionary.

Input : pinyin_simp.dict.yaml (rime/rime-pinyin-simp, Apache-2.0)
        https://github.com/rime/rime-pinyin-simp
Output: src/fonts/pinyin_table.h — a "PYI1" binary blob embedded as a C array:
        12-byte header (magic "PYI1", version u16, record size u16, count u32)
        followed by sorted 10-byte records:
          bytes 0-5  pinyin code, NUL-padded, lower-case ASCII
          bytes 6-8  candidate hanzi, UTF-8 (one char, NUL-padded)
          byte  9    rank within the code (0 = most frequent)
        Records are sorted by code so the runtime engine can binary-search a
        prefix; codes keep their top-12 weighted candidates each. Candidates are
        filtered to the codepoints the CJK fallback face (gen_cjk_font.py)
        actually renders, so no candidate can rasterize as a missing glyph.

The runtime side (src/pinyin_ime.cpp) binary-searches the prefix of the
composition and surfaces candidates 5 at a time, mirroring the
Meshtastic-CardputerADV-CN behaviour.

Usage: python3 tools/gen_pinyin_ime.py path/to/pinyin_simp.dict.yaml
"""
import struct
import re
import sys
import os
from collections import defaultdict


def _freq_ranked_cjk():
    """Extracts the frequency list and subset size from gen_cjk_font.py.

    Importing would execute that script's argv check and font pipeline, so the
    two values are read out of the source text instead. The slice length must
    match the font subset exactly — DEFAULT_CHARS, not the full 2,000-entry
    list — or candidates beyond the font's coverage would sneak back in.
    """
    path = os.path.join(os.path.dirname(os.path.abspath(__file__)), "gen_cjk_font.py")
    with open(path, encoding="utf-8") as fh:
        text = fh.read()
    m = re.search(r"DEFAULT_CHARS = (\d+)", text)
    if not m:
        sys.exit("gen_cjk_font.py: DEFAULT_CHARS not found")
    n = int(m.group(1))
    m = re.search(r"FREQ_RANKED_CJK = \(\n(.*?)\n\)", text, re.S)
    if not m:
        sys.exit("gen_cjk_font.py: FREQ_RANKED_CJK block not found")
    ranked = "".join(re.findall(r'"([^"]*)"', m.group(1)))
    return ranked[:n]


def font_covered_chars():
    """The codepoints the CJK fallback face can actually render.

    Mirrors the selection in gen_cjk_font.py (frequency-ranked Han + CJK
    punctuation + fullwidth forms + a few halfwidth marks). Candidates outside
    this set would rasterize as the missing-glyph box, so they are dropped
    before ranking rather than embedded unreachable.
    """
    allowed = set(_freq_ranked_cjk())
    allowed |= {chr(c) for c in range(0x3000, 0x303F + 1)}   # CJK punctuation
    allowed |= {chr(c) for c in range(0xFF00, 0xFFEF + 1)}   # fullwidth forms
    allowed |= {chr(c) for c in
                (0x00B7, 0x2013, 0x2014, 0x2018, 0x2019, 0x201C, 0x201D, 0x2026)}
    return allowed


PINYIN_MAGIC = b"PYI1"
PINYIN_HEADER = struct.Struct("<4sHHI")
PINYIN_RECORD = struct.Struct("<6s3sB")
CANDIDATES_PER_CODE = 12


def read_pinyin(path, candidates_per_code, allowed):
    ranked = defaultdict(list)
    with open(path, encoding="utf-8") as fh:
        for raw in fh:
            if not raw or raw.startswith("#") or raw.startswith("---"):
                continue
            parts = raw.rstrip("\n").split("\t")
            if len(parts) < 2:
                continue
            word, code = parts[0].strip(), parts[1].strip().lower()
            if len(word) != 1 or not code.isascii() or not code.isalpha() or len(code) > 6:
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
    return (PINYIN_HEADER.pack(PINYIN_MAGIC, 1, PINYIN_RECORD.size, len(records))
            + b"".join(PINYIN_RECORD.pack(code.encode("ascii").ljust(6, b"\0"),
                                          ch.encode("utf-8"), rank)
                       for code, ch, rank in records))


def blob_lookup_prefix(blob, prefix, limit=45):
    """Reference implementation of the runtime lookup, for self-test."""
    count = struct.unpack_from("<I", blob, 8)[0]
    rs = PINYIN_RECORD.size
    lo, hi = 0, count
    while lo < hi:
        mid = (lo + hi) // 2
        code = blob[PINYIN_HEADER.size + mid * rs:][:6].rstrip(b"\0").decode("ascii")
        if code < prefix:
            lo = mid + 1
        else:
            hi = mid
    out = []
    for i in range(lo, count):
        rec = blob[PINYIN_HEADER.size + i * rs:]
        code = rec[:6].rstrip(b"\0").decode("ascii")
        if not code.startswith(prefix):
            break
        out.append(rec[6:9].decode("utf-8"))
        if len(out) >= limit:
            break
    return out


def main():
    if len(sys.argv) != 2:
        sys.exit("Usage: python3 tools/gen_pinyin_ime.py path/to/pinyin_simp.dict.yaml")
    src = sys.argv[1]
    out = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "src", "fonts", "pinyin_table.h"))

    table, chars = read_pinyin(src, CANDIDATES_PER_CODE, font_covered_chars())
    blob = build_blob(table)

    # Self-test against the reference lookup before writing anything.
    checks = [("ni", "你"), ("zhong", "中"), ("guo", "国")]
    for prefix, want in checks:
        cands = blob_lookup_prefix(blob, prefix)
        assert cands, f"no candidates for {prefix!r}"
        assert want in cands, f"{prefix!r}: {want!r} not among candidates ({cands[:5]}...)"
        print(f"self-test {prefix!r}: first={cands[0]!r} contains={want!r} at "
              f"index {cands.index(want)} of {len(cands)}")

    os.makedirs(os.path.dirname(out), exist_ok=True)
    with open(out, "w") as h:
        h.write("// Auto-generated by tools/gen_pinyin_ime.py — do not hand-edit.\n")
        h.write("// Pinyin-IME candidate table, 'PYI1' format (see the generator's docstring).\n")
        h.write("// Derived from rime/rime-pinyin-simp (Apache-2.0), ")
        h.write("https://github.com/rime/rime-pinyin-simp\n")
        h.write(f"// {len(table)} codes, {sum(len(v) for v in table.values())} candidates, "
                f"top-{CANDIDATES_PER_CODE} weighted per code, {len(chars)} unique hanzi.\n")
        h.write("#pragma once\n#include <stdint.h>\n\n")
        h.write(f"const uint8_t kPinyinTableData[] = {{")
        for i in range(0, len(blob), 20):
            h.write("\n  " + ",".join(str(b) for b in blob[i:i + 20]) + ",")
        h.write("\n};\n")
        h.write(f"const unsigned kPinyinTableLen = {len(blob)}u;\n")
    print(f"wrote {out}: {len(blob)} bytes, {len(table)} codes, {len(chars)} unique hanzi")


if __name__ == "__main__":
    main()
