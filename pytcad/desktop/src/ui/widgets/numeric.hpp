// Quantities for the number fields (N3b, NATIVE-DESKTOP-PLAN.md 27.8.3): how text becomes a number, a number becomes
// text, and a step moves a value. Portable: no Win32, no text engine. Values are SI underneath; a unit and a prefix
// are only ever in the text.
//
// parseQuantity accepts (decision 1 of 27.8: scientific/engineering entry with units):
//   [sign] digits [. digits] [e|E [sign] digits]   "1e17", "-2.5E-3", ".5", "3."
//   then optionally a space and an SI prefix       "5k" = 5000, "2.5 u" = 2.5e-6    (f p n u/micro m k M G T)
//   then optionally the field's unit               "5 kV" for a "V" field; the unit alone is also fine "5 V"
// An exponent and a prefix together ("1e3k") are refused. When the text after the number is EXACTLY the field's unit
// it is the unit, never a prefix: "5 m" in a "m" field is five metres, "5 mm" is five millimetres.
//
// formatQuantity (Auto) is what Qt's QDoubleSpinBox would print -- `decimals` places -- whenever that shows the value
// exactly (to 9 significant digits) and is under 1e9; else, between 1e-3 and 1e9, with as many more places as it
// needs (1.5 in a 0-decimals field is "1.5"); else the shortest scientific text that reads back the same to 9 digits
// ("1e17", "2.5e-10"). A value is never shown as something it is not, and never rounded to `decimals`.
#pragma once

#include <string>
#include <string_view>

namespace tcad::ui {

enum class ParseStatus {
    Ok,
    Empty,       // nothing (or only a unit): a field being cleared
    Incomplete,  // a start of a valid quantity that cannot be one yet: "-", "1e", "1e-"
    Invalid,
};

struct Parsed {
    ParseStatus status = ParseStatus::Invalid;
    double value = 0;
};

Parsed parseQuantity(std::string_view text, std::string_view unit = {});

enum class Notation { Auto, Fixed };

// The text of `value`; `unit` (as given, a leading space is kept) is appended.
std::string formatQuantity(double value, int decimals, std::string_view unit = {}, Notation n = Notation::Auto);

// `value` rounded to 15 significant digits: 0.1 + 0.2 is 0.3, not 0.30000000000000004 (steps stay readable).
double tidy(double value);

// One step of a linear field: value + n * step, tidied.
double stepLinear(double value, int n, double step);

// One step of a DECADE field (a log-scaled one: doping, time): Up (n > 0) multiplies by 10 and Down divides, n times;
// a negative value moves toward zero going up. Zero steps to +/- `seed` (the field's single step); a value
// smaller than `seed` in magnitude steps through zero.
double stepDecade(double value, int n, double seed);

}  // namespace tcad::ui
