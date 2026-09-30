// N2d (NATIVE-DESKTOP-PLAN.md 27.7): text through DirectWrite -- measurement, wrapping, the monospace family, caret
// stops by cluster (combining marks, a Devanagari conjunct, surrogate pairs, an emoji ZWJ sequence, Arabic letters),
// hit-testing round trips including right-to-left runs, the layout cache, and goldens of Latin, wrapped, aligned,
// Arabic, mixed bidi, Devanagari, CJK, emoji and monospace text at 100/150/200% on WARP. Part of tcad_ui_render_tests.
#include "mini_test.hpp"
#include "render_test_support.hpp"

#include "ui/core/style.hpp"
#include "ui/render/window_surface.hpp"
#include "ui/win32/d2d_painter.hpp"
#include "ui/win32/dwrite_text.hpp"

#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

using namespace tcad::ui;
using namespace tcad::ui::testing;
using tcad::desktop::theme::T;

namespace {

DWriteTextEngine& engine() {
    static DWriteTextEngine e(warp()->dwrite());
    return e;
}

TextStyle st(float size = 12, FontFamily f = FontFamily::Ui) {
    TextStyle s;
    s.size = size;
    s.family = f;
    return s;
}

bool approx(float a, float b, float tol) { return std::fabs(a - b) <= tol; }

std::string stops(const std::vector<std::size_t>& v) {
    std::string s;
    for (std::size_t x : v) s += std::to_string(x) + " ";
    return s;
}

// Samples (UTF-8): e + combining acute; a Devanagari conjunct with a vowel sign; a surrogate pair; a family emoji
// (man ZWJ woman ZWJ girl); Arabic "salam" (4 letters).
const std::string kCombining = "e\xCC\x81x";
const std::string kConjunct = "\xE0\xA4\x95\xE0\xA5\x8D\xE0\xA4\xB7\xE0\xA4\xBF";  // क्षि
const std::string kSmiley = "a\xF0\x9F\x98\x80" "b";                                   // a😀b
const std::string kFamily = "\xF0\x9F\x91\xA8\xE2\x80\x8D\xF0\x9F\x91\xA9\xE2\x80\x8D\xF0\x9F\x91\xA7";  // 👨‍👩‍👧
const std::string kArabic = "\xD8\xB3\xD9\x84\xD8\xA7\xD9\x85";                         // سلام

}  // namespace

TEST(measure_is_consistent_and_scales_with_the_font) {
    auto& e = engine();
    const SizeF empty = e.measure("", st()), hello = e.measure("Hello", st());
    std::printf("  12-DIP Segoe UI: line height %.2f, \"Hello\" %.2f DIPs\n", empty.height, hello.width);
    CHECK(empty.width == 0 && empty.height > 12 && empty.height < 20);
    CHECK(hello.width > 20 && approx(hello.height, empty.height, 0.01f));
    CHECK(approx(e.measure("HelloHello", st()).width, 2 * hello.width, 0.5f));
    CHECK(approx(e.measure("Hello", st(24)).width, 2 * hello.width, 0.5f));  // natural metrics: linear in the size
    TextStyle bold = st();
    bold.bold = true;
    CHECK(e.measure("Hello", bold).width > hello.width);
    CHECK(e.measure("Hello   ", st()).width > hello.width);  // one line keeps trailing spaces
}

TEST(monospace_gives_every_character_the_same_advance) {
    auto& e = engine();
    const float i4 = e.measure("iiii", st(12, FontFamily::Monospace)).width, w4 = e.measure("WWWW", st(12, FontFamily::Monospace)).width;
    CHECK(approx(i4, w4, 0.01f));
    CHECK(e.measure("iiii", st()).width < 0.6f * e.measure("WWWW", st()).width);  // the UI font is proportional
}

TEST(wrapping_breaks_at_words_and_never_exceeds_the_width_unless_a_word_must) {
    auto& e = engine();
    const std::string text = "one two three four five six seven";
    const float two = e.measure("one two", st()).width;
    const SizeF wrapped = e.measureWrapped(text, st(), two + 1);
    const int lines = e.lineCount(text, st(), two + 1);
    const float line_h = e.measure("x", st()).height;
    std::printf("  wrapped at %.1f DIPs: %d lines, %.1f x %.1f\n", two + 1, lines, wrapped.width, wrapped.height);
    CHECK(lines >= 3);
    CHECK(wrapped.width <= two + 1);
    CHECK(approx(wrapped.height, lines * line_h, 0.5f));
    CHECK_EQ(e.lineCount(text, st(), 10000), 1);
    CHECK_EQ(e.lineCount("Unbreakable", st(), 5), 1);  // a word wider than the box overflows rather than splits
    CHECK(e.measureWrapped("Unbreakable", st(), 5).width > 5);
}

TEST(caret_stops_follow_clusters_not_bytes_or_code_points) {
    auto& e = engine();
    CHECK_EQ(stops(e.caretStops("abc", st())), std::string("0 1 2 3 "));
    CHECK_EQ(stops(e.caretStops(kCombining, st())), std::string("0 3 4 "));  // e+U+0301 is one cluster
    CHECK_EQ(stops(e.caretStops(kSmiley, st())), std::string("0 1 5 6 "));   // the surrogate pair is one
    CHECK_EQ(stops(e.caretStops(kFamily, st())), std::string("0 18 "));      // the whole ZWJ sequence is one
    const auto conj = e.caretStops(kConjunct, st());
    std::printf("  Devanagari conjunct (4 code points, 12 bytes): stops %s\n", stops(conj).c_str());
    CHECK(conj.front() == 0 && conj.back() == 12 && conj.size() <= 3);  // fewer caret stops than code points
    CHECK_EQ(stops(e.caretStops(kArabic, st())), std::string("0 2 4 6 8 "));
    CHECK_EQ(stops(e.caretStops("", st())), std::string("0 "));
}

TEST(hit_testing_round_trips_every_caret_stop_including_right_to_left) {
    auto& e = engine();
    const std::string mixed = std::string("ab ") + kArabic + " cd";
    for (const std::string& s : {std::string("Voltage 5 V"), kCombining, kSmiley, kConjunct, kArabic, mixed}) {
        int bad = 0;
        for (std::size_t off : e.caretStops(s, st())) {
            const RectF c = e.caretRect(s, st(), 0, off);
            const TextHit h = e.hitTest(s, st(), 0, {c.x, c.y + c.height / 2});
            // one direction: exactly the same offset. Mixed: at a direction boundary one visual x is two logical
            // offsets, so the hit must come back to the same CARET POSITION, which is what the user sees.
            const bool ok = s == mixed ? approx(e.caretRect(s, st(), 0, h.offset).x, c.x, 0.01f) : h.offset == off;
            if (!ok) {
                ++bad;
                std::printf("  offset %zu: caret x %.2f hit back %zu\n", off, c.x, h.offset);
            }
        }
        CHECK_EQ(bad, 0);
    }
    // inside the mixed string the Arabic run still reads right to left: its first letter (offset 3) is to the right of
    // its last (offset 9); and offset 3 (the run's right end) and offset 11 (the space after it) are one visual
    // position -- the boundary the round trip above has to allow for
    const float first = e.caretRect(mixed, st(), 0, 3).x, last = e.caretRect(mixed, st(), 0, 9).x;
    CHECK(first > last);
    CHECK(approx(first, e.caretRect(mixed, st(), 0, 11).x, 0.01f));
    // an all-Arabic run reads right to left: its logical start is visually at the right
    const float start = e.caretRect(kArabic, st(), 0, 0).x, end = e.caretRect(kArabic, st(), 0, kArabic.size()).x;
    std::printf("  Arabic caret x: offset 0 at %.2f, end at %.2f\n", start, end);
    CHECK(start > end);
    // Latin reads left to right
    CHECK(e.caretRect("abc", st(), 0, 0).x < e.caretRect("abc", st(), 0, 3).x);
}

TEST(hits_beyond_the_text_land_on_its_ends_and_alignment_moves_the_caret) {
    auto& e = engine();
    const TextHit after = e.hitTest("Hello", st(), 0, {500, 5});
    CHECK(after.offset == 5 && !after.inside);
    const TextHit before = e.hitTest("Hello", st(), 0, {-5, 5});
    CHECK(before.offset == 0 && !before.inside);
    const TextHit over = e.hitTest("Hello", st(), 0, {3, 5});
    CHECK(over.inside);
    TextStyle centre = st();
    centre.halign = HAlign::Center;
    const float w = e.measure("Hello", st()).width;
    CHECK(approx(e.caretRect("Hello", centre, 200, 0).x, (200 - w) / 2, 0.5f));
    TextStyle wrap = st();
    wrap.wrap = true;
    const std::string two_lines = "first second";
    const float first_w = e.measure("first ", st()).width;
    const RectF second = e.caretRect(two_lines, wrap, first_w + 1, 6);  // "second" starts the second line
    CHECK(second.x < 0.5f && second.y > 5);
    CHECK_EQ(e.hitTest(two_lines, wrap, first_w + 1, {1, second.y + second.height / 2}).offset, std::size_t{6});
}

TEST(the_layout_cache_reuses_and_evicts) {
    DWriteTextEngine e(warp()->dwrite());
    TextStyle s = st();
    IDWriteTextLayout* a = e.layout("cached", s, 100, 20);
    CHECK(e.layout("cached", s, 100, 20) == a);  // the same object: measuring, drawing and hit-testing agree
    s.color = Color::rgb(0xFF0000);
    CHECK(e.layout("cached", s, 100, 20) == a);  // colour is not part of a layout
    CHECK(e.stats().hits == 2 && e.stats().misses == 1);
    for (int i = 0; i < 300; ++i) e.measure("text " + std::to_string(i), st());
    CHECK(e.stats().entries == DWriteTextEngine::kCacheCapacity);
    CHECK(e.layout("text 299", st(), DWriteTextEngine::kUnbounded, DWriteTextEngine::kUnbounded) != nullptr);
}

TEST(text_samples_match_the_goldens_at_three_scales) {
    auto& d = warp();
    auto win = hiddenWindow();
    CHECK(d && win);
    if (!d || !win) return;
    const float W = 440, H = 330;
    auto draw = [&](ID2D1DeviceContext2* ctx, double scale) {
        D2DPainter p(ctx, *d, engine(), scale);
        p.fillRect({0, 0, W, H}, token(T::Window));
        TextStyle s = st();
        s.color = token(T::Text);
        s.valign = VAlign::Top;
        const RectF box{12, 10, 200, 62};
        p.strokeRect(p.crisp(box, 1).rect, token(T::Border), p.crisp(box, 1).width);
        TextStyle wrap = s;
        wrap.wrap = true;
        p.drawText({16, 12, 192, 58}, "Wrapped text: the quick brown fox jumps over the lazy dog, twice over.", wrap);
        TextStyle c = s, r = s;
        c.halign = HAlign::Center;
        r.halign = HAlign::Right;
        p.strokeRect(p.crisp({230, 10, 198, 22}, 1).rect, token(T::Border), p.crisp({230, 10, 198, 22}, 1).width);
        p.drawText({230, 13, 198, 18}, "centred", c);
        p.strokeRect(p.crisp({230, 38, 198, 22}, 1).rect, token(T::Border), p.crisp({230, 38, 198, 22}, 1).width);
        p.drawText({230, 41, 198, 18}, "right-aligned", r);
        TextStyle big = s;
        big.size = 16;
        float y = 84;
        for (const std::string& t : {std::string("\xD9\x85\xD8\xB1\xD8\xAD\xD8\xA8\xD8\xA7 \xD8\xA8\xD8\xA7\xD9\x84\xD8\xB9\xD8\xA7\xD9\x84\xD9\x85"),  // مرحبا بالعالم
                                     std::string("Voltage \xD8\xA7\xD9\x84\xD8\xAC\xD9\x87\xD8\xAF = 5 V"),                                  // Voltage الجهد = 5 V
                                     std::string("\xE0\xA4\xA8\xE0\xA4\xAE\xE0\xA4\xB8\xE0\xA5\x8D\xE0\xA4\xA4\xE0\xA5\x87 ") + kConjunct,  // नमस्ते क्षि
                                     std::string("\xE4\xBD\xA0\xE5\xA5\xBD\xEF\xBC\x8C\xE4\xB8\x96\xE7\x95\x8C \xC2\xB7 \xE3\x81\x93\xE3\x82\x93\xE3\x81\xAB\xE3\x81\xA1\xE3\x81\xAF"),  // 你好，世界 · こんにちは
                                     std::string("Emoji ") + kSmiley + " " + kFamily + " \xE2\x9C\x94"}) {                               // ✔
            p.drawText({12, y, 416, 28}, t, big);
            y += 32;
        }
        TextStyle mono = s;
        mono.family = FontFamily::Monospace;
        p.drawText({12, y + 4, 416, 20}, "Console: [  1.23e-05 A ]  iiii|WWWW|", mono);
        TextStyle bold = s;
        bold.bold = true;
        bold.size = 13;
        p.drawText({12, y + 30, 416, 20}, "Semibold label, 13 DIP", bold);
    };
    for (int pct : {100, 150, 200}) {
        const double scale = pct / 100.0;
        auto surf = WindowSurface::create(d, win->hwnd(), px(W, scale), px(H, scale), scale);
        CHECK(surf.has_value());
        if (!surf) return;
        Image img;
        CHECK((*surf)->render([&](ID2D1DeviceContext2* ctx) { draw(ctx, scale); }, {.vsync = false, .capture = &img}) ==
              FrameStatus::Presented);
        const GoldenResult r = checkGolden(*d, "n2d_text@" + std::to_string(pct) + ".png", img);
        CHECK(r != GoldenResult::Mismatch && r != GoldenResult::Missing);
    }
}
