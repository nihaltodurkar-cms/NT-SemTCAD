#include "ui/widgets/validator.hpp"

#include <charconv>
#include <cmath>
#include <string>

namespace tcad::ui {

Validator::State DoubleValidator::validate(std::string_view s) const {
    using S = State;
    if (s.empty()) return S::Intermediate;
    std::size_t i = 0;
    if (s[i] == '+' || s[i] == '-') ++i;
    int mantissa_digits = 0, decimals = 0;
    bool point = false;
    for (; i < s.size(); ++i) {
        const char c = s[i];
        if (c >= '0' && c <= '9') {
            ++mantissa_digits;
            if (point && ++decimals > decimals_) return S::Invalid;
        } else if (c == '.' && !point) {
            point = true;
        } else {
            break;
        }
    }
    bool complete = mantissa_digits > 0;  // "-", "." and "-." are unfinished mantissas
    if (i < s.size()) {
        if ((s[i] != 'e' && s[i] != 'E') || mantissa_digits == 0) return S::Invalid;
        ++i;
        if (i < s.size() && (s[i] == '+' || s[i] == '-')) ++i;
        int exponent_digits = 0;
        for (; i < s.size() && s[i] >= '0' && s[i] <= '9'; ++i) ++exponent_digits;
        if (i != s.size()) return S::Invalid;  // a second 'e', a second sign, a letter
        complete = exponent_digits > 0;
    }
    if (!complete) return S::Intermediate;
    const char* b = s.data();
    if (*b == '+') ++b;  // from_chars refuses a leading '+'
    double v = 0;
    const auto r = std::from_chars(b, s.data() + s.size(), v);
    if (r.ec == std::errc::result_out_of_range) return S::Intermediate;  // 1e999: more typing cannot help, but it is not Invalid in Qt either
    if (r.ec != std::errc() || r.ptr != s.data() + s.size()) return S::Invalid;
    return v >= bottom_ && v <= top_ ? S::Acceptable : S::Intermediate;
}

}  // namespace tcad::ui
