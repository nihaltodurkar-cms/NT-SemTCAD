// Transcription of device.py's Device1D, baseline-model slice only --
// see device1d.hpp's file header. Every formula below is a direct
// line-for-line port of the corresponding Python; comments cite the
// Python method they came from so a future Phase 2 diff is easy to
// place.
#include "tcad/device1d/device1d.hpp"

#include <algorithm>
#include <charconv>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

#include "tcad/base/errors.hpp"
#include "tcad/physics/fermi.hpp"
#include "tcad/physics/generation.hpp"
#include "tcad/physics/kernels.hpp"
#include "tcad/nonlocal/evaluate.hpp"
#include "tcad/solver/direct_lu.hpp"

namespace tcad::device1d {

namespace {
constexpr double kQ = 1.602176634e-19;   // constants.Q
constexpr double kD0Ref = 1.0;           // kernels.D0_REF

double clip(double v, double lo, double hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}

/// Python's f"{v:.3e}" (C's %.3e, but "inf"/"-inf"/"nan" spelled as
/// Python does -- MSVC's printf would write "-nan(ind)").
std::string py_e3(double v) {
    if (std::isnan(v)) return "nan";
    if (std::isinf(v)) return v > 0 ? "inf" : "-inf";
    char buf[32];
    std::snprintf(buf, sizeof buf, "%.3e", v);
    return buf;
}

/// Python's f"{v}" (repr) for a float: shortest round-trip digits, with
/// ".0" appended to an integral value (the ladder stage 1.0 -> "1.0").
std::string py_repr(double v) {
    if (std::isnan(v)) return "nan";
    if (std::isinf(v)) return v > 0 ? "inf" : "-inf";
    char buf[40];
    const auto r = std::to_chars(buf, buf + sizeof buf, v);
    std::string s(buf, r.ptr);
    if (s.find_first_of(".en") == std::string::npos) s += ".0";
    return s;
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
                   std::span<const double> tat_kn, std::span<const double> tat_kp,
                   NonlocalParams nl, Extras ex)
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
      tat_kn_(tat_kn.begin(), tat_kn.end()), tat_kp_(tat_kp.begin(), tat_kp.end()),
      nl_(nl), ex_(std::move(ex)) {
    if (models_.impact_nonlocal &&
        (!models_.impact || !(nl_.lambda_n_cm > 0.0) || !(nl_.lambda_p_cm > 0.0)))
        throw tcad::InvalidArgument(
            "Device1D: impact_nonlocal requires impact and lambda_n/lambda_p > 0");
    if (models_.btbt_nonlocal &&
        (!(nl_.Eg_J > 0.0) || !(nl_.mr_kg > 0.0) || !(nl_.u > 1.0) || !(nl_.Eg_eV > 0.0)))
        throw tcad::InvalidArgument(
            "Device1D: btbt_nonlocal requires Eg/mass parameters (u > 1)");
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
    Ns_ = ex_.Ns > 0.0 ? ex_.Ns : std::max(doping_max_abs, ni_);
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

    // --- Phase 2 slice 5: homojunction defaults for the empty Extras
    // arrays (et = 1, band_shift = 0, the scalar Auger coefficients),
    // so the earlier slices' arithmetic is unchanged by their existence.
    const auto sized = [&](const std::vector<double>& v, std::size_t m, const char* what) {
        if (!v.empty() && v.size() != m)
            throw tcad::InvalidArgument(std::string("Device1D: Extras.") + what +
                                        " has the wrong length");
        return !v.empty();
    };
    const std::size_t Nn = static_cast<std::size_t>(N_), Ne = Nn - 1;
    et_ = sized(ex_.et, Ne, "et") ? ex_.et : std::vector<double>(Ne, 1.0);
    band_shift_ = sized(ex_.band_shift, Nn, "band_shift") ? ex_.band_shift
                                                          : std::vector<double>(Nn, 0.0);
    Cn_node_ = sized(ex_.Cn_auger, Nn, "Cn_auger") ? ex_.Cn_auger
                                                   : std::vector<double>(Nn, Cn_auger_);
    Cp_node_ = sized(ex_.Cp_auger, Nn, "Cp_auger") ? ex_.Cp_auger
                                                   : std::vector<double>(Nn, Cp_auger_);
    if (models_.thermionic) {
        const bool ok = sized(ex_.te_edge, Ne, "te_edge") && sized(ex_.te_Kn, Ne, "te_Kn") &&
                        sized(ex_.te_Kp, Ne, "te_Kp") && sized(ex_.te_dlnNc, Ne, "te_dlnNc") &&
                        sized(ex_.te_dlnNv, Ne, "te_dlnNv") && sized(ex_.te_rNc, Ne, "te_rNc") &&
                        sized(ex_.te_rNv, Ne, "te_rNv");
        if (!ok) throw tcad::InvalidArgument("Device1D: thermionic needs the te_* edge arrays");
    }
    if ((models_.field_mobility || models_.energy_balance) &&
        !(ex_.vsat_n > 0.0 && ex_.beta_n > 0.0 && ex_.vsat_p > 0.0 && ex_.beta_p > 0.0))
        throw tcad::InvalidArgument("Device1D: field mobility needs vsat/beta > 0");
    if (models_.energy_balance && !(ex_.tau_w > 0.0 && ex_.kB > 0.0 && ex_.q_hydro > 0.0))
        throw tcad::InvalidArgument("Device1D: energy_balance needs tau_w/kB/q");
    if (models_.dg && !(sized(ex_.m_n_star, Nn, "m_n_star") &&
                        sized(ex_.m_p_star, Nn, "m_p_star") && ex_.dg_hbar > 0.0 &&
                        ex_.dg_m0 > 0.0 && ex_.dg_q > 0.0))
        throw tcad::InvalidArgument("Device1D: dg needs m_n_star/m_p_star and hbar/m0/q");
}

std::vector<double> Device1D::lu_solve(const Coo& J, std::int64_t M,
                                       const std::vector<double>& rhs) const {
    if (!collect_stats_) return tcad::solver::solve_direct_lu(J.rows, J.cols, J.vals, M, rhs);
    std::vector<std::int64_t> keys(J.rows.size());
    for (std::size_t k = 0; k < keys.size(); ++k) keys[k] = J.rows[k] * M + J.cols[k];
    std::sort(keys.begin(), keys.end());
    const long long nnz = std::unique(keys.begin(), keys.end()) - keys.begin();
    stats_.dof = std::max<long long>(stats_.dof, M);
    stats_.nnz = std::max(stats_.nnz, nnz);
    const auto t0 = std::chrono::steady_clock::now();
    struct Done {  // counted even when the solve throws (singular)
        Stats& s; std::chrono::steady_clock::time_point t0;
        ~Done() {
            s.linsolve_s += std::chrono::duration<double>(
                std::chrono::steady_clock::now() - t0).count();
            s.linsolve_calls += 1;
        }
    } done{stats_, t0};
    return tcad::solver::solve_direct_lu(J.rows, J.cols, J.vals, M, rhs);
}

Device1D::ResidualJacobian Device1D::assemble(
    const std::vector<double>& psi, const std::vector<double>& n,
    const std::vector<double>& p, const std::array<ContactBC, 2>& bc, double strength,
    const EbLag* eb) const {
    if (!collect_stats_) return residual_jacobian(psi, n, p, bc, Pn_, Pp_, strength, nl_paths_, eb);
    const auto t0 = std::chrono::steady_clock::now();
    ResidualJacobian rj = residual_jacobian(psi, n, p, bc, Pn_, Pp_, strength, nl_paths_, eb);
    stats_.assembly_s +=
        std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    stats_.assembly_calls += 1;
    return rj;
}

void Device1D::set_Tn(std::span<const double> Tn) {
    if (!Tn.empty() && Tn.size() != static_cast<std::size_t>(N_))
        throw tcad::InvalidArgument("Device1D.set_Tn: Tn must have length N (or 0)");
    Tn_.assign(Tn.begin(), Tn.end());
}

void Device1D::set_edge_diffusivity(const std::vector<double>& mu_n,
                                    const std::vector<double>& mu_p) {
    // device.py's _set_edge_diffusivity: hmean(mu) * VT / D0_REF.
    const std::vector<double> hn = hmean(mu_n), hp = hmean(mu_p);
    for (std::size_t k = 0; k < hn.size(); ++k) {
        dn_edge_[k] = hn[k] * VT_ / kD0Ref;
        dp_edge_[k] = hp[k] * VT_ / kD0Ref;
    }
}

std::vector<double> Device1D::mobility_field(const std::vector<double>& mu0,
                                             const std::vector<double>& E,
                                             int carrier) const {
    // materials.mobility_field (Canali): mu0 / (1 + (mu0|E|/vsat)^beta)^(1/beta)
    const double vsat = carrier == 0 ? ex_.vsat_n : ex_.vsat_p;
    const double beta = carrier == 0 ? ex_.beta_n : ex_.beta_p;
    std::vector<double> out(mu0.size());
    for (std::size_t i = 0; i < mu0.size(); ++i) {
        const double x = mu0[i] * std::abs(E[i]) / vsat;
        out[i] = mu0[i] / std::pow(1.0 + std::pow(x, beta), 1.0 / beta);
    }
    return out;
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
    // device.py's _contact_values:
    //   ohmic: n0 - p0 = C, n0*p0 = nie^2  =>  n0 = 0.5*(C + sqrt(C^2+4nie^2))
    //   (majority carrier from the sqrt; minority from mass action);
    //   M46-S1 Schottky: the barrier-limited (n0, p0) evaluated in Python.
    const double Cc = C_[i], nie = nie_s_[i];
    double n0, p0;
    const std::size_t s = static_cast<std::size_t>(side);
    if (ex_.contact_override[s]) {
        n0 = ex_.contact_n0[s];
        p0 = ex_.contact_p0[s];
    } else {
        const double root = std::sqrt(Cc * Cc + 4.0 * nie * nie);
        if (Cc >= 0.0) {
            n0 = 0.5 * (Cc + root);
            p0 = nie * nie / n0;
        } else {
            p0 = 0.5 * (-Cc + root);
            n0 = nie * nie / p0;
        }
    }
    // M33-S1: n0/p0 are gauge-free; only psi0's reference moves, by -s[i].
    const double psi0 = (V / VT_ + std::log(n0 / nie)) - band_shift_[i];
    return {psi0, n0, p0};
}

double Device1D::fd_neutral_eta(int node) const {
    return tcad::physics::fd_neutral_eta_node(
        fermi_table_, nc_s_[node], nv_s_[node], eg_kt_[node], C_[node], ded_kt_,
        nd_arr_[node], na_arr_[node], models_.incomplete_ion);
}

void Device1D::effective_field(const std::vector<double>& psi, int carrier,
                               std::vector<double>& E, std::vector<double>* D) const {
    // ii_nonlocal.effective_field, line for line: exact per-edge
    // relaxation recursion E_down = a E_up + (1-a)|E_edge|, a =
    // exp(-h/lambda), along each edge's transport direction (strong
    // edges: drift direction; weak edges inherit the nearest strong
    // edge's), visited in Kahn topological order. E = W |E_edge|, so the
    // Jacobian is W times d|E_edge|/dpsi (direction pattern piecewise
    // constant -> no contribution).
    constexpr double kStrong = 1.0e2;  // ii_nonlocal.E_STRONG_VCM
    const int N = N_, Ne = N_ - 1;
    const double lam = carrier == 0 ? nl_.lambda_n_cm : nl_.lambda_p_cm;
    std::vector<double> h(Ne), dpsi(Ne), Emag(Ne), s(Ne), xm(Ne);
    std::vector<bool> strong(Ne);
    int n_strong = 0;
    for (int k = 0; k < Ne; ++k) {
        h[k] = x_[k + 1] - x_[k];
        dpsi[k] = psi[k + 1] - psi[k];
        Emag[k] = std::abs(dpsi[k]) * VT_ / h[k];
        const double sg = dpsi[k] > 0.0 ? 1.0 : (dpsi[k] < 0.0 ? -1.0 : 0.0);
        s[k] = carrier == 0 ? sg : -sg;
        strong[k] = Emag[k] >= kStrong;
        n_strong += strong[k] ? 1 : 0;
        xm[k] = 0.5 * (x_[k + 1] + x_[k]);
    }
    if (n_strong == 0) {
        std::fill(s.begin(), s.end(), 0.0);
    } else if (n_strong < Ne) {
        std::vector<int> si;
        std::vector<double> xs;
        for (int k = 0; k < Ne; ++k)
            if (strong[k]) { si.push_back(k); xs.push_back(xm[k]); }
        const std::vector<double> s0 = s;
        for (int k = 0; k < Ne; ++k) {
            if (strong[k]) continue;
            int near;
            if (si.size() > 1) {
                // np.searchsorted(side='left'), clipped to [1, len-1]
                int j = static_cast<int>(std::lower_bound(xs.begin(), xs.end(), xm[k]) - xs.begin());
                j = std::clamp(j, 1, static_cast<int>(si.size()) - 1);
                const int left = si[j - 1], right = si[j];
                near = std::abs(xm[k] - xm[left]) <= std::abs(xm[right] - xm[k]) ? left : right;
            } else {
                near = si[0];
            }
            s[k] = s0[near];
        }
    }
    std::vector<bool> fwd(Ne), bwd(Ne);
    std::vector<double> a(Ne);
    for (int k = 0; k < Ne; ++k) {
        fwd[k] = s[k] > 0.0;
        bwd[k] = s[k] < 0.0;
        a[k] = std::exp(-h[k] / lam);
    }
    // Kahn topological order (stack seeded in descending index order,
    // popped from the back -- same visiting order as the Python)
    std::vector<int> indeg(N, 0);
    for (int k = 0; k < Ne; ++k) {
        if (fwd[k]) indeg[k + 1] += 1;
        if (bwd[k]) indeg[k] += 1;
    }
    std::vector<int> stack, order;
    for (int v = N - 1; v >= 0; --v)
        if (indeg[v] == 0) stack.push_back(v);
    while (!stack.empty()) {
        const int v = stack.back();
        stack.pop_back();
        order.push_back(v);
        if (v < N - 1 && fwd[v] && --indeg[v + 1] == 0) stack.push_back(v + 1);
        if (v > 0 && bwd[v - 1] && --indeg[v - 1] == 0) stack.push_back(v - 1);
    }
    E.assign(N, 0.0);
    std::vector<double> W;
    if (D) W.assign(static_cast<std::size_t>(N) * Ne, 0.0);
    for (int v : order) {
        int ne = 0, ie[2], iu[2];
        if (v > 0 && fwd[v - 1]) { ie[ne] = v - 1; iu[ne] = v - 1; ++ne; }
        if (v < N - 1 && bwd[v]) { ie[ne] = v; iu[ne] = v + 1; ++ne; }
        if (ne == 0) continue;  // cold: E_eff = 0
        const double w = 1.0 / ne;
        for (int t = 0; t < ne; ++t) {
            const int e = ie[t], u = iu[t];
            E[v] += w * (a[e] * E[u] + (1.0 - a[e]) * Emag[e]);
            if (D) {
                double* Wv = &W[static_cast<std::size_t>(v) * Ne];
                const double* Wu = &W[static_cast<std::size_t>(u) * Ne];
                const double f = w * a[e];
                for (int c = 0; c < Ne; ++c) Wv[c] += f * Wu[c];
                Wv[e] += w * (1.0 - a[e]);
            }
        }
    }
    if (!D) return;
    D->assign(static_cast<std::size_t>(N) * N, 0.0);
    std::vector<double> g(Ne);
    for (int k = 0; k < Ne; ++k) {
        const double sg = dpsi[k] > 0.0 ? 1.0 : (dpsi[k] < 0.0 ? -1.0 : 0.0);
        g[k] = sg * VT_ / h[k];
    }
    for (int v = 0; v < N; ++v) {
        const double* Wv = &W[static_cast<std::size_t>(v) * Ne];
        double* Dv = &(*D)[static_cast<std::size_t>(v) * N];
        for (int e = 0; e < Ne; ++e) Dv[e + 1] += Wv[e] * g[e];
        for (int e = 0; e < Ne; ++e) Dv[e] -= Wv[e] * g[e];
    }
}

std::pair<std::vector<double>, std::vector<double>>
Device1D::effective_field_for_test(std::span<const double> psi, int carrier) const {
    if (!(nl_.lambda_n_cm > 0.0) || !(nl_.lambda_p_cm > 0.0))
        throw tcad::InvalidArgument("Device1D: effective field needs lambda_n/lambda_p > 0");
    if (psi.size() != static_cast<std::size_t>(N_))
        throw tcad::InvalidArgument("Device1D: psi must have length N");
    std::vector<double> E, D;
    effective_field(std::vector<double>(psi.begin(), psi.end()), carrier, E, &D);
    return {std::move(E), std::move(D)};
}

std::pair<std::vector<std::int64_t>, std::vector<std::int64_t>>
Device1D::locate_btbt_nl_paths(std::span<const double> psi) const {
    // device.py's _btbt_nl_build_paths (path location only; build_1d is
    // build_nl_paths below). Interior starts/crossings only -- the
    // Dirichlet stamping overwrites contact rows.
    const int N = N_;
    const double thr = nl_.Eg_eV / VT_;
    double psi_max = psi[0];
    for (int i = 1; i < N; ++i) psi_max = std::max(psi_max, psi[i]);
    std::vector<std::int64_t> starts, ends;
    for (int i0 = 1; i0 < N - 2; ++i0) {
        const double dpsi0 = psi[i0 + 1] - psi[i0];
        const double c_edge = VT_ / (LD_ * h_[i0]);
        if (dpsi0 <= 0.0 || dpsi0 * c_edge < 1.0e3) continue;
        if (psi_max - psi[i0] < thr) continue;
        int j = i0 + 1;
        while (j < N && psi[j] - psi[i0] < thr) ++j;
        if (j > N - 2) continue;
        int k = j;
        while (k < N - 2 && psi[k] - psi[i0] < 1.5 * thr) ++k;
        starts.push_back(i0);
        ends.push_back(k);
    }
    return {std::move(starts), std::move(ends)};
}

NlPaths Device1D::build_nl_paths(const std::vector<std::int64_t>& starts,
                                 const std::vector<std::int64_t>& ends) const {
    // nonlocal_path.build_1d with x_m = x * 1e-2 (cm -> m).
    NlPaths P;
    P.start = starts;
    P.end = ends;
    P.offset.push_back(0);
    for (std::size_t p = 0; p < starts.size(); ++p) {
        if (ends[p] <= starts[p] || starts[p] < 0 || ends[p] >= N_)
            throw tcad::InvalidArgument("Device1D: every 1D path needs at least one edge");
        for (std::int64_t v = starts[p]; v <= ends[p]; ++v) {
            P.sidx.push_back(v);
            P.swts.push_back(1.0);
            P.seg_len.push_back(v < ends[p] ? x_[v + 1] * 1e-2 - x_[v] * 1e-2 : 0.0);
        }
        P.offset.push_back(static_cast<std::int64_t>(P.sidx.size()));
        const double h0 = x_[starts[p] + 1] * 1e-2 - x_[starts[p]] * 1e-2;
        P.gidx.push_back(starts[p]);
        P.gidx.push_back(starts[p] + 1);
        P.gwts.push_back(-1.0 / h0);
        P.gwts.push_back(1.0 / h0);
    }
    return P;
}

void Device1D::set_btbt_nl_paths(std::span<const std::int64_t> starts,
                                 std::span<const std::int64_t> ends) {
    if (starts.size() != ends.size())
        throw tcad::InvalidArgument("Device1D: starts/ends length mismatch");
    nl_paths_ = build_nl_paths(std::vector<std::int64_t>(starts.begin(), starts.end()),
                               std::vector<std::int64_t>(ends.begin(), ends.end()));
    nl_paths_valid_ = true;
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
    // device.py's solve_equilibrium clears every per-bias cache and, with
    // energy_balance, sets Tn = T (zero current => theta == 1 exactly).
    Lam_n_.clear();
    Lam_p_.clear();
    if (models_.dg) {
        solve_equilibrium_dg(opts);
    } else if (models_.fd || models_.incomplete_ion) {
        solve_equilibrium_fd(opts);
    } else {
        solve_equilibrium_boltzmann(opts);
    }
    if (models_.energy_balance) Tn_.assign(static_cast<std::size_t>(N_), T_);
}

void Device1D::solve_equilibrium_boltzmann(const NewtonOptions& opts) {
    // device.py's solve_equilibrium, non-FD/non-incomplete_ion/non-dg
    // branch: Poisson-only Newton, carriers slaved to psi + band_shift
    // (M33-S1), permittivity-weighted edge flux et (M11-S3).
    const ContactBC bc0 = contact_value(0, 0.0);
    const ContactBC bc1 = contact_value(1, 0.0);

    std::vector<double> psi(N_);
    for (int i = 0; i < N_; ++i)
        psi[i] = std::asinh(C_[i] / (2.0 * nie_s_[i])) - band_shift_[i];
    psi[0] = bc0.psi0;
    psi[N_ - 1] = bc1.psi0;

    last_eq_converged_ = false;
    for (int it = 0; it < opts.max_iter; ++it) {
        std::vector<double> n(N_), p(N_), dnp(N_);
        for (int i = 0; i < N_; ++i) {
            const double psi_c = psi[i] + band_shift_[i];
            n[i] = nie_s_[i] * std::exp(clip(psi_c, -700.0, 700.0));
            p[i] = nie_s_[i] * std::exp(clip(-psi_c, -700.0, 700.0));
            dnp[i] = n[i] + p[i];
        }
        std::vector<double> F(N_, 0.0);
        Coo J;
        for (int i = 1; i < N_ - 1; ++i) {
            F[i] = et_[i] * (psi[i + 1] - psi[i]) / h_[i] -
                   et_[i - 1] * (psi[i] - psi[i - 1]) / h_[i - 1] -
                   dV_[i] * (n[i] - p[i] - C_[i]);
            J.add(i, i, -et_[i] / h_[i] - et_[i - 1] / h_[i - 1] - dV_[i] * dnp[i]);
            J.add(i, i + 1, et_[i] / h_[i]);
            J.add(i, i - 1, et_[i - 1] / h_[i - 1]);
        }
        F[0] = psi[0] - bc0.psi0;
        F[N_ - 1] = psi[N_ - 1] - bc1.psi0;
        J.add(0, 0, 1.0);
        J.add(N_ - 1, N_ - 1, 1.0);

        std::vector<double> rhs(N_);
        for (int i = 0; i < N_; ++i) rhs[i] = -F[i];
        std::vector<double> d =
            lu_solve(J, N_, rhs);
        double max_d = 0.0;
        for (int i = 0; i < N_; ++i) {
            d[i] = clip(d[i], -opts.max_dpsi, opts.max_dpsi);
            psi[i] += d[i];
            max_d = std::max(max_d, std::abs(d[i]));
        }
        if (opts.verbose && log_) {
            char head[32];
            std::snprintf(head, sizeof head, "    eq it %2d  |dpsi|=", it);
            log_(head + py_e3(max_d));
        }
        if (max_d < opts.tol_update) {
            last_eq_converged_ = true;
            break;
        }
    }

    psi_ = psi;
    n_.resize(N_);
    p_.resize(N_);
    // The same carrier law the loop solved with, n = nie exp(psi + s)
    // (device.py's 2026-09-28 fix; s == 0 in the nie gauge).
    for (int i = 0; i < N_; ++i) {
        const double psi_c = psi_[i] + band_shift_[i];
        n_[i] = nie_s_[i] * std::exp(clip(psi_c, -700.0, 700.0));
        p_[i] = nie_s_[i] * std::exp(clip(-psi_c, -700.0, 700.0));
    }
    has_solution_ = true;
}

void Device1D::solve_equilibrium_fd(const NewtonOptions& opts) {
    // device.py's solve_equilibrium, `if fd or ion:` branch. Poisson-
    // only Newton (still one unknown per node), but the density law
    // and its psi-derivative are chosen per models_.fd, and the
    // charge/derivative gain an incomplete-ionization correction when
    // models_.incomplete_ion. band_shift == 0 here (device.py refuses
    // the affinity gauge with fd/incomplete_ion); et carries eps(x).
    namespace ph = tcad::physics;
    std::vector<double> psi(N_);
    for (int i = 0; i < N_; ++i) psi[i] = fd_neutral_eta(i) + ln_gn_[i];
    const ContactBC bc0 = contact_value(0, 0.0);
    const ContactBC bc1 = contact_value(1, 0.0);
    psi[0] = bc0.psi0;
    psi[N_ - 1] = bc1.psi0;

    last_eq_converged_ = false;
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
            F[i] = et_[i] * (psi[i + 1] - psi[i]) / h_[i] -
                   et_[i - 1] * (psi[i] - psi[i - 1]) / h_[i - 1] -
                   dV_[i] * (n[i] - p[i] - c_eff[i]);
            J.add(i, i, -et_[i] / h_[i] - et_[i - 1] / h_[i - 1] - dV_[i] * dnp[i]);
            J.add(i, i + 1, et_[i] / h_[i]);
            J.add(i, i - 1, et_[i - 1] / h_[i - 1]);
        }
        F[0] = psi[0] - bc0.psi0;
        F[N_ - 1] = psi[N_ - 1] - bc1.psi0;
        J.add(0, 0, 1.0);
        J.add(N_ - 1, N_ - 1, 1.0);

        std::vector<double> rhs(N_);
        for (int i = 0; i < N_; ++i) rhs[i] = -F[i];
        std::vector<double> d =
            lu_solve(J, N_, rhs);
        double max_d = 0.0;
        for (int i = 0; i < N_; ++i) {
            d[i] = clip(d[i], -opts.max_dpsi, opts.max_dpsi);
            psi[i] += d[i];
            max_d = std::max(max_d, std::abs(d[i]));
        }
        if (opts.verbose && log_) {
            char head[32];
            std::snprintf(head, sizeof head, "    eq it %2d  |dpsi|=", it);
            log_(head + py_e3(max_d));
        }
        if (max_d < opts.tol_update) {
            last_eq_converged_ = true;
            break;
        }
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

std::pair<std::vector<double>, Coo> Device1D::dg_residual_jacobian(
    const std::vector<double>& psi, const std::vector<double>& Lam_n,
    const std::vector<double>& Lam_p, const std::array<ContactBC, 2>& bc,
    double gamma) const {
    // device.py's _dg_residual_jacobian_eq: unknowns interleaved
    // [psi_i, Lambda_n_i, Lambda_p_i]; psi Dirichlet at both ohmic
    // contacts, Lambda = 0 at both ends; the Lambda rows discretize
    // Lambda sqrt(n) + pref (sqrt n)'' in PHYSICAL cm.
    const int N = N_;
    const double VT = VT_;
    std::vector<double> n(N), p(N), dnp(N), rho(N), gn(N), gp(N), pref_n(N), pref_p(N);
    for (int i = 0; i < N; ++i) {
        const double e = clip(psi[i], -700.0, 700.0);
        n[i] = nie_s_[i] * std::exp(e) * std::exp(-Lam_n[i] / VT);
        p[i] = nie_s_[i] * std::exp(-e) * std::exp(-Lam_p[i] / VT);
        dnp[i] = n[i] + p[i];
        rho[i] = n[i] - p[i] - C_[i];
        gn[i] = std::sqrt(std::max(n[i], 1e-300));
        gp[i] = std::sqrt(std::max(p[i], 1e-300));
        // dg._dg_prefactor(m, gamma) * 1e4, same operation order
        pref_n[i] = gamma * ex_.dg_hbar * ex_.dg_hbar /
                    (2.0 * ex_.m_n_star[i] * ex_.dg_m0 * ex_.dg_q) * 1e4;
        pref_p[i] = gamma * ex_.dg_hbar * ex_.dg_hbar /
                    (2.0 * ex_.m_p_star[i] * ex_.dg_m0 * ex_.dg_q) * 1e4;
    }
    std::vector<double> F(3 * static_cast<std::size_t>(N), 0.0);
    Coo J;
    const auto ip = [](int i) { return 3 * i; };
    const auto iln = [](int i) { return 3 * i + 1; };
    const auto ilp = [](int i) { return 3 * i + 2; };

    F[ip(0)] = psi[0] - bc[0].psi0;
    J.add(ip(0), ip(0), 1.0);
    F[ip(N - 1)] = psi[N - 1] - bc[1].psi0;
    J.add(ip(N - 1), ip(N - 1), 1.0);
    for (int i = 1; i < N - 1; ++i) {
        F[ip(i)] = et_[i] * (psi[i + 1] - psi[i]) / h_[i] -
                   et_[i - 1] * (psi[i] - psi[i - 1]) / h_[i - 1] - dV_[i] * rho[i];
        J.add(ip(i), ip(i - 1), et_[i - 1] / h_[i - 1]);
        J.add(ip(i), ip(i), -et_[i] / h_[i] - et_[i - 1] / h_[i - 1] - dV_[i] * dnp[i]);
        J.add(ip(i), ip(i + 1), et_[i] / h_[i]);
        J.add(ip(i), iln(i), dV_[i] * n[i] / VT);
        J.add(ip(i), ilp(i), -dV_[i] * p[i] / VT);
    }
    for (int b = 0; b < 2; ++b) {
        for (int end = 0; end < 2; ++end) {
            const int node = end == 0 ? 0 : N - 1;
            const int r = b == 0 ? iln(node) : ilp(node);
            F[r] = (b == 0 ? Lam_n : Lam_p)[node];
            J.add(r, r, 1.0);
        }
    }
    for (int i = 1; i < N - 1; ++i) {
        const double hm = x_[i] - x_[i - 1], hp = x_[i + 1] - x_[i];  // np.diff(x)
        const double c0 = 2.0 / (hm + hp);
        for (int b = 0; b < 2; ++b) {
            const std::vector<double>& g = b == 0 ? gn : gp;
            const std::vector<double>& Lam = b == 0 ? Lam_n : Lam_p;
            const double pref = (b == 0 ? pref_n : pref_p)[i];
            const double sign = b == 0 ? 1.0 : -1.0;
            const auto idx = [&](int k) { return b == 0 ? iln(k) : ilp(k); };
            const double dd_i = c0 * ((g[i + 1] - g[i]) / hp - (g[i] - g[i - 1]) / hm);
            F[idx(i)] = Lam[i] * g[i] + pref * dd_i;

            const double dg_dpsi_i = sign * g[i] / 2.0;
            const double dg_dLam_i = -g[i] / (2.0 * VT);
            const double dg_dpsi_im1 = sign * g[i - 1] / 2.0;
            const double dg_dLam_im1 = -g[i - 1] / (2.0 * VT);
            const double dg_dpsi_ip1 = sign * g[i + 1] / 2.0;
            const double dg_dLam_ip1 = -g[i + 1] / (2.0 * VT);
            const double ddd_dgi = -c0 * (1.0 / hp + 1.0 / hm);
            const double ddd_dgim1 = c0 / hm;
            const double ddd_dgip1 = c0 / hp;

            J.add(idx(i), ip(i), Lam[i] * dg_dpsi_i + pref * ddd_dgi * dg_dpsi_i);
            J.add(idx(i), idx(i), g[i] + Lam[i] * dg_dLam_i + pref * ddd_dgi * dg_dLam_i);
            J.add(idx(i), ip(i - 1), pref * ddd_dgim1 * dg_dpsi_im1);
            J.add(idx(i), idx(i - 1), pref * ddd_dgim1 * dg_dLam_im1);
            J.add(idx(i), ip(i + 1), pref * ddd_dgip1 * dg_dpsi_ip1);
            J.add(idx(i), idx(i + 1), pref * ddd_dgip1 * dg_dLam_ip1);
        }
    }
    return {std::move(F), std::move(J)};
}

bool Device1D::dg_newton(std::vector<double>& psi, std::vector<double>& Lam_n,
                         std::vector<double>& Lam_p, const std::array<ContactBC, 2>& bc,
                         double gamma, int max_iter, double tol) const {
    // device.py's _dg_newton_solve_eq: never raises on a singular or
    // non-finite step, reports failure instead (state left untouched,
    // as the Python returns its inputs).
    const int N = N_;
    const std::int64_t M = 3 * static_cast<std::int64_t>(N);
    const std::array<std::int64_t, 6> drows = {0, 3 * (N - 1), 1, 2, 3 * (N - 1) + 1,
                                               3 * (N - 1) + 2};
    std::vector<double> psi_w = psi, ln_w = Lam_n, lp_w = Lam_p;
    for (int it = 0; it < max_iter; ++it) {
        auto [F, J] = dg_residual_jacobian(psi_w, ln_w, lp_w, bc, gamma);
        std::vector<double> rhs(static_cast<std::size_t>(M));
        for (std::int64_t k = 0; k < M; ++k) rhs[k] = -F[k];
        eliminate_dirichlet(J, rhs, drows);
        std::vector<double> d;
        try {
            d = lu_solve(J, M, rhs);
        } catch (const tcad::Error&) {
            return false;
        }
        for (double v : d)
            if (!std::isfinite(v)) return false;
        double err = 0.0;
        for (int i = 0; i < N; ++i) {
            const double dpsi = clip(d[3 * i], -5.0, 5.0);
            const double dln = clip(d[3 * i + 1], -10.0 * VT_, 10.0 * VT_);
            const double dlp = clip(d[3 * i + 2], -10.0 * VT_, 10.0 * VT_);
            psi_w[i] += dpsi;
            ln_w[i] += dln;
            lp_w[i] += dlp;
            err = std::max({err, std::abs(dpsi), std::abs(dln), std::abs(dlp)});
        }
        if (err < tol) {
            psi = psi_w;
            Lam_n = ln_w;
            Lam_p = lp_w;
            return true;
        }
    }
    return false;
}

void Device1D::solve_equilibrium_dg(const NewtonOptions& opts) {
    // device.py's _solve_equilibrium_dg_coupled: gamma continuation from
    // 0 with warm restarts; a failed stage inserts the midpoint before it
    // (at most 20 retries per stage).
    std::vector<double> psi(N_);
    for (int i = 0; i < N_; ++i) psi[i] = std::asinh(C_[i] / (2.0 * nie_s_[i]));
    const std::array<ContactBC, 2> bc = {contact_value(0, 0.0), contact_value(1, 0.0)};
    psi[0] = bc[0].psi0;
    psi[N_ - 1] = bc[1].psi0;
    std::vector<double> Lam_n(N_, 0.0), Lam_p(N_, 0.0);

    std::vector<double> stages = {0.0, 0.02, 0.05, 0.1, 0.2, 0.35, 0.5, 0.7, 1.0};
    std::size_t k = 0;
    int retries = 0;
    bool converged_final = true;
    while (k < stages.size()) {
        const double gamma_k = ex_.dg_gamma * stages[k];
        if (dg_newton(psi, Lam_n, Lam_p, bc, gamma_k, opts.max_iter, opts.tol_update)) {
            ++k;
            retries = 0;
            continue;
        }
        if (++retries > 20) {
            converged_final = false;
            break;
        }
        const double prev = k > 0 ? stages[k - 1] : 0.0;
        stages.insert(stages.begin() + static_cast<std::ptrdiff_t>(k),
                      0.5 * prev + 0.5 * stages[k]);
    }
    last_eq_converged_ = converged_final;

    psi_ = psi;
    Lam_n_ = Lam_n;
    Lam_p_ = Lam_p;
    n_.resize(N_);
    p_.resize(N_);
    for (int i = 0; i < N_; ++i) {
        n_[i] = nie_s_[i] * std::exp(clip(psi[i], -700.0, 700.0)) * std::exp(-Lam_n[i] / VT_);
        p_[i] = nie_s_[i] * std::exp(clip(-psi[i], -700.0, 700.0)) * std::exp(-Lam_p[i] / VT_);
    }
    has_solution_ = true;
}

std::tuple<std::vector<double>, std::vector<std::int64_t>,
          std::vector<std::int64_t>, std::vector<double>>
Device1D::dg_residual_jacobian_for_test(std::span<const double> psi,
                                        std::span<const double> Lam_n,
                                        std::span<const double> Lam_p,
                                        double gamma) const {
    if (!models_.dg) throw tcad::InvalidArgument("Device1D: DG hook needs models.dg");
    const std::size_t Nn = static_cast<std::size_t>(N_);
    if (psi.size() != Nn || Lam_n.size() != Nn || Lam_p.size() != Nn)
        throw tcad::InvalidArgument("Device1D: psi/Lam_n/Lam_p must have length N");
    auto [F, J] = dg_residual_jacobian(
        std::vector<double>(psi.begin(), psi.end()),
        std::vector<double>(Lam_n.begin(), Lam_n.end()),
        std::vector<double>(Lam_p.begin(), Lam_p.end()),
        {contact_value(0, 0.0), contact_value(1, 0.0)}, gamma);
    return {std::move(F), std::move(J.rows), std::move(J.cols), std::move(J.vals)};
}

Device1D::ResidualJacobian Device1D::residual_jacobian(
    const std::vector<double>& psi, const std::vector<double>& n,
    const std::vector<double>& p, const std::array<ContactBC, 2>& bc,
    const std::vector<double>& Pn, const std::vector<double>& Pp,
    double strength, const NlPaths& paths, const EbLag* eb) const {
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
        // M33-S1: the affinity gauge's shared edge term (0 in the nie gauge)
        const double ds = band_shift_[k + 1] - band_shift_[k];
        delta[k] = dpsi + dlnnie + ds;
        delta_p[k] = dpsi - dlnnie + ds;
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
        if (models_.thermionic && ex_.te_edge[k] != 0.0) {
            // M33-S2: on a material-change edge the SG flux is REPLACED
            // by the detailed-balanced emission-limited one, written into
            // the same five slots (device.py's `if self.models.thermionic`
            // block): Jn = K [n2 g2 - n1 g1], u = delta_n - dln(Nc).
            const double u = delta[k] - ex_.te_dlnNc[k];
            const double g1 = std::min(1.0, std::exp(clip(u, -700.0, 700.0)));
            const double g2 = ex_.te_rNc[k] * std::min(1.0, std::exp(clip(-u, -700.0, 700.0)));
            const double w = -delta_p[k] - ex_.te_dlnNv[k];
            const double h1 = std::min(1.0, std::exp(clip(w, -700.0, 700.0)));
            const double h2 = ex_.te_rNv[k] * std::min(1.0, std::exp(clip(-w, -700.0, 700.0)));
            an[k] = ex_.te_Kn[k];
            ap[k] = ex_.te_Kp[k];
            Bp[k] = g2;
            Bm[k] = g1;
            dBp[k] = u > 0.0 ? -g2 : 0.0;
            dBm[k] = u < 0.0 ? -g1 : 0.0;
            Bm_h[k] = h2;
            Bp_h[k] = h1;
            dBm_h[k] = w > 0.0 ? -h2 : 0.0;
            dBp_h[k] = w < 0.0 ? -h1 : 0.0;
        }
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
    // assisted capture plus the unchanged Auger term, applied after the
    // srh=False zeroing exactly as the Python orders it.
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
                tau_n_[j], tau_p_[j], Cn_node_[j], Cp_node_[j], models_.auger);
            R = r.R; dRdn = r.dRdn; dRdp = r.dRdp;
        } else {
            const ph::RecombinationResult r = ph::recombination_boltzmann(
                n_phys, p_phys, nie_[j], tau_n_[j], tau_p_[j], Cn_node_[j], Cp_node_[j],
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
            // TAT modifies the SRH channel only; Auger stays (device.py's
            // 2026-09-28 fix, same form/order as recombination()).
            if (models_.auger && models_.srh) {
                const double Caug = Cn_node_[j] * n_phys + Cp_node_[j] * p_phys;
                R = R + Caug * excess;
                dRdn = dRdn + Cn_node_[j] * excess + Caug * (p_phys - dqdn);
                dRdp = dRdp + Cp_node_[j] * excess + Caug * (n_phys - dqdp);
            }
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
        out.F[3 * i] = et_[eR] * (psi[i + 1] - psi[i]) / h_[eR] -
                       et_[eL] * (psi[i] - psi[i - 1]) / h_[eL] -
                       dV_[i] * (n[i] - p[i] - charge_term);
        J.add(3 * i, 3 * i, -et_[eR] / h_[eR] - et_[eL] / h_[eL]);
        J.add(3 * i, 3 * (i + 1), et_[eR] / h_[eR]);
        J.add(3 * i, 3 * (i - 1), et_[eL] / h_[eL]);
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
            // M34-S2: with impact_nonlocal alpha sees a per-carrier
            // EFFECTIVE field; its psi-dependence is dense and stamped
            // separately below (the local tridiagonal alpha chain is
            // zeroed, exactly as device.py does).
            const bool ii_nl = models_.impact_nonlocal;
            std::vector<double> En_ii, Ep_ii, Dn_ii, Dp_ii;
            if (ii_nl) {
                effective_field(psi, 0, En_ii, &Dn_ii);
                effective_field(psi, 1, Ep_ii, &Dp_ii);
            } else {
                En_ii = E_node;
                Ep_ii = E_node;
            }
            out.ii_gs.assign(N, 0.0);
            std::vector<double> gs_full(N);
            for (int j = 0; j < N; ++j) {
                double Snj, Spj;
                if (j == 0) { Snj = aJn[0]; Spj = aJp[0]; }
                else if (j == N - 1) { Snj = aJn[Ne - 1]; Spj = aJp[Ne - 1]; }
                else { Snj = aJn[j - 1] + aJn[j]; Spj = aJp[j - 1] + aJp[j]; }
                gs_full[j] = Kgen * (ph::ii_alpha(ph::kAlphaN, En_ii[j]) * Snj +
                                     ph::ii_alpha(ph::kAlphaP, Ep_ii[j]) * Spj);
                out.ii_gs[j] = gs_full[j] * strength;
            }
            // dense block coefficients, filled in the node loop below
            std::vector<double> cn_nl, cp_nl;
            if (ii_nl) { cn_nl.assign(N, 0.0); cp_nl.assign(N, 0.0); }
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

                const double an_i = ph::ii_alpha(ph::kAlphaN, En_ii[i]);
                const double ap_i = ph::ii_alpha(ph::kAlphaP, Ep_ii[i]);
                const double dan_i = ph::ii_dalpha_dE(ph::kAlphaN, En_ii[i]);
                const double dap_i = ph::ii_dalpha_dE(ph::kAlphaP, Ep_ii[i]);
                double dEL = 0.0, dEM = 0.0, dER = 0.0;
                if (!ii_nl) dEi(i, dEL, dEM, dER);
                const double Sn_i = aJn[eL] + aJn[eR];
                const double Sp_i = aJp[eL] + aJp[eR];
                if (ii_nl) {
                    cn_nl[i] = strength * Kgen * dV_[i] * dan_i * Sn_i;
                    cp_nl[i] = strength * Kgen * dV_[i] * dap_i * Sp_i;
                }

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
            if (ii_nl) {
                // M34-S2: dG_i/dpsi_k through alpha(E_eff) for every k
                // upstream of i. Entries below 1e-15 of the block's
                // largest are dropped (device.py's own sparsification).
                double amax = 0.0;
                std::vector<double> dense(static_cast<std::size_t>(N - 2) * N);
                for (int i = 1; i < N - 1; ++i) {
                    const double* Dn = &Dn_ii[static_cast<std::size_t>(i) * N];
                    const double* Dp = &Dp_ii[static_cast<std::size_t>(i) * N];
                    double* row = &dense[static_cast<std::size_t>(i - 1) * N];
                    for (int k = 0; k < N; ++k) {
                        row[k] = cn_nl[i] * Dn[k] + cp_nl[i] * Dp[k];
                        amax = std::max(amax, std::abs(row[k]));
                    }
                }
                const double cut = 1e-15 * amax;
                for (int i = 1; i < N - 1; ++i) {
                    const double* row = &dense[static_cast<std::size_t>(i - 1) * N];
                    for (int k = 0; k < N; ++k) {
                        if (!(std::abs(row[k]) > cut)) continue;
                        J.add(3 * i + 1, 3 * k, row[k]);
                        J.add(3 * i + 2, 3 * k, -row[k]);
                    }
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

    // --- M34-S1: nonlocal path BTBT (Esseni 2017 eq 11). Only the path
    // GEOMETRY is frozen; psi along every path is live. Holes are
    // deposited at each path's start node, electrons spread around the
    // delta = 1 crossing (dep weights, which move with psi -> ddep).
    // Strength 0 contributes exactly nothing and is skipped, as in
    // device.py (it also keeps 0 * inf out of the residual).
    if (models_.btbt_nonlocal && paths.n_paths() > 0 && strength > 0.0) {
        const std::int64_t P = paths.n_paths();
        const tcad::nonlocal::EvalResult ev = tcad::nonlocal::evaluate_paths(
            psi.data(), N, paths.start.data(), paths.offset.data(), P,
            paths.sidx.data(), paths.swts.data(),
            static_cast<std::int64_t>(paths.sidx.size()), 1, paths.seg_len.data(),
            paths.gidx.data(), paths.gwts.data(), 2, VT_, nl_.Eg_J, nl_.mr_kg,
            nl_.mc_kg, nl_.mv_kg, nl_.u, nl_.hbar, nl_.q);
        std::vector<double> fac(P), cnt(P);
        for (std::int64_t p = 0; p < P; ++p) {
            const std::int64_t st = paths.start[p];
            fac[p] = strength * 1e-6 / R0_ * dV_[st];   // SI -> scaled box count
            cnt[p] = fac[p] * ev.G[p];
            out.F[3 * st + 2] += -cnt[p];
        }
        // per-path groupings of the dep / dG COO entries
        std::vector<std::vector<std::size_t>> dep_of(P), dG_of(P);
        for (std::size_t t = 0; t < ev.dep_vals.size(); ++t) {
            const std::int64_t p = ev.dep_rows[t];
            out.F[3 * ev.dep_cols[t] + 1] += cnt[p] * ev.dep_vals[t];
            dep_of[p].push_back(t);
        }
        for (std::size_t t = 0; t < ev.dG_vals.size(); ++t) {
            const std::int64_t p = ev.dG_rows[t];
            J.add(3 * paths.start[p] + 2, 3 * ev.dG_cols[t], -fac[p] * ev.dG_vals[t]);
            dG_of[p].push_back(t);
        }
        // electron rows: w_pn * dG_p/dpsi ...
        for (std::int64_t p = 0; p < P; ++p)
            for (std::size_t td : dep_of[p])
                for (std::size_t tg : dG_of[p])
                    J.add(3 * ev.dep_cols[td] + 1, 3 * ev.dG_cols[tg],
                          fac[p] * ev.dep_vals[td] * ev.dG_vals[tg]);
        // ... plus G_p * d w_pn/dpsi (the crossing moves)
        for (std::size_t t = 0; t < ev.ddep_val.size(); ++t)
            J.add(3 * ev.ddep_node[t] + 1, 3 * ev.ddep_col[t],
                  cnt[ev.ddep_p[t]] * ev.ddep_val[t]);
    }

    // --- contacts (device.py's "Dirichlet contacts (Robin on n/p when
    // M14 S_n/S_p != 0)" block). psi is always Dirichlet. A zero scaled
    // velocity keeps the exact pre-M14 density clamp; a nonzero one
    // (M14 S_n/S_p, or an M46-S2 Robin Schottky contact's v_R, already
    // resolved per side in Python) replaces the row with the flux
    // balance Jn[edge] + bsign * S * (n - n0) = 0 on the one edge
    // touching the contact, reusing that edge's current partials.
    std::vector<std::int64_t>& drows = out.dirichlet;
    for (int side = 0; side < 2; ++side) {
        const int node = (side == 0) ? 0 : N - 1;
        const ContactBC& c = bc[static_cast<std::size_t>(side)];
        const double S_n_s = ex_.S_n_s[static_cast<std::size_t>(side)];
        const double S_p_s = ex_.S_p_s[static_cast<std::size_t>(side)];
        out.F[3 * node] = psi[node] - c.psi0;
        J.add(3 * node, 3 * node, 1.0);
        drows.push_back(3 * node);
        const int edge = node == 0 ? 0 : N - 2;
        const int other = node == 0 ? node + 1 : node - 1;
        const bool left = node == 0;
        const double bsign_n = left ? -1.0 : 1.0;
        const double bsign_p = left ? 1.0 : -1.0;
        if (S_n_s == 0.0) {
            out.F[3 * node + 1] = n[node] - c.n0;
            J.add(3 * node + 1, 3 * node + 1, 1.0);
            drows.push_back(3 * node + 1);
        } else {
            out.F[3 * node + 1] = Jn[edge] + bsign_n * S_n_s * (n[node] - c.n0);
            const double dpsi_node = left ? -dJn_dpsiR[edge] : dJn_dpsiR[edge];
            const double dpsi_other = left ? dJn_dpsiR[edge] : -dJn_dpsiR[edge];
            double dn_node = left ? -an[edge] * Bm[edge] : an[edge] * Bp[edge];
            double dn_other = left ? an[edge] * Bp[edge] : -an[edge] * Bm[edge];
            if (fd) {
                const double Sn_edge = n[edge + 1] * dBp[edge] + n[edge] * dBm[edge];
                dn_node += left ? (-an[edge] * Sn_edge * wn[node])
                                : (an[edge] * Sn_edge * wn[node]);
                dn_other += left ? (an[edge] * Sn_edge * wn[other])
                                 : (-an[edge] * Sn_edge * wn[other]);
            }
            J.add(3 * node + 1, 3 * node, dpsi_node);
            J.add(3 * node + 1, 3 * other, dpsi_other);
            J.add(3 * node + 1, 3 * node + 1, dn_node + bsign_n * S_n_s);
            J.add(3 * node + 1, 3 * other + 1, dn_other);
        }
        if (S_p_s == 0.0) {
            out.F[3 * node + 2] = p[node] - c.p0;
            J.add(3 * node + 2, 3 * node + 2, 1.0);
            drows.push_back(3 * node + 2);
        } else {
            out.F[3 * node + 2] = Jp[edge] + bsign_p * S_p_s * (p[node] - c.p0);
            const double dpsi_node = left ? -dJp_dpsiR[edge] : dJp_dpsiR[edge];
            const double dpsi_other = left ? dJp_dpsiR[edge] : -dJp_dpsiR[edge];
            double dp_node = left ? ap[edge] * Bp_h[edge] : -ap[edge] * Bm_h[edge];
            double dp_other = left ? -ap[edge] * Bm_h[edge] : ap[edge] * Bp_h[edge];
            if (fd) {
                const double Sp_edge = p[edge + 1] * dBm_h[edge] + p[edge] * dBp_h[edge];
                dp_node += left ? (ap[edge] * Sp_edge * wp[node])
                                : (-ap[edge] * Sp_edge * wp[node]);
                dp_other += left ? (-ap[edge] * Sp_edge * wp[other])
                                 : (ap[edge] * Sp_edge * wp[other]);
            }
            J.add(3 * node + 2, 3 * node, dpsi_node);
            J.add(3 * node + 2, 3 * other, dpsi_other);
            J.add(3 * node + 2, 3 * node + 2, dp_node + bsign_p * S_p_s);
            J.add(3 * node + 2, 3 * other + 2, dp_other);
        }
    }

    // --- M44: coupled electron energy balance, a 4th block (rows/cols
    // 3N..4N-1) with lagged n/Jn/Joule heat; the psi/n/p block above is
    // unchanged by it (device.py's `if theta is not None` block).
    if (eb != nullptr) {
        const std::vector<double>& th = *eb->theta;
        const std::vector<double>& n_lag = *eb->n_lag;
        const std::vector<double>& Jn_lag = *eb->Jn_lag;
        const std::vector<double>& Qh = *eb->Qheat_lag;
        const double KAPPA0 = ex_.kappa0, ALPHA = ex_.alpha_relax;
        std::vector<double> n_floor(N), kappa_s(N);
        for (int j = 0; j < N; ++j) {
            n_floor[j] = std::max(n_lag[j], 1e-8);
            kappa_s[j] = KAPPA0 * mu_n0_[j] * n_floor[j] * th[j];
        }
        std::vector<double> w_edge(Ne), dwL(Ne), dwR(Ne);
        for (int k = 0; k < Ne; ++k) {
            const double theta_edge = 0.5 * (th[k] + th[k + 1]);
            const double kappa_edge = 0.5 * (kappa_s[k] + kappa_s[k + 1]);
            const double grad_theta = (th[k + 1] - th[k]) / h_[k];
            w_edge[k] = -2.5 * theta_edge * Jn_lag[k] - kappa_edge * grad_theta;
            const double dkL = 0.5 * KAPPA0 * mu_n0_[k] * n_floor[k];
            const double dkR = 0.5 * KAPPA0 * mu_n0_[k + 1] * n_floor[k + 1];
            dwL[k] = (-2.5 * 0.5 * Jn_lag[k] - dkL * grad_theta - kappa_edge * (-1.0 / h_[k]));
            dwR[k] = (-2.5 * 0.5 * Jn_lag[k] - dkR * grad_theta - kappa_edge * (1.0 / h_[k]));
        }
        const std::int64_t base = 3 * static_cast<std::int64_t>(N);
        out.F.resize(4 * static_cast<std::size_t>(N), 0.0);
        out.F[base] = th[0] - 1.0;
        J.add(base, base, 1.0);
        drows.push_back(base);
        out.F[base + N - 1] = th[N - 1] - 1.0;
        J.add(base + N - 1, base + N - 1, 1.0);
        drows.push_back(base + N - 1);
        for (int i = 1; i < N - 1; ++i) {
            const double Q_src = Qh[i] - ALPHA * n_floor[i] * (th[i] - 1.0);
            const double dQ = -ALPHA * n_floor[i];
            out.F[base + i] = (w_edge[i] - w_edge[i - 1]) - dV_[i] * Q_src;
            J.add(base + i, base + i - 1, -dwL[i - 1]);
            J.add(base + i, base + i, dwL[i] - dwR[i - 1] - dV_[i] * dQ);
            J.add(base + i, base + i + 1, dwR[i]);
        }
    }
    std::sort(drows.begin(), drows.end());
    return out;
}

std::pair<bool, double> Device1D::newton(const std::array<ContactBC, 2>& bc,
                                         const NewtonOptions& opts, double strength,
                                         bool line_search, bool floored,
                                         std::vector<double>& psi,
                                         std::vector<double>& n,
                                         std::vector<double>& p,
                                         std::vector<double>* theta,
                                         std::vector<double>* Jn_prev) {
    // device.py's solve_bias._newton. `line_search` is its `stiff_gen`
    // (turned off for the btbt_nonlocal refresh re-solves), `floored` its
    // `floored` (stiff_gen or an M34 nonlocal flag).
    const int N = N_;
    const bool eb_on = theta != nullptr;
    double err = std::numeric_limits<double>::infinity();
    for (int it = 0; it < opts.max_iter; ++it) {
        // Lagged mobility, re-set every iterate from the CURRENT state
        // (device.py's _set_edge_diffusivity calls): energy_balance's
        // Tn-consistent field replaces field_mobility's local one.
        if (eb_on) {
            std::vector<double> E_eff(N);
            for (int i = 0; i < N; ++i) {
                // hydrodynamic.effective_field_from_temperature
                const double dT = std::max((*theta)[i] * T_ - T_, 0.0);
                const double mu_SI = mu_n0_[i] * 1.0e-4;
                const double ppc = dT * ex_.kB * 1.5 / ex_.tau_w;
                E_eff[i] = std::sqrt(ppc / (ex_.q_hydro * mu_SI)) * 1.0e-2;
            }
            set_edge_diffusivity(mobility_field(mu_n0_, E_eff, 0),
                                 mobility_field(mu_p0_, E_eff, 1));
        } else if (models_.field_mobility) {
            std::vector<double> E_abs(N - 1), E_node(N);
            for (int k = 0; k < N - 1; ++k)
                E_abs[k] = std::abs(-(psi[k + 1] - psi[k]) * VT_ / (h_[k] * LD_));
            E_node[0] = E_abs[0];
            E_node[N - 1] = E_abs[N - 2];
            for (int i = 1; i < N - 1; ++i) E_node[i] = 0.5 * (E_abs[i - 1] + E_abs[i]);
            set_edge_diffusivity(mobility_field(mu_n0_, E_node, 0),
                                 mobility_field(mu_p0_, E_node, 1));
        }

        ResidualJacobian rj;
        if (eb_on) {
            // M44 Joule heat Jn . E_n with E_n = -grad(phi_n) of the
            // lagged state (Wachutka), edge product then node average.
            const std::vector<double> n_lag = n;
            std::vector<double> Qheat(N), Hn(N - 1);
            for (int k = 0; k < N - 1; ++k) {
                const double phi_L = psi[k] - std::log(std::max(n_lag[k], 1e-300) / nie_s_[k]);
                const double phi_R =
                    psi[k + 1] - std::log(std::max(n_lag[k + 1], 1e-300) / nie_s_[k + 1]);
                const double En_edge = -(phi_R - phi_L) / h_[k];
                Hn[k] = (*Jn_prev)[k] * En_edge;
            }
            Qheat[0] = Hn[0];
            Qheat[N - 1] = Hn[N - 2];
            for (int i = 1; i < N - 1; ++i) Qheat[i] = 0.5 * (Hn[i - 1] + Hn[i]);
            const EbLag lag{theta, &n_lag, Jn_prev, &Qheat};
            rj = assemble(psi, n, p, bc, strength, &lag);
            *Jn_prev = rj.Jn;
        } else {
            rj = assemble(psi, n, p, bc, strength);
        }
        const std::int64_t M = static_cast<std::int64_t>(rj.F.size());
        std::vector<double> rhs(static_cast<std::size_t>(M));
        for (std::int64_t k = 0; k < M; ++k) rhs[k] = -rj.F[k];
        eliminate_dirichlet(rj.J, rhs, rj.dirichlet);
        std::vector<double> du =
            lu_solve(rj.J, M, rhs);

        std::vector<double> dpsi(N), dn(N), dp(N), n_new(N), p_new(N);
        double max_dpsi = 0.0, rel_n = 0.0, rel_p = 0.0;
        for (int i = 0; i < N; ++i) {
            dpsi[i] = clip(du[3 * i], -opts.max_dpsi, opts.max_dpsi);
            dn[i] = du[3 * i + 1];
            dp[i] = du[3 * i + 2];
            n_new[i] = clip(n[i] + dn[i], 0.1 * n[i], 10.0 * n[i]);
            p_new[i] = clip(p[i] + dp[i], 0.1 * p[i], 10.0 * p[i]);
            max_dpsi = std::max(max_dpsi, std::abs(dpsi[i]));
            if (floored) {
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
        if (eb_on) {
            double max_dth = 0.0;
            for (int i = 0; i < N; ++i)
                max_dth = std::max(max_dth, std::abs(du[3 * static_cast<std::size_t>(N) + i]));
            err = std::max(err, max_dth);
        }

        if (line_search && err >= kLsNewtonRegion) {
            // M15 backtracking on the 2-norm merit of the FULL residual
            // (contact rows included, as device.py's F is).
            double base = 0.0;
            for (double f : rj.F) base += f * f;
            base *= 0.5;
            double lam = 1.0;
            bool accepted = false;
            double ft_last = 0.0;  // device.py prints the LAST trial's merit
            std::vector<double> psi_t(N), n_t(N), p_t(N);
            for (int h = 0; h < kLsMaxHalvings + 1; ++h) {
                for (int i = 0; i < N; ++i) {
                    psi_t[i] = psi[i] + lam * dpsi[i];
                    n_t[i] = clip(n[i] + lam * dn[i], 0.1 * n[i], 10.0 * n[i]);
                    p_t[i] = clip(p[i] + lam * dp[i], 0.1 * p[i], 10.0 * p[i]);
                }
                const ResidualJacobian rt =
                    assemble(psi_t, n_t, p_t, bc, strength);
                double ft = 0.0;
                for (double f : rt.F) ft += f * f;
                ft *= 0.5;
                ft_last = ft;
                if (std::isfinite(ft) && ft <= base * (1.0 - 1e-4 * lam)) {
                    accepted = true;
                    break;
                }
                lam *= 0.5;
            }
            // No trial reduced the merit: take the full step (M34-S7).
            if (!accepted) lam = 1.0;
            if (opts.verbose && log_)
                log_("   stage " + py_repr(strength) + "  lam=" + py_e3(lam) + "  merit " +
                     py_e3(base) + " -> " + py_e3(ft_last));
            for (int i = 0; i < N; ++i) {
                psi[i] = psi[i] + lam * dpsi[i];
                n_new[i] = clip(n[i] + lam * dn[i], 0.1 * n[i], 10.0 * n[i]);
                p_new[i] = clip(p[i] + lam * dp[i], 0.1 * p[i], 10.0 * p[i]);
            }
        } else {
            for (int i = 0; i < N; ++i) psi[i] += dpsi[i];
            if (eb_on) {
                // relative 0.1x-10x clip, endpoints pinned (device.py)
                std::vector<double>& th = *theta;
                for (int i = 0; i < N; ++i) {
                    const double dth = du[3 * static_cast<std::size_t>(N) + i];
                    th[i] = clip(th[i] + dth, 0.1 * th[i], 10.0 * th[i]);
                }
                th[0] = 1.0;
                th[N - 1] = 1.0;
            }
        }
        n = n_new;
        p = p_new;
        if (opts.verbose && log_) {
            // device.py: |F| over the FULL residual (theta rows included),
            // |dpsi| of the clipped update, |dn/n| = the update-test rel_n
            double fmax = 0.0;
            for (double f : rj.F)  // NaN propagates, as numpy's max does
                fmax = std::isnan(f) || std::isnan(fmax) ? std::nan("") : std::max(fmax, std::abs(f));
            char head[24];
            std::snprintf(head, sizeof head, "   it %2d  |F|=", it);
            log_(head + py_e3(fmax) + "  |dpsi|=" + py_e3(max_dpsi) + "  |dn/n|=" +
                 py_e3(rel_n));
        }
        if (err < opts.tol_update) return {true, err};
    }
    return {false, err};
}

bool Device1D::solve_bias(std::array<double, 2> V, const NewtonOptions& opts) {
    // M20 DG is equilibrium-only (device.py raises NotImplementedError
    // before reaching here; this is the compiled class's own guard).
    if (models_.dg)
        throw tcad::InvalidArgument("Device1D: models.dg is equilibrium-only (no solve_bias)");
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
    const bool btbt_nl = models_.btbt_nonlocal;
    const bool stiff = models_.impact || models_.btbt || btbt_nl;
    const bool floored = stiff || btbt_nl || models_.impact_nonlocal;
    // M34-S1: tunnel paths frozen for this call, located from the same
    // contact-stamped warm start device.py's first residual evaluation
    // sees (it resets _btbt_nl_paths = None at the top of solve_bias).
    if (btbt_nl) {
        const auto [s0, e0] = locate_btbt_nl_paths(psi);
        nl_paths_ = build_nl_paths(s0, e0);
        nl_paths_valid_ = true;
    }
    // M44: theta = Tn/T warm-started from the previous solve (1 on the
    // first bias point), contacts pinned at 1, Jn_prev = 0 (no Joule
    // heating before the first iterate produces a current).
    std::vector<double> theta, Jn_prev;
    std::vector<double>* th_ptr = nullptr;
    std::vector<double>* jp_ptr = nullptr;
    if (models_.energy_balance) {
        theta.assign(static_cast<std::size_t>(N_), 1.0);
        if (!Tn_.empty())
            for (int i = 0; i < N_; ++i) theta[i] = Tn_[i] / T_;
        theta[0] = 1.0;
        theta[N_ - 1] = 1.0;
        Jn_prev.assign(static_cast<std::size_t>(N_ - 1), 0.0);
        th_ptr = &theta;
        jp_ptr = &Jn_prev;
    }
    bool converged = false;
    double err = std::numeric_limits<double>::infinity();
    if (stiff) {
        for (double stage : kIiStages) {
            ii_strength_ = stage;
            std::tie(converged, err) =
                newton({bc0, bc1}, opts, stage, true, floored, psi, n, p, th_ptr, jp_ptr);
            if (!converged) break;
        }
    } else {
        ii_strength_ = 1.0;
        std::tie(converged, err) =
            newton({bc0, bc1}, opts, 1.0, false, floored, psi, n, p, th_ptr, jp_ptr);
    }

    // M34-S1: make the converged state consistent with its own paths --
    // re-locate at the converged psi; if the start set changed or a path
    // is truncated, re-solve (plain full-step Newton, still floored) with
    // the new ones. Bounded at 4 refreshes, outcome reported.
    nl_refreshes_ = 0;
    nl_stable_ = -1;
    if (btbt_nl && converged) {
        nl_stable_ = 0;
        while (true) {
            const auto [s_new, e_new] = locate_btbt_nl_paths(psi);
            bool all_reached = true;
            if (nl_paths_.n_paths() > 0) {
                const tcad::nonlocal::EvalResult ev = tcad::nonlocal::evaluate_paths(
                    psi.data(), N_, nl_paths_.start.data(), nl_paths_.offset.data(),
                    nl_paths_.n_paths(), nl_paths_.sidx.data(), nl_paths_.swts.data(),
                    static_cast<std::int64_t>(nl_paths_.sidx.size()), 1,
                    nl_paths_.seg_len.data(), nl_paths_.gidx.data(),
                    nl_paths_.gwts.data(), 2, VT_, nl_.Eg_J, nl_.mr_kg, nl_.mc_kg,
                    nl_.mv_kg, nl_.u, nl_.hbar, nl_.q);
                for (auto r : ev.reached) all_reached = all_reached && (r != 0);
            }
            if (s_new == nl_paths_.start && all_reached) {
                nl_stable_ = 1;
                break;
            }
            if (nl_refreshes_ == 4) break;
            nl_paths_ = build_nl_paths(s_new, e_new);
            nl_refreshes_ += 1;
            // device.py sets stiff_gen = False here: no line search
            std::tie(converged, err) = newton({bc0, bc1}, opts, ii_strength_, false,
                                              floored, psi, n, p, th_ptr, jp_ptr);
            if (!converged) break;
        }
    }

    psi_ = psi;
    n_ = n;
    p_ = p;
    if (models_.energy_balance) {
        Tn_.resize(static_cast<std::size_t>(N_));
        for (int i = 0; i < N_; ++i) Tn_[i] = theta[i] * T_;
    }
    ResidualJacobian rj = assemble(psi_, n_, p_, {bc0, bc1}, ii_strength_);
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
        std::vector<double>(p.begin(), p.end()), {bc0, bc1}, Pn, Pp, strength,
        // frozen paths when set (device.py reads self._btbt_nl_paths),
        // else located at this psi -- its own lazy None fill
        (models_.btbt_nonlocal && !nl_paths_valid_)
            ? build_nl_paths(locate_btbt_nl_paths(psi_v).first,
                             locate_btbt_nl_paths(psi_v).second)
            : nl_paths_);
    return {std::move(rj.F), std::move(rj.J.rows), std::move(rj.J.cols),
           std::move(rj.J.vals)};
}

std::tuple<std::vector<double>, std::vector<std::int64_t>,
          std::vector<std::int64_t>, std::vector<double>>
Device1D::residual_jacobian_eb_for_test(std::span<const double> psi,
                                        std::span<const double> n,
                                        std::span<const double> p, double V_left,
                                        double V_right, std::span<const double> theta,
                                        std::span<const double> n_lag,
                                        std::span<const double> Jn_lag,
                                        std::span<const double> Qheat_lag) const {
    const std::size_t Nn = static_cast<std::size_t>(N_);
    if (psi.size() != Nn || n.size() != Nn || p.size() != Nn || theta.size() != Nn ||
        n_lag.size() != Nn || Jn_lag.size() != Nn - 1 || Qheat_lag.size() != Nn)
        throw tcad::InvalidArgument("Device1D: EB hook array lengths (N, Jn_lag N-1)");
    const std::vector<double> th(theta.begin(), theta.end()), nl(n_lag.begin(), n_lag.end()),
        jl(Jn_lag.begin(), Jn_lag.end()), qh(Qheat_lag.begin(), Qheat_lag.end());
    const EbLag lag{&th, &nl, &jl, &qh};
    ResidualJacobian rj = residual_jacobian(
        std::vector<double>(psi.begin(), psi.end()), std::vector<double>(n.begin(), n.end()),
        std::vector<double>(p.begin(), p.end()),
        {contact_value(0, V_left), contact_value(1, V_right)}, Pn_, Pp_, 1.0, nl_paths_,
        &lag);
    return {std::move(rj.F), std::move(rj.J.rows), std::move(rj.J.cols),
           std::move(rj.J.vals)};
}

Device1D::ResidualJacobian Device1D::assemble_explicit(
    std::span<const double> psi, std::span<const double> n, std::span<const double> p,
    ContactBC bc0, ContactBC bc1, std::span<const double> Pn, std::span<const double> Pp,
    double strength, std::span<const std::int64_t> starts,
    std::span<const std::int64_t> ends, const EbLag* eb) const {
    const std::size_t Nn = static_cast<std::size_t>(N_);
    if (psi.size() != Nn || n.size() != Nn || p.size() != Nn)
        throw tcad::InvalidArgument("Device1D.assemble: psi/n/p must have length N");
    if (models_.tat && (Pn.size() != Nn || Pp.size() != Nn))
        throw tcad::InvalidArgument("Device1D.assemble: models.tat needs Pn/Pp of length N");
    if (starts.size() != ends.size())
        throw tcad::InvalidArgument("Device1D.assemble: starts/ends length mismatch");
    const NlPaths paths = models_.btbt_nonlocal
        ? build_nl_paths(std::vector<std::int64_t>(starts.begin(), starts.end()),
                         std::vector<std::int64_t>(ends.begin(), ends.end()))
        : NlPaths{};
    return residual_jacobian(std::vector<double>(psi.begin(), psi.end()),
                             std::vector<double>(n.begin(), n.end()),
                             std::vector<double>(p.begin(), p.end()), {bc0, bc1},
                             std::vector<double>(Pn.begin(), Pn.end()),
                             std::vector<double>(Pp.begin(), Pp.end()), strength, paths, eb);
}

void Device1D::set_edge_diffusivity_scaled(std::span<const double> dn,
                                           std::span<const double> dp) {
    const std::size_t Ne = static_cast<std::size_t>(N_ - 1);
    if (dn.size() != Ne || dp.size() != Ne)
        throw tcad::InvalidArgument("Device1D: dn_edge/dp_edge must have length N-1");
    dn_edge_.assign(dn.begin(), dn.end());
    dp_edge_.assign(dp.begin(), dp.end());
}

std::pair<std::vector<double>, Coo> Device1D::dg_assemble(
    std::span<const double> psi, std::span<const double> Lam_n,
    std::span<const double> Lam_p, ContactBC bc0, ContactBC bc1, double gamma) const {
    if (!models_.dg) throw tcad::InvalidArgument("Device1D: dg_assemble needs models.dg");
    const std::size_t Nn = static_cast<std::size_t>(N_);
    if (psi.size() != Nn || Lam_n.size() != Nn || Lam_p.size() != Nn)
        throw tcad::InvalidArgument("Device1D: psi/Lam_n/Lam_p must have length N");
    return dg_residual_jacobian(std::vector<double>(psi.begin(), psi.end()),
                                std::vector<double>(Lam_n.begin(), Lam_n.end()),
                                std::vector<double>(Lam_p.begin(), Lam_p.end()),
                                {bc0, bc1}, gamma);
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
