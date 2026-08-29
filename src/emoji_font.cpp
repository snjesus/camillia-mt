#include <Arduino.h>
#include "emoji_font.h"
#include "config.h"
#include "fonts/emoji_font.h"   // kEmojiFontData / kEmojiFontDataLen (flash)
#include "fonts/cjk_font.h"     // kCjkFontData / kCjkFontDataLen (flash) — CJK fallback
// lvgl.h pulls in src/libs/tiny_ttf/lv_tiny_ttf.h, which declares lv_tiny_ttf_*
// when LV_USE_TINY_TTF is set (see lv_conf.h). emoji_font.h already includes it.

// Text sizes that carry user content (messages, names, previews) and therefore
// need an emoji fallback. Titles/hints/splash faces are omitted deliberately —
// they don't render user text, and each extra instance costs LVGL-pool cache.
namespace {
struct EmojiSlot {
    const lv_font_t *base;     // built-in Montserrat face
    int32_t px;                // tiny_ttf render size to match it
    lv_font_t merged;          // mutable copy of base with emoji fallback attached
    lv_font_t *emoji;          // tiny_ttf instance (owns glyph cache) — emoji
    lv_font_t *cjk;            // tiny_ttf instance (owns glyph cache) — CJK fallback
    bool ready;
};

// The Montserrat sizes chat/DM/node text is actually drawn at (see main_lvgl
// scaledChatFont / kMainScreenFont / kChannelChatFont). montserrat_16 is here
// too because reply previews and some node rows use it.
EmojiSlot s_slots[] = {
    { &lv_font_montserrat_10, 12, {}, nullptr, nullptr, false },
    { &lv_font_montserrat_12, 14, {}, nullptr, nullptr, false },
    { &lv_font_montserrat_14, 16, {}, nullptr, nullptr, false },
    { &lv_font_montserrat_16, 18, {}, nullptr, nullptr, false },
    { &lv_font_montserrat_18, 20, {}, nullptr, nullptr, false },
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
// out-of-memory assert while scrolling the emoji picker.
//
// So keep expressing the budget in bytes, as v8 did, and derive the count per
// slot below. Every tiny_ttf allocation comes from the LVGL pool, so the
// Cardputer's 96 KB pool sets the ceiling. Emoji are re-rasterized on a cache
// miss — slower on scroll, never a failure — so a small cache is a fine trade.
#if defined(DEVICE_CARDPUTER_LORA_HAT)
// Cardputer has neither PSRAM nor much flash headroom. Tiny cache per glyph
// type — large glyphs are re-rasterized often, scroll stutters slightly.
constexpr size_t kEmojiCacheBytes = 1536;
constexpr size_t kCjkCacheBytes   = 512;   // CJK glyphs are smaller; chat row
                                           // rarely shows more than 2-3 unique
                                           // CJK chars per scroll.
#else
constexpr size_t kEmojiCacheBytes = 4096;
constexpr size_t kCjkCacheBytes   = 2048;
#endif

// Bitmap bytes plus the lv_draw_buf_t header, LRU node and TLSF block overhead
// that each cache entry drags along.
constexpr size_t kEmojiEntryOverhead = 110;

// Never drop below this: a cache too small to hold the glyphs on one row would
// re-rasterize on every frame of a scroll.
constexpr size_t kEmojiCacheMinGlyphs = 4;

size_t cacheGlyphsFor(int32_t px, size_t budgetBytes) {
    const size_t perEntry = (size_t)(px * px) + kEmojiEntryOverhead;
    const size_t n = budgetBytes / perEntry;
    return (n < kEmojiCacheMinGlyphs) ? kEmojiCacheMinGlyphs : n;
}

bool s_inited = false;
}

void emojiFontInit() {
#if LV_USE_TINY_TTF
    if (s_inited) return;
    s_inited = true;

    int readyCount = 0;
    size_t budgetTotal = 0;
    for (int i = 0; i < kSlotCount; i++) {
        EmojiSlot &s = s_slots[i];
        // Emoji glyph is bigger, gets larger cache budget
        const size_t cacheGlyphs = cacheGlyphsFor(s.px, kEmojiCacheBytes);
        budgetTotal += cacheGlyphs * ((size_t)(s.px * s.px) + kEmojiEntryOverhead);
        // Kerning is meaningless between emoji and costs an extra LVGL-pool
        // cache per instance, so it stays off.
        s.emoji = lv_tiny_ttf_create_data_ex((const void *)kEmojiFontData,
                                             (size_t)kEmojiFontDataLen,
                                             s.px, LV_FONT_KERNING_NONE,
                                             cacheGlyphs);
        if (!s.emoji) {
            // Out of pool at boot: leave this size without emoji rather than
            // half-initialize. emojiFont() will return the plain base for it.
            s.ready = false;
            Serial.printf("[emoji] slot %d (px=%d) tiny_ttf create FAILED "
                          "(LVGL pool exhausted?)\n", i, (int)s.px);
            // Still try to set up CJK even if emoji failed — text with CJK
            // but no emoji in that px size should still render chinese chars.
        } else {
            // Copy the const base into a writable face and chain the emoji
            // fallback. The copy shares the base's glyph data (pointers), so
            // only the struct is duplicated; the fallback is what LVGL walks
            // for missing glyphs.
            s.merged = *s.base;
            s.merged.fallback = s.emoji;
        }

        // Chain a second fallback (CJK) past emoji. LVGL walks the fallback
        // chain on a miss: Montserrat glyph misses → emoji tinyTTF → if still
        // missing → CJK tinyTTF. Two tinyTTF instances per slot is the price
        // we pay for keeping emoji and CJK in independent glyph caches.
        const size_t cjkCacheGlyphs = cacheGlyphsFor(s.px, kCjkCacheBytes);
        budgetTotal += cjkCacheGlyphs * ((size_t)(s.px * s.px) + kEmojiEntryOverhead);
        s.cjk = lv_tiny_ttf_create_data_ex((const void *)kCjkFontData,
                                           (size_t)kCjkFontDataLen,
                                           s.px, LV_FONT_KERNING_NONE,
                                           cjkCacheGlyphs);
        if (s.cjk) {
            // Re-anchor the chain so missed glyphs walk emoji → CJK.
            // If emoji init failed above, merged.fallback was never set, so
            // hang the CJK off the base directly instead.
            if (s.merged.fallback) {
                s.emoji->fallback = s.cjk;
            } else {
                s.merged = *s.base;
                s.merged.fallback = s.cjk;
            }
            s.ready = true;
        } else {
            // CJK create failed: leave the chain as-is (emoji only or
            // unhooked base), don't break what works.
            Serial.printf("[cjk] slot %d (px=%d) tiny_ttf create FAILED "
                          "(LVGL pool exhausted?)\n", i, (int)s.px);
        }

        if (s.ready) readyCount++;
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

const lv_font_t *emojiFont(const lv_font_t *base) {
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
