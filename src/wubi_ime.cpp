#include "wubi_ime.h"
#include "fonts/wubi_table.h"

#if defined(ESP32)
#  include <esp32s3/rom/miniz.h>     // tinfl_decompress_mem_to_mem
#  include <esp_heap_caps.h>          // MALLOC_CAP_SPIRAM
#else
#  include <cstdlib>                  // host fallback (unit tests / sim)
#  ifndef MALLOC_CAP_SPIRAM
#    define MALLOC_CAP_SPIRAM 0
#  endif
#endif

#include <algorithm>
#include <cstring>

// WBI2 header (32 bytes, mirror of tools/gen_wubi_ime.py's pack string)
//   magic[4] ver[2] flags[2] recordCount[4] recordsSize[4] indexOffset[4]
//   indexCount[4] compType[4] reserved[4]
namespace {
constexpr size_t kHeaderSize = 32;
constexpr size_t kCodeSize = 4;
constexpr uint8_t kCompTypeDeflateRaw = 1;
constexpr uint32_t kMagicWBI2 = 0x32494257u;  // "WBI2" little-endian

uint32_t rdU32(const uint8_t *p) {
    return uint32_t(p[0]) | (uint32_t(p[1]) << 8) |
           (uint32_t(p[2]) << 16) | (uint32_t(p[3]) << 24);
}
}  // namespace

// Runtime state -----------------------------------------------------------

namespace {

// The decompressed WBI2 payload lives in PSRAM. All offsets below are
// absolute byte offsets from s.payload (i.e. from the 32-byte header byte 0).
const uint8_t *s_payload = nullptr;
uint32_t s_recordCount = 0;
uint32_t s_indexOffset = 0;      // abs offset (from s_payload) to index[]
uint32_t s_indexCount = 0;       // number of u32 entries in the index

uint32_t idxOff(uint32_t i);  // forward decl

struct RecView {
    char code[5];
    const uint8_t *text;
    uint8_t textLen;
    uint8_t rank;
    uint32_t recSize;
};

RecView parse(uint32_t off);   // forward decl

uint32_t idxOff(uint32_t i) {
    return rdU32(s_payload + s_indexOffset + sizeof(uint32_t) * i);
}

RecView parse(uint32_t off) {
    RecView v{};
    const uint8_t *p = s_payload + off;
    for (int i = 0; i < 4; ++i) {
        char c = (char)p[i];
        v.code[i] = (c == '\0' ? '\0' : c);
    }
    v.code[4] = '\0';
    v.textLen = p[kCodeSize];
    v.text = p + kCodeSize + 1;
    v.rank = p[kCodeSize + 1 + v.textLen];
    v.recSize = uint32_t(kCodeSize + 1 + v.textLen + 1);
    return v;
}

struct State {
    bool enabled = false;
    char composition[wubi_ime::kMaxComposition + 1] = {0};
    // Each candidate is a pointer into the PSRAM payload's text bytes,
    // with its own length stored separately.
    const uint8_t *cands[wubi_ime::kMaxCandidates] = {nullptr};
    uint8_t candLens[wubi_ime::kMaxCandidates] = {0};
    int candCount = 0;
    int page = 0;
};
State s;

void lookup() {
    s.candCount = 0;
    s.page = 0;
    if (!s_payload || !s.composition[0]) return;

    const size_t len = strlen(s.composition);
    const uint32_t nBlocks = s_indexCount - 1;
    if (nBlocks == 0) return;

    // Block-level lower-bound: first block whose FIRST record code
    // (string-compared over `len`) is >= composition.
    uint32_t lo = 0, hi = nBlocks;
    while (lo < hi) {
        const uint32_t mid = lo + (hi - lo) / 2;
        const RecView v = parse(idxOff(mid));
        if (strncmp(v.code, s.composition, len) < 0) {
            lo = mid + 1;
        } else {
            hi = mid;
        }
    }
    // `lo` is our first block. But if block lo's first record is strictly
    // greater than the composition, records in block (lo-1) may still end
    // with codes that share the prefix — start one back and verify.
    uint32_t b = (lo == 0) ? 0 : lo - 1;
    for (; b < nBlocks && s.candCount < wubi_ime::kMaxCandidates; ++b) {
        uint32_t off = idxOff(b);
        const uint32_t end = idxOff(b + 1);
        while (off < end && s.candCount < wubi_ime::kMaxCandidates) {
            const RecView v = parse(off);
            // Sorted invariant: once code's prefix bytes exceed composition,
            // we can exit the whole scan.
            const int cmp = strncmp(v.code, s.composition, len);
            if (cmp > 0) return;
            off += v.recSize;
            if (cmp < 0) continue;

            // Deduplicate by text content (len + byte compare). Within the
            // same prefix the same phrase can surface through different
            // jianma/short-code entries; keep only the first occurrence.
            bool dup = false;
            for (int j = 0; j < s.candCount; ++j) {
                if (s.candLens[j] != v.textLen) continue;
                if (memcmp(s.cands[j], v.text, v.textLen) == 0) { dup = true; break; }
            }
            if (dup) continue;
            s.cands[s.candCount] = v.text;
            s.candLens[s.candCount] = v.textLen;
            ++s.candCount;
        }
    }
}

int pageCountFor(int n) { return n == 0 ? 0 : (n + wubi_ime::kPageSize - 1) / wubi_ime::kPageSize; }

#if defined(ESP32)
bool inflateOnce() {
    if (s_payload) return true;
    void *mem = heap_caps_malloc(kWubiTableRawSize, MALLOC_CAP_SPIRAM);
    if (!mem) return false;
    // ESP32-S3 ROM exports tinfl_decompress_mem_to_mem; flags=0 = raw deflate
    // (no TINFL_FLAG_PARSE_ZLIB_HEADER).
    const size_t got = tinfl_decompress_mem_to_mem(
        mem, kWubiTableRawSize,
        kWubiTableCompressed, kWubiTableCompressedLen,
        /*flags=*/TINFL_FLAG_USING_NON_WRAPPING_OUTPUT_BUF);
    if (got == TINFL_DECOMPRESS_MEM_TO_MEM_FAILED || got != kWubiTableRawSize) {
        heap_caps_free(mem);
        return false;
    }
    const uint8_t *buf = (const uint8_t *)mem;
    // Validate the header.
    if (rdU32(buf) != kMagicWBI2) { heap_caps_free(mem); return false; }
    const uint32_t compType = rdU32(buf + 24);
    if (compType != kCompTypeDeflateRaw) { heap_caps_free(mem); return false; }
    s_payload     = buf;
    s_recordCount = rdU32(buf + 8);
    s_indexOffset = rdU32(buf + 16);
    s_indexCount  = rdU32(buf + 20);
    return true;
}
#else   // host / sim: just decompress into a static buffer (caller's malloc)
// For host sim: zlib available? Keep tiny inline raw-inflate not needed —
// return success false; real engines only run on ESP32 boards.
bool inflateOnce() { return false; }
#endif

}  // namespace

// Public API -------------------------------------------------------------

namespace wubi_ime {

bool ready() { return s_payload != nullptr; }
bool ensureInit() {
    static bool attempted = false;
    if (attempted) return ready();
    attempted = true;
    return inflateOnce();
}

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

// Because records are contiguous in the decompressed payload we guarantee
// text is followed by at least one pad byte (the next record's code[0] is
// 'a'-'y' or 0x00 — not a UTF-8 continuation), but for safety candidate()
// copies the text into a thread-local scratch buffer, NUL-terminated.
const char *candidate(int index) {
    thread_local char buf[kMaxCandidateBytes];
    if (index < 0 || index >= s.candCount) return nullptr;
    const int n = std::min<int>(s.candLens[index], kMaxCandidateBytes - 1);
    memcpy(buf, s.cands[index], n);
    buf[n] = '\0';
    return buf;
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
    if (c == 'z') return false;
    if (c < 'a' || c > 'y') return false;
    const size_t len = strlen(s.composition);
    if (len >= kMaxComposition) return false;
    if (!ensureInit()) return false;
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

bool commitIndex(int index, char *out) {
    if (index < 0 || index >= s.candCount) return false;
    const int n = s.candLens[index];
    memcpy(out, s.cands[index], n);
    out[n] = '\0';
    reset();
    return true;
}

bool commitFirst(char *out) {
    const int first = s.page * kPageSize;
    return commitIndex(first, out);
}

}  // namespace wubi_ime
