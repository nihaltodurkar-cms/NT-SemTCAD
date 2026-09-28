// Field-driven generation laws shared by the native Device1D (Phase 2
// slice 3 of the device port): scalar transcriptions of
//   pytcad/ionization.py  -- van Overstraeten-de Man alpha(E), dalpha_dE
//   pytcad/btbt.py        -- local Kane BTBT btbt_generation, dbtbt_dF
// Constants are the published tables those modules carry (vOdM 1970;
// Hurkx, Klaassen & Knuvers 1992, Table I). Same operation order as the
// numpy originals; exp() is libm's rather than numpy's, so results agree
// to round-off, not bit-for-bit (see CLAUDE.md on transcendentals).
#pragma once

#include <algorithm>
#include <cmath>

namespace tcad::physics {

// ionization.py
inline constexpr double kIiSwitchN = 5e5;    // E_SWITCH_N [V/cm]
inline constexpr double kIiSwitchP = 4.0e5;  // E_SWITCH_P [V/cm]
inline constexpr double kIiAnLow = 7.03e5, kIiBnLow = 1.231e6;
inline constexpr double kIiAnHigh = 7.03e5, kIiBnHigh = 1.231e6;
inline constexpr double kIiApLow = 1.582e6, kIiBpLow = 2.036e6;
inline constexpr double kIiApHigh = 6.71e5, kIiBpHigh = 1.693e6;
inline constexpr double kIiQ = 1.602176634e-19;  // ionization.Q_E

struct AlphaTable {
    double A_low, B_low, A_high, B_high, sw;
};
inline constexpr AlphaTable kAlphaN{kIiAnLow, kIiBnLow, kIiAnHigh, kIiBnHigh, kIiSwitchN};
inline constexpr AlphaTable kAlphaP{kIiApLow, kIiBpLow, kIiApHigh, kIiBpHigh, kIiSwitchP};

/// ionization._alpha: A exp(-B / max(E, 1e-9)) on the branch E selects.
inline double ii_alpha(const AlphaTable& t, double E) {
    const double Es = std::max(E, 1e-9);
    return E < t.sw ? t.A_low * std::exp(-t.B_low / Es)
                    : t.A_high * std::exp(-t.B_high / Es);
}

/// ionization.dalpha_dE: alpha * B / max(E, 1e-9)^2 on the active branch.
inline double ii_dalpha_dE(const AlphaTable& t, double E) {
    const double a = ii_alpha(t, E);
    const double B = E < t.sw ? t.B_low : t.B_high;
    const double Es = std::max(E, 1e-9);
    return a * B / (Es * Es);
}

// btbt.py
inline constexpr double kKaneA = 3.5e21;  // KANE_A_SI [cm^-3 s^-1]
inline constexpr double kKaneB = 1.03e8;  // KANE_B_SI [V/cm]

/// btbt.btbt_generation: A F^2 exp(-B/F) for F > 0, else exactly 0.
inline double btbt_generation(double F) {
    if (!(F > 0.0)) return 0.0;
    const double Fs = std::max(F, 1e-30);
    return kKaneA * Fs * Fs * std::exp(-kKaneB / Fs);
}

/// btbt.dbtbt_dF: G (2/F + B/F^2) for F > 0, else exactly 0.
inline double dbtbt_dF(double F) {
    if (!(F > 0.0)) return 0.0;
    const double Fs = std::max(F, 1e-30);
    const double G = kKaneA * Fs * Fs * std::exp(-kKaneB / Fs);
    return G * (2.0 / Fs + kKaneB / (Fs * Fs));
}

}  // namespace tcad::physics
