#include "tcad/physics/kernels.hpp"

namespace tcad::physics {

void bernoulli_array(std::span<const double> x, std::span<double> out) {
    const std::size_t n = x.size();
    for (std::size_t i = 0; i < n; ++i) out[i] = bernoulli(x[i]);
}

void dbernoulli_array(std::span<const double> x, std::span<double> out) {
    const std::size_t n = x.size();
    for (std::size_t i = 0; i < n; ++i) out[i] = dbernoulli(x[i]);
}

void recombination_boltzmann_array(
    std::span<const double> n, std::span<const double> p,
    std::span<const double> nie, std::span<const double> tau_n,
    std::span<const double> tau_p, double Cn_auger, double Cp_auger,
    bool auger, std::span<double> R, std::span<double> dRdn,
    std::span<double> dRdp) {
    const std::size_t count = n.size();
    for (std::size_t i = 0; i < count; ++i) {
        const RecombinationResult r = recombination_boltzmann(
            n[i], p[i], nie[i], tau_n[i], tau_p[i], Cn_auger, Cp_auger, auger);
        R[i] = r.R;
        dRdn[i] = r.dRdn;
        dRdp[i] = r.dRdp;
    }
}

}  // namespace tcad::physics
