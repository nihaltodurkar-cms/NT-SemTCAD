// Shared elementwise physics primitives, native C++ counterparts of
// pytcad/kernels.py's bernoulli/dbernoulli and materials.py's
// recombination() (SRH + Auger) -- the two per-Newton-iterate,
// per-node/per-edge functions the baseline (Boltzmann-only, no FD
// composition) Device1D residual/Jacobian needs every call.
//
// Scalar, not vectorized: called from inside the assembly loop, once
// per edge/node, mirroring the loop structure the Device1D C++ port
// itself uses. Bit-identical to the numpy originals under
// -ffp-contract=off (see core/CMakeLists.txt) -- verified by a
// bit-identical parity test against the pure-Python function it
// replaces, not assumed from the formula matching alone.
#pragma once

#include <cmath>
#include <span>

namespace tcad::physics {

inline double clip_700(double x) {
    if (x < -700.0) return -700.0;
    if (x > 700.0) return 700.0;
    return x;
}

/// B(x) = x / (exp(x) - 1), with B(0) = 1. Matches pytcad.kernels.bernoulli.
inline double bernoulli(double x_in) {
    const double x = clip_700(x_in);
    if (std::abs(x) < 1e-4) {
        return 1.0 - x / 2.0 + x * x / 12.0;
    }
    return x / std::expm1(x);
}

/// dB/dx, computed as B(x) [1/x - 1/(1 - e^-x)] for stability, with a
/// small-x series -- matches pytcad.kernels.dbernoulli exactly.
inline double dbernoulli(double x_in) {
    const double x = clip_700(x_in);
    if (std::abs(x) < 1e-4) {
        return -0.5 + x / 6.0 - x * x * x / 180.0;
    }
    const double B = x / std::expm1(x);
    double em = -std::expm1(-x);            // 1 - exp(-x)
    if (std::abs(em) < 1e-300) em = 1e-300;
    return B * (1.0 / x - 1.0 / em);
}

/// Net SRH + Auger recombination and its exact derivatives, the
/// Boltzmann-equilibrium form (materials.recombination()'s np_eq=None
/// branch -- the FD-composition branch is Phase 2, gated separately
/// when Models.fd is ported).
struct RecombinationResult {
    double R;
    double dRdn;
    double dRdp;
};

inline RecombinationResult recombination_boltzmann(
    double n, double p, double nie, double tau_n, double tau_p,
    double Cn_auger, double Cp_auger, bool auger) {
    const double ni2 = nie * nie;
    const double excess = n * p - ni2;
    const double den = tau_p * (n + nie) + tau_n * (p + nie);

    double R = excess / den;
    double dRdn = (p * den - excess * tau_p) / (den * den);
    double dRdp = (n * den - excess * tau_n) / (den * den);

    if (auger) {
        const double C = Cn_auger * n + Cp_auger * p;
        R = R + C * excess;
        dRdn = dRdn + Cn_auger * excess + C * p;
        dRdp = dRdp + Cp_auger * excess + C * n;
    }
    return {R, dRdn, dRdp};
}

/// Net SRH + Auger recombination with the M13 Fermi-Dirac np_eq
/// composition -- materials.recombination()'s np_eq-given branch
/// (Phase 2 slice 1: FD). `nie` is the ordinary (Boltzmann) effective
/// intrinsic concentration feeding the SRH denominator, exactly as in
/// the Boltzmann form; `np_eq`/`dnpq_dn`/`dnpq_dp` are the FD
/// equilibrium product nie^2*nu_n*nu_p and its derivatives wrt the
/// PHYSICAL densities (already converted from the scaled-nu chain by
/// the caller, exactly as device.py's own `dnpq_dns / self.Ns`
/// conversion does) -- ONLY the equilibrium-product numerator changes
/// under FD, not the denominator's nie, matching materials.
/// recombination() exactly.
inline RecombinationResult recombination_fd(
    double n, double p, double nie, double np_eq, double dnpq_dn, double dnpq_dp,
    double tau_n, double tau_p, double Cn_auger, double Cp_auger, bool auger) {
    const double excess = n * p - np_eq;
    const double den = tau_p * (n + nie) + tau_n * (p + nie);

    double R = excess / den;
    double dRdn = ((p - dnpq_dn) * den - excess * tau_p) / (den * den);
    double dRdp = ((n - dnpq_dp) * den - excess * tau_n) / (den * den);

    if (auger) {
        const double C = Cn_auger * n + Cp_auger * p;
        R = R + C * excess;
        dRdn = dRdn + Cn_auger * excess + C * (p - dnpq_dn);
        dRdp = dRdp + Cp_auger * excess + C * (n - dnpq_dp);
    }
    return {R, dRdn, dRdp};
}

/// Elementwise `bernoulli`/`dbernoulli` over an array -- the vectorized
/// entry point bound to Python, since the SG assembly calls these on
/// a whole edge array per Jacobian build, not one edge at a time.
/// Implemented in kernels.cpp (compiled with -ffp-contract=off, see
/// core/CMakeLists.txt), not inline here, so that flag actually applies.
void bernoulli_array(std::span<const double> x, std::span<double> out);
void dbernoulli_array(std::span<const double> x, std::span<double> out);

/// Elementwise recombination_boltzmann over a node array. `Cn_auger`/
/// `Cp_auger` are scalar (one material, matching the baseline-model
/// homojunction case Phase 1 covers -- a per-node material array is
/// Phase 2's heterojunction port).
void recombination_boltzmann_array(
    std::span<const double> n, std::span<const double> p,
    std::span<const double> nie, std::span<const double> tau_n,
    std::span<const double> tau_p, double Cn_auger, double Cp_auger,
    bool auger, std::span<double> R, std::span<double> dRdn,
    std::span<double> dRdp);

}  // namespace tcad::physics
