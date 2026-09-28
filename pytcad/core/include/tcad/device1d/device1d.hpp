// Native C++ port of pytcad.device.Device1D. Phase 1: baseline physics
// only (homojunction, Boltzmann statistics, full ionization;
// doping_mobility/srh/auger/bgn on or off). Phase 2 slice 1 (this
// revision) adds Fermi-Dirac statistics (Models.fd) and incomplete
// ionization (Models.incomplete_ion) -- see device1d.cpp's per-method
// comments for exactly which Python branches each addition mirrors.
// Phase 2 slice 2 adds M12-S2 trap-assisted tunneling (Models.tat);
// slice 3 adds M15 local impact ionization (Models.impact) and M16
// local Kane BTBT (Models.btbt) with the stiff-generation ladder.
// Every OTHER Models() flag (impact/btbt nonlocal, dg, S_n/S_p,
// thermionic, heterojunction, energy_balance) is still Phase 2+ and
// refused by the Python wrapper before construction, not by this
// class -- see device.py's own thin-wrapper dispatch
// (_NATIVE_DEVICE1D_MODEL_FIELDS).
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
};

// device.py's stiff-path constants (see their long comments there).
inline constexpr double kIiStages[] = {0.0, 0.02, 0.05, 0.1, 0.2, 0.35, 0.5, 0.7, 1.0};
inline constexpr int kLsMaxHalvings = 10;           // _LS_MAX_HALVINGS
inline constexpr double kLsNewtonRegion = 1e-3;     // _LS_NEWTON_REGION
inline constexpr double kStiffDensityFloor = 1e-8;  // _STIFF_DENSITY_FLOOR
inline constexpr double kIiJEpsRel = 1e-6;          // _II_J_EPS_REL

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
             std::span<const double> tat_kp = {});

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
    /// Overwrite the solution state (scaled psi/n/p, length N) -- the
    /// Python wrapper's attributes are the source of truth for the warm
    /// start (continuation.py restores dev.psi/n/p after a failed step
    /// and then calls solve_bias again).
    void set_state(std::span<const double> psi, std::span<const double> n,
                   std::span<const double> p);
    bool has_solution() const { return has_solution_; }
    bool last_converged() const { return last_converged_; }
    double last_newton_err() const { return last_newton_err_; }

    struct ResidualJacobian {
        std::vector<double> F;   // length 3*N
        Coo J;                   // 3N x 3N
        std::vector<double> Jn;  // per-edge scaled electron current, length N-1
        std::vector<double> Jp;
        std::vector<double> ii_gs;    // empty unless models.impact
        std::vector<double> btbt_gs;  // empty unless models.btbt
    };

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
    ResidualJacobian residual_jacobian(const std::vector<double>& psi,
                                       const std::vector<double>& n,
                                       const std::vector<double>& p,
                                       const std::array<ContactBC, 2>& bc,
                                       const std::vector<double>& Pn,
                                       const std::vector<double>& Pp,
                                       double strength) const;
    /// One Newton solve at the given contact BCs (in place on psi/n/p),
    /// with the frozen TAT probabilities Pn_/Pp_ (ignored unless tat) and
    /// generation strength `strength`. `stiff` selects device.py's
    /// stiff-path update test (density floor) and backtracking line
    /// search. Returns (converged, final err).
    std::pair<bool, double> newton(const std::array<ContactBC, 2>& bc,
                                   const NewtonOptions& opts, double strength,
                                   bool stiff,
                                   std::vector<double>& psi,
                                   std::vector<double>& n,
                                   std::vector<double>& p) const;

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

    std::vector<double> psi_, n_, p_;
    std::vector<double> Jn_scaled_, Jp_scaled_;  // last solve's per-edge currents
    std::array<std::int64_t, 6> dirichlet_rows_{};
    bool has_solution_ = false;
    bool last_converged_ = false;
    double last_newton_err_ = 0.0;
};

}  // namespace tcad::device1d
