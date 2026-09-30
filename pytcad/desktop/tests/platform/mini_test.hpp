// A dependency-free test harness for the Qt-free platform layer (N1): no QtTest, no gtest.
#pragma once

#include <cstdio>
#include <functional>
#include <string>
#include <vector>

namespace minitest {

struct Case {
    const char* name;
    std::function<void()> fn;
};
inline std::vector<Case>& cases() {
    static std::vector<Case> c;
    return c;
}
inline int& failures() {
    static int f = 0;
    return f;
}
struct Register {
    Register(const char* n, std::function<void()> f) { cases().push_back({n, std::move(f)}); }
};

inline int runAll(int argc, char** argv) {
    const std::string only = argc > 1 ? argv[1] : "";
    int ran = 0, bad = 0;
    for (auto& c : cases()) {
        if (!only.empty() && only != c.name) continue;
        const int before = failures();
        std::printf("RUN %s\n", c.name);
        std::fflush(stdout);   // an abort inside a test still names it
        c.fn();
        ++ran;
        const bool ok = failures() == before;
        if (!ok) ++bad;
        std::printf("%s %s\n", ok ? "PASS" : "FAIL", c.name);
    }
    std::printf("%d test(s), %d failed\n", ran, bad);
    return bad ? 1 : 0;
}

}  // namespace minitest

#define TEST(name)                                                   \
    static void test_##name();                                       \
    static minitest::Register reg_##name(#name, test_##name);        \
    static void test_##name()
#define CHECK(cond)                                                                                  \
    do {                                                                                             \
        if (!(cond)) {                                                                               \
            ++minitest::failures();                                                                  \
            std::printf("  CHECK failed at %s:%d: %s\n", __FILE__, __LINE__, #cond);                 \
        }                                                                                            \
    } while (0)
#define CHECK_EQ(a, b)                                                                               \
    do {                                                                                             \
        const auto va_ = (a);                                                                       \
        const auto vb_ = (b);                                                                       \
        if (!(va_ == vb_)) {                                                                         \
            ++minitest::failures();                                                                  \
            std::printf("  CHECK_EQ failed at %s:%d: %s == %s\n", __FILE__, __LINE__, #a, #b);       \
        }                                                                                            \
    } while (0)
