// Bindings for the shared physics kernels (core/src/physics/kernels.cpp)
// -- bernoulli/dbernoulli and the Boltzmann recombination formula, the
// Phase 0 foundation of the Device1D native port (see
// pytcad/M31-CPP-ARCHITECTURE-PLAN.md and the device1d port plan).
#include <nanobind/nanobind.h>
#include <nanobind/ndarray.h>

#include <cstdint>
#include <span>
#include <vector>

#include "tcad/base/checks.hpp"
#include "tcad/physics/kernels.hpp"

namespace nb = nanobind;

namespace {

using F64 = nb::ndarray<const double, nb::ndim<1>, nb::c_contig>;

template <typename T>
nb::ndarray<nb::numpy, T> publish(std::vector<T>&& v) {
    auto* held = new std::vector<T>(std::move(v));
    nb::capsule owner(held, [](void* p) noexcept { delete static_cast<std::vector<T>*>(p); });
    return nb::ndarray<nb::numpy, T>(held->data(), {held->size()}, owner);
}

template <typename T, typename Nd>
std::span<const T> as_span(const Nd& a) {
    return std::span<const T>(a.data(), static_cast<std::size_t>(a.shape(0)));
}

}  // namespace

void register_physics(nb::module_& m) {
    m.def("bernoulli", [](F64 x) {
        std::vector<double> out(x.shape(0));
        tcad::physics::bernoulli_array(as_span<double>(x), out);
        return publish(std::move(out));
    }, nb::arg("x"),
       "B(x) = x / (exp(x) - 1), elementwise. Bit-identical to "
       "pytcad.kernels.bernoulli.");

    m.def("dbernoulli", [](F64 x) {
        std::vector<double> out(x.shape(0));
        tcad::physics::dbernoulli_array(as_span<double>(x), out);
        return publish(std::move(out));
    }, nb::arg("x"),
       "dB/dx, elementwise. Bit-identical to pytcad.kernels.dbernoulli.");

    m.def("recombination_boltzmann", [](F64 n, F64 p, F64 nie, F64 tau_n,
                                        F64 tau_p, double Cn_auger,
                                        double Cp_auger, bool auger) {
        const auto count = static_cast<std::int64_t>(n.shape(0));
        tcad::check_length(p.shape(0), count, "p");
        tcad::check_length(nie.shape(0), count, "nie");
        tcad::check_length(tau_n.shape(0), count, "tau_n");
        tcad::check_length(tau_p.shape(0), count, "tau_p");
        std::vector<double> R(count), dRdn(count), dRdp(count);
        tcad::physics::recombination_boltzmann_array(
            as_span<double>(n), as_span<double>(p), as_span<double>(nie),
            as_span<double>(tau_n), as_span<double>(tau_p), Cn_auger,
            Cp_auger, auger, R, dRdn, dRdp);
        return nb::make_tuple(publish(std::move(R)), publish(std::move(dRdn)),
                              publish(std::move(dRdp)));
    }, nb::arg("n"), nb::arg("p"), nb::arg("nie"), nb::arg("tau_n"),
       nb::arg("tau_p"), nb::arg("Cn_auger"), nb::arg("Cp_auger"),
       nb::arg("auger"),
       "Net SRH+Auger recombination and its derivatives (Boltzmann "
       "equilibrium form, np_eq=None). Bit-identical to "
       "materials.recombination() with np_eq/dnpq_dn/dnpq_dp=None.");
}
