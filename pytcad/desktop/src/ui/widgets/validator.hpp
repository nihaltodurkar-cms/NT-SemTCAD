// Edit validators (N3d, NATIVE-DESKTOP-PLAN.md 27.8.5). Portable. The panels use ONE (measured 2026-10-01): a
// QDoubleValidator in scientific notation on the display range's min and max fields; so one is built, behind the
// interface a QValidator has, and no integer or regular-expression one.
//
// Qt's three answers, kept:
//   Invalid       the text can never become acceptable: an edit that would make it is refused (the key does nothing);
//   Intermediate  not acceptable yet but could become so ("", "-", "1e", "1e-"); typing it is allowed, but Enter and
//                 focus loss do not report editing finished;
//   Acceptable    a complete value.
// DoubleValidator, scientific notation, the C locale ('.' is the decimal point; no grouping): an optional sign, digits
// with at most one '.', and an optional exponent e/E with an optional sign and digits. A value outside [bottom, top]
// is Intermediate (Qt's rule for scientific notation: more typing could still bring it in range), never Invalid.
// `decimals` limits the digits after the point of the mantissa (Qt's default is 1000).
#pragma once

#include <string_view>

namespace tcad::ui {

class Validator {
public:
    enum class State { Invalid, Intermediate, Acceptable };
    virtual ~Validator() = default;
    virtual State validate(std::string_view utf8) const = 0;
};

class DoubleValidator final : public Validator {
public:
    DoubleValidator() = default;
    DoubleValidator(double bottom, double top, int decimals = 1000) : bottom_(bottom), top_(top), decimals_(decimals) {}
    void setRange(double bottom, double top) { bottom_ = bottom, top_ = top; }
    void setDecimals(int d) { decimals_ = d; }
    double bottom() const { return bottom_; }
    double top() const { return top_; }
    State validate(std::string_view utf8) const override;

private:
    double bottom_ = -1.7976931348623157e308, top_ = 1.7976931348623157e308;
    int decimals_ = 1000;
};

}  // namespace tcad::ui
