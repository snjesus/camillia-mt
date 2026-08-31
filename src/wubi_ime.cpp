#include "wubi_ime.h"
#include "fonts/wubi_table.h"

#include <algorithm>
#include <cstring>

// Table layout, mirror of tools/gen_wubi_ime.py:
//   header : magic "WBI1" (4) | version u16 | record size u16 | count u32
//   record : code[6] NUL-padded lower-case ASCII | hanzi[3] UTF-8 | rank u8
// Records are sorted by code, so a composition prefix-matches a contiguous
// run found by a lower-bound binary search. The 6-byte code field is the
// old pinyin geometry kept on purpose — wubi-86 codes (max 4) just pad.

namespace {
constexpr size_t kHeaderSize = 12;
constexpr size_t kRecordSize = 10;
constexpr size_t kCodeSize = 6;

uint32_t readU32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

struct State {
    bool enabled = false;
    char composition[wubi_ime::kMaxComposition + 1] = {0};
    // Candidates are direct pointers at each record's hanzi bytes in the
    // flash-resident table — no copies, no allocation.
    const uint8_t *cands[wubi_ime::kMaxCandidates] = {nullptr};
    int candCount = 0;
    int page = 0;
};

State s;

const uint8_t *tableHanzi(const uint8_t *rec) { return rec + kCodeSize; }

int recordCount() {
    if (kWubiTableLen < kHeaderSize || memcmp(kWubiTableData, "WBI1", 4) != 0) return 0;
    return (int)readU32(kWubiTableData + 8);
}

bool codeHasPrefix(const uint8_t *rec, const char *prefix, size_t len) {
    return strncmp((const char *)rec, prefix, len) == 0;
}

void lookup() {
    s.candCount = 0;
    s.page = 0;
    if (!s.composition[0]) return;
    const int count = recordCount();
    if (count <= 0) return;

    const size_t len = strlen(s.composition);
    int lo = 0, hi = count;
    // Lower bound: first record whose 6-byte code is >= the composition when
    // compared over the composition's length. strncmp over len bytes treats a
    // code equal to the prefix ("v") and longer codes ("vvvg") alike.
    while (lo < hi) {
        const int mid = lo + (hi - lo) / 2;
        const uint8_t *rec = kWubiTableData + kHeaderSize + (size_t)mid * kRecordSize;
        if (strncmp((const char *)rec, s.composition, len) < 0) {
            lo = mid + 1;
        } else {
            hi = mid;
        }
    }
    for (int i = lo; i < count && s.candCount < wubi_ime::kMaxCandidates; ++i) {
        const uint8_t *rec = kWubiTableData + kHeaderSize + (size_t)i * kRecordSize;
        if (!codeHasPrefix(rec, s.composition, len)) break;
        // The dict carries a char once per jianma level (2-key, 3-key, full
        // code). Under a short prefix the same hanzi shows up through several
        // of its codes — keep only its first (shortest) occurrence.
        const uint8_t *hz = tableHanzi(rec);
        bool dup = false;
        for (int j = 0; j < s.candCount; ++j) {
            if (memcmp(s.cands[j], hz, 3) == 0) { dup = true; break; }
        }
        if (dup) continue;
        s.cands[s.candCount++] = hz;
    }
}

int pageCountFor(int candCount) {
    return candCount == 0 ? 0 : (candCount + wubi_ime::kPageSize - 1) / wubi_ime::kPageSize;
}

}  // namespace

namespace wubi_ime {

bool enabled() { return s.enabled; }

void setEnabled(bool on) {
    if (s.enabled == on) return;
    s.enabled = on;
    reset();
}

bool toggle() {
    s.enabled = !s.enabled;
    reset();
    return s.enabled;
}

void reset() {
    s.composition[0] = '\0';
    s.candCount = 0;
    s.page = 0;
}

const char *composition() { return s.composition; }

bool hasComposition() { return s.composition[0] != '\0'; }

int candidateCount() { return s.candCount; }

const char *candidate(int index) {
    if (index < 0 || index >= s.candCount) return nullptr;
    return (const char *)s.cands[index];
}

int page() { return s.page; }

int pageCount() { return pageCountFor(s.candCount); }

bool nextPage() {
    if (s.page + 1 >= pageCountFor(s.candCount)) return false;
    ++s.page;
    return true;
}

bool prevPage() {
    if (s.page == 0) return false;
    --s.page;
    return true;
}

bool feedLetter(char c) {
    if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
    // Z is the wubi-86 wildcard/learn key, not a code key here — reject it so
    // the caller lets the letter fall through to ordinary typing.
    if (c == 'z') return false;
    if (c < 'a' || c > 'y') return false;
    const size_t len = strlen(s.composition);
    if (len >= kMaxComposition) return false;
    s.composition[len] = c;
    s.composition[len + 1] = '\0';
    lookup();
    return true;
}

bool feedBackspace() {
    const size_t len = strlen(s.composition);
    if (len == 0) return false;
    s.composition[len - 1] = '\0';
    lookup();
    return true;
}

bool commitIndex(int index, char out[4]) {
    const char *hz = candidate(index);
    if (!hz) return false;
    memcpy(out, hz, 3);
    out[3] = '\0';
    reset();
    return true;
}

bool commitFirst(char out[4]) {
    const int first = s.page * kPageSize;
    return commitIndex(first, out);
}

}  // namespace wubi_ime
