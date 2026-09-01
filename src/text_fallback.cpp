#include <Arduino.h>
#include "text_fallback.h"
#include "config.h"
// Emoji fallback face, flash-resident (Noto Emoji, monochrome). Two cuts ship
// (selected via platformio.ini build_flags):
//   default              — the full Noto Emoji cmap (all 1489 renderable
//                          codepoints, ~776 KB). Used by cardputer-cap (paired
//                          with the 5,000-char CJK small face, 99.1 % slot) and
//                          tlora-pager-tft (paired with the 7,000-char default
//                          CJK face, ~93 % slot) since v4.8.5.
//   -DEMOJI_FONT_COMPACT — the 400-common subset (~212 KB), for the remaining
//                          16 MB boards. ZWJ ligatures and regional-indicator
//                          flag pairs still need GSUB (we strip layout tables),
//                          so those composite sequences never render regardless
//                          of cut.
#if defined(EMOJI_FONT_COMPACT)
#include "fonts/emoji_font_compact.h"
#else
#include "fonts/emoji_font.h"
#endif
// CJK fallback face, flash-resident. Two cuts ship (see tools/gen_cjk_font.py):
//   default          — a 7,000-character list (~1.6 MB) for the 16 MB boards;
//                      trimmed from the full 9,903 list in v4.8.5 so the pager
//                      could take the full emoji face, and kept by the other
//                      16 MB envs (which pair it with the compact emoji cut)
//   -DCJK_FONT_SMALL — a 5,000-character list (~1.1 MB) for cardputer-cap, the
//                      one 8 MB flash, whose slots cap at 3.75 MB
#if defined(CJK_FONT_SMALL)
#include "fonts/cjk_font_small.h"
#else
#include "fonts/cjk_font.h"
#endif
// lvgl.h pulls in src/libs/tiny_ttf/lv_tiny_ttf.h, which declares lv_tiny_ttf_*
// when LV_USE_TINY_TTF is set (see lv_conf.h). text_fallback.h already includes it.

// Text sizes that carry user content (messages, names, previews) and therefore
// need the fallback chain. Titles/hints/splash faces are omitted deliberately —
// they don't render user text, and each extra instance costs LVGL-pool cache.
namespace {
struct FallbackSlot {
    const lv_font_t *base;     // built-in Montserrat face
    int32_t px;                // tiny_ttf render size to match it
    lv_font_t merged;          // mutable copy of base with the fallback attached
    lv_font_t *emoji;          // tiny_ttf instance (owns glyph cache) — emoji
    lv_font_t *cjk;            // tiny_ttf instance (owns glyph cache) — CJK fallback
    bool ready;
};

// The Montserrat sizes chat/DM/node text is actually drawn at (see main_lvgl
// scaledChatFont / kMainScreenFont / kChannelChatFont). montserrat_16 is here
// too because reply previews and some node rows use it.
//
// Cardputer (DEVICE_CARDPUTER_LORA_HAT) carries only 3 of the 5 sizes: its
// 80 KB LVGL pool cannot afford 10 tiny_ttf instances (5 slots × emoji + CJK).
// The 3 kept are the ones actually drawn at by default: montserrat_10 (main
// screen / DM list / node rows), montserrat_14 (default Medium chat), and
// montserrat_16 (Large chat / reply previews). Small (12) and XLarge (18)
// lose fallback — CJK/emoji at those sizes renders as boxes until the user
// switches to a size that has a slot. This trades 4 instances (~8 KB of pool)
// back to the system for WiFi + LVGL objects.
#if defined(DEVICE_CARDPUTER_LORA_HAT)
FallbackSlot s_slots[] = {
    { &lv_font_montserrat_10, 12, {}, nullptr, nullptr, false },
    { &lv_font_montserrat_14, 16, {}, nullptr, nullptr, false },
    { &lv_font_montserrat_16, 18, {}, nullptr, nullptr, false },
};
#else
FallbackSlot s_slots[] = {
    { &lv_font_montserrat_10, 12, {}, nullptr, nullptr, false },
    { &lv_font_montserrat_12, 14, {}, nullptr, nullptr, false },
    { &lv_font_montserrat_14, 16, {}, nullptr, nullptr, false },
    { &lv_font_montserrat_16, 18, {}, nullptr, nullptr, false },
    { &lv_font_montserrat_18, 20, {}, nullptr, nullptr, false },
};
#endif
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
// Cardputer's 80 KB pool sets the ceiling. Glyphs are re-rasterized on a cache
// miss — slower on scroll, never a failure — so a small cache is a fine trade.
#if defined(DEVICE_CARDPUTER_LORA_HAT)
// Cardputer has neither PSRAM nor much flash headroom. Two tiny_ttf instances
// per slot (emoji + CJK) still fits: ~1.5 KB + ~0.5 KB per slot of cache.
constexpr size_t kEmojiCacheBytes = 1536;
constexpr size_t kCjkCacheBytes   = 512;
#else
constexpr size_t kEmojiCacheBytes = 4096;
constexpr size_t kCjkCacheBytes   = 2048;
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
        s.emoji = lv_tiny_ttf_create_data_ex((const void *)kEmojiFontData,
                                             (size_t)kEmojiFontDataLen,
                                             s.px, LV_FONT_KERNING_NONE,
                                             cacheGlyphsFor(s.px, kEmojiCacheBytes));
        if (s.emoji) {
            // Copy the const base into a writable face and chain the emoji
            // fallback. The copy shares the base's glyph data (pointers), so
            // only the struct is duplicated; the fallback is what LVGL walks
            // for missing glyphs: Montserrat miss → emoji tinyTTF.
            s.merged = *s.base;
            s.merged.fallback = s.emoji;
        } else {
            // Out of pool at boot: leave this size without emoji rather than
            // half-initialize; the CJK pass below re-anchors on the base.
            s.ready = false;
            Serial.printf("[font] slot %d (px=%d) emoji tiny_ttf create FAILED "
                          "(LVGL pool exhausted?)\n", i, (int)s.px);
        }

        // Chain a second fallback (CJK) past emoji. LVGL walks the fallback
        // chain on a miss: Montserrat glyph misses → emoji tinyTTF → if still
        // missing → CJK tinyTTF. Two tinyTTF instances per slot is the price
        // we pay for keeping emoji and CJK in independent glyph caches.
        const size_t cjkCacheGlyphs = cacheGlyphsFor(s.px, kCjkCacheBytes);
        budgetTotal += cjkCacheGlyphs * ((size_t)(s.px * s.px) + kGlyphEntryOverhead);
        s.cjk = lv_tiny_ttf_create_data_ex((const void *)kCjkFontData,
                                           (size_t)kCjkFontDataLen,
                                           s.px, LV_FONT_KERNING_NONE,
                                           cjkCacheGlyphs);
        if (s.cjk) {
            if (s.emoji) {
                s.emoji->fallback = s.cjk;
                s.ready = true;
            } else {
                // Emoji init failed: hang CJK off the base directly so CJK
                // text still renders at this size.
                s.merged = *s.base;
                s.merged.fallback = s.cjk;
                s.ready = true;
            }
            readyCount++;
        } else {
            // CJK create failed: leave the chain as-is (emoji only or unhooked
            // base), don't break what works.
            Serial.printf("[font] slot %d (px=%d) cjk tiny_ttf create FAILED "
                          "(LVGL pool exhausted?)\n", i, (int)s.px);
        }
    }
    Serial.printf("[font] init: %d/%d fallback faces ready (emoji %u bytes, "
                  "cjk %u bytes; glyph cache budget ~%u bytes of LVGL pool)\n",
                  readyCount, kSlotCount,
                  (unsigned)kEmojiFontDataLen, (unsigned)kCjkFontDataLen,
                  (unsigned)budgetTotal);
#else
    Serial.println("[font] LV_USE_TINY_TTF is 0 — emoji/cjk fallback disabled");
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

bool emojiFaceCovers(uint32_t cp) {
#if LV_USE_TINY_TTF
    if (!s_slots[0].emoji) return false;
    // Glyph presence is independent of the render size, so slot 0's instance
    // answers for the face. (Another px would rasterize nothing here — the
    // query walks the face's cmap, not the cache.)
    lv_font_glyph_dsc_t dsc;
    return lv_font_get_glyph_dsc(s_slots[0].emoji, &dsc, (uint32_t)cp, (uint32_t)cp);
#else
    (void)cp;
    return false;
#endif
}
