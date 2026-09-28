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
#include <nanobind/stl/optional.h>
#include <nanobind/stl/pair.h>
#include <nanobind/stl/tuple.h>
#include <nanobind/stl/vector.h>

#include "tcad/device1d/device1d.hpp"

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
    nb::class_<tcad::device1d::Models>(m, "Device1DModels")
        .def(nb::init<>())
        .def_rw("srh", &tcad::device1d::Models::srh)
        .def_rw("auger", &tcad::device1d::Models::auger)
        .def_rw("fd", &tcad::device1d::Models::fd)
        .def_rw("incomplete_ion", &tcad::device1d::Models::incomplete_ion)
        .def_rw("tat", &tcad::device1d::Models::tat)
        .def_rw("impact", &tcad::device1d::Models::impact)
        .def_rw("btbt", &tcad::device1d::Models::btbt);

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
                        std::optional<F64> tat_kp) {
                 return new tcad::device1d::Device1D(
                     to_vec(x), to_vec(doping), T, VT, eps, ni, to_vec(mu_n0),
                     to_vec(mu_p0), to_vec(tau_n), to_vec(tau_p), to_vec(nie),
                     Cn_auger, Cp_auger, models,
                     to_vec(nc_s), to_vec(nv_s), to_vec(ln_gn), to_vec(ln_gp),
                     to_vec(eg_kt), to_vec(nd_arr), to_vec(na_arr), ded_kt,
                     to_vec(fermi_e), to_vec(fermi_g), to_vec(fermi_gp),
                     to_vec(fermi_q), to_vec(fermi_qp), to_vec(tat_kn),
                     to_vec(tat_kp));
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
             nb::arg("tat_kn") = nb::none(), nb::arg("tat_kp") = nb::none())
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
        .def_prop_ro("N", &tcad::device1d::Device1D::N)
        .def_prop_ro("Pn", [](const tcad::device1d::Device1D& d) { return d.Pn(); })
        .def_prop_ro("Pp", [](const tcad::device1d::Device1D& d) { return d.Pp(); })
        .def_prop_ro("ii_gs_cache",
                     [](const tcad::device1d::Device1D& d) { return d.ii_gs_cache(); })
        .def_prop_ro("btbt_gs_cache",
                     [](const tcad::device1d::Device1D& d) { return d.btbt_gs_cache(); })
        .def_prop_ro("ii_strength", &tcad::device1d::Device1D::ii_strength)
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
