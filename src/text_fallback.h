#pragma once
// User-text fallback face support (CJK).
//
// LVGL resolves a glyph the current font lacks by walking lv_font_t::fallback.
// We can't set that field on the const built-in Montserrat faces, so at startup
// we make a mutable copy of each text size and point its fallback at an
// lv_tiny_ttf instance of the same size, backed by a flash-resident Source Han
// Sans SC face (src/fonts/cjk_font.h) covering the full Jun Da frequency list.
// Chinese text then draws inline wherever the message, name, or preview
// contains it — no per-call-site handling.
//
// Naming note: a monochrome emoji face used to be chained in front of the CJK
// face here. It was removed (its 2.4 MB of flash went to the full 9,903-char
// CJK face) and this module renamed from emoji_font.* accordingly. Emoji
// codepoints — including tapback reactions — now render as the missing-glyph
// box; the reactions are still sent and understood on the wire.
#include <lvgl.h>

// Build the fallback-enabled Montserrat copies + tiny_ttf instances. Call once
// after lv_init() and before any UI is built. Safe to call more than once.
void textFallbackFontInit();

// Returns the fallback-enabled equivalent of a Montserrat text face. `base` is
// one of the &lv_font_montserrat_* pointers used for chat/DM/node text. Unknown
// fonts (or a build with the fallback disabled) return `base` unchanged, so
// callers can wrap unconditionally.
const lv_font_t *textFallbackFont(const lv_font_t *base);
