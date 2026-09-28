// Transcription of device.py's Device1D, baseline-model slice only --
// see device1d.hpp's file header. Every formula below is a direct
// line-for-line port of the corresponding Python; comments cite the
// Python method they came from so a future Phase 2 diff is easy to
// place.
#include "tcad/device1d/device1d.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <vector>

#include "tcad/base/errors.hpp"
#include "tcad/physics/fermi.hpp"
#include "tcad/physics/generation.hpp"
#include "tcad/physics/kernels.hpp"
#include "tcad/solver/direct_lu.hpp"

namespace tcad::device1d {

namespace {
constexpr double kQ = 1.602176634e-19;   // constants.Q
constexpr double kD0Ref = 1.0;           // kernels.D0_REF

double clip(double v, double lo, double hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}

std::vector<double> hmean(const std::vector<double>& a) {
    const std::size_t m = a.size() - 1;
    std::vector<double> out(m);
    for (std::size_t k = 0; k < m; ++k)
        out[k] = 2.0 * a[k] * a[k + 1] / (a[k] + a[k + 1]);
    return out;
}

/// Symmetric Dirichlet elimination on COO triplets -- transcription of
/// dirichlet.eliminate_coo (see that module's docstring for the
/// derivation). `rhs` is modified in place; `J`'s triplets are filtered
/// in place. The constrained rows are assumed already stamped as the
/// unit row (diagonal 1, nothing else) by the caller, matching
/// eliminate_coo's own precondition.
void eliminate_dirichlet(Coo& J, std::vector<double>& rhs,
                         std::span<const std::int64_t> dirichlet) {
    const std::size_t n = rhs.size();
    std::vector<bool> is_d(n, false);
    for (auto d : dirichlet) is_d[static_cast<std::size_t>(d)] = true;
    std::vector<double> g(n, 0.0);
    for (auto d : dirichlet) g[static_cast<std::size_t>(d)] = rhs[static_cast<std::size_t>(d)];

    std::vector<std::int64_t> new_rows, new_cols;
    std::vector<double> new_vals;
    new_rows.reserve(J.rows.size());
    new_cols.reserve(J.rows.size());
    new_vals.reserve(J.rows.size());
    for (std::size_t k = 0; k < J.rows.size(); ++k) {
        const std::int64_t r = J.rows[k], c = J.cols[k];
        const double v = J.vals[k];
        const bool col_d = is_d[static_cast<std::size_t>(c)];
        const bool row_d = is_d[static_cast<std::size_t>(r)];
        if (col_d && !row_d) {
            rhs[static_cast<std::size_t>(r)] -= v * g[static_cast<std::size_t>(c)];
            continue;
        }
        if (col_d && row_d && r != c) continue;
        new_rows.push_back(r);
        new_cols.push_back(c);
        new_vals.push_back(v);
    }
    J.rows = std::move(new_rows);
    J.cols = std::move(new_cols);
    J.vals = std::move(new_vals);
    for (auto d : dirichlet) rhs[static_cast<std::size_t>(d)] = g[static_cast<std::size_t>(d)];
}

}  // namespace

Device1D::Device1D(std::span<const double> x, std::span<const double> doping,
                   double T, double VT, double eps, double ni,
                   std::span<const double> mu_n0_phys,
                   std::span<const double> mu_p0_phys,
                   std::span<const double> tau_n_phys,
                   std::span<const double> tau_p_phys,
                   std::span<const double> nie_phys, double Cn_auger,
                   double Cp_auger, Models models,
                   std::span<const double> nc_s, std::span<const double> nv_s,
                   std::span<const double> ln_gn, std::span<const double> ln_gp,
                   std::span<const double> eg_kt, std::span<const double> nd_arr,
                   std::span<const double> na_arr, double ded_kt,
                   std::span<const double> fermi_e, std::span<const double> fermi_g,
                   std::span<const double> fermi_gp, std::span<const double> fermi_q,
                   std::span<const double> fermi_qp,
                   std::span<const double> tat_kn, std::span<const double> tat_kp)
    : N_(static_cast<int>(x.size())),
      T_(T), VT_(VT), eps_(eps), ni_(ni),
      x_(x.begin(), x.end()), doping_(doping.begin(), doping.end()),
      mu_n0_(mu_n0_phys.begin(), mu_n0_phys.end()),
      mu_p0_(mu_p0_phys.begin(), mu_p0_phys.end()),
      tau_n_(tau_n_phys.begin(), tau_n_phys.end()),
      tau_p_(tau_p_phys.begin(), tau_p_phys.end()),
      nie_(nie_phys.begin(), nie_phys.end()),
      Cn_auger_(Cn_auger), Cp_auger_(Cp_auger), models_(models),
      nc_s_(nc_s.begin(), nc_s.end()), nv_s_(nv_s.begin(), nv_s.end()),
      ln_gn_(ln_gn.begin(), ln_gn.end()), ln_gp_(ln_gp.begin(), ln_gp.end()),
      eg_kt_(eg_kt.begin(), eg_kt.end()), nd_arr_(nd_arr.begin(), nd_arr.end()),
      na_arr_(na_arr.begin(), na_arr.end()), ded_kt_(ded_kt),
      fermi_table_{std::vector<double>(fermi_e.begin(), fermi_e.end()),
                   std::vector<double>(fermi_g.begin(), fermi_g.end()),
                   std::vector<double>(fermi_gp.begin(), fermi_gp.end()),
                   std::vector<double>(fermi_q.begin(), fermi_q.end()),
                   std::vector<double>(fermi_qp.begin(), fermi_qp.end())},
      tat_kn_(tat_kn.begin(), tat_kn.end()), tat_kp_(tat_kp.begin(), tat_kp.end()) {
    if (models_.tat && (tat_kn_.size() != static_cast<std::size_t>(N_) ||
                        tat_kp_.size() != static_cast<std::size_t>(N_)))
        throw tcad::InvalidArgument(
            "Device1D: models.tat requires tat_kn/tat_kp (length N)");
    if ((models_.fd || models_.incomplete_ion) &&
        (nc_s_.size() != static_cast<std::size_t>(N_) ||
         nv_s_.size() != static_cast<std::size_t>(N_) ||
         ln_gn_.size() != static_cast<std::size_t>(N_) ||
         eg_kt_.size() != static_cast<std::size_t>(N_) ||
         nd_arr_.size() != static_cast<std::size_t>(N_) ||
         na_arr_.size() != static_cast<std::size_t>(N_) ||
         fermi_table_.e.empty()))
        throw tcad::InvalidArgument(
            "Device1D: models.fd/incomplete_ion require nc_s/nv_s/ln_gn/"
            "eg_kt/nd_arr/na_arr (length N) and a nonempty FD table");
    if (doping_.size() != static_cast<std::size_t>(N_) ||
        mu_n0_.size() != static_cast<std::size_t>(N_) ||
        mu_p0_.size() != static_cast<std::size_t>(N_) ||
        tau_n_.size() != static_cast<std::size_t>(N_) ||
        tau_p_.size() != static_cast<std::size_t>(N_) ||
        nie_.size() != static_cast<std::size_t>(N_))
        throw tcad::InvalidArgument("Device1D: all per-node arrays must have length N");

    // --- __init__: concentration/length/current/rate scales ---
    double doping_max_abs = 0.0;
    for (double d : doping_) doping_max_abs = std::max(doping_max_abs, std::abs(d));
    Ns_ = std::max(doping_max_abs, ni_);
    LD_ = std::sqrt(eps_ * VT_ / (kQ * Ns_));
    J0_ = kQ * kD0Ref * Ns_ / LD_;
    R0_ = kD0Ref * Ns_ / (LD_ * LD_);

    xs_.resize(N_);
    for (int i = 0; i < N_; ++i) xs_[i] = x_[i] / LD_;
    h_.resize(N_ - 1);
    for (int k = 0; k < N_ - 1; ++k) h_[k] = xs_[k + 1] - xs_[k];
    dV_.assign(N_, 0.0);
    dV_[0] = 0.5 * h_[0];
    dV_[N_ - 1] = 0.5 * h_[N_ - 2];
    for (int i = 1; i < N_ - 1; ++i) dV_[i] = 0.5 * (h_[i - 1] + h_[i]);

    C_.resize(N_);
    for (int i = 0; i < N_; ++i) C_[i] = doping_[i] / Ns_;
    nie_s_.resize(N_);
    for (int i = 0; i < N_; ++i) nie_s_[i] = nie_[i] / Ns_;

    // _set_edge_diffusivity: Einstein relation, harmonic-mean onto edges.
    dn_edge_ = hmean(mu_n0_);
    dp_edge_ = hmean(mu_p0_);
    for (auto& v : dn_edge_) v *= VT_ / kD0Ref;
    for (auto& v : dp_edge_) v *= VT_ / kD0Ref;

    dirichlet_rows_ = {0, 1, 2, 3 * (N_ - 1), 3 * (N_ - 1) + 1, 3 * (N_ - 1) + 2};
}

ContactBC Device1D::contact_value(int side, double V) const {
    const int i = (side == 0) ? 0 : N_ - 1;
    if (models_.fd || models_.incomplete_ion) {
        // device.py's _fd_contact_values: local neutrality solved in
        // eta-space (fd_neutral_eta), then mass-action densities and
        // psi0 = V/VT + e0 + ln(Nc/nie). Used whenever EITHER fd or
        // incomplete_ion is set (device.py's own `_contact_values`
        // dispatch condition), since incomplete ionization's contact
        // value still needs the FD density law to define n0/p0 even
        // when the BULK statistics stay Boltzmann.
        const double e0 = fd_neutral_eta(i);
        const double n0 = tcad::physics::fd_density(fermi_table_, nc_s_[i], e0);
        const double ep0 = -e0 - eg_kt_[i];
        const double p0 = tcad::physics::fd_density(fermi_table_, nv_s_[i], ep0);
        const double psi0 = V / VT_ + e0 + ln_gn_[i];
        return {psi0, n0, p0};
    }
    // device.py's _contact_values, Schottky-free / homojunction branch:
    //   n0 - p0 = C, n0*p0 = nie^2  =>  n0 = 0.5*(C + sqrt(C^2+4nie^2))
    //   (majority carrier from the sqrt; minority from mass action)
    const double Cc = C_[i], nie = nie_s_[i];
    const double root = std::sqrt(Cc * Cc + 4.0 * nie * nie);
    double n0, p0;
    if (Cc >= 0.0) {
        n0 = 0.5 * (Cc + root);
        p0 = nie * nie / n0;
    } else {
        p0 = 0.5 * (-Cc + root);
        n0 = nie * nie / p0;
    }
    const double psi0 = V / VT_ + std::log(n0 / nie);  // band_shift == 0 (homojunction)
    return {psi0, n0, p0};
}

double Device1D::fd_neutral_eta(int node) const {
    return tcad::physics::fd_neutral_eta_node(
        fermi_table_, nc_s_[node], nv_s_[node], eg_kt_[node], C_[node], ded_kt_,
        nd_arr_[node], na_arr_[node], models_.incomplete_ion);
}

void Device1D::tat_probabilities(const std::vector<double>& psi,
                                 std::vector<double>& Pn,
                                 std::vector<double>& Pp) const {
    // device.py's _update_tat_probabilities: node field = mean of the
    // two adjacent edge fields (the boundary edge's at the ends), in
    // V/m because B is SI-calibrated; P = exp(-B phi^1.5 / max(F, 1)).
    // tat_kn_/tat_kp_ hold B*phi^1.5 (psi-independent, built in Python).
    if (tat_kn_.size() != static_cast<std::size_t>(N_) ||
        tat_kp_.size() != static_cast<std::size_t>(N_) ||
        psi.size() != static_cast<std::size_t>(N_))
        throw tcad::InvalidArgument(
            "Device1D: TAT probabilities need tat_kn/tat_kp and psi of length N");
    const int Ne = N_ - 1;
    std::vector<double> edge_F(Ne);
    for (int k = 0; k < Ne; ++k)
        edge_F[k] = std::abs(psi[k + 1] - psi[k]) * VT_ / (LD_ * h_[k]) * 100.0;
    Pn.assign(N_, 0.0);
    Pp.assign(N_, 0.0);
    for (int i = 0; i < N_; ++i) {
        double F;
        if (i == 0) F = edge_F[0];
        else if (i == N_ - 1) F = edge_F[Ne - 1];
        else F = 0.5 * (edge_F[i - 1] + edge_F[i]);
        const double safe_F = std::max(F, 1.0);
        Pn[i] = std::exp(-tat_kn_[i] / safe_F);
        Pp[i] = std::exp(-tat_kp_[i] / safe_F);
    }
}

void Device1D::solve_equilibrium(const NewtonOptions& opts) {
    if (models_.fd || models_.incomplete_ion) {
        solve_equilibrium_fd(opts);
        return;
    }
    // device.py's solve_equilibrium, non-FD/non-incomplete_ion/non-dg
    // branch: Poisson-only Newton, carriers slaved to psi.
    const ContactBC bc0 = contact_value(0, 0.0);
    const ContactBC bc1 = contact_value(1, 0.0);

    std::vector<double> psi(N_);
    for (int i = 0; i < N_; ++i) psi[i] = std::asinh(C_[i] / (2.0 * nie_s_[i]));
    psi[0] = bc0.psi0;
    psi[N_ - 1] = bc1.psi0;

    for (int it = 0; it < opts.max_iter; ++it) {
        std::vector<double> n(N_), p(N_), dnp(N_);
        for (int i = 0; i < N_; ++i) {
            n[i] = nie_s_[i] * std::exp(clip(psi[i], -700.0, 700.0));
            p[i] = nie_s_[i] * std::exp(clip(-psi[i], -700.0, 700.0));
            dnp[i] = n[i] + p[i];
        }
        std::vector<double> F(N_, 0.0);
        Coo J;
        for (int i = 1; i < N_ - 1; ++i) {
            F[i] = (psi[i + 1] - psi[i]) / h_[i] - (psi[i] - psi[i - 1]) / h_[i - 1] -
                   dV_[i] * (n[i] - p[i] - C_[i]);
            J.add(i, i, -1.0 / h_[i] - 1.0 / h_[i - 1] - dV_[i] * dnp[i]);
            J.add(i, i + 1, 1.0 / h_[i]);
            J.add(i, i - 1, 1.0 / h_[i - 1]);
        }
        F[0] = psi[0] - bc0.psi0;
        F[N_ - 1] = psi[N_ - 1] - bc1.psi0;
        J.add(0, 0, 1.0);
        J.add(N_ - 1, N_ - 1, 1.0);

        std::vector<double> rhs(N_);
        for (int i = 0; i < N_; ++i) rhs[i] = -F[i];
        std::vector<double> d =
            tcad::solver::solve_direct_lu(J.rows, J.cols, J.vals, N_, rhs);
        double max_d = 0.0;
        for (int i = 0; i < N_; ++i) {
            d[i] = clip(d[i], -opts.max_dpsi, opts.max_dpsi);
            psi[i] += d[i];
            max_d = std::max(max_d, std::abs(d[i]));
        }
        if (max_d < opts.tol_update) break;
    }

    psi_ = psi;
    n_.resize(N_);
    p_.resize(N_);
    for (int i = 0; i < N_; ++i) {
        n_[i] = nie_s_[i] * std::exp(clip(psi_[i], -700.0, 700.0));
        p_[i] = nie_s_[i] * std::exp(clip(-psi_[i], -700.0, 700.0));
    }
    has_solution_ = true;
}

void Device1D::solve_equilibrium_fd(const NewtonOptions& opts) {
    // device.py's solve_equilibrium, `if fd or ion:` branch. Poisson-
    // only Newton (still one unknown per node), but the density law
    // and its psi-derivative are chosen per models_.fd, and the
    // charge/derivative gain an incomplete-ionization correction when
    // models_.incomplete_ion. et == 1 everywhere (homojunction,
    // band_shift == 0), matching the baseline loop's own convention.
    namespace ph = tcad::physics;
    std::vector<double> psi(N_);
    for (int i = 0; i < N_; ++i) psi[i] = fd_neutral_eta(i) + ln_gn_[i];
    const ContactBC bc0 = contact_value(0, 0.0);
    const ContactBC bc1 = contact_value(1, 0.0);
    psi[0] = bc0.psi0;
    psi[N_ - 1] = bc1.psi0;

    for (int it = 0; it < opts.max_iter; ++it) {
        std::vector<double> n(N_), p(N_), dnp(N_), c_eff(N_);
        for (int i = 0; i < N_; ++i) {
            if (models_.fd) {
                const double en = std::min(psi[i] - ln_gn_[i], ph::kFermiEtaMax);
                const double ep = std::min(-psi[i] - ln_gp_[i], ph::kFermiEtaMax);
                n[i] = ph::fd_density(fermi_table_, nc_s_[i], en);
                p[i] = ph::fd_density(fermi_table_, nv_s_[i], ep);
                dnp[i] = ph::fd_ddensity_deta(fermi_table_, nc_s_[i], en)
                       + ph::fd_ddensity_deta(fermi_table_, nv_s_[i], ep);
            } else {
                n[i] = nie_s_[i] * std::exp(clip(psi[i], -700.0, 700.0));
                p[i] = nie_s_[i] * std::exp(clip(-psi[i], -700.0, 700.0));
                dnp[i] = n[i] + p[i];
            }
            c_eff[i] = C_[i];
            if (models_.incomplete_ion) {
                const ph::IonizedDopingResult ic = ph::ionized_doping(
                    fermi_table_, nd_arr_[i], na_arr_[i], n[i], p[i], nc_s_[i],
                    nv_s_[i], ded_kt_);
                c_eff[i] = ic.cion;
                if (models_.fd) {
                    const double fddn = ph::fd_ddensity_deta(
                        fermi_table_, nc_s_[i],
                        std::min(psi[i] - ln_gn_[i], ph::kFermiEtaMax));
                    const double fddp = ph::fd_ddensity_deta(
                        fermi_table_, nv_s_[i],
                        std::min(-psi[i] - ln_gp_[i], ph::kFermiEtaMax));
                    dnp[i] = dnp[i] - ic.dcion_dn * fddn + ic.dcion_dp * fddp;
                } else {
                    dnp[i] = dnp[i] - ic.dcion_dn * n[i] + ic.dcion_dp * p[i];
                }
            }
        }

        std::vector<double> F(N_, 0.0);
        Coo J;
        for (int i = 1; i < N_ - 1; ++i) {
            F[i] = (psi[i + 1] - psi[i]) / h_[i] - (psi[i] - psi[i - 1]) / h_[i - 1] -
                   dV_[i] * (n[i] - p[i] - c_eff[i]);
            J.add(i, i, -1.0 / h_[i] - 1.0 / h_[i - 1] - dV_[i] * dnp[i]);
            J.add(i, i + 1, 1.0 / h_[i]);
            J.add(i, i - 1, 1.0 / h_[i - 1]);
        }
        F[0] = psi[0] - bc0.psi0;
        F[N_ - 1] = psi[N_ - 1] - bc1.psi0;
        J.add(0, 0, 1.0);
        J.add(N_ - 1, N_ - 1, 1.0);

        std::vector<double> rhs(N_);
        for (int i = 0; i < N_; ++i) rhs[i] = -F[i];
        std::vector<double> d =
            tcad::solver::solve_direct_lu(J.rows, J.cols, J.vals, N_, rhs);
        double max_d = 0.0;
        for (int i = 0; i < N_; ++i) {
            d[i] = clip(d[i], -opts.max_dpsi, opts.max_dpsi);
            psi[i] += d[i];
            max_d = std::max(max_d, std::abs(d[i]));
        }
        if (max_d < opts.tol_update) break;
    }

    psi_ = psi;
    n_.resize(N_);
    p_.resize(N_);
    if (models_.fd) {
        // device.py's own post-loop honesty check: the in-loop clamp
        // protects a TRANSIENT overshoot, but a CONVERGED eta genuinely
        // outside the validated range must still refuse loudly rather
        // than silently accept it -- check the RAW, unclamped eta.
        for (int i = 0; i < N_; ++i) {
            const double en_raw = psi_[i] - ln_gn_[i];
            const double ep_raw = -psi_[i] - ln_gp_[i];
            if (en_raw > ph::kFermiEtaMax || ep_raw > ph::kFermiEtaMax) {
                throw tcad::InvalidArgument(
                    "FD equilibrium converged beyond the validated Fermi-"
                    "integral range (M13 G7 applicability). Refusing to "
                    "extrapolate.");
            }
            n_[i] = ph::fd_density(fermi_table_, nc_s_[i], en_raw);
            p_[i] = ph::fd_density(fermi_table_, nv_s_[i], ep_raw);
        }
    } else {
        for (int i = 0; i < N_; ++i) {
            n_[i] = nie_s_[i] * std::exp(clip(psi_[i], -700.0, 700.0));
            p_[i] = nie_s_[i] * std::exp(clip(-psi_[i], -700.0, 700.0));
        }
    }
    has_solution_ = true;
}

Device1D::ResidualJacobian Device1D::residual_jacobian(
    const std::vector<double>& psi, const std::vector<double>& n,
    const std::vector<double>& p, const std::array<ContactBC, 2>& bc,
    const std::vector<double>& Pn, const std::vector<double>& Pp,
    double strength) const {
    namespace ph = tcad::physics;
    const int N = N_;
    const int Ne = N - 1;
    const bool fd = models_.fd;

    // --- M13 Fermi-Dirac nu-factor SG (device.py's `_fd_factors` call,
    // fd_node_factors): per-node Ln/Lp/wn/wp, only when fd. `thr`
    // matches fermi.py's `f_half(-30.0)`, computed ONCE per call here
    // exactly as the Python vectorized call does (not once per node).
    std::vector<double> Ln, Lp, wn, wp, nu_n, nu_p;
    if (fd) {
        Ln.assign(N, 0.0); Lp.assign(N, 0.0); wn.assign(N, 0.0); wp.assign(N, 0.0);
        nu_n.assign(N, 0.0); nu_p.assign(N, 0.0);
        const double thr = ph::f_half(fermi_table_, -30.0);
        for (int j = 0; j < N; ++j) {
            const ph::FdNodeFactors f = ph::fd_node_factors_node(
                fermi_table_, thr, nc_s_[j], nv_s_[j], n[j], p[j]);
            Ln[j] = f.Ln; Lp[j] = f.Lp; wn[j] = f.wn; wp[j] = f.wp;
            nu_n[j] = std::exp(Ln[j]);
            nu_p[j] = std::exp(Lp[j]);
        }
    }

    // --- per-edge SG quantities (device.py's _residual_jacobian, top) ---
    std::vector<double> delta(Ne), delta_p(Ne);
    for (int k = 0; k < Ne; ++k) {
        const double dlnnie = std::log(nie_s_[k + 1] / nie_s_[k]);
        const double dpsi = psi[k + 1] - psi[k];
        delta[k] = dpsi + dlnnie;
        delta_p[k] = dpsi - dlnnie;
        if (fd) {
            delta[k] += Ln[k + 1] - Ln[k];
            delta_p[k] -= Lp[k + 1] - Lp[k];
        }
    }
    std::vector<double> Bp(Ne), Bm(Ne), dBp(Ne), dBm(Ne);
    std::vector<double> Bp_h(Ne), Bm_h(Ne), dBp_h(Ne), dBm_h(Ne);
    std::vector<double> an(Ne), ap(Ne);
    std::vector<double> Jn(Ne), Jp(Ne), dJn_dpsiR(Ne), dJp_dpsiR(Ne);
    std::vector<double> Sn, Sp;
    if (fd) { Sn.assign(Ne, 0.0); Sp.assign(Ne, 0.0); }
    for (int k = 0; k < Ne; ++k) {
        Bp[k] = tcad::physics::bernoulli(delta[k]);
        Bm[k] = tcad::physics::bernoulli(-delta[k]);
        dBp[k] = tcad::physics::dbernoulli(delta[k]);
        dBm[k] = tcad::physics::dbernoulli(-delta[k]);
        Bp_h[k] = tcad::physics::bernoulli(delta_p[k]);
        Bm_h[k] = tcad::physics::bernoulli(-delta_p[k]);
        dBp_h[k] = tcad::physics::dbernoulli(delta_p[k]);
        dBm_h[k] = tcad::physics::dbernoulli(-delta_p[k]);
        an[k] = dn_edge_[k] / h_[k];
        ap[k] = dp_edge_[k] / h_[k];
        Jn[k] = an[k] * (n[k + 1] * Bp[k] - n[k] * Bm[k]);
        Jp[k] = -ap[k] * (p[k + 1] * Bm_h[k] - p[k] * Bp_h[k]);
        dJn_dpsiR[k] = an[k] * (n[k + 1] * dBp[k] + n[k] * dBm[k]);
        dJp_dpsiR[k] = ap[k] * (p[k + 1] * dBm_h[k] + p[k] * dBp_h[k]);
        if (fd) {
            Sn[k] = n[k + 1] * dBp[k] + n[k] * dBm[k];
            Sp[k] = p[k + 1] * dBm_h[k] + p[k] * dBp_h[k];
        }
    }

    // --- recombination (materials.recombination): the FD np_eq
    // composition (device.py's `npq_args`) when fd, else the plain
    // Boltzmann form -- unchanged from Phase 1 in that case.
    // M12-S2 TAT (device.py's `if tat and (Pn.any() or Pp.any())`): the
    // all-zero case leaves the SRH/Auger arrays UNTOUCHED (traps-off
    // bit-identity), otherwise R is REPLACED -- SRH with tunneling-
    // assisted capture, no Auger, and applied after the srh=False
    // zeroing exactly as the Python orders it.
    bool tat_active = false;
    if (models_.tat) {
        for (int j = 0; j < N && !tat_active; ++j)
            tat_active = (Pn[j] != 0.0) || (Pp[j] != 0.0);
    }
    std::vector<double> Rs(N), dRs_dn(N), dRs_dp(N);
    for (int j = 0; j < N; ++j) {
        const double n_phys = n[j] * Ns_, p_phys = p[j] * Ns_;
        double R, dRdn, dRdp;
        double npq = 0.0, dnpq_dns = 0.0, dnpq_dps = 0.0;
        if (fd) {
            const double nie2 = nie_[j] * nie_[j];
            npq = nie2 * nu_n[j] * nu_p[j];
            dnpq_dns = nie2 * nu_p[j] * nu_n[j] * wn[j];
            dnpq_dps = nie2 * nu_n[j] * nu_p[j] * wp[j];
            const ph::RecombinationResult r = ph::recombination_fd(
                n_phys, p_phys, nie_[j], npq, dnpq_dns / Ns_, dnpq_dps / Ns_,
                tau_n_[j], tau_p_[j], Cn_auger_, Cp_auger_, models_.auger);
            R = r.R; dRdn = r.dRdn; dRdp = r.dRdp;
        } else {
            const ph::RecombinationResult r = ph::recombination_boltzmann(
                n_phys, p_phys, nie_[j], tau_n_[j], tau_p_[j], Cn_auger_, Cp_auger_,
                models_.auger);
            R = r.R; dRdn = r.dRdn; dRdp = r.dRdp;
        }
        if (!models_.srh) { R = 0.0; dRdn = 0.0; dRdp = 0.0; }
        if (tat_active) {
            // R_TAT = (n p - np_eq) / [taup(n + nie(1+Pp)) + taun(p + nie(1+Pn))]
            const double nie2 = fd ? npq : nie_[j] * nie_[j];
            const double dqdn = fd ? dnpq_dns / Ns_ : 0.0;
            const double dqdp = fd ? dnpq_dps / Ns_ : 0.0;
            const double den = tau_p_[j] * (n_phys + nie_[j] * (1.0 + Pp[j]))
                             + tau_n_[j] * (p_phys + nie_[j] * (1.0 + Pn[j]));
            const double excess = n_phys * p_phys - nie2;
            R = excess / den;
            dRdn = ((p_phys - dqdn) * den - excess * tau_p_[j]) / (den * den);
            dRdp = ((n_phys - dqdp) * den - excess * tau_n_[j]) / (den * den);
        }
        Rs[j] = R / R0_;
        dRs_dn[j] = dRdn * Ns_ / R0_;
        dRs_dp[j] = dRdp * Ns_ / R0_;
    }

    // --- M13 incomplete ionization: net ionized doping and its
    // derivatives wrt the slot densities, per node (device.py's
    // `_ionized_C`) -- only when incomplete_ion.
    std::vector<double> cion, dcden, dcdp;
    if (models_.incomplete_ion) {
        cion.assign(N, 0.0); dcden.assign(N, 0.0); dcdp.assign(N, 0.0);
        for (int j = 0; j < N; ++j) {
            const ph::IonizedDopingResult ic = ph::ionized_doping(
                fermi_table_, nd_arr_[j], na_arr_[j], n[j], p[j], nc_s_[j],
                nv_s_[j], ded_kt_);
            cion[j] = ic.cion; dcden[j] = ic.dcion_dn; dcdp[j] = ic.dcion_dp;
        }
    }

    ResidualJacobian out;
    out.F.assign(3 * N, 0.0);
    out.Jn = Jn;
    out.Jp = Jp;
    Coo& J = out.J;

    for (int i = 1; i < N - 1; ++i) {
        const int eR = i, eL = i - 1;  // right/left edge indices

        // Poisson row (et == 1 everywhere: single material). M13
        // incomplete ionization replaces the fixed doping C_[i] with
        // the node's net IONIZED doping cion[i] and its Jacobian
        // columns with the ionization chain -- device.py's own
        // `if incomplete_ion: ... else: add(..., -dV_); add(..., dV_)`.
        const double charge_term = models_.incomplete_ion ? cion[i] : C_[i];
        out.F[3 * i] = (psi[i + 1] - psi[i]) / h_[eR] - (psi[i] - psi[i - 1]) / h_[eL] -
                       dV_[i] * (n[i] - p[i] - charge_term);
        J.add(3 * i, 3 * i, -1.0 / h_[eR] - 1.0 / h_[eL]);
        J.add(3 * i, 3 * (i + 1), 1.0 / h_[eR]);
        J.add(3 * i, 3 * (i - 1), 1.0 / h_[eL]);
        if (models_.incomplete_ion) {
            J.add(3 * i, 3 * i + 1, -dV_[i] * (1.0 - dcden[i]));
            J.add(3 * i, 3 * i + 2, dV_[i] * (1.0 + dcdp[i]));
        } else {
            J.add(3 * i, 3 * i + 1, -dV_[i]);
            J.add(3 * i, 3 * i + 2, dV_[i]);
        }

        // electron continuity
        out.F[3 * i + 1] = Jn[eR] - Jn[eL] - Rs[i] * dV_[i];
        J.add(3 * i + 1, 3 * i + 1,
              -an[eR] * Bm[eR] - an[eL] * Bp[eL] - dRs_dn[i] * dV_[i]);
        J.add(3 * i + 1, 3 * (i + 1) + 1, an[eR] * Bp[eR]);
        J.add(3 * i + 1, 3 * (i - 1) + 1, an[eL] * Bm[eL]);
        J.add(3 * i + 1, 3 * i + 2, -dRs_dp[i] * dV_[i]);
        J.add(3 * i + 1, 3 * i, -dJn_dpsiR[eR] - dJn_dpsiR[eL]);
        J.add(3 * i + 1, 3 * (i + 1), dJn_dpsiR[eR]);
        J.add(3 * i + 1, 3 * (i - 1), dJn_dpsiR[eL]);
        if (fd) {
            // M13: density columns gain the d(delta_tilde)/dn chain --
            // device.py's own `if fd:` block right after the electron
            // continuity Jacobian.
            J.add(3 * i + 1, 3 * i + 1,
                  -wn[i] * (an[eR] * Sn[eR] + an[eL] * Sn[eL]));
            J.add(3 * i + 1, 3 * (i + 1) + 1, an[eR] * Sn[eR] * wn[i + 1]);
            J.add(3 * i + 1, 3 * (i - 1) + 1, an[eL] * Sn[eL] * wn[i - 1]);
        }

        // hole continuity
        out.F[3 * i + 2] = Jp[eR] - Jp[eL] + Rs[i] * dV_[i];
        J.add(3 * i + 2, 3 * i + 2,
              ap[eR] * Bp_h[eR] + ap[eL] * Bm_h[eL] + dRs_dp[i] * dV_[i]);
        J.add(3 * i + 2, 3 * (i + 1) + 2, -ap[eR] * Bm_h[eR]);
        J.add(3 * i + 2, 3 * (i - 1) + 2, -ap[eL] * Bp_h[eL]);
        J.add(3 * i + 2, 3 * i + 1, dRs_dn[i] * dV_[i]);
        J.add(3 * i + 2, 3 * i, -dJp_dpsiR[eR] - dJp_dpsiR[eL]);
        J.add(3 * i + 2, 3 * (i + 1), dJp_dpsiR[eR]);
        J.add(3 * i + 2, 3 * (i - 1), dJp_dpsiR[eL]);
        if (fd) {
            // M13 hole mirror: opposite sign to the electron block,
            // matching device.py's own comment there.
            J.add(3 * i + 2, 3 * i + 2,
                  wp[i] * (ap[eR] * Sp[eR] + ap[eL] * Sp[eL]));
            J.add(3 * i + 2, 3 * (i + 1) + 2, -ap[eR] * Sp[eR] * wp[i + 1]);
            J.add(3 * i + 2, 3 * (i - 1) + 2, -ap[eL] * Sp[eL] * wp[i - 1]);
        }
    }

    // --- M15 impact ionization / M16 local BTBT (device.py's blocks
    // after both continuity rows, before Dirichlet stamping; interior
    // nodes only). Node field E_i = 0.5 (e_{i-1} + e_i), e_k = |dpsi_k|
    // c_k with c_k = VT/(LD h_k); its psi-chain is shared by both.
    if (models_.impact || models_.btbt) {
        std::vector<double> c_edge(Ne), e_mag(Ne), s_edge(Ne);
        for (int k = 0; k < Ne; ++k) {
            c_edge[k] = VT_ / (LD_ * h_[k]);
            const double d = psi[k + 1] - psi[k];
            e_mag[k] = std::abs(d) * c_edge[k];
            s_edge[k] = (d > 0.0) ? 1.0 : ((d < 0.0) ? -1.0 : 0.0);
        }
        std::vector<double> E_node(N);
        E_node[0] = e_mag[0];
        E_node[N - 1] = e_mag[Ne - 1];
        for (int i = 1; i < N - 1; ++i) E_node[i] = 0.5 * (e_mag[i - 1] + e_mag[i]);
        // d(E_i)/d psi_{i-1}, psi_i, psi_{i+1}
        auto dEi = [&](int i, double& L, double& M, double& R) {
            L = 0.5 * (-s_edge[i - 1] * c_edge[i - 1]);
            M = 0.5 * (s_edge[i - 1] * c_edge[i - 1] + (-s_edge[i] * c_edge[i]));
            R = 0.5 * (s_edge[i] * c_edge[i]);
        };

        if (models_.impact) {
            // _ii_compute_gs_frozen + the R1b analytic Jacobian. Jn/Jp
            // here equal the SG currents _ii_compute_gs_frozen rebuilds
            // (homojunction, no thermionic: identical deltas), so one
            // j_eps serves value and derivative alike, as device.py
            // requires.
            double jmax = 1e-300;
            for (int k = 0; k < Ne; ++k)
                jmax = std::max({jmax, std::abs(Jn[k]), std::abs(Jp[k])});
            // numpy: max(max|Jn|, max|Jp|, 1e-300) -- same value.
            const double j_eps = kIiJEpsRel * jmax;
            const double e2 = j_eps * j_eps;
            std::vector<double> aJn(Ne), aJp(Ne), sgn_n(Ne), sgn_p(Ne);
            for (int k = 0; k < Ne; ++k) {
                const double rn = std::sqrt(Jn[k] * Jn[k] + e2);
                const double rp = std::sqrt(Jp[k] * Jp[k] + e2);
                aJn[k] = rn * J0_;
                aJp[k] = rp * J0_;
                sgn_n[k] = Jn[k] / rn;
                sgn_p[k] = Jp[k] / rp;
            }
            const double Kgen = 0.5 / (ph::kIiQ * R0_);
            out.ii_gs.assign(N, 0.0);
            std::vector<double> gs_full(N);
            for (int j = 0; j < N; ++j) {
                double Snj, Spj;
                if (j == 0) { Snj = aJn[0]; Spj = aJp[0]; }
                else if (j == N - 1) { Snj = aJn[Ne - 1]; Spj = aJp[Ne - 1]; }
                else { Snj = aJn[j - 1] + aJn[j]; Spj = aJp[j - 1] + aJp[j]; }
                gs_full[j] = Kgen * (ph::ii_alpha(ph::kAlphaN, E_node[j]) * Snj +
                                     ph::ii_alpha(ph::kAlphaP, E_node[j]) * Spj);
                out.ii_gs[j] = gs_full[j] * strength;
            }
            // per-edge current partials (the continuity Jacobian's own)
            std::vector<double> dJn_dn_L(Ne), dJn_dn_R(Ne), dJp_dp_L(Ne), dJp_dp_R(Ne);
            for (int k = 0; k < Ne; ++k) {
                dJn_dn_L[k] = -an[k] * Bm[k];
                dJn_dn_R[k] = an[k] * Bp[k];
                dJp_dp_L[k] = ap[k] * Bp_h[k];
                dJp_dp_R[k] = -ap[k] * Bm_h[k];
                if (fd) {
                    dJn_dn_L[k] = dJn_dn_L[k] - an[k] * Sn[k] * wn[k];
                    dJn_dn_R[k] = dJn_dn_R[k] + an[k] * Sn[k] * wn[k + 1];
                    dJp_dp_L[k] = dJp_dp_L[k] + ap[k] * Sp[k] * wp[k];
                    dJp_dp_R[k] = dJp_dp_R[k] - ap[k] * Sp[k] * wp[k + 1];
                }
            }
            for (int i = 1; i < N - 1; ++i) {
                const int eL = i - 1, eR = i;
                out.F[3 * i + 1] += strength * gs_full[i] * dV_[i];
                out.F[3 * i + 2] -= strength * gs_full[i] * dV_[i];

                const double an_i = ph::ii_alpha(ph::kAlphaN, E_node[i]);
                const double ap_i = ph::ii_alpha(ph::kAlphaP, E_node[i]);
                const double dan_i = ph::ii_dalpha_dE(ph::kAlphaN, E_node[i]);
                const double dap_i = ph::ii_dalpha_dE(ph::kAlphaP, E_node[i]);
                double dEL, dEM, dER;
                dEi(i, dEL, dEM, dER);
                const double Sn_i = aJn[eL] + aJn[eR];
                const double Sp_i = aJp[eL] + aJp[eR];

                const double dSn_psi_L = sgn_n[eL] * (-dJn_dpsiR[eL]) * J0_;
                const double dSn_psi_M = (sgn_n[eL] * dJn_dpsiR[eL] +
                                          sgn_n[eR] * (-dJn_dpsiR[eR])) * J0_;
                const double dSn_psi_R = sgn_n[eR] * dJn_dpsiR[eR] * J0_;
                const double dSn_n_L = sgn_n[eL] * dJn_dn_L[eL] * J0_;
                const double dSn_n_M = (sgn_n[eL] * dJn_dn_R[eL] +
                                        sgn_n[eR] * dJn_dn_L[eR]) * J0_;
                const double dSn_n_R = sgn_n[eR] * dJn_dn_R[eR] * J0_;

                const double dSp_psi_L = sgn_p[eL] * (-dJp_dpsiR[eL]) * J0_;
                const double dSp_psi_M = (sgn_p[eL] * dJp_dpsiR[eL] +
                                          sgn_p[eR] * (-dJp_dpsiR[eR])) * J0_;
                const double dSp_psi_R = sgn_p[eR] * dJp_dpsiR[eR] * J0_;
                const double dSp_p_L = sgn_p[eL] * dJp_dp_L[eL] * J0_;
                const double dSp_p_M = (sgn_p[eL] * dJp_dp_R[eL] +
                                        sgn_p[eR] * dJp_dp_L[eR]) * J0_;
                const double dSp_p_R = sgn_p[eR] * dJp_dp_R[eR] * J0_;

                auto dG_dpsi = [&](double dE, double dSn, double dSp) {
                    return Kgen * (dan_i * dE * Sn_i + an_i * dSn +
                                   dap_i * dE * Sp_i + ap_i * dSp);
                };
                const double g_psi_L = strength * dG_dpsi(dEL, dSn_psi_L, dSp_psi_L);
                const double g_psi_M = strength * dG_dpsi(dEM, dSn_psi_M, dSp_psi_M);
                const double g_psi_R = strength * dG_dpsi(dER, dSn_psi_R, dSp_psi_R);
                const double g_n_L = strength * Kgen * an_i * dSn_n_L;
                const double g_n_M = strength * Kgen * an_i * dSn_n_M;
                const double g_n_R = strength * Kgen * an_i * dSn_n_R;
                const double g_p_L = strength * Kgen * ap_i * dSp_p_L;
                const double g_p_M = strength * Kgen * ap_i * dSp_p_M;
                const double g_p_R = strength * Kgen * ap_i * dSp_p_R;

                const double dVi = dV_[i];
                for (int sgn_row = 0; sgn_row < 2; ++sgn_row) {
                    const int row = 3 * i + 1 + sgn_row;          // e row, then h row
                    const double s = sgn_row == 0 ? dVi : -dVi;
                    J.add(row, 3 * (i - 1), s * g_psi_L);
                    J.add(row, 3 * i, s * g_psi_M);
                    J.add(row, 3 * (i + 1), s * g_psi_R);
                    J.add(row, 3 * (i - 1) + 1, s * g_n_L);
                    J.add(row, 3 * i + 1, s * g_n_M);
                    J.add(row, 3 * (i + 1) + 1, s * g_n_R);
                    J.add(row, 3 * (i - 1) + 2, s * g_p_L);
                    J.add(row, 3 * i + 2, s * g_p_M);
                    J.add(row, 3 * (i + 1) + 2, s * g_p_R);
                }
            }
        }

        if (models_.btbt) {
            // G_i = A E_i^2 exp(-B/E_i), dependence through E(psi) only.
            out.btbt_gs.assign(N, 0.0);
            for (int j = 0; j < N; ++j)
                out.btbt_gs[j] = ph::btbt_generation(E_node[j]) * strength;
            for (int i = 1; i < N - 1; ++i) {
                const double G = ph::btbt_generation(E_node[i]);
                const double dGs = ph::dbtbt_dF(E_node[i]) / R0_ * strength;
                double dEL, dEM, dER;
                dEi(i, dEL, dEM, dER);
                out.F[3 * i + 1] += strength * G / R0_ * dV_[i];
                out.F[3 * i + 2] -= strength * G / R0_ * dV_[i];
                const double dVi = dV_[i];
                J.add(3 * i + 1, 3 * (i - 1), dVi * (dGs * dEL));
                J.add(3 * i + 1, 3 * i, dVi * (dGs * dEM));
                J.add(3 * i + 1, 3 * (i + 1), dVi * (dGs * dER));
                J.add(3 * i + 2, 3 * (i - 1), -dVi * (dGs * dEL));
                J.add(3 * i + 2, 3 * i, -dVi * (dGs * dEM));
                J.add(3 * i + 2, 3 * (i + 1), -dVi * (dGs * dER));
            }
        }
    }

    // Dirichlet contacts, S_n = S_p = 0 (baseline: plain clamp, bit-
    // identical pre-M14 row, per device.py's own comment on that branch).
    for (int side = 0; side < 2; ++side) {
        const int node = (side == 0) ? 0 : N - 1;
        const ContactBC& c = bc[static_cast<std::size_t>(side)];
        out.F[3 * node] = psi[node] - c.psi0;
        J.add(3 * node, 3 * node, 1.0);
        out.F[3 * node + 1] = n[node] - c.n0;
        J.add(3 * node + 1, 3 * node + 1, 1.0);
        out.F[3 * node + 2] = p[node] - c.p0;
        J.add(3 * node + 2, 3 * node + 2, 1.0);
    }

    return out;
}

std::pair<bool, double> Device1D::newton(const std::array<ContactBC, 2>& bc,
                                         const NewtonOptions& opts, double strength,
                                         bool stiff,
                                         std::vector<double>& psi,
                                         std::vector<double>& n,
                                         std::vector<double>& p) const {
    // device.py's solve_bias._newton. `stiff` == stiff_gen == floored
    // for the ported flags (the M34 nonlocal flags that make `floored`
    // differ from `stiff_gen` are not ported).
    const int N = N_;
    double err = std::numeric_limits<double>::infinity();
    for (int it = 0; it < opts.max_iter; ++it) {
        ResidualJacobian rj = residual_jacobian(psi, n, p, bc, Pn_, Pp_, strength);
        std::vector<double> rhs(3 * N);
        for (int k = 0; k < 3 * N; ++k) rhs[k] = -rj.F[k];
        eliminate_dirichlet(rj.J, rhs, dirichlet_rows_);
        std::vector<double> du =
            tcad::solver::solve_direct_lu(rj.J.rows, rj.J.cols, rj.J.vals, 3 * N, rhs);

        std::vector<double> dpsi(N), dn(N), dp(N), n_new(N), p_new(N);
        double max_dpsi = 0.0, rel_n = 0.0, rel_p = 0.0;
        for (int i = 0; i < N; ++i) {
            dpsi[i] = clip(du[3 * i], -opts.max_dpsi, opts.max_dpsi);
            dn[i] = du[3 * i + 1];
            dp[i] = du[3 * i + 2];
            n_new[i] = clip(n[i] + dn[i], 0.1 * n[i], 10.0 * n[i]);
            p_new[i] = clip(p[i] + dp[i], 0.1 * p[i], 10.0 * p[i]);
            max_dpsi = std::max(max_dpsi, std::abs(dpsi[i]));
            if (stiff) {
                // M34-S7: full correction against the stiff density floor
                rel_n = std::max(rel_n, std::abs(n_new[i] - n[i]) /
                                            std::max(n[i], kStiffDensityFloor));
                rel_p = std::max(rel_p, std::abs(p_new[i] - p[i]) /
                                            std::max(p[i], kStiffDensityFloor));
            } else {
                rel_n = std::max(rel_n, std::abs(n_new[i] / std::max(n[i], 1e-300) - 1.0));
                rel_p = std::max(rel_p, std::abs(p_new[i] / std::max(p[i], 1e-300) - 1.0));
            }
        }
        err = std::max({max_dpsi, rel_n, rel_p});

        if (stiff && err >= kLsNewtonRegion) {
            // M15 backtracking on the 2-norm merit of the FULL residual
            // (contact rows included, as device.py's F is).
            double base = 0.0;
            for (double f : rj.F) base += f * f;
            base *= 0.5;
            double lam = 1.0;
            bool accepted = false;
            std::vector<double> psi_t(N), n_t(N), p_t(N);
            for (int h = 0; h < kLsMaxHalvings + 1; ++h) {
                for (int i = 0; i < N; ++i) {
                    psi_t[i] = psi[i] + lam * dpsi[i];
                    n_t[i] = clip(n[i] + lam * dn[i], 0.1 * n[i], 10.0 * n[i]);
                    p_t[i] = clip(p[i] + lam * dp[i], 0.1 * p[i], 10.0 * p[i]);
                }
                const ResidualJacobian rt =
                    residual_jacobian(psi_t, n_t, p_t, bc, Pn_, Pp_, strength);
                double ft = 0.0;
                for (double f : rt.F) ft += f * f;
                ft *= 0.5;
                if (std::isfinite(ft) && ft <= base * (1.0 - 1e-4 * lam)) {
                    accepted = true;
                    break;
                }
                lam *= 0.5;
            }
            // No trial reduced the merit: take the full step (M34-S7).
            if (!accepted) lam = 1.0;
            for (int i = 0; i < N; ++i) {
                psi[i] = psi[i] + lam * dpsi[i];
                n_new[i] = clip(n[i] + lam * dn[i], 0.1 * n[i], 10.0 * n[i]);
                p_new[i] = clip(p[i] + lam * dp[i], 0.1 * p[i], 10.0 * p[i]);
            }
        } else {
            for (int i = 0; i < N; ++i) psi[i] += dpsi[i];
        }
        n = n_new;
        p = p_new;
        if (err < opts.tol_update) return {true, err};
    }
    return {false, err};
}

bool Device1D::solve_bias(std::array<double, 2> V, const NewtonOptions& opts) {
    const ContactBC bc0 = contact_value(0, V[0]);
    const ContactBC bc1 = contact_value(1, V[1]);
    if (!has_solution_) solve_equilibrium(opts);

    std::vector<double> psi = psi_, n = n_, p = p_;
    psi[0] = bc0.psi0;
    n[0] = bc0.n0;
    p[0] = bc0.p0;
    psi[N_ - 1] = bc1.psi0;
    n[N_ - 1] = bc1.n0;
    p[N_ - 1] = bc1.p0;

    // M12-S2: probabilities FROZEN for this whole call, from the field
    // of the first iterate (device.py resets _Pn/_Pp at the top of
    // solve_bias and its first _residual_jacobian call recomputes them
    // from exactly this contact-stamped psi).
    if (models_.tat) {
        tat_probabilities(psi, Pn_, Pp_);
    } else {
        Pn_.clear();
        Pp_.clear();
    }

    // Generation-strength continuation (device.py's stage loop): each
    // stage a full Newton solve warm-started from the previous one,
    // stopping at the first stage that fails to converge. Non-stiff
    // configurations run the single stage 1.0.
    const bool stiff = models_.impact || models_.btbt;
    bool converged = false;
    double err = std::numeric_limits<double>::infinity();
    if (stiff) {
        for (double stage : kIiStages) {
            ii_strength_ = stage;
            std::tie(converged, err) = newton({bc0, bc1}, opts, stage, true, psi, n, p);
            if (!converged) break;
        }
    } else {
        ii_strength_ = 1.0;
        std::tie(converged, err) = newton({bc0, bc1}, opts, 1.0, false, psi, n, p);
    }

    psi_ = psi;
    n_ = n;
    p_ = p;
    ResidualJacobian rj =
        residual_jacobian(psi_, n_, p_, {bc0, bc1}, Pn_, Pp_, ii_strength_);
    Jn_scaled_ = rj.Jn;
    Jp_scaled_ = rj.Jp;
    ii_gs_cache_ = std::move(rj.ii_gs);
    btbt_gs_cache_ = std::move(rj.btbt_gs);
    last_converged_ = converged;
    last_newton_err_ = err;
    has_solution_ = true;
    return converged;
}

std::tuple<std::vector<double>, std::vector<std::int64_t>,
          std::vector<std::int64_t>, std::vector<double>>
Device1D::residual_jacobian_for_test(std::span<const double> psi,
                                     std::span<const double> n,
                                     std::span<const double> p, double V_left,
                                     double V_right,
                                     std::span<const double> Pn_in,
                                     std::span<const double> Pp_in,
                                     double strength) const {
    const ContactBC bc0 = contact_value(0, V_left);
    const ContactBC bc1 = contact_value(1, V_right);
    std::vector<double> psi_v(psi.begin(), psi.end());
    // TAT probabilities, in priority order: explicitly supplied (the
    // FD-Jacobian gate freezes them itself, since realistic fields
    // underflow P to 0 and would leave the TAT branch untested), else
    // the last solve_bias's frozen ones (what device.py's
    // _residual_jacobian reads after a solve), else computed from this
    // psi -- device.py's own lazy `if self._Pn is None` fill.
    std::vector<double> Pn, Pp;
    if (!Pn_in.empty() || !Pp_in.empty()) {
        if (Pn_in.size() != static_cast<std::size_t>(N_) ||
            Pp_in.size() != static_cast<std::size_t>(N_))
            throw tcad::InvalidArgument("Device1D: Pn/Pp must both have length N");
        Pn.assign(Pn_in.begin(), Pn_in.end());
        Pp.assign(Pp_in.begin(), Pp_in.end());
    } else {
        Pn = Pn_;
        Pp = Pp_;
        if (models_.tat && Pn.empty()) tat_probabilities(psi_v, Pn, Pp);
    }
    ResidualJacobian rj = residual_jacobian(
        psi_v, std::vector<double>(n.begin(), n.end()),
        std::vector<double>(p.begin(), p.end()), {bc0, bc1}, Pn, Pp, strength);
    return {std::move(rj.F), std::move(rj.J.rows), std::move(rj.J.cols),
           std::move(rj.J.vals)};
}

void Device1D::set_state(std::span<const double> psi, std::span<const double> n,
                         std::span<const double> p) {
    if (psi.size() != static_cast<std::size_t>(N_) ||
        n.size() != static_cast<std::size_t>(N_) ||
        p.size() != static_cast<std::size_t>(N_))
        throw tcad::InvalidArgument("Device1D.set_state: psi/n/p must have length N");
    psi_.assign(psi.begin(), psi.end());
    n_.assign(n.begin(), n.end());
    p_.assign(p.begin(), p.end());
    has_solution_ = true;
}

std::vector<double> Device1D::psi_V() const {
    std::vector<double> out(psi_.size());
    for (std::size_t i = 0; i < psi_.size(); ++i) out[i] = psi_[i] * VT_;
    return out;
}

std::vector<double> Device1D::n_cm3() const {
    std::vector<double> out(n_.size());
    for (std::size_t i = 0; i < n_.size(); ++i) out[i] = n_[i] * Ns_;
    return out;
}

std::vector<double> Device1D::p_cm3() const {
    std::vector<double> out(p_.size());
    for (std::size_t i = 0; i < p_.size(); ++i) out[i] = p_[i] * Ns_;
    return out;
}

std::vector<double> Device1D::E_field() const {
    std::vector<double> out(N_ - 1);
    for (int k = 0; k < N_ - 1; ++k)
        out[k] = -(psi_[k + 1] - psi_[k]) * VT_ / (h_[k] * LD_);
    return out;
}

std::vector<double> Device1D::Jn() const {
    std::vector<double> out(Jn_scaled_.size());
    for (std::size_t k = 0; k < out.size(); ++k) out[k] = Jn_scaled_[k] * J0_;
    return out;
}

std::vector<double> Device1D::Jp() const {
    std::vector<double> out(Jp_scaled_.size());
    for (std::size_t k = 0; k < out.size(); ++k) out[k] = Jp_scaled_[k] * J0_;
    return out;
}

std::pair<double, double> Device1D::current_density() const {
    std::vector<double> Jt(Jn_scaled_.size());
    double sum = 0.0;
    for (std::size_t k = 0; k < Jt.size(); ++k) {
        Jt[k] = (Jn_scaled_[k] + Jp_scaled_[k]) * J0_;
        sum += Jt[k];
    }
    const double mean = sum / static_cast<double>(Jt.size());
    double var = 0.0;
    for (double v : Jt) var += (v - mean) * (v - mean);
    const double stddev = std::sqrt(var / static_cast<double>(Jt.size()));
    return {mean, stddev / (std::abs(mean) + 1e-30)};
}

}  // namespace tcad::device1d
