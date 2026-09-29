// Scalar ports of pytcad/materials.py's bulk-parameter models, used by the
// compiled Device1D input evaluation (tcad/device1d/inputs.hpp). The
// Python originals stay: Device2D/Device3D still evaluate their materials
// through materials.py, and tests/test_device1d_native_gates.py holds these
// ports to them (bit-identical where both sides call the same libm
// function, a few ULP where numpy's vectorized exp/log differ).
//
// Every formula keeps the Python operation order, so a difference can
// only come from the transcendental implementations themselves.
#pragma once

#include <algorithm>
#include <cmath>

#include "tcad/physics/fermi.hpp"  // kPi (== np.pi)

namespace tcad::physics {

// pytcad/constants.py (CODATA 2018 exact / recommended values); gated
// equal to the Python constants in the test suite.
inline constexpr double kQ = 1.602176634e-19;      // C
inline constexpr double kKB = 1.380649e-23;        // J/K
inline constexpr double kKB_EV = 8.617333262e-5;   // eV/K
inline constexpr double kEPS0 = 8.8541878128e-14;  // F/cm
inline constexpr double kHBAR = 1.054571817e-34;   // J s
inline constexpr double kM0 = 9.1093837015e-31;    // kg

/// materials.Semiconductor's numeric fields (name/kappa_th excluded).
struct MaterialParams {
    double eps_r = 11.7, chi = 4.05, Eg0 = 1.17, varshni_alpha = 4.73e-4,
           varshni_beta = 636.0, Nc300 = 2.86e19, Nv300 = 3.10e19;
    double mu_n_min = 92.0, mu_n_max = 1360.0, mu_n_Nref = 1.3e17, mu_n_alpha = 0.91,
           mu_n_Texp = -2.33;
    double mu_p_min = 47.7, mu_p_max = 495.0, mu_p_Nref = 6.3e16, mu_p_alpha = 0.76,
           mu_p_Texp = -2.23;
    double vsat_n = 1.07e7, vsat_p = 8.37e6, beta_n = 2.0, beta_p = 1.0;
    double tau_n0 = 1.0e-5, tau_p0 = 3.0e-6, tau_Nref = 5.0e16;
    double Cn_auger = 2.8e-31, Cp_auger = 9.9e-32;
    double bgn_E0 = 6.92e-3, bgn_N0 = 1.3e17;
    double m_n_star = 0.26, m_p_star = 0.386;
};

/// Semiconductor.Eg: Varshni, Eg0 - alpha T^2 / (T + beta) [eV].
inline double band_gap(const MaterialParams& m, double T) {
    return m.Eg0 - m.varshni_alpha * (T * T) / (T + m.varshni_beta);
}
/// Semiconductor.Nc / Nv: N300 (T/300)^1.5 [cm^-3].
inline double dos_c(const MaterialParams& m, double T) { return m.Nc300 * std::pow(T / 300.0, 1.5); }
inline double dos_v(const MaterialParams& m, double T) { return m.Nv300 * std::pow(T / 300.0, 1.5); }
/// Semiconductor.ni: sqrt(Nc Nv) exp(-Eg / 2kT) [cm^-3].
inline double intrinsic_density(const MaterialParams& m, double T) {
    return std::sqrt(dos_c(m, T) * dos_v(m, T)) * std::exp(-band_gap(m, T) / (2.0 * kKB_EV * T));
}

/// materials.mobility_caughey_thomas (N = total ionised impurity).
inline double mobility_caughey_thomas(const MaterialParams& m, double N, double T, bool electron) {
    N = std::max(N, 1.0);
    const double mu_max = (electron ? m.mu_n_max : m.mu_p_max) *
                          std::pow(T / 300.0, electron ? m.mu_n_Texp : m.mu_p_Texp);
    const double mu_min = electron ? m.mu_n_min : m.mu_p_min;
    const double Nref = electron ? m.mu_n_Nref : m.mu_p_Nref;
    const double a = electron ? m.mu_n_alpha : m.mu_p_alpha;
    return mu_min + (mu_max - mu_min) / (1.0 + std::pow(N / Nref, a));
}

/// materials.bandgap_narrowing_slotboom [eV] (0 at and below N0).
inline double bandgap_narrowing_slotboom(const MaterialParams& m, double N) {
    N = std::max(N, 1.0);
    const double x = std::log(N / m.bgn_N0);
    const double dEg = m.bgn_E0 * (x + std::sqrt(x * x + 0.5) - std::sqrt(0.5));
    return N > m.bgn_N0 ? dEg : 0.0;
}

/// materials.nie_effective [cm^-3].
inline double nie_effective(const MaterialParams& m, double N, double T, bool use_bgn) {
    const double ni = intrinsic_density(m, T);
    if (!use_bgn) return ni;
    return ni * std::exp(bandgap_narrowing_slotboom(m, N) / (2.0 * kKB_EV * T));
}

/// materials.lifetime_scharfetter [s].
inline double lifetime_scharfetter(double N, double tau0, double Nref) {
    N = std::max(N, 1.0);
    return tau0 / (1.0 + N / Nref);
}

/// device.emission_velocity: thermionic emission velocity [cm/s] of a band
/// with effective DOS N_dos [cm^-3] (m_dos recovered from N_dos).
inline double emission_velocity(double N_dos_cm3, double T) {
    const double N_si = N_dos_cm3 * 1.0e6;          // m^-3
    const double h = 2.0 * kPi * kHBAR;
    const double kT = kKB_EV * kQ * T;              // J
    const double m_dos = std::pow(N_si / 2.0, 2.0 / 3.0) * h * h / (2.0 * kPi * kT);
    return std::sqrt(kT / (2.0 * kPi * m_dos)) * 100.0;   // cm/s
}

}  // namespace tcad::physics
