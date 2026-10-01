#include "ui/widgets/numeric.hpp"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>

namespace tcad::ui {

namespace {

bool isDigit(char c) { return c >= '0' && c <= '9'; }

std::string_view trim(std::string_view s) {
    while (!s.empty() && (s.front() == ' ' || s.front() == '\t')) s.remove_prefix(1);
    while (!s.empty() && (s.back() == ' ' || s.back() == '\t')) s.remove_suffix(1);
    return s;
}

// The SI prefix at the start of `s`: its exponent and its byte length; 0 length when there is none.
struct Prefix {
    int exponent = 0;
    std::size_t length = 0;
};
Prefix prefixAt(std::string_view s) {
    if (s.empty()) return {};
    switch (s[0]) {
        case 'f': return {-15, 1};
        case 'p': return {-12, 1};
        case 'n': return {-9, 1};
        case 'u': return {-6, 1};
        case 'm': return {-3, 1};
        case 'k': return {3, 1};
        case 'M': return {6, 1};
        case 'G': return {9, 1};
        case 'T': return {12, 1};
        default: break;
    }
    const auto b0 = static_cast<unsigned char>(s[0]);
    const auto b1 = s.size() >= 2 ? static_cast<unsigned char>(s[1]) : 0;
    if (b0 == 0xC2 && b1 == 0xB5) return {-6, 2};  // U+00B5 micro sign
    if (b0 == 0xCE && b1 == 0xBC) return {-6, 2};  // U+03BC Greek small mu
    return {};
}

}  // namespace

Parsed parseQuantity(std::string_view text, std::string_view unit) {
    const std::string_view u = trim(unit);
    const std::string_view s = trim(text);
    if (s.empty() || s == u) return {ParseStatus::Empty, 0};

    // The number: [sign] digits [. digits] [e [sign] digits]
    std::size_t i = 0;
    const std::size_t n = s.size();
    if (i < n && (s[i] == '+' || s[i] == '-')) ++i;
    std::size_t digits = 0;
    while (i < n && isDigit(s[i])) ++i, ++digits;
    if (i < n && s[i] == '.') {
        ++i;
        while (i < n && isDigit(s[i])) ++i, ++digits;
    }
    if (digits == 0) return {i == n ? ParseStatus::Incomplete : ParseStatus::Invalid, 0};  // "-", "+", "." start a number
    bool has_exp = false;
    if (i < n && (s[i] == 'e' || s[i] == 'E')) {
        std::size_t j = i + 1;
        if (j < n && (s[j] == '+' || s[j] == '-')) ++j;
        std::size_t exp_digits = 0;
        while (j < n && isDigit(s[j])) ++j, ++exp_digits;
        if (exp_digits == 0) return {j == n ? ParseStatus::Incomplete : ParseStatus::Invalid, 0};  // "1e", "1e-"
        has_exp = true;
        i = j;
    }
    double value = 0;
    {
        std::string_view num = s.substr(0, i);
        if (!num.empty() && num.front() == '+') num.remove_prefix(1);  // from_chars takes no '+'
        const auto r = std::from_chars(num.data(), num.data() + num.size(), value);
        if (r.ec != std::errc() || r.ptr != num.data() + num.size() || !std::isfinite(value)) return {ParseStatus::Invalid, 0};
    }

    // After the number: a space, a prefix, the unit.
    const std::string_view rest = trim(s.substr(i));
    if (rest.empty() || rest == u) return {ParseStatus::Ok, value};
    const Prefix p = prefixAt(rest);
    if (p.length == 0 || has_exp) return {ParseStatus::Invalid, 0};
    const std::string_view after = trim(rest.substr(p.length));
    if (!after.empty() && after != u) return {ParseStatus::Invalid, 0};
    const double scaled = tidy(value * std::pow(10.0, p.exponent));
    if (!std::isfinite(scaled)) return {ParseStatus::Invalid, 0};
    return {ParseStatus::Ok, scaled};
}

double tidy(double v) {
    if (v == 0 || !std::isfinite(v)) return v;
    char buf[40];
    std::snprintf(buf, sizeof buf, "%.15g", v);
    return std::strtod(buf, nullptr);
}

namespace {

bool sameTo9(double a, double b) {
    if (a == b) return true;
    return std::fabs(a - b) <= 1e-9 * std::max(std::fabs(a), std::fabs(b));
}

// "1.5e-07" -> "1.5e-7": no plus sign, no leading zeros in the exponent.
std::string compactExponent(const std::string& s) {
    const auto e = s.find('e');
    if (e == std::string::npos) return s;
    std::string exp = s.substr(e + 1);
    bool neg = false;
    if (!exp.empty() && (exp[0] == '+' || exp[0] == '-')) {
        neg = exp[0] == '-';
        exp.erase(0, 1);
    }
    while (exp.size() > 1 && exp[0] == '0') exp.erase(0, 1);
    return s.substr(0, e) + "e" + (neg ? "-" : "") + exp;
}

std::string fixedText(double v, int decimals) {
    char buf[400];
    std::snprintf(buf, sizeof buf, "%.*f", std::clamp(decimals, 0, 30), v);
    std::string s = buf;
    // Rounding a tiny negative to zero must not print "-0.00".
    if (!s.empty() && s[0] == '-' && s.find_first_not_of("-0.") == std::string::npos) s.erase(0, 1);
    return s;
}

}  // namespace

std::string formatQuantity(double value, int decimals, std::string_view unit, Notation notation) {
    std::string out;
    if (notation == Notation::Fixed) {
        out = fixedText(value, decimals);
    } else {
        bool fixed = std::fabs(value) < 1e9;
        if (fixed) {
            out = fixedText(value, decimals);
            fixed = sameTo9(std::strtod(out.c_str(), nullptr), value);
        }
        if (!fixed && std::fabs(value) >= 1e-3 && std::fabs(value) < 1e9) {  // more places, not a different notation
            for (int d = decimals + 1; d <= 15; ++d) {
                const std::string more = fixedText(value, d);
                if (sameTo9(std::strtod(more.c_str(), nullptr), value)) {
                    out = more;
                    fixed = true;
                    break;
                }
            }
        }
        if (!fixed) {
            for (int sig = 1; sig <= 9; ++sig) {  // the shortest mantissa that reads back the same to 9 digits
                char buf[64];
                std::snprintf(buf, sizeof buf, "%.*e", sig - 1, value);
                if (sig == 9 || sameTo9(std::strtod(buf, nullptr), value)) {
                    out = compactExponent(buf);
                    break;
                }
            }
        }
    }
    out.append(unit);
    return out;
}

double stepLinear(double value, int n, double step) { return tidy(value + n * step); }

double stepDecade(double value, int n, double seed) {
    seed = std::fabs(seed);
    for (int k = 0; k < std::abs(n); ++k) {
        const bool up = n > 0;
        if (value == 0) {
            value = up ? seed : -seed;
        } else if (value > 0) {
            if (up) value *= 10;
            else value = value / 10 < seed ? 0 : value / 10;
        } else {
            if (up) value = -value / 10 < seed ? 0 : value / 10;
            else value *= 10;
        }
        value = tidy(value);
    }
    return value;
}

}  // namespace tcad::ui
