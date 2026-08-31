#include <Arduino.h>
#include "text_fallback.h"
#include "config.h"
// CJK fallback face, flash-resident. Two cuts ship (see tools/gen_cjk_font.py):
//   default          — the full 9,903-character list (~2.4 MB), for the 16 MB
//                      boards whose OTA slots grew to ~5 MB to hold it
//   -DCJK_FONT_SMALL — a 7,000-character list (~1.6 MB) for cardputer-cap, the
//                      one 8 MB flash, whose slots cap at 3.75 MB
#if defined(CJK_FONT_SMALL)
#include "fonts/cjk_font_small.h"
#else
#include "fonts/cjk_font.h"
#endif
// lvgl.h pulls in src/libs/tiny_ttf/lv_tiny_ttf.h, which declares lv_tiny_ttf_*
// when LV_USE_TINY_TTF is set (see lv_conf.h). text_fallback.h already includes it.

// Text sizes that carry user content (messages, names, previews) and therefore
// need the CJK fallback. Titles/hints/splash faces are omitted deliberately —
// they don't render user text, and each extra instance costs LVGL-pool cache.
namespace {
struct FallbackSlot {
    const lv_font_t *base;     // built-in Montserrat face
    int32_t px;                // tiny_ttf render size to match it
    lv_font_t merged;          // mutable copy of base with the fallback attached
    lv_font_t *cjk;            // tiny_ttf instance (owns glyph cache) — CJK fallback
    bool ready;
};

// The Montserrat sizes chat/DM/node text is actually drawn at (see main_lvgl
// scaledChatFont / kMainScreenFont / kChannelChatFont). montserrat_16 is here
// too because reply previews and some node rows use it.
FallbackSlot s_slots[] = {
    { &lv_font_montserrat_10, 12, {}, nullptr, false },
    { &lv_font_montserrat_12, 14, {}, nullptr, false },
    { &lv_font_montserrat_14, 16, {}, nullptr, false },
    { &lv_font_montserrat_16, 18, {}, nullptr, false },
    { &lv_font_montserrat_18, 20, {}, nullptr, false },
};
constexpr int kSlotCount = (int)(sizeof(s_slots) / sizeof(s_slots[0]));

// Per-instance glyph cache budget, in BYTES.
//
// LVGL v9 takes this as a glyph *count*, not a byte budget like v8 did — but a
// count is the wrong thing to hold constant across slots. Each cached entry
// owns a full A8 bitmap of the rendered glyph (lv_draw_buf_create_ex in
// tiny_ttf_draw_data_cache_create_cb), so its cost scales with px²: a 20px
// entry is ~2x a 12px one. A flat count therefore spends far more on the large
// slots than the small ones, and with five instances live it overran the pool —
// stb's own scratch allocations then failed mid-rasterize and tripped its
// out-of-memory assert while scrolling a picker grid.
//
// So keep expressing the budget in bytes, as v8 did, and derive the count per
// slot below. Every tiny_ttf allocation comes from the LVGL pool, so the
// Cardputer's 96 KB pool sets the ceiling. Glyphs are re-rasterized on a cache
// miss — slower on scroll, never a failure — so a small cache is a fine trade.
#if defined(DEVICE_CARDPUTER_LORA_HAT)
// Cardputer has neither PSRAM nor much flash headroom. Small cache per slot —
// large glyphs are re-rasterized often, scroll stutters slightly.
constexpr size_t kCjkCacheBytes = 1536;
#else
constexpr size_t kCjkCacheBytes = 4096;
#endif

// Bitmap bytes plus the lv_draw_buf_t header, LRU node and TLSF block overhead
// that each cache entry drags along.
constexpr size_t kGlyphEntryOverhead = 110;

// Never drop below this: a cache too small to hold the glyphs on one row would
// re-rasterize on every frame of a scroll.
constexpr size_t kCacheMinGlyphs = 4;

size_t cacheGlyphsFor(int32_t px, size_t budgetBytes) {
    const size_t perEntry = (size_t)(px * px) + kGlyphEntryOverhead;
    const size_t n = budgetBytes / perEntry;
    return (n < kCacheMinGlyphs) ? kCacheMinGlyphs : n;
}

bool s_inited = false;
}

void textFallbackFontInit() {
#if LV_USE_TINY_TTF
    if (s_inited) return;
    s_inited = true;

    int readyCount = 0;
    size_t budgetTotal = 0;
    for (int i = 0; i < kSlotCount; i++) {
        FallbackSlot &s = s_slots[i];
        // Kerning is meaningless across scripts here and costs an extra
        // LVGL-pool cache per instance, so it stays off.
        const size_t cacheGlyphs = cacheGlyphsFor(s.px, kCjkCacheBytes);
        budgetTotal += cacheGlyphs * ((size_t)(s.px * s.px) + kGlyphEntryOverhead);
        s.cjk = lv_tiny_ttf_create_data_ex((const void *)kCjkFontData,
                                           (size_t)kCjkFontDataLen,
                                           s.px, LV_FONT_KERNING_NONE,
                                           cacheGlyphs);
        if (s.cjk) {
            // Copy the const base into a writable face and chain the fallback.
            // The copy shares the base's glyph data (pointers), so only the
            // struct is duplicated; the fallback is what LVGL walks for
            // missing glyphs: Montserrat miss → CJK tinyTTF.
            s.merged = *s.base;
            s.merged.fallback = s.cjk;
            s.ready = true;
            readyCount++;
        } else {
            // Out of pool at boot: leave this size on the plain base rather
            // than half-initialize. textFallbackFont() returns the plain base
            // for it, so CJK text shows tofu boxes at that size only.
            Serial.printf("[font] slot %d (px=%d) tiny_ttf create FAILED "
                          "(LVGL pool exhausted?)\n", i, (int)s.px);
        }
    }
    Serial.printf("[font] init: %d/%d fallback faces ready (cjk %u bytes; "
                  "glyph cache budget ~%u bytes of LVGL pool)\n",
                  readyCount, kSlotCount,
                  (unsigned)kCjkFontDataLen, (unsigned)budgetTotal);
#else
    Serial.println("[font] LV_USE_TINY_TTF is 0 — cjk fallback disabled");
#endif
}

const lv_font_t *textFallbackFont(const lv_font_t *base) {
#if LV_USE_TINY_TTF
    if (base) {
        for (int i = 0; i < kSlotCount; i++) {
            if (s_slots[i].base == base) {
                return s_slots[i].ready ? &s_slots[i].merged : base;
            }
        }
    }
#endif
    return base;
}
