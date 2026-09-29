// See tcad/device1d/inputs.hpp. Each block cites the device.py statement
// it replaced; the operation order is Python's so the only possible
// difference is a transcendental's last bit (gated in the test suite).
#include "tcad/device1d/inputs.hpp"

#include <algorithm>
#include <cmath>
#include <string>

#include "tcad/base/errors.hpp"

namespace tcad::device1d {

namespace ph = tcad::physics;

namespace {
std::vector<double> hmean(const std::vector<double>& a) {
    std::vector<double> out(a.size() - 1);
    for (std::size_t k = 0; k + 1 < a.size(); ++k)
        out[k] = 2.0 * a[k] * a[k + 1] / (a[k] + a[k + 1]);
    return out;
}
}  // namespace

Inputs evaluate_inputs(std::span<const double> x, std::span<const double> doping,
                       std::span<const double> Ntot, double T,
                       std::span<const ph::MaterialParams> mats,
                       std::span<const std::int64_t> node_mat, const InputFlags& f) {
    const std::size_t N = x.size();
    if (N < 3 || doping.size() != N || Ntot.size() != N || node_mat.size() != N)
        throw tcad::InvalidArgument(
            "device1d inputs: x/doping/Ntot/node_mat must share a length >= 3");
    for (auto k : node_mat)
        if (k < 0 || static_cast<std::size_t>(k) >= mats.size())
            throw tcad::InvalidArgument("device1d inputs: node_mat index out of range");
    const auto mat = [&](std::size_t i) -> const ph::MaterialParams& {
        return mats[static_cast<std::size_t>(node_mat[i])];
    };

    Inputs r;
    r.VT = ph::kKB * T / ph::kQ;                                 // thermal_voltage
    r.eps_arr.resize(N); r.chi_arr.resize(N); r.Eg0_arr.resize(N);
    for (std::size_t i = 0; i < N; ++i) {
        r.eps_arr[i] = mat(i).eps_r * ph::kEPS0;
        r.chi_arr[i] = mat(i).chi;
        r.Eg0_arr[i] = mat(i).Eg0;
    }
    r.eps = r.eps_arr[0];
    r.ni = ph::intrinsic_density(mat(0), T);

    // Concentration scale = peak doping (or the caller's, on a rebuild).
    double dmax = 0.0;
    for (double d : doping) dmax = std::max(dmax, std::abs(d));
    r.Ns = f.Ns > 0.0 ? f.Ns : std::max(dmax, r.ni);
    r.LD = std::sqrt(r.eps * r.VT / (ph::kQ * r.Ns));
    r.J0 = ph::kQ * 1.0 * r.Ns / r.LD;                           // D0_REF = 1
    r.R0 = 1.0 * r.Ns / (r.LD * r.LD);

    r.xs.resize(N); r.h.resize(N - 1); r.dV.assign(N, 0.0); r.C.resize(N);
    for (std::size_t i = 0; i < N; ++i) r.xs[i] = x[i] / r.LD;
    for (std::size_t k = 0; k + 1 < N; ++k) r.h[k] = r.xs[k + 1] - r.xs[k];
    for (std::size_t i = 1; i + 1 < N; ++i) r.dV[i] = 0.5 * (r.h[i - 1] + r.h[i]);
    r.dV[0] = 0.5 * r.h[0];
    r.dV[N - 1] = 0.5 * r.h[N - 2];
    for (std::size_t i = 0; i < N; ++i) r.C[i] = doping[i] / r.Ns;

    // Per-node material fields (materials.py's nie_effective /
    // mobility_caughey_thomas / lifetime_scharfetter).
    r.nie.resize(N); r.mu_n0.resize(N); r.mu_p0.resize(N); r.tau_n.resize(N);
    r.tau_p.resize(N); r.nie_s.resize(N);
    r.nc_s.resize(N); r.nv_s.resize(N); r.ln_gn.resize(N); r.ln_gp.resize(N);
    r.eg_kt.resize(N); r.Cn_auger.resize(N); r.Cp_auger.resize(N);
    r.m_n_star.resize(N); r.m_p_star.resize(N);
    for (std::size_t i = 0; i < N; ++i) {
        const ph::MaterialParams& m = mat(i);
        const double nt = Ntot[i];
        r.nie[i] = ph::nie_effective(m, nt, T, f.bgn);
        r.mu_n0[i] = f.doping_mobility ? ph::mobility_caughey_thomas(m, nt, T, true) : m.mu_n_max;
        r.mu_p0[i] = f.doping_mobility ? ph::mobility_caughey_thomas(m, nt, T, false) : m.mu_p_max;
        r.tau_n[i] = ph::lifetime_scharfetter(nt, m.tau_n0, m.tau_Nref);
        r.tau_p[i] = ph::lifetime_scharfetter(nt, m.tau_p0, m.tau_Nref);
        r.nie_s[i] = r.nie[i] / r.Ns;
        // M13 band-DOS arrays (FD statistics)
        r.nc_s[i] = ph::dos_c(m, T) / r.Ns;
        r.nv_s[i] = ph::dos_v(m, T) / r.Ns;
        r.ln_gn[i] = std::log(r.nc_s[i] / r.nie_s[i]);
        r.ln_gp[i] = std::log(r.nv_s[i] / r.nie_s[i]);
        r.eg_kt[i] = ph::band_gap(m, T) / (ph::kKB_EV * T);
        r.Cn_auger[i] = m.Cn_auger;
        r.Cp_auger[i] = m.Cp_auger;
        r.m_n_star[i] = m.m_n_star;
        r.m_p_star[i] = m.m_p_star;
    }

    // M33-S1: affinity-gauge band shift s = ln(Nc/nie) + chi/VT, referenced
    // to node 0 (identically 0 in the nie gauge).
    r.band_shift.assign(N, 0.0);
    if (f.affinity) {
        std::vector<double> s(N);
        for (std::size_t i = 0; i < N; ++i) s[i] = r.ln_gn[i] + r.chi_arr[i] / r.VT;
        for (std::size_t i = 0; i < N; ++i) r.band_shift[i] = s[i] - s[0];
    }

    // M33-S2: heterointerface edges and the thermionic edge arrays.
    r.te_edge.resize(N - 1);
    bool any_iface = false;
    for (std::size_t k = 0; k + 1 < N; ++k) {
        r.te_edge[k] = node_mat[k] != node_mat[k + 1] ? 1 : 0;
        any_iface = any_iface || r.te_edge[k];
    }
    if (f.thermionic) {
        if (!any_iface)
            throw tcad::InvalidArgument(
                "Models(thermionic=True) but the device is a homojunction -- there "
                "is no interface to apply a thermionic flux to. Refusing rather "
                "than silently doing nothing.");
        std::vector<double> vn(N), vp(N);
        for (std::size_t i = 0; i < N; ++i) {
            vn[i] = ph::emission_velocity(r.nc_s[i] * r.Ns, T);
            vp[i] = ph::emission_velocity(r.nv_s[i] * r.Ns, T);
        }
        const std::vector<double> hn = hmean(vn), hp = hmean(vp);
        r.te_Kn.resize(N - 1); r.te_Kp.resize(N - 1); r.te_dlnNc.resize(N - 1);
        r.te_dlnNv.resize(N - 1); r.te_rNc.resize(N - 1); r.te_rNv.resize(N - 1);
        for (std::size_t k = 0; k + 1 < N; ++k) {
            r.te_Kn[k] = hn[k] * r.LD / 1.0;
            r.te_Kp[k] = hp[k] * r.LD / 1.0;
            r.te_dlnNc[k] = std::log(r.nc_s[k + 1] / r.nc_s[k]);
            r.te_dlnNv[k] = std::log(r.nv_s[k + 1] / r.nv_s[k]);
            r.te_rNc[k] = r.nc_s[k] / r.nc_s[k + 1];
            r.te_rNv[k] = r.nv_s[k] / r.nv_s[k + 1];
        }
    }

    // M13 incomplete ionization: dopant split of the net doping (scaled).
    r.nd_arr.resize(N); r.na_arr.resize(N);
    for (std::size_t i = 0; i < N; ++i) {
        r.nd_arr[i] = std::max(doping[i], 0.0) / r.Ns;
        r.na_arr[i] = std::max(-doping[i], 0.0) / r.Ns;
    }
    // _set_edge_diffusivity: Einstein relation, harmonic mean onto edges.
    const std::vector<double> hmn = hmean(r.mu_n0), hmp = hmean(r.mu_p0);
    r.dn_edge.resize(N - 1); r.dp_edge.resize(N - 1);
    for (std::size_t k = 0; k + 1 < N; ++k) {
        r.dn_edge[k] = hmn[k] * r.VT / 1.0;
        r.dp_edge[k] = hmp[k] * r.VT / 1.0;
    }
    // _eps_tilde_edge: harmonic-mean eps normalised by node 0's (1 for a
    // uniform device).
    {
        std::vector<double> et_n(N);
        for (std::size_t i = 0; i < N; ++i) et_n[i] = r.eps_arr[i] / r.eps_arr[0];
        r.et = hmean(et_n);
    }
    // M44 energy-balance scaling constants (device.py _ALPHA_RELAX/_KAPPA0).
    if (f.tau_w > 0.0) {
        r.alpha_relax = 1.5 * ph::kKB * T * r.Ns * r.LD / (f.tau_w * r.J0 * r.VT);
        r.kappa0 = 2.5 * (ph::kKB * ph::kKB / ph::kQ) * r.Ns * T * T / (r.LD * r.J0 * r.VT);
    }

    // M12-S2 TAT: psi-independent WKB exponent numerators B phi^1.5 (SI).
    if (f.tat) {
        r.tat_kn.resize(N); r.tat_kp.resize(N);
        for (std::size_t i = 0; i < N; ++i) {
            const ph::MaterialParams& m = mat(i);
            const double phi_n = m.Eg0 * (1.0 - f.trap_et_rel);
            const double phi_p = ph::band_gap(m, T) * f.trap_et_rel;
            const double mn = m.m_n_star * ph::kM0, mp = m.m_p_star * ph::kM0;
            const double Bn = 4.0 * std::sqrt(2.0 * mn * ph::kQ) / (3.0 * ph::kHBAR);
            const double Bp = 4.0 * std::sqrt(2.0 * mp * ph::kQ) / (3.0 * ph::kHBAR);
            r.tat_kn[i] = Bn * std::pow(phi_n, 1.5);
            r.tat_kp[i] = Bp * std::pow(phi_p, 1.5);
        }
    }

    // M46-S1 Schottky contacts: barrier-limited majority density (Schottky-
    // Mott phi_Bn = phi_m - chi; phi_Bp = Eg - phi_Bn on a p-type node),
    // minority from mass action. FD/incomplete-ion contacts ignore them.
    if (!f.fd_or_ion) {
        for (int side = 0; side < 2; ++side) {
            if (!f.schottky[static_cast<std::size_t>(side)]) continue;
            const std::size_t i = side == 0 ? 0 : N - 1;
            const double nie = r.nie_s[i], kT = ph::kKB_EV * T;
            const double phi_Bn = f.phi_metal_eV[static_cast<std::size_t>(side)] - r.chi_arr[i];
            double n0, p0;
            if (r.C[i] >= 0.0) {
                n0 = r.nc_s[i] * std::exp(-phi_Bn / kT);
                p0 = nie * nie / n0;
            } else {
                const double phi_B = ph::band_gap(mat(i), T) - phi_Bn;
                p0 = r.nv_s[i] * std::exp(-phi_B / kT);
                n0 = nie * nie / p0;
            }
            r.contact_override[static_cast<std::size_t>(side)] = true;
            r.contact_n0[static_cast<std::size_t>(side)] = n0;
            r.contact_p0[static_cast<std::size_t>(side)] = p0;
        }
    }

    // M34-S1 nonlocal BTBT: material 0's band parameters (homojunction).
    if (f.btbt_nonlocal) {
        const ph::MaterialParams& m = mat(0);
        r.nl_mc = m.m_n_star * ph::kM0;
        r.nl_mv = m.m_p_star * ph::kM0;
        r.nl_Eg_eV = ph::band_gap(m, T);
        r.nl_Eg_J = r.nl_Eg_eV * ph::kQ;
        r.nl_mr = 1.0 / (1.0 / r.nl_mc + 1.0 / r.nl_mv);
        r.nl_u = ph::kM0 / (2.0 * r.nl_mr);
        if (!(r.nl_u > 1.0))
            throw tcad::InvalidArgument(
                "eq (9) needs u = m0/(2 mr) > 1 (mr < m0/2) for kappa to vanish at "
                "both band edges; got mr = " + std::to_string(r.nl_mr / ph::kM0) + " m0");
    }
    return r;
}

}  // namespace tcad::device1d
