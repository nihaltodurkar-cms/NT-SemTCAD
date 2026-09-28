// Native C++ counterpart of pytcad/fermi.py's TABULATED fast path
// (f_half/f_mhalf/f_half_inv) plus device.py's FD-statistics helpers
// built on it (fd_density/fd_ddensity_deta, fd_node_factors,
// ionized_eta_doping/ionized_doping, and the eta-space contact/
// neutrality bisection device.py calls _fd_neutral_eta) -- Phase 2
// slice 1 (FD + incomplete ionization) of
// ~/.claude/plans/eager-purring-fairy.md.
//
// This does NOT reimplement fermi.py's quadrature (_gl_eval/
// f_half_exact/f_mhalf_exact) or the one-time table build (_table()):
// that table (5 arrays: e, g, gp, q, qp, over [_SERIES_ETA,
// FERMI_ETA_MAX] step _TAB_H=0.005) is built ONCE in Python (already
// module-level-cached there) and handed to the native Device1D
// constructor as plain arrays -- the same "materials evaluation stays
// Python, called once before construction" pattern Phase 1 already
// uses for mobility/lifetime/BGN. This header only reproduces the
// HOT-PATH lookup (cubic Hermite on the table, or the exact
// deep-Boltzmann series below _SERIES_ETA) that pytcad.fermi.f_half/
// f_mhalf/f_half_inv actually run per Newton iterate -- a direct,
// line-for-line port of fermi.py's `_fd_series`/`_hermite`/
// `_tabulated`/`f_half_inv`, scalar (not vectorized) to match this
// port's per-node/per-edge loop structure, exactly like
// physics/kernels.hpp's bernoulli/dbernoulli.
#pragma once

#include <algorithm>
#include <cmath>
#include <vector>

#include "tcad/base/errors.hpp"

namespace tcad::physics {

// pytcad.fermi's own module constants.
inline constexpr double kFermiEtaMin = -40.0;
inline constexpr double kFermiEtaMax = 40.0;
inline constexpr double kFermiSeriesEta = -10.0;
inline constexpr double kFermiTabH = 0.005;
inline constexpr double kPi = 3.14159265358979323846;

/// The table pytcad.fermi._table() builds once and caches at module
/// scope: e is the uniform eta grid, g/gp are (log F_1/2, its exact
/// eta-derivative), q/qp are (log F_-1/2, an 8th-order-FD derivative).
struct FermiTable {
    std::vector<double> e, g, gp, q, qp;
};

/// sum_{k=1..} (-1)^(k+1) exp(k*eta)/k^power -- fermi.py's `_fd_series`,
/// exact for eta <= kFermiSeriesEta (deep Boltzmann).
inline double fd_series(double eta, double power) {
    double out = 0.0, sign = 1.0;
    int k = 1;
    for (;;) {
        const double term = sign * std::exp(k * eta) / std::pow(static_cast<double>(k), power);
        out += term;
        if (std::abs(term) <= 1e-18 * (std::abs(out) + 1e-300)) break;
        if (k > 200) break;
        sign = -sign;
        ++k;
    }
    return out;
}

/// Cubic Hermite lookup on the uniform table grid -- fermi.py's
/// `_hermite`. `eta` must be > kFermiSeriesEta (the table does not
/// extend below it; callers branch on that exactly as `_tabulated` does).
inline double fermi_hermite(const std::vector<double>& y, const std::vector<double>& yp,
                            const FermiTable& tab, double eta) {
    const auto& e = tab.e;
    long long i = static_cast<long long>((eta - e[0]) / kFermiTabH);
    const long long imax = static_cast<long long>(e.size()) - 2;
    if (i < 0) i = 0;
    if (i > imax) i = imax;
    const double t = (eta - e[static_cast<std::size_t>(i)]) / kFermiTabH;
    const double t2 = t * t, t3 = t2 * t;
    const auto u = static_cast<std::size_t>(i);
    return (2.0 * t3 - 3.0 * t2 + 1.0) * y[u]
         + (t3 - 2.0 * t2 + t) * kFermiTabH * yp[u]
         + (-2.0 * t3 + 3.0 * t2) * y[u + 1]
         + (t3 - t2) * kFermiTabH * yp[u + 1];
}

/// F_{1/2}(eta), tabulated fast path -- fermi.py's `f_half`.
inline double f_half(const FermiTable& tab, double eta) {
    if (eta <= kFermiSeriesEta) return fd_series(eta, 1.5);
    return std::exp(fermi_hermite(tab.g, tab.gp, tab, eta));
}

/// F_{-1/2}(eta) = dF_{1/2}/d eta, tabulated fast path -- fermi.py's
/// `f_mhalf` (== `df_half`).
inline double f_mhalf(const FermiTable& tab, double eta) {
    if (eta <= kFermiSeriesEta) return fd_series(eta, 0.5);
    return std::exp(fermi_hermite(tab.q, tab.qp, tab, eta));
}

/// Inverse of F_{1/2}: given nu = F_{1/2}(eta), return eta -- fermi.py's
/// `f_half_inv` (bracketed-Newton, then a 3-step polish). `nu` must be
/// > 0 and <= F_{1/2}(kFermiEtaMax) -- callers here always pass a
/// density ratio already known positive and in range, matching every
/// Python call site (device.py never calls f_half_inv on an unchecked
/// value), so the two ValueError guards fermi.py's own f_half_inv has
/// are not reproduced -- an out-of-range nu is a caller bug, not a
/// runtime condition to report to a user.
inline double f_half_inv(const FermiTable& tab, double nu) {
    if (nu < 1e-12) return std::log(nu);
    double lo = std::max(std::log(nu) - 1.0, kFermiEtaMin);
    double hi = std::min(std::pow(0.75 * std::sqrt(kPi) * nu, 2.0 / 3.0) + 1.0, kFermiEtaMax);
    double r = (nu < 0.5) ? std::log(std::max(nu, 1e-300)) : 0.5 * (lo + hi);
    r = std::clamp(r, lo, hi);
    for (int it = 0; it < 50; ++it) {
        const double f = f_half(tab, r);
        const double d = std::max(f_mhalf(tab, r), 1e-300);
        const bool left = f > nu;
        if (left) hi = std::min(r, hi); else lo = std::max(r, lo);
        const double newr = std::clamp(r - (f - nu) / d, lo, hi);
        const bool done = (hi - lo < 1e-14 * (1.0 + std::abs(lo)))
                        || (std::abs(newr - r) <= 1e-14 * (1.0 + std::abs(r)));
        r = newr;
        if (done) break;
    }
    for (int it = 0; it < 3; ++it) {
        const double f = f_half(tab, r);
        const double d = f_mhalf(tab, r);
        double step = (f - nu) / std::max(d, 1e-300);
        step = std::clamp(step, -0.5, 0.5);
        r = std::clamp(r - step, kFermiEtaMin, kFermiEtaMax);
    }
    return r;
}

/// n = nc * F_{1/2}(eta), the M13 asymmetric eta policy -- kernels.py's
/// `fd_density`. `eta` must already be <= kFermiEtaMax (every Python
/// call site clamps first); the internal >35-exact-tail / clip-at-MIN
/// behavior is reproduced exactly.
inline double fd_density(const FermiTable& tab, double nc, double eta) {
    if (eta < -35.0) return nc * std::exp(std::min(eta, 700.0));
    const double e = std::clamp(eta, kFermiEtaMin, kFermiEtaMax);
    return nc * f_half(tab, e);
}

/// d(nc F(eta))/d(eta) -- kernels.py's `fd_ddensity_deta`.
inline double fd_ddensity_deta(const FermiTable& tab, double nc, double eta) {
    if (eta < -35.0) return nc * std::exp(std::min(eta, 700.0));
    const double e = std::clamp(eta, kFermiEtaMin, kFermiEtaMax);
    return nc * f_mhalf(tab, e);
}

struct FdNodeFactors { double Ln = 0.0, Lp = 0.0, wn = 0.0, wp = 0.0; };

/// Per-node nu-factor SG quantities -- device.py's module-level
/// `fd_node_factors`, one node at a time. `thr` is
/// `f_half(FermiTable, -30.0)`, computed ONCE per residual/Jacobian
/// call by the caller (Python recomputes it once per vectorized call
/// too; recomputing it per-node here would be correct but wasteful).
inline FdNodeFactors fd_node_factors_node(const FermiTable& tab, double thr,
                                          double nc_s, double nv_s, double n, double p) {
    FdNodeFactors out;
    if (n / nc_s > thr) {
        const double en = f_half_inv(tab, std::max(n, 1e-300) / nc_s);
        const double Fn = f_half(tab, en);
        const double dFn = f_mhalf(tab, en);
        out.Ln = std::log(Fn) - en;
        out.wn = (dFn / Fn - 1.0) / (nc_s * dFn);
    }
    if (p / nv_s > thr) {
        const double ep = f_half_inv(tab, std::max(p, 1e-300) / nv_s);
        const double Fp = f_half(tab, ep);
        const double dFp = f_mhalf(tab, ep);
        out.Lp = std::log(Fp) - ep;
        out.wp = (dFp / Fp - 1.0) / (nv_s * dFp);
    }
    return out;
}

struct IonizedEtaResult { double cion = 0.0, dcion_deta_n = 0.0, dcion_deta_p = 0.0; };

/// Net IONIZED doping from the reduced Fermi energies -- device.py's
/// module-level `ionized_eta_doping`.
inline IonizedEtaResult ionized_eta_doping(double nd, double na, double eta_n, double eta_p,
                                           double ded_kt) {
    const double ed_n = std::exp(std::min(eta_n + ded_kt, 700.0));
    const double ea_p = std::exp(std::min(eta_p + ded_kt, 700.0));
    const double ndp = nd / (1.0 + 2.0 * ed_n);
    const double nam = na / (1.0 + 4.0 * ea_p);
    const double one2 = 1.0 + 2.0 * ed_n;
    const double one4 = 1.0 + 4.0 * ea_p;
    const double dndp_deta = -2.0 * ed_n / (one2 * one2) * nd;
    const double dnam_deta = -4.0 * ea_p / (one4 * one4) * na;
    return {ndp - nam, dndp_deta, -dnam_deta};
}

struct IonizedDopingResult { double cion = 0.0, dcion_dn = 0.0, dcion_dp = 0.0; };

/// Net ionized doping and its derivatives wrt the SLOT DENSITIES --
/// device.py's module-level `ionized_doping` (Device1D's `_ionized_C`
/// binding), one node at a time.
inline IonizedDopingResult ionized_doping(const FermiTable& tab, double nd, double na,
                                          double n, double p, double nc_s, double nv_s,
                                          double ded_kt) {
    const double en = f_half_inv(tab, std::max(n, 1e-300) / nc_s);
    const double ep = f_half_inv(tab, std::max(p, 1e-300) / nv_s);
    const IonizedEtaResult ie = ionized_eta_doping(nd, na, en, ep, ded_kt);
    const double tail_n = std::exp(std::min(en, 700.0));
    const double tail_p = std::exp(std::min(ep, 700.0));
    const double den_n = (en >= kFermiEtaMin)
        ? f_mhalf(tab, std::clamp(en, kFermiEtaMin, kFermiEtaMax)) : tail_n;
    const double den_p = (ep >= kFermiEtaMin)
        ? f_mhalf(tab, std::clamp(ep, kFermiEtaMin, kFermiEtaMax)) : tail_p;
    const double detn = 1.0 / std::max(nc_s * den_n, 1e-300);
    const double detp = 1.0 / std::max(nv_s * den_p, 1e-300);
    return {ie.cion, ie.dcion_deta_n * detn, ie.dcion_deta_p * detp};
}

/// Vectorized bracketed-bisection eta-space neutrality root, ONE node
/// -- device.py's `Device1D._fd_neutral_eta`, per-node body (each
/// node's root is independent; Python vectorizes the same scalar
/// algorithm over all N at once). Used both for the equilibrium
/// initial guess (every node) and ohmic-contact values (nodes 0/N-1).
/// Throws tcad::InvalidArgument exactly where the Python raises
/// ValueError (not bracketed / root beyond the validated range) --
/// translated to Python's ValueError the same way every other native
/// InvalidArgument is (see module.cpp's exception translator).
inline double fd_neutral_eta_node(const FermiTable& tab, double nc_s, double nv_s,
                                  double eg_kt, double C, double ded_kt, double nd,
                                  double na, bool incomplete_ion) {
    const double lo0 = -eg_kt - (kFermiEtaMax - kFermiEtaMin) - 1.0;
    const double hi0 = kFermiEtaMax;
    auto g = [&](double e) {
        const double n_ = fd_density(tab, nc_s, std::min(e, kFermiEtaMax));
        const double p_ = fd_density(tab, nv_s, std::min(-e - eg_kt, kFermiEtaMax));
        double cion;
        if (incomplete_ion) {
            cion = ionized_eta_doping(nd, na, std::min(e, kFermiEtaMax),
                                      std::min(-e - eg_kt, kFermiEtaMax), ded_kt).cion;
        } else {
            cion = C;
        }
        return n_ - p_ - cion;
    };
    double lo = lo0, hi = hi0;
    const double flo = g(lo), fhi = g(hi);
    if (flo > 0.0 || fhi < 0.0) {
        throw tcad::InvalidArgument(
            "FD contact neutrality root not bracketed at a contact "
            "(doping outside the model's validated regime?)");
    }
    for (int it = 0; it < 300; ++it) {
        const double mid = 0.5 * (lo + hi);
        if (g(mid) < 0.0) lo = mid; else hi = mid;
        if (hi - lo < 3e-15 * (1.0 + std::abs(lo))) break;
    }
    const double res = 0.5 * (lo + hi);
    if (res > kFermiEtaMax - 2.0) {
        throw tcad::InvalidArgument(
            "FD neutrality eta beyond the validated range; refusing "
            "(M13 G7 applicability limit).");
    }
    return res;
}

}  // namespace tcad::physics
