# Stops LVGL tiny_ttf from logging a bogus ERROR line for every missing glyph.
#
# LVGL 9.5.0's lv_tiny_ttf has a flaw in ttf_get_glyph_dsc_cb: when a glyph is
# NOT in the face, tiny_ttf_glyph_cache_create_cb() returns false, and
# lv_cache_acquire_or_create() turns that into a NULL entry — the same NULL it
# would return for an allocation failure. The callback then logs
#
#   [lvgl:ERROR] [Error] ttf_get_glyph_dsc_cb: cache not allocated lv_tiny_ttf.c:322
#
# and returns false. With a glyph CACHE enabled (our case) every lookup of a
# character the face doesn't have therefore logs this error — and in a fallback
# chain that is the NORMAL case, happening constantly:
#
#   Montserrat miss -> emoji tiny_ttf: every CJK character misses here -> ERROR
#                   -> CJK tiny_ttf: hit, renders fine
#
# So every Chinese character on screen produced one bogus ERROR line per
# redraw (a single wake refresh flooded ~1,650 lines and dragged the refresh
# to ~1 s, since ERROR logging is a synchronous Serial write). Rendering was
# never actually broken — the chain still fell through to the CJK face — but
# the log was unusable and the redraw needlessly slow.
#
# The patch replaces the error branch with a direct probe: create_cb does no
# allocation (it only reads stb_truetype tables), so we can ask the face
# directly and return the honest answer — true with metrics when the glyph
# exists, false when it doesn't (which is also what emojiFaceCovers() in
# text_fallback.cpp relies on to detect coverage). Genuine cache-machinery
# allocation failures would land here too, and now degrade to the un-cached
# direct path instead of failing the glyph.
#
# The same false-error exists in ttf_get_glyph_bitmap_cb, but there it only
# fires when the glyph exists and the draw-buffer cache itself failed — a real
# OOM worth reporting — so it is left alone.
#
# Scope: PlatformIO gives each environment its own libdeps copy, so this only
# touches the env that lists the script in extra_scripts.
#
# Idempotent, and safe to run before libdeps exist: on a fresh checkout the
# first build may run this before lvgl has been downloaded, in which case it
# warns and the patch lands on the next build. If lv_tiny_ttf.c exists but no
# longer matches, fail the build rather than silently shipping it unpatched.
Import("env")
import os

MARKER = "camillia-ttf-miss-patch"

TARGET = """        LV_LOG_ERROR("cache not allocated");
        return false;
    }"""

REPLACEMENT = """        // camillia-ttf-miss-patch: a NULL entry here also means "glyph not in
        // this face" (the create callback reports the miss), which is the
        // normal outcome for a fallback chain — not an allocation failure.
        // Probe the face directly (no allocation involved) and return the
        // honest answer instead of logging a bogus ERROR per character.
        if(tiny_ttf_glyph_cache_create_cb(&search_key, dsc)) {
            *dsc_out = search_key.glyph_dsc;
            dsc_out->entry = NULL;
            return true;
        }
        return false;
    }"""

path = os.path.join(env.subst("$PROJECT_LIBDEPS_DIR"), env.subst("$PIOENV"),
                    "lvgl", "src", "libs", "tiny_ttf", "lv_tiny_ttf.c")

if not os.path.isfile(path):
    print("[patch_lvgl_tiny_ttf] lvgl not fetched yet under %s" %
          env.subst("$PROJECT_LIBDEPS_DIR"))
    print("[patch_lvgl_tiny_ttf] NOT patched - run the build once more")
else:
    with open(path, encoding="utf-8") as f:
        src = f.read()
    if MARKER in src:
        print("[patch_lvgl_tiny_ttf] already patched")
    elif TARGET in src:
        src = src.replace(TARGET, REPLACEMENT, 1)
        with open(path, "w", encoding="utf-8") as f:
            f.write(src)
        print("[patch_lvgl_tiny_ttf] patched tiny_ttf missing-glyph error flood")
    else:
        raise RuntimeError(
            "[patch_lvgl_tiny_ttf] pattern not found in lv_tiny_ttf.c - version drift?")
