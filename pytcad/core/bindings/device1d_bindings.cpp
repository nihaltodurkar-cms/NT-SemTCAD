// Bindings for tcad::device1d::Device1D (core/src/device1d/device1d.cpp)
// -- the FIRST stateful, instance-constructed C++ object bound in this
// codebase (every prior binding is free functions over arrays, or a
// static-only registry -- see the port plan's Phase 0 note). Ownership
// is nanobind's default (the Python object owns the one C++ instance,
// destroyed on GC); no holder policy needed since nothing else ever
// references it. Long solves (`solve_equilibrium`, `solve_bias`) hold
// the GIL for now -- Phase 1 has no other Python-side work happening
// concurrently with a solve, so releasing it is a Phase-2-or-later
// optimization once there's an actual concurrency benefit to measure,
// not before.
#include <nanobind/nanobind.h>
#include <nanobind/ndarray.h>
#include <nanobind/stl/array.h>
#include <nanobind/stl/function.h>
#include <nanobind/stl/optional.h>
#include <nanobind/stl/string.h>
#include <nanobind/stl/pair.h>
#include <nanobind/stl/tuple.h>
#include <nanobind/stl/vector.h>

#include "tcad/device1d/device1d.hpp"
#include "tcad/device1d/inputs.hpp"
#include "tcad/physics/materials.hpp"

namespace nb = nanobind;

namespace {
using F64 = nb::ndarray<const double, nb::ndim<1>, nb::c_contig>;

std::vector<double> to_vec(F64 a) {
    return std::vector<double>(a.data(), a.data() + a.shape(0));
}

std::vector<double> to_vec(const std::optional<F64>& a) {
    return a ? to_vec(*a) : std::vector<double>{};
}
}  // namespace

void register_device1d(nb::module_& m) {
    // --- compiled material / input evaluation (tcad/device1d/inputs.hpp) ---
    using MP = tcad::physics::MaterialParams;
    auto mp = nb::class_<MP>(m, "MaterialParams").def(nb::init<>());
#define TCAD_MP_FIELD(f) mp.def_rw(#f, &MP::f);
    TCAD_MP_FIELD(eps_r) TCAD_MP_FIELD(chi) TCAD_MP_FIELD(Eg0) TCAD_MP_FIELD(varshni_alpha)
    TCAD_MP_FIELD(varshni_beta) TCAD_MP_FIELD(Nc300) TCAD_MP_FIELD(Nv300)
    TCAD_MP_FIELD(mu_n_min) TCAD_MP_FIELD(mu_n_max) TCAD_MP_FIELD(mu_n_Nref)
    TCAD_MP_FIELD(mu_n_alpha) TCAD_MP_FIELD(mu_n_Texp) TCAD_MP_FIELD(mu_p_min)
    TCAD_MP_FIELD(mu_p_max) TCAD_MP_FIELD(mu_p_Nref) TCAD_MP_FIELD(mu_p_alpha)
    TCAD_MP_FIELD(mu_p_Texp) TCAD_MP_FIELD(vsat_n) TCAD_MP_FIELD(vsat_p) TCAD_MP_FIELD(beta_n)
    TCAD_MP_FIELD(beta_p) TCAD_MP_FIELD(tau_n0) TCAD_MP_FIELD(tau_p0) TCAD_MP_FIELD(tau_Nref)
    TCAD_MP_FIELD(Cn_auger) TCAD_MP_FIELD(Cp_auger) TCAD_MP_FIELD(bgn_E0) TCAD_MP_FIELD(bgn_N0)
    TCAD_MP_FIELD(m_n_star) TCAD_MP_FIELD(m_p_star)
#undef TCAD_MP_FIELD

    m.def("device1d_inputs",
          [](F64 x, F64 doping, F64 Ntot, double T, const std::vector<MP>& mats,
             const std::vector<std::int64_t>& node_mat, bool bgn, bool doping_mobility,
             bool affinity, bool thermionic, bool tat, double trap_et_rel,
             bool btbt_nonlocal, double tau_w, bool fd_or_ion,
             std::array<bool, 2> schottky, std::array<double, 2> phi_metal_eV, double Ns) {
              tcad::device1d::InputFlags f;
              f.bgn = bgn; f.doping_mobility = doping_mobility; f.affinity = affinity;
              f.thermionic = thermionic; f.tat = tat; f.trap_et_rel = trap_et_rel;
              f.btbt_nonlocal = btbt_nonlocal; f.tau_w = tau_w; f.fd_or_ion = fd_or_ion;
              f.schottky = schottky; f.phi_metal_eV = phi_metal_eV; f.Ns = Ns;
              const tcad::device1d::Inputs r = tcad::device1d::evaluate_inputs(
                  to_vec(x), to_vec(doping), to_vec(Ntot), T, mats, node_mat, f);
              nb::dict d;
#define TCAD_OUT(k) d[#k] = r.k;
              TCAD_OUT(VT) TCAD_OUT(ni) TCAD_OUT(Ns) TCAD_OUT(LD) TCAD_OUT(J0) TCAD_OUT(R0)
              TCAD_OUT(eps) TCAD_OUT(eps_arr) TCAD_OUT(chi_arr) TCAD_OUT(Eg0_arr) TCAD_OUT(xs)
              TCAD_OUT(h) TCAD_OUT(dV) TCAD_OUT(C) TCAD_OUT(nie) TCAD_OUT(mu_n0) TCAD_OUT(mu_p0)
              TCAD_OUT(tau_n) TCAD_OUT(tau_p) TCAD_OUT(nie_s) TCAD_OUT(nc_s) TCAD_OUT(nv_s)
              TCAD_OUT(ln_gn) TCAD_OUT(ln_gp) TCAD_OUT(eg_kt) TCAD_OUT(band_shift)
              TCAD_OUT(te_edge) TCAD_OUT(te_Kn) TCAD_OUT(te_Kp) TCAD_OUT(te_dlnNc)
              TCAD_OUT(te_dlnNv) TCAD_OUT(te_rNc) TCAD_OUT(te_rNv) TCAD_OUT(nd_arr)
              TCAD_OUT(na_arr) TCAD_OUT(dn_edge) TCAD_OUT(dp_edge) TCAD_OUT(et)
              TCAD_OUT(alpha_relax) TCAD_OUT(kappa0) TCAD_OUT(tat_kn) TCAD_OUT(tat_kp)
              TCAD_OUT(Cn_auger) TCAD_OUT(Cp_auger) TCAD_OUT(m_n_star) TCAD_OUT(m_p_star)
              TCAD_OUT(contact_override) TCAD_OUT(contact_n0) TCAD_OUT(contact_p0)
              TCAD_OUT(nl_Eg_eV) TCAD_OUT(nl_Eg_J) TCAD_OUT(nl_mr) TCAD_OUT(nl_mc)
              TCAD_OUT(nl_mv) TCAD_OUT(nl_u)
#undef TCAD_OUT
              return d;
          },
          nb::arg("x"), nb::arg("doping"), nb::arg("Ntot"), nb::arg("T"), nb::arg("mats"),
          nb::arg("node_mat"), nb::arg("bgn"), nb::arg("doping_mobility"),
          nb::arg("affinity"), nb::arg("thermionic"), nb::arg("tat"),
          nb::arg("trap_et_rel"), nb::arg("btbt_nonlocal"), nb::arg("tau_w"),
          nb::arg("fd_or_ion"), nb::arg("schottky"), nb::arg("phi_metal_eV"),
          nb::arg("Ns") = 0.0,
          "Device1D's derived inputs from raw ones (tcad/device1d/inputs.hpp).");
    m.attr("MATERIAL_CONSTANTS") = nb::make_tuple(
        tcad::physics::kQ, tcad::physics::kKB, tcad::physics::kKB_EV, tcad::physics::kEPS0,
        tcad::physics::kHBAR, tcad::physics::kM0);

    nb::class_<tcad::device1d::Models>(m, "Device1DModels")
        .def(nb::init<>())
        .def_rw("srh", &tcad::device1d::Models::srh)
        .def_rw("auger", &tcad::device1d::Models::auger)
        .def_rw("fd", &tcad::device1d::Models::fd)
        .def_rw("incomplete_ion", &tcad::device1d::Models::incomplete_ion)
        .def_rw("tat", &tcad::device1d::Models::tat)
        .def_rw("impact", &tcad::device1d::Models::impact)
        .def_rw("btbt", &tcad::device1d::Models::btbt)
        .def_rw("impact_nonlocal", &tcad::device1d::Models::impact_nonlocal)
        .def_rw("btbt_nonlocal", &tcad::device1d::Models::btbt_nonlocal)
        .def_rw("thermionic", &tcad::device1d::Models::thermionic)
        .def_rw("field_mobility", &tcad::device1d::Models::field_mobility)
        .def_rw("energy_balance", &tcad::device1d::Models::energy_balance)
        .def_rw("dg", &tcad::device1d::Models::dg);

    // Phase 2 slice 5 inputs. Vector fields take any float sequence
    // (the wrapper passes ndarray.tolist(), exact doubles).
    using EX = tcad::device1d::Extras;
    nb::class_<EX>(m, "Device1DExtras")
        .def(nb::init<>())
        .def_rw("Ns", &EX::Ns)
        .def_rw("et", &EX::et)
        .def_rw("band_shift", &EX::band_shift)
        .def_rw("Cn_auger", &EX::Cn_auger)
        .def_rw("Cp_auger", &EX::Cp_auger)
        .def_rw("te_edge", &EX::te_edge)
        .def_rw("te_Kn", &EX::te_Kn)
        .def_rw("te_Kp", &EX::te_Kp)
        .def_rw("te_dlnNc", &EX::te_dlnNc)
        .def_rw("te_dlnNv", &EX::te_dlnNv)
        .def_rw("te_rNc", &EX::te_rNc)
        .def_rw("te_rNv", &EX::te_rNv)
        .def("set_contact_override",
             [](EX& e, int side, double n0, double p0) {
                 if (side < 0 || side > 1) throw nb::index_error("side must be 0 or 1");
                 e.contact_override[static_cast<std::size_t>(side)] = true;
                 e.contact_n0[static_cast<std::size_t>(side)] = n0;
                 e.contact_p0[static_cast<std::size_t>(side)] = p0;
             }, nb::arg("side"), nb::arg("n0"), nb::arg("p0"))
        .def("set_surface_velocity",
             [](EX& e, int side, double S_n_s, double S_p_s) {
                 if (side < 0 || side > 1) throw nb::index_error("side must be 0 or 1");
                 e.S_n_s[static_cast<std::size_t>(side)] = S_n_s;
                 e.S_p_s[static_cast<std::size_t>(side)] = S_p_s;
             }, nb::arg("side"), nb::arg("S_n_s"), nb::arg("S_p_s"))
        .def_rw("vsat_n", &EX::vsat_n)
        .def_rw("beta_n", &EX::beta_n)
        .def_rw("vsat_p", &EX::vsat_p)
        .def_rw("beta_p", &EX::beta_p)
        .def_rw("alpha_relax", &EX::alpha_relax)
        .def_rw("kappa0", &EX::kappa0)
        .def_rw("tau_w", &EX::tau_w)
        .def_rw("kB", &EX::kB)
        .def_rw("q_hydro", &EX::q_hydro)
        .def_rw("m_n_star", &EX::m_n_star)
        .def_rw("m_p_star", &EX::m_p_star)
        .def_rw("dg_gamma", &EX::dg_gamma)
        .def_rw("dg_hbar", &EX::dg_hbar)
        .def_rw("dg_m0", &EX::dg_m0)
        .def_rw("dg_q", &EX::dg_q);

    using NLP = tcad::device1d::NonlocalParams;
    nb::class_<NLP>(m, "Device1DNonlocalParams")
        .def(nb::init<>())
        .def_rw("lambda_n_cm", &NLP::lambda_n_cm)
        .def_rw("lambda_p_cm", &NLP::lambda_p_cm)
        .def_rw("Eg_eV", &NLP::Eg_eV)
        .def_rw("Eg_J", &NLP::Eg_J)
        .def_rw("mr_kg", &NLP::mr_kg)
        .def_rw("mc_kg", &NLP::mc_kg)
        .def_rw("mv_kg", &NLP::mv_kg)
        .def_rw("u", &NLP::u)
        .def_rw("hbar", &NLP::hbar)
        .def_rw("q", &NLP::q);

    nb::class_<tcad::device1d::NewtonOptions>(m, "Device1DNewtonOptions")
        .def(nb::init<>())
        .def_rw("max_iter", &tcad::device1d::NewtonOptions::max_iter)
        .def_rw("tol_update", &tcad::device1d::NewtonOptions::tol_update)
        .def_rw("max_dpsi", &tcad::device1d::NewtonOptions::max_dpsi)
        .def_rw("verbose", &tcad::device1d::NewtonOptions::verbose);

    nb::class_<tcad::device1d::Device1D>(m, "Device1D")
        .def(nb::new_([](F64 x, F64 doping, double T, double VT, double eps,
                        double ni, F64 mu_n0, F64 mu_p0, F64 tau_n, F64 tau_p,
                        F64 nie, double Cn_auger, double Cp_auger,
                        const tcad::device1d::Models& models,
                        F64 nc_s, F64 nv_s, F64 ln_gn, F64 ln_gp, F64 eg_kt,
                        F64 nd_arr, F64 na_arr, double ded_kt,
                        F64 fermi_e, F64 fermi_g, F64 fermi_gp, F64 fermi_q,
                        F64 fermi_qp, std::optional<F64> tat_kn,
                        std::optional<F64> tat_kp, std::optional<NLP> nl,
                        std::optional<EX> ex) {
                 return new tcad::device1d::Device1D(
                     to_vec(x), to_vec(doping), T, VT, eps, ni, to_vec(mu_n0),
                     to_vec(mu_p0), to_vec(tau_n), to_vec(tau_p), to_vec(nie),
                     Cn_auger, Cp_auger, models,
                     to_vec(nc_s), to_vec(nv_s), to_vec(ln_gn), to_vec(ln_gp),
                     to_vec(eg_kt), to_vec(nd_arr), to_vec(na_arr), ded_kt,
                     to_vec(fermi_e), to_vec(fermi_g), to_vec(fermi_gp),
                     to_vec(fermi_q), to_vec(fermi_qp), to_vec(tat_kn),
                     to_vec(tat_kp), nl ? *nl : NLP{}, ex ? *ex : EX{});
             }),
             nb::arg("x"), nb::arg("doping"), nb::arg("T"), nb::arg("VT"),
             nb::arg("eps"), nb::arg("ni"), nb::arg("mu_n0"), nb::arg("mu_p0"),
             nb::arg("tau_n"), nb::arg("tau_p"), nb::arg("nie"),
             nb::arg("Cn_auger"), nb::arg("Cp_auger"), nb::arg("models"),
             // Phase 2 slice 1 (FD / incomplete ionization). The Python
             // wrapper always passes SOMETHING for these (real per-node
             // arrays when models.fd or models.incomplete_ion, else
             // length-0 arrays) -- see device.py's native-dispatch
             // construction site.
             nb::arg("nc_s"), nb::arg("nv_s"), nb::arg("ln_gn"), nb::arg("ln_gp"),
             nb::arg("eg_kt"), nb::arg("nd_arr"), nb::arg("na_arr"),
             nb::arg("ded_kt"), nb::arg("fermi_e"), nb::arg("fermi_g"),
             nb::arg("fermi_gp"), nb::arg("fermi_q"), nb::arg("fermi_qp"),
             // Phase 2 slice 2 (TAT): keyword-optional so pre-existing
             // callers (the Phase-1/slice-1 gate files) keep working.
             nb::arg("tat_kn") = nb::none(), nb::arg("tat_kp") = nb::none(),
             nb::arg("nl") = nb::none(), nb::arg("ex") = nb::none())
        .def("solve_equilibrium",
             [](tcad::device1d::Device1D& d, const tcad::device1d::NewtonOptions& o) {
                 d.solve_equilibrium(o);
             }, nb::arg("opts"))
        .def("solve_bias",
             [](tcad::device1d::Device1D& d, double V_left, double V_right,
                const tcad::device1d::NewtonOptions& o) {
                 return d.solve_bias({V_left, V_right}, o);
             }, nb::arg("V_left"), nb::arg("V_right"), nb::arg("opts"))
        .def_prop_ro("psi", [](const tcad::device1d::Device1D& d) { return d.psi(); })
        .def_prop_ro("n", [](const tcad::device1d::Device1D& d) { return d.n(); })
        .def_prop_ro("p", [](const tcad::device1d::Device1D& d) { return d.p(); })
        .def_prop_ro("psi_V", &tcad::device1d::Device1D::psi_V)
        .def_prop_ro("n_cm3", &tcad::device1d::Device1D::n_cm3)
        .def_prop_ro("p_cm3", &tcad::device1d::Device1D::p_cm3)
        .def_prop_ro("E_field", &tcad::device1d::Device1D::E_field)
        .def_prop_ro("Jn", &tcad::device1d::Device1D::Jn)
        .def_prop_ro("Jp", &tcad::device1d::Device1D::Jp)
        .def("current_density", &tcad::device1d::Device1D::current_density)
        .def_prop_ro("has_solution", &tcad::device1d::Device1D::has_solution)
        .def_prop_ro("last_converged", &tcad::device1d::Device1D::last_converged)
        .def_prop_ro("last_newton_err", &tcad::device1d::Device1D::last_newton_err)
        .def_prop_ro("last_eq_converged", &tcad::device1d::Device1D::last_eq_converged)
        .def_prop_ro("dn_edge", [](const tcad::device1d::Device1D& d) { return d.dn_edge(); })
        .def_prop_ro("dp_edge", [](const tcad::device1d::Device1D& d) { return d.dp_edge(); })
        .def_prop_ro("Tn", [](const tcad::device1d::Device1D& d) { return d.Tn(); })
        .def("set_Tn",
             [](tcad::device1d::Device1D& d, std::optional<F64> Tn) { d.set_Tn(to_vec(Tn)); },
             nb::arg("Tn").none())
        .def("set_logger",
             [](tcad::device1d::Device1D& d,
                std::optional<std::function<void(const std::string&)>> log) {
                 d.set_logger(log ? *log : std::function<void(const std::string&)>{});
             }, nb::arg("log").none(),
             "Line sink for NewtonOptions.verbose progress (device.py's formats).")
        .def("assemble",
             [](const tcad::device1d::Device1D& d, F64 psi, F64 n, F64 p,
                std::array<double, 3> bc_left, std::array<double, 3> bc_right,
                std::optional<F64> Pn, std::optional<F64> Pp, double strength,
                const std::vector<std::int64_t>& starts,
                const std::vector<std::int64_t>& ends, std::optional<F64> theta,
                std::optional<F64> n_lag, std::optional<F64> Jn_lag,
                std::optional<F64> Qheat_lag) {
                 using D1 = tcad::device1d::Device1D;
                 const std::vector<double> pn = to_vec(Pn), pp = to_vec(Pp);
                 const std::vector<double> th = to_vec(theta), nl = to_vec(n_lag),
                                           jl = to_vec(Jn_lag), qh = to_vec(Qheat_lag);
                 const tcad::device1d::EbLag lag{&th, &nl, &jl, &qh};
                 if (theta && (nl.size() != th.size() || qh.size() != th.size() ||
                               jl.size() + 1 != th.size() ||
                               th.size() != static_cast<std::size_t>(d.N())))
                     throw nb::value_error("theta/n_lag/Qheat_lag need length N, Jn_lag N-1");
                 D1::ResidualJacobian rj = d.assemble_explicit(
                     to_vec(psi), to_vec(n), to_vec(p),
                     {bc_left[0], bc_left[1], bc_left[2]},
                     {bc_right[0], bc_right[1], bc_right[2]}, pn, pp, strength, starts, ends,
                     theta ? &lag : nullptr);
                 return nb::make_tuple(rj.F, rj.J.rows, rj.J.cols, rj.J.vals, rj.Jn, rj.Jp,
                                       rj.dirichlet, rj.ii_gs, rj.btbt_gs);
             },
             nb::arg("psi"), nb::arg("n"), nb::arg("p"), nb::arg("bc_left"),
             nb::arg("bc_right"), nb::arg("Pn").none(), nb::arg("Pp").none(),
             nb::arg("strength"), nb::arg("starts"), nb::arg("ends"),
             nb::arg("theta").none() = nb::none(), nb::arg("n_lag").none() = nb::none(),
             nb::arg("Jn_lag").none() = nb::none(), nb::arg("Qheat_lag").none() = nb::none(),
             "device.py's _residual_jacobian with every frozen input explicit: "
             "(F, rows, cols, vals, Jn, Jp, dirichlet_rows, ii_gs, btbt_gs).")
        .def("set_edge_diffusivity",
             [](tcad::device1d::Device1D& d, F64 dn, F64 dp) {
                 d.set_edge_diffusivity_scaled(to_vec(dn), to_vec(dp));
             }, nb::arg("dn_edge"), nb::arg("dp_edge"))
        .def("contact_values",
             [](const tcad::device1d::Device1D& d, double V_left, double V_right) {
                 const auto bc = d.contact_values(V_left, V_right);
                 return nb::make_tuple(nb::make_tuple(bc[0].psi0, bc[0].n0, bc[0].p0),
                                       nb::make_tuple(bc[1].psi0, bc[1].n0, bc[1].p0));
             }, nb::arg("V_left"), nb::arg("V_right"))
        .def("tat_probabilities",
             [](const tcad::device1d::Device1D& d, F64 psi) {
                 return d.tat_probabilities_at(to_vec(psi));
             }, nb::arg("psi"))
        .def("dg_assemble",
             [](const tcad::device1d::Device1D& d, F64 psi, F64 Lam_n, F64 Lam_p,
                std::array<double, 3> bc_left, std::array<double, 3> bc_right, double gamma) {
                 auto [F, J] = d.dg_assemble(to_vec(psi), to_vec(Lam_n), to_vec(Lam_p),
                                             {bc_left[0], bc_left[1], bc_left[2]},
                                             {bc_right[0], bc_right[1], bc_right[2]}, gamma);
                 return nb::make_tuple(F, J.rows, J.cols, J.vals);
             }, nb::arg("psi"), nb::arg("Lam_n"), nb::arg("Lam_p"), nb::arg("bc_left"),
             nb::arg("bc_right"), nb::arg("gamma"))
        .def("set_collect_stats", &tcad::device1d::Device1D::set_collect_stats,
             nb::arg("on"), "Enable (and reset) the M32 benchmark counters.")
        .def("stats",
             [](const tcad::device1d::Device1D& d) {
                 const auto& s = d.stats();
                 nb::dict out;
                 out["assembly_s"] = s.assembly_s;
                 out["assembly_calls"] = s.assembly_calls;
                 out["linsolve_s"] = s.linsolve_s;
                 out["linsolve_calls"] = s.linsolve_calls;
                 out["dof"] = s.dof;
                 out["nnz"] = s.nnz;
                 return out;
             })
        .def_prop_ro("dg_Lam_n", [](const tcad::device1d::Device1D& d) { return d.dg_Lam_n(); })
        .def_prop_ro("dg_Lam_p", [](const tcad::device1d::Device1D& d) { return d.dg_Lam_p(); })
        .def("_residual_jacobian_eb_for_test",
             [](const tcad::device1d::Device1D& d, F64 psi, F64 n, F64 p, double V_left,
                double V_right, F64 theta, F64 n_lag, F64 Jn_lag, F64 Qheat_lag) {
                 return d.residual_jacobian_eb_for_test(
                     to_vec(psi), to_vec(n), to_vec(p), V_left, V_right, to_vec(theta),
                     to_vec(n_lag), to_vec(Jn_lag), to_vec(Qheat_lag));
             }, nb::arg("psi"), nb::arg("n"), nb::arg("p"), nb::arg("V_left"),
             nb::arg("V_right"), nb::arg("theta"), nb::arg("n_lag"), nb::arg("Jn_lag"),
             nb::arg("Qheat_lag"),
             "M44 FD-Jacobian test hook: the 4N (F, rows, cols, vals).")
        .def("_dg_residual_jacobian_for_test",
             [](const tcad::device1d::Device1D& d, F64 psi, F64 Lam_n, F64 Lam_p,
                double gamma) {
                 return d.dg_residual_jacobian_for_test(to_vec(psi), to_vec(Lam_n),
                                                        to_vec(Lam_p), gamma);
             }, nb::arg("psi"), nb::arg("Lam_n"), nb::arg("Lam_p"), nb::arg("gamma"),
             "M20 FD-Jacobian test hook: _dg_residual_jacobian_eq at V = 0.")
        .def_prop_ro("N", &tcad::device1d::Device1D::N)
        .def_prop_ro("Pn", [](const tcad::device1d::Device1D& d) { return d.Pn(); })
        .def_prop_ro("Pp", [](const tcad::device1d::Device1D& d) { return d.Pp(); })
        .def_prop_ro("ii_gs_cache",
                     [](const tcad::device1d::Device1D& d) { return d.ii_gs_cache(); })
        .def_prop_ro("btbt_gs_cache",
                     [](const tcad::device1d::Device1D& d) { return d.btbt_gs_cache(); })
        .def_prop_ro("ii_strength", &tcad::device1d::Device1D::ii_strength)
        .def_prop_ro("btbt_nl_starts",
                     [](const tcad::device1d::Device1D& d) { return d.btbt_nl_paths().start; })
        .def_prop_ro("btbt_nl_ends",
                     [](const tcad::device1d::Device1D& d) { return d.btbt_nl_paths().end; })
        .def_prop_ro("last_btbt_nl_refreshes",
                     &tcad::device1d::Device1D::last_btbt_nl_refreshes)
        .def_prop_ro("last_btbt_nl_stable",
                     [](const tcad::device1d::Device1D& d) -> nb::object {
                         const int s = d.last_btbt_nl_stable();
                         if (s < 0) return nb::none();
                         return nb::bool_(s == 1);
                     })
        .def("set_btbt_nl_paths",
             [](tcad::device1d::Device1D& d, const std::vector<std::int64_t>& starts,
                const std::vector<std::int64_t>& ends) {
                 d.set_btbt_nl_paths(starts, ends);
             }, nb::arg("starts"), nb::arg("ends"))
        .def("locate_btbt_nl_paths",
             [](const tcad::device1d::Device1D& d, F64 psi) {
                 return d.locate_btbt_nl_paths(to_vec(psi));
             }, nb::arg("psi"))
        .def("_effective_field_for_test",
             [](const tcad::device1d::Device1D& d, F64 psi, int carrier) {
                 return d.effective_field_for_test(to_vec(psi), carrier);
             }, nb::arg("psi"), nb::arg("carrier"))
        .def("set_state",
             [](tcad::device1d::Device1D& d, F64 psi, F64 n, F64 p) {
                 d.set_state(to_vec(psi), to_vec(n), to_vec(p));
             }, nb::arg("psi"), nb::arg("n"), nb::arg("p"))
        .def("_residual_jacobian_for_test",
             [](const tcad::device1d::Device1D& d, F64 psi, F64 n, F64 p,
                double V_left, double V_right, std::optional<F64> Pn,
                std::optional<F64> Pp, double strength) {
                 const std::vector<double> pn = to_vec(Pn), pp = to_vec(Pp);
                 return d.residual_jacobian_for_test(to_vec(psi), to_vec(n),
                                                     to_vec(p), V_left, V_right,
                                                     pn, pp, strength);
             }, nb::arg("psi"), nb::arg("n"), nb::arg("p"), nb::arg("V_left"),
             nb::arg("V_right"), nb::arg("Pn") = nb::none(),
             nb::arg("Pp") = nb::none(), nb::arg("strength") = 1.0,
             "FD-Jacobian test hook: (F, rows, cols, vals) at an arbitrary "
             "(psi, n, p), optionally with explicit frozen TAT probabilities. "
             "Not used by solve_equilibrium/solve_bias.")
        .def("_tat_probabilities_for_test",
             [](const tcad::device1d::Device1D& d, F64 psi) {
                 return d.tat_probabilities_for_test(to_vec(psi));
             }, nb::arg("psi"),
             "Test hook: device.py's _update_tat_probabilities for psi.");
}
