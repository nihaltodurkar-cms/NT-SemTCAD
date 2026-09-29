// Compiled evaluation of everything Device1D.__init__ derives from its raw
// inputs (mesh, doping, T, material parameters, Models flags): the scaling
// (Ns/LD/J0/R0), scaled geometry, the per-node material fields (nie with
// bandgap narrowing, Caughey-Thomas mobilities, Scharfetter lifetimes,
// band-DOS arrays), the M33 affinity band shift and thermionic edge
// arrays, the harmonic-mean edge diffusivities, the M44 energy-balance
// constants, the M12 TAT exponent numerators, the M46 Schottky contact
// densities and the M34 nonlocal-BTBT band parameters.
//
// device.py assigns the results to the same Python attributes it always
// had, so attribute access (and post-construction mutation, which rebuilds
// the solver from those attributes) is unchanged.
#pragma once

#include <array>
#include <cstdint>
#include <span>
#include <vector>

#include "tcad/physics/materials.hpp"

namespace tcad::device1d {

struct InputFlags {
    bool bgn = true;
    bool doping_mobility = true;
    bool affinity = false;     // Models.band_offset == "affinity"
    bool thermionic = false;
    bool tat = false;
    double trap_et_rel = 0.5;
    bool btbt_nonlocal = false;
    double tau_w = 0.0;        // hydrodynamic.TAU_W_N (M44 constants)
    bool fd_or_ion = false;    // Schottky specs are ignored then (FD contacts)
    // M46: per side, whether a Schottky contact is attached, its metal
    // work function [eV].
    std::array<bool, 2> schottky{false, false};
    std::array<double, 2> phi_metal_eV{0.0, 0.0};
    double Ns = 0.0;           // > 0: use this scale instead of max(|doping|, ni)
};

struct Inputs {
    double VT = 0, ni = 0, Ns = 0, LD = 0, J0 = 0, R0 = 0, eps = 0;
    std::vector<double> eps_arr, chi_arr, Eg0_arr, xs, h, dV, C;
    std::vector<double> nie, mu_n0, mu_p0, tau_n, tau_p, nie_s;
    std::vector<double> nc_s, nv_s, ln_gn, ln_gp, eg_kt, band_shift;
    std::vector<std::uint8_t> te_edge;                 // N-1
    std::vector<double> te_Kn, te_Kp, te_dlnNc, te_dlnNv, te_rNc, te_rNv;  // thermionic
    std::vector<double> nd_arr, na_arr, dn_edge, dp_edge, et;
    double alpha_relax = 0, kappa0 = 0;
    std::vector<double> tat_kn, tat_kp;                // tat
    std::vector<double> Cn_auger, Cp_auger, m_n_star, m_p_star;
    std::array<bool, 2> contact_override{false, false};
    std::array<double, 2> contact_n0{0, 0}, contact_p0{0, 0};
    // M34-S1 (btbt_nonlocal): material 0's band parameters.
    double nl_Eg_eV = 0, nl_Eg_J = 0, nl_mr = 0, nl_mc = 0, nl_mv = 0, nl_u = 0;
};

/// `node_mat[i]` indexes `mats` (identity-grouped in Python: nodes that
/// share one Semiconductor object share an index; an edge whose two nodes
/// have different indices is a heterointerface).
Inputs evaluate_inputs(std::span<const double> x, std::span<const double> doping,
                       std::span<const double> Ntot, double T,
                       std::span<const physics::MaterialParams> mats,
                       std::span<const std::int64_t> node_mat, const InputFlags& flags);

}  // namespace tcad::device1d
