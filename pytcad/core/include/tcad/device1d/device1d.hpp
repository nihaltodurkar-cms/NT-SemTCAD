// Native C++ port of pytcad.device.Device1D. Phase 1: baseline physics
// only (homojunction, Boltzmann statistics, full ionization;
// doping_mobility/srh/auger/bgn on or off). Phase 2 slice 1 (this
// revision) adds Fermi-Dirac statistics (Models.fd) and incomplete
// ionization (Models.incomplete_ion) -- see device1d.cpp's per-method
// comments for exactly which Python branches each addition mirrors.
// Phase 2 slice 2 adds M12-S2 trap-assisted tunneling (Models.tat);
// slice 3 adds M15 local impact ionization (Models.impact) and M16
// local Kane BTBT (Models.btbt) with the stiff-generation ladder; slice
// 4 adds their M34 nonlocal variants (impact_nonlocal, btbt_nonlocal);
// slice 5 adds the rest of Device1D: heterostructures (per-node
// materials, eps(x), the M33-S1 affinity gauge, M33-S2 thermionic
// interface flux), M14 S_n/S_p Robin contacts, M46 Schottky contacts
// (Dirichlet and Robin), lagged field mobility, M44 energy balance and
// the M20 density-gradient equilibrium solve. Refusals (unvalidated
// model compositions) stay in the Python wrapper, which raises before
// constructing or calling this class.
//
// Materials evaluation (Caughey-Thomas mobility, Slotboom BGN,
// Scharfetter lifetime) happens ONCE, in Python, before construction --
// exactly like today's Device1D.__init__ (materials.py's functions are
// called only there, never inside the Newton loop; verified by reading
// device.py before writing this). This class receives the resulting
// per-node PHYSICAL arrays (mu_n0/mu_p0 [cm^2/(V s)], tau_n/tau_p [s],
// nie [cm^-3]) rather than re-implementing materials.py. The same
// applies to Phase 2's FD table: pytcad.fermi._table() builds it once
// in Python (already module-level-cached there) and it is handed to
// this constructor as five plain arrays, not rebuilt here -- see
// tcad/physics/fermi.hpp's own header comment.
#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <string>
#include <span>
#include <tuple>
#include <utility>
#include <vector>

#include "tcad/physics/fermi.hpp"

namespace tcad::device1d {

struct Models {
    bool srh = true;
    bool auger = true;
    // Phase 2 slice 1.
    bool fd = false;
    bool incomplete_ion = false;
    // Phase 2 slice 2: M12-S2 trap-assisted tunneling (frozen-field WKB).
    bool tat = false;
    // Phase 2 slice 3: M15 local impact ionization and M16 local Kane
    // BTBT, both live-coupled, both driving the stiff-generation ladder
    // + backtracking line search in solve_bias.
    bool impact = false;
    bool btbt = false;
    // Phase 2 slice 4: M34-S2 nonlocal (effective-field) impact
    // ionization (requires impact) and M34-S1 nonlocal path Kane BTBT.
    bool impact_nonlocal = false;
    bool btbt_nonlocal = false;
    // Phase 2 slice 5: M33-S2 thermionic interface flux, lagged Canali
    // field mobility, M44 electron energy balance, M20 density-gradient
    // equilibrium. Heterostructures, the affinity gauge, M14 S_n/S_p and
    // M46 Schottky contacts need no flag here: they are carried entirely
    // by the per-node/per-side arrays in Extras below.
    bool thermionic = false;
    bool field_mobility = false;
    bool energy_balance = false;
    bool dg = false;
};

/// Phase 2 slice 5 inputs, all evaluated ONCE in Python (materials stay
/// Python, as for every earlier slice) and fixed for the device's
/// lifetime. Every empty vector means "the homojunction value" (et = 1,
/// band_shift = 0, the scalar Auger coefficients), so a default Extras
/// leaves the earlier slices' arithmetic untouched.
struct Extras {
    // device.py's self.Ns (0 => max(|doping|, ni), the constructor's own
    // rule). Passed so a device rebuilt after its doping was mutated keeps
    // the Python object's scaling (n_cm3 = n * self.Ns), as device.py does.
    double Ns = 0.0;
    std::vector<double> et;          // N-1: device.py's _eps_tilde_edge()
    std::vector<double> band_shift;  // N:   device.py's band_shift
    std::vector<double> Cn_auger, Cp_auger;  // N: per-node material values
    // M33-S2 thermionic emission (N-1 each; only read when thermionic).
    std::vector<double> te_edge, te_Kn, te_Kp, te_dlnNc, te_dlnNv, te_rNc, te_rNv;
    // M46-S1 Schottky contacts: barrier-limited (n0, p0) replace local
    // neutrality on that side (non-FD contacts only, as in device.py).
    std::array<bool, 2> contact_override{false, false};
    std::array<double, 2> contact_n0{0.0, 0.0}, contact_p0{0.0, 0.0};
    // M14 S_n/S_p and M46-S2 Robin Schottky: SCALED surface velocities
    // per side, exactly as device.py's contact loop ends up using them
    // (0 => the plain Dirichlet density row).
    std::array<double, 2> S_n_s{0.0, 0.0}, S_p_s{0.0, 0.0};
    // Canali field mobility (materials.mobility_field) of self.mat.
    double vsat_n = 0.0, beta_n = 0.0, vsat_p = 0.0, beta_p = 0.0;
    // M44 energy balance: device.py's _ALPHA_RELAX/_KAPPA0 and
    // hydrodynamic.effective_field_from_temperature's constants.
    double alpha_relax = 0.0, kappa0 = 0.0, tau_w = 0.0, kB = 0.0, q_hydro = 0.0;
    // M20 DG: relative masses per node, target gamma, dg.py's constants.
    std::vector<double> m_n_star, m_p_star;
    double dg_gamma = 1.0, dg_hbar = 0.0, dg_m0 = 0.0, dg_q = 0.0;
};

/// M44 lagged energy-balance inputs of one residual evaluation
/// (device.py's theta / n_lag / Jn_lag / Qheat_lag arguments).
struct EbLag {
    const std::vector<double>* theta;
    const std::vector<double>* n_lag;
    const std::vector<double>* Jn_lag;
    const std::vector<double>* Qheat_lag;
};

/// Physical parameters of the nonlocal models, evaluated in Python (the
/// materials stay Python) and fixed for the device's lifetime.
struct NonlocalParams {
    double lambda_n_cm = 0.0;  // Models.impact_lambda_n
    double lambda_p_cm = 0.0;  // Models.impact_lambda_p
    double Eg_eV = 0.0;        // mats[0].Eg(T): path-locator threshold
    double Eg_J = 0.0;         // _btbt_nl_params()
    double mr_kg = 0.0, mc_kg = 0.0, mv_kg = 0.0;
    double u = 0.0;            // btbt._kane_u(mr_kg) (refusal stays Python)
    double hbar = 1.054571817e-34;  // btbt.HBAR_SI
    double q = 1.602176634e-19;     // btbt.Q_SI
};

/// Frozen 1D tunnel-path geometry: nonlocal_path.build_1d's flat arrays
/// (K = 1 unit stencils, Kg = 2 start-gradient stencil).
struct NlPaths {
    std::vector<std::int64_t> start, end;  // nodes start[p]..end[p]
    std::vector<std::int64_t> offset, sidx, gidx;
    std::vector<double> swts, seg_len, gwts;
    std::int64_t n_paths() const { return static_cast<std::int64_t>(start.size()); }
};

// device.py's stiff-path constants (see their long comments there).
inline constexpr double kIiStages[] = {0.0, 0.02, 0.05, 0.1, 0.2, 0.35, 0.5, 0.7, 1.0};
inline constexpr int kLsMaxHalvings = 10;           // _LS_MAX_HALVINGS
inline constexpr double kLsNewtonRegion = 1e-3;     // _LS_NEWTON_REGION
inline constexpr double kStiffDensityFloor = 1e-8;  // _STIFF_DENSITY_FLOOR
inline constexpr double kIiJEpsRel = 1e-6;          // _II_J_EPS_REL
// Per-edge floor of the |J| regularizer, relative to the magnitude of the
// two SG terms whose difference IS the edge current (see device1d.cpp's
// impact block): below it the computed current is round-off.
inline constexpr double kIiJRoundoffRel = 1e-10;

struct NewtonOptions {
    int max_iter = 100;
    double tol_update = 1e-8;
    double max_dpsi = 5.0;
    bool verbose = false;
};

struct ContactBC {
    double psi0 = 0.0;
    double n0 = 0.0;
    double p0 = 0.0;
};

struct Coo {
    std::vector<std::int64_t> rows;
    std::vector<std::int64_t> cols;
    std::vector<double> vals;
    void add(std::int64_t r, std::int64_t c, double v) {
        rows.push_back(r);
        cols.push_back(c);
        vals.push_back(v);
    }
};

class Device1D {
public:
    Device1D(std::span<const double> x, std::span<const double> doping,
             double T, double VT, double eps, double ni,
             std::span<const double> mu_n0_phys,
             std::span<const double> mu_p0_phys,
             std::span<const double> tau_n_phys,
             std::span<const double> tau_p_phys,
             std::span<const double> nie_phys, double Cn_auger,
             double Cp_auger, Models models,
             // Phase 2 slice 1 (FD / incomplete ionization). Empty spans
             // are valid and expected whenever models.fd == models.
             // incomplete_ion == false -- nothing below is touched in
             // that case, so the Phase 1 baseline path is unaffected by
             // any of these arguments existing.
             std::span<const double> nc_s = {}, std::span<const double> nv_s = {},
             std::span<const double> ln_gn = {}, std::span<const double> ln_gp = {},
             std::span<const double> eg_kt = {}, std::span<const double> nd_arr = {},
             std::span<const double> na_arr = {}, double ded_kt = 0.0,
             std::span<const double> fermi_e = {}, std::span<const double> fermi_g = {},
             std::span<const double> fermi_gp = {}, std::span<const double> fermi_q = {},
             std::span<const double> fermi_qp = {},
             // Phase 2 slice 2 (TAT). Per-node WKB exponent numerators
             // B_n * phi_n^1.5 and B_p * phi_p^1.5 [SI: V/m], computed
             // once in Python from the materials (device.py's
             // _update_tat_probabilities, everything there that does
             // not depend on psi). Empty unless models.tat.
             std::span<const double> tat_kn = {},
             std::span<const double> tat_kp = {},
             // Phase 2 slice 4 (impact_nonlocal / btbt_nonlocal).
             NonlocalParams nl = {},
             // Phase 2 slice 5 (hetero / contacts / mobility / EB / DG).
             Extras ex = {});

    void solve_equilibrium(const NewtonOptions& opts);
    /// Returns true if converged. V = {V_left, V_right} [volts].
    bool solve_bias(std::array<double, 2> V, const NewtonOptions& opts);

    int N() const { return N_; }
    const std::vector<double>& psi() const { return psi_; }
    const std::vector<double>& n() const { return n_; }
    const std::vector<double>& p() const { return p_; }
    std::vector<double> psi_V() const;
    std::vector<double> n_cm3() const;
    std::vector<double> p_cm3() const;
    std::vector<double> E_field() const;
    /// Per-edge physical current densities [A/cm^2] from the last solve
    /// (N-1 entries) -- gui/services/solver_runner.py's Jn+Jp field-plot
    /// path reads these directly on a Device1D instance.
    std::vector<double> Jn() const;
    std::vector<double> Jp() const;
    /// (mean J [A/cm^2], relative spread) over Jn+Jp per edge.
    std::pair<double, double> current_density() const;
    /// Frozen TAT escape probabilities from the last solve_bias (empty
    /// until one has run with models.tat) -- device.py's `_Pn`/`_Pp`.
    const std::vector<double>& Pn() const { return Pn_; }
    const std::vector<double>& Pp() const { return Pp_; }
    /// Generation sources from the last solve_bias's final residual
    /// evaluation, strength-scaled -- device.py's `_ii_gs_cache` (scaled
    /// units) and `_btbt_gs_cache` (physical cm^-3 s^-1). Empty when the
    /// corresponding flag is off (device.py's None).
    const std::vector<double>& ii_gs_cache() const { return ii_gs_cache_; }
    const std::vector<double>& btbt_gs_cache() const { return btbt_gs_cache_; }
    /// The ladder stage the last solve_bias ended on (device.py's
    /// `_ii_strength`): 1.0 on a converged or non-stiff solve.
    double ii_strength() const { return ii_strength_; }
    /// M34-S1 frozen paths of the last solve (device.py's
    /// `_btbt_nl_paths`, as build_1d's starts/ends) and the outcome of
    /// the post-convergence refresh (`last_btbt_nl_refreshes`/`_stable`;
    /// stable: -1 = None, 0 = False, 1 = True).
    const NlPaths& btbt_nl_paths() const { return nl_paths_; }
    int last_btbt_nl_refreshes() const { return nl_refreshes_; }
    int last_btbt_nl_stable() const { return nl_stable_; }
    /// Replace the frozen path set (test hook / wrapper round-trip).
    void set_btbt_nl_paths(std::span<const std::int64_t> starts,
                           std::span<const std::int64_t> ends);
    /// device.py's _btbt_nl_build_paths at `psi` (starts, ends).
    std::pair<std::vector<std::int64_t>, std::vector<std::int64_t>>
    locate_btbt_nl_paths(std::span<const double> psi) const;
    /// ii_nonlocal.effective_field for this mesh: (E_eff, dense dE/dpsi
    /// row-major N x N). carrier 0 = electrons, 1 = holes.
    std::pair<std::vector<double>, std::vector<double>>
    effective_field_for_test(std::span<const double> psi, int carrier) const;
    /// Overwrite the solution state (scaled psi/n/p, length N) -- the
    /// Python wrapper's attributes are the source of truth for the warm
    /// start (continuation.py restores dev.psi/n/p after a failed step
    /// and then calls solve_bias again).
    void set_state(std::span<const double> psi, std::span<const double> n,
                   std::span<const double> p);
    bool has_solution() const { return has_solution_; }
    bool last_converged() const { return last_converged_; }
    double last_newton_err() const { return last_newton_err_; }
    /// Whether the last solve_equilibrium converged (device.py warns
    /// "Equilibrium Poisson solve did not converge." / the DG message).
    bool last_eq_converged() const { return last_eq_converged_; }
    /// Edge diffusivities (scaled) the last residual evaluation used --
    /// device.py's dn_edge/dp_edge, which field_mobility/energy_balance
    /// overwrite every Newton iterate and leave behind after a solve.
    const std::vector<double>& dn_edge() const { return dn_edge_; }
    const std::vector<double>& dp_edge() const { return dp_edge_; }
    /// M44 carrier temperature [K] (device.py's Tn; empty == None).
    const std::vector<double>& Tn() const { return Tn_; }
    void set_Tn(std::span<const double> Tn);
    /// M20 DG quantum potentials [V] of the last DG equilibrium solve.
    const std::vector<double>& dg_Lam_n() const { return Lam_n_; }
    const std::vector<double>& dg_Lam_p() const { return Lam_p_; }
    /// Line sink for NewtonOptions.verbose progress, emitted in
    /// device.py's exact print formats (the GUI parses them from stdout:
    /// gui/services/progress_channel.py). Unset => verbose prints nothing.
    void set_logger(std::function<void(const std::string&)> log) { log_ = std::move(log); }

    /// Opt-in solve counters for the M32 benchmark probe
    /// (benchmarks/instrument.py), which cannot patch compiled code:
    /// wall time + calls of residual/Jacobian assembly (every
    /// residual_jacobian call, line-search trials included, as the
    /// Python probe counts _residual_jacobian) and of the LU solves,
    /// plus the largest solved system's DOF/NNZ (after Dirichlet
    /// elimination, duplicates summed -- what the LU actually factors).
    struct Stats {
        double assembly_s = 0.0, linsolve_s = 0.0;
        long long assembly_calls = 0, linsolve_calls = 0, dof = 0, nnz = 0;
    };
    void set_collect_stats(bool on) { collect_stats_ = on; stats_ = Stats{}; }
    const Stats& stats() const { return stats_; }

    struct ResidualJacobian {
        std::vector<double> F;   // length 3*N (4*N with an energy-balance block)
        Coo J;                   // 3N x 3N (4N x 4N)
        std::vector<double> Jn;  // per-edge scaled electron current, length N-1
        std::vector<double> Jp;
        std::vector<double> ii_gs;    // empty unless models.impact
        // smoothed |Jn|, |Jp| per edge (times J0), the ones ii_gs is
        // built from; empty unless models.impact
        std::vector<double> ii_abs_jn, ii_abs_jp;
        std::vector<double> btbt_gs;  // empty unless models.btbt
        std::vector<std::int64_t> dirichlet;  // device.py's _dirichlet_rows
    };

    /// The residual/Jacobian at an arbitrary state with EVERY frozen input
    /// explicit -- the entry point device.py's _residual_jacobian (and
    /// through it transient.py / continuation.py / ac.py) calls. Pn/Pp
    /// empty unless models.tat; starts/ends are the frozen btbt_nonlocal
    /// path spans (empty unless btbt_nonlocal); eb non-null adds the M44
    /// 4th block. Uses the CURRENT dn_edge_/dp_edge_ (set_edge_diffusivity_scaled).
    ResidualJacobian assemble_explicit(std::span<const double> psi,
                                       std::span<const double> n,
                                       std::span<const double> p, ContactBC bc0,
                                       ContactBC bc1, std::span<const double> Pn,
                                       std::span<const double> Pp, double strength,
                                       std::span<const std::int64_t> starts,
                                       std::span<const std::int64_t> ends,
                                       const EbLag* eb) const;
    /// Overwrite the (scaled) edge diffusivities -- device.py's
    /// dn_edge/dp_edge attributes are the source of truth.
    void set_edge_diffusivity_scaled(std::span<const double> dn, std::span<const double> dp);
    /// device.py's _contact_values(V): (psi0, n0, p0) for both contacts.
    std::array<ContactBC, 2> contact_values(double V_left, double V_right) const {
        return {contact_value(0, V_left), contact_value(1, V_right)};
    }
    /// device.py's _update_tat_probabilities for psi.
    std::pair<std::vector<double>, std::vector<double>>
    tat_probabilities_at(std::span<const double> psi) const {
        return tat_probabilities_for_test(psi);
    }
    /// device.py's _dg_residual_jacobian_eq with explicit contact BCs.
    std::pair<std::vector<double>, Coo> dg_assemble(std::span<const double> psi,
                                                    std::span<const double> Lam_n,
                                                    std::span<const double> Lam_p,
                                                    ContactBC bc0, ContactBC bc1,
                                                    double gamma) const;

    /// Test-only hook (M44 FD-Jacobian gate): the 4N residual/Jacobian
    /// with the energy-balance block for the given lagged inputs.
    std::tuple<std::vector<double>, std::vector<std::int64_t>,
              std::vector<std::int64_t>, std::vector<double>>
    residual_jacobian_eb_for_test(std::span<const double> psi,
                                  std::span<const double> n,
                                  std::span<const double> p, double V_left,
                                  double V_right, std::span<const double> theta,
                                  std::span<const double> n_lag,
                                  std::span<const double> Jn_lag,
                                  std::span<const double> Qheat_lag) const;
    /// Test-only hook (M20 FD-Jacobian gate): device.py's
    /// _dg_residual_jacobian_eq at V = 0 for the given gamma.
    std::tuple<std::vector<double>, std::vector<std::int64_t>,
              std::vector<std::int64_t>, std::vector<double>>
    dg_residual_jacobian_for_test(std::span<const double> psi,
                                  std::span<const double> Lam_n,
                                  std::span<const double> Lam_p,
                                  double gamma) const;

    /// Test-only hook (FD-Jacobian gate): (F, rows, cols, vals) for an
    /// arbitrary (psi, n, p) at the current contact bias. Not used by
    /// solve_equilibrium/solve_bias themselves.
    std::tuple<std::vector<double>, std::vector<std::int64_t>,
              std::vector<std::int64_t>, std::vector<double>>
    residual_jacobian_for_test(std::span<const double> psi,
                              std::span<const double> n,
                              std::span<const double> p, double V_left,
                              double V_right,
                              std::span<const double> Pn = {},
                              std::span<const double> Pp = {},
                              double strength = 1.0) const;

    /// Test-only hook: device.py's _update_tat_probabilities for `psi`.
    std::pair<std::vector<double>, std::vector<double>>
    tat_probabilities_for_test(std::span<const double> psi) const {
        std::vector<double> Pn, Pp;
        tat_probabilities(std::vector<double>(psi.begin(), psi.end()), Pn, Pp);
        return {std::move(Pn), std::move(Pp)};
    }

private:
    ContactBC contact_value(int side, double V) const;  // side 0=left, 1=right
    /// device.py's Device1D._fd_neutral_eta, one node -- used both for
    /// the FD/incomplete_ion equilibrium initial guess (every node) and
    /// FD ohmic-contact values (nodes 0/N-1, via contact_value).
    double fd_neutral_eta(int node) const;
    /// device.py's _update_tat_probabilities: WKB probabilities from
    /// the node fields of `psi` (frozen for one solve_bias call).
    void tat_probabilities(const std::vector<double>& psi,
                           std::vector<double>& Pn,
                           std::vector<double>& Pp) const;
    /// device.py's solve_equilibrium's `if fd or ion:` branch -- a
    /// separate scalar Newton loop (still Poisson-only, still
    /// tridiagonal) from the baseline one solve_equilibrium already
    /// has, exactly mirroring the Python function's own two-branch
    /// shape rather than threading FD conditionals through the
    /// baseline loop.
    void solve_equilibrium_fd(const NewtonOptions& opts);
    /// device.py's solve_equilibrium, Boltzmann (non-FD/ion/DG) branch.
    void solve_equilibrium_boltzmann(const NewtonOptions& opts);
    ResidualJacobian residual_jacobian(const std::vector<double>& psi,
                                       const std::vector<double>& n,
                                       const std::vector<double>& p,
                                       const std::array<ContactBC, 2>& bc,
                                       const std::vector<double>& Pn,
                                       const std::vector<double>& Pp,
                                       double strength,
                                       const NlPaths& paths,
                                       const EbLag* eb = nullptr) const;
    /// One Newton solve at the given contact BCs (in place on psi/n/p),
    /// with the frozen TAT probabilities Pn_/Pp_ (ignored unless tat),
    /// frozen tunnel paths nl_paths_, and generation strength `strength`.
    /// `line_search` = device.py's `stiff_gen` (backtracking), `floored`
    /// = its `floored` (density-floor update test). `theta`/`Jn_prev` are
    /// the energy-balance state (null unless models.energy_balance),
    /// carried across calls exactly like device.py's closure variables.
    /// Not const: field_mobility/energy_balance re-set dn_edge_/dp_edge_
    /// every iterate, as device.py's _set_edge_diffusivity does. Returns
    /// (converged, final err).
    std::pair<bool, double> newton(const std::array<ContactBC, 2>& bc,
                                   const NewtonOptions& opts, double strength,
                                   bool line_search, bool floored,
                                   std::vector<double>& psi,
                                   std::vector<double>& n,
                                   std::vector<double>& p,
                                   std::vector<double>* theta = nullptr,
                                   std::vector<double>* Jn_prev = nullptr);
    /// device.py's _set_edge_diffusivity from per-node mobilities.
    void set_edge_diffusivity(const std::vector<double>& mu_n,
                              const std::vector<double>& mu_p);
    /// materials.mobility_field (Canali) for carrier 0 = n, 1 = p.
    std::vector<double> mobility_field(const std::vector<double>& mu0,
                                       const std::vector<double>& E,
                                       int carrier) const;
    /// M20: device.py's _solve_equilibrium_dg_coupled and its helpers.
    void solve_equilibrium_dg(const NewtonOptions& opts);
    std::pair<std::vector<double>, Coo> dg_residual_jacobian(
        const std::vector<double>& psi, const std::vector<double>& Lam_n,
        const std::vector<double>& Lam_p, const std::array<ContactBC, 2>& bc,
        double gamma) const;
    bool dg_newton(std::vector<double>& psi, std::vector<double>& Lam_n,
                   std::vector<double>& Lam_p, const std::array<ContactBC, 2>& bc,
                   double gamma, int max_iter, double tol) const;

    int N_;
    double T_, VT_, eps_, ni_, Ns_, LD_, J0_, R0_;
    std::vector<double> x_, doping_, xs_, h_, dV_, C_;
    std::vector<double> mu_n0_, mu_p0_, tau_n_, tau_p_, nie_, nie_s_;
    std::vector<double> dn_edge_, dp_edge_;
    double Cn_auger_, Cp_auger_;
    Models models_;

    // Phase 2 slice 1: only populated (nonempty) when models_.fd ||
    // models_.incomplete_ion; harmless-empty otherwise (nothing reads
    // them on the baseline path).
    std::vector<double> nc_s_, nv_s_, ln_gn_, ln_gp_, eg_kt_, nd_arr_, na_arr_;
    double ded_kt_ = 0.0;
    tcad::physics::FermiTable fermi_table_;

    // Phase 2 slice 2 (TAT): psi-independent exponent numerators (set
    // at construction) and the per-solve_bias frozen probabilities.
    std::vector<double> tat_kn_, tat_kp_;
    std::vector<double> Pn_, Pp_;

    // Phase 2 slice 3 (impact/btbt): last solve's ladder stage + sources.
    double ii_strength_ = 1.0;
    std::vector<double> ii_gs_cache_, btbt_gs_cache_;

    // Phase 2 slice 4.
    NonlocalParams nl_;
    NlPaths nl_paths_;
    bool nl_paths_valid_ = false;  // false == device.py's `_btbt_nl_paths is None`
    int nl_refreshes_ = 0;
    int nl_stable_ = -1;
    NlPaths build_nl_paths(const std::vector<std::int64_t>& starts,
                           const std::vector<std::int64_t>& ends) const;
    /// ii_nonlocal.effective_field (x in cm). D is row-major N x N.
    void effective_field(const std::vector<double>& psi, int carrier,
                         std::vector<double>& E, std::vector<double>* D) const;

    // Phase 2 slice 5 (see Extras); et_/band_shift_/Cn_/Cp_ are always
    // full-length (homojunction defaults filled in by the constructor).
    Extras ex_;
    std::function<void(const std::string&)> log_;
    bool collect_stats_ = false;
    mutable Stats stats_;
    /// solve_direct_lu, timed and sized into stats_ when collecting.
    std::vector<double> lu_solve(const Coo& J, std::int64_t M,
                                 const std::vector<double>& rhs) const;
    /// residual_jacobian, timed into stats_ when collecting.
    ResidualJacobian assemble(const std::vector<double>& psi, const std::vector<double>& n,
                              const std::vector<double>& p,
                              const std::array<ContactBC, 2>& bc, double strength,
                              const EbLag* eb = nullptr) const;
    std::vector<double> et_, band_shift_, Cn_node_, Cp_node_;
    std::vector<double> Tn_;            // empty == device.py's Tn None
    std::vector<double> Lam_n_, Lam_p_;  // M20 DG equilibrium potentials

    std::vector<double> psi_, n_, p_;
    std::vector<double> Jn_scaled_, Jp_scaled_;  // last solve's per-edge currents
    bool has_solution_ = false;
    bool last_converged_ = false;
    bool last_eq_converged_ = true;
    double last_newton_err_ = 0.0;
};

}  // namespace tcad::device1d
