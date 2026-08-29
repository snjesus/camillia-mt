#include "pinyin_ime.h"
#include "fonts/pinyin_table.h"

#include <algorithm>
#include <cstring>

// Table layout, mirror of tools/gen_pinyin_ime.py:
//   header : magic "PYI1" (4) | version u16 | record size u16 | count u32
//   record : code[6] NUL-padded lower-case ASCII | hanzi[3] UTF-8 | rank u8
// Records are sorted by code, so a composition prefix-matches a contiguous
// run found by a lower-bound binary search.

namespace {
constexpr size_t kHeaderSize = 12;
constexpr size_t kRecordSize = 10;
constexpr size_t kCodeSize = 6;

uint32_t readU32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

struct State {
    bool enabled = false;
    char composition[pinyin_ime::kMaxComposition + 1] = {0};
    // Candidates are direct pointers at each record's hanzi bytes in the
    // flash-resident table — no copies, no allocation.
    const uint8_t *cands[pinyin_ime::kMaxCandidates] = {nullptr};
    int candCount = 0;
    int page = 0;
};

State s;

const uint8_t *tableHanzi(const uint8_t *rec) { return rec + kCodeSize; }

int recordCount() {
    if (kPinyinTableLen < kHeaderSize || memcmp(kPinyinTableData, "PYI1", 4) != 0) return 0;
    return (int)readU32(kPinyinTableData + 8);
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
    // code equal to the prefix ("ni") and longer codes ("niang") alike.
    while (lo < hi) {
        const int mid = lo + (hi - lo) / 2;
        const uint8_t *rec = kPinyinTableData + kHeaderSize + (size_t)mid * kRecordSize;
        if (strncmp((const char *)rec, s.composition, len) < 0) {
            lo = mid + 1;
        } else {
            hi = mid;
        }
    }
    for (int i = lo; i < count && s.candCount < pinyin_ime::kMaxCandidates; ++i) {
        const uint8_t *rec = kPinyinTableData + kHeaderSize + (size_t)i * kRecordSize;
        if (!codeHasPrefix(rec, s.composition, len)) break;
        s.cands[s.candCount++] = tableHanzi(rec);
    }
}

int pageCountFor(int candCount) {
    return candCount == 0 ? 0 : (candCount + pinyin_ime::kPageSize - 1) / pinyin_ime::kPageSize;
}

}  // namespace

namespace pinyin_ime {

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
    if (c < 'a' || c > 'z') return false;
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

}  // namespace pinyin_ime
