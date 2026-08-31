#pragma once
// User-text fallback face support (emoji + CJK).
//
// LVGL resolves a glyph the current font lacks by walking lv_font_t::fallback.
// We can't set that field on the const built-in Montserrat faces, so at startup
// we make a mutable copy of each text size and point its fallback at an
// lv_tiny_ttf instance of the same size — a monochrome Noto Emoji face
// (src/fonts/emoji_font.h), which itself chains into a Source Han Sans SC face
// (src/fonts/cjk_font.h) for Chinese. Emoji and Chinese text then draw inline
// wherever the message, name, or preview contains them — no per-call-site
// handling.
//
// History: this module was named emoji_font.* and carried only the emoji face
// until v4.8.0, which dropped that face and renamed the module for the CJK-only
// era (v4.8.0-v4.8.2). v4.8.3 brought the emoji face back — as a common-400
// subset — and re-chained it in front of the CJK face. v4.8.4 expanded the
// Cardputer emoji face to the full Noto Emoji cmap (all 1489 renderable
// glyphs, ~776 KB). v4.8.5 gave the pager the full emoji face too, paying for
// it by trimming the default CJK face from 9,903 to 7,000 characters.
#include <stdint.h>
#include <lvgl.h>

// Build the fallback-enabled Montserrat copies + tiny_ttf instances. Call once
// after lv_init() and before any UI is built. Safe to call more than once.
void textFallbackFontInit();

// Returns the fallback-enabled equivalent of a Montserrat text face. `base` is
// one of the &lv_font_montserrat_* pointers used for chat/DM/node text. Unknown
// fonts (or a build with the fallback disabled) return `base` unchanged, so
// callers can wrap unconditionally.
const lv_font_t *textFallbackFont(const lv_font_t *base);

// True when the emoji face can draw this codepoint (render size independent).
// Used to decide pass-through vs ASCII-alias in renderEmojiSafeText().
bool emojiFaceCovers(uint32_t cp);
