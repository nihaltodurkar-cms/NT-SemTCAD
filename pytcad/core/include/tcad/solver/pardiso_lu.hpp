// MKL PARDISO (parallel supernodal direct solver, nested-dissection
// ordering) behind the same interface as ReusableLU: keeps the symbolic
// analysis while the CSC sparsity pattern is unchanged (or a subset of the
// analyzed one) and redoes only the numeric factorization per Newton
// iteration.
//
// MKL is loaded at RUNTIME (LoadLibrary/dlopen of the path the caller
// gives, normally the Python environment's mkl_rt), so _core has no
// import-time dependency on MKL and needs no MKL headers or import
// library to build -- a missing MKL simply leaves available() false and
// linsolve.DirectSession uses Eigen's SparseLU. Measured 2026-09-29 on
// B3's 2D MOSFET Jacobian (72,912 unknowns): refactor+solve 218 ms (1
// thread) / 99 ms (10 threads) vs Eigen SparseLU 1,456 ms.
//
// PARDISO perturbs tiny pivots instead of failing on a singular matrix, so
// every solve's relative residual is checked (one sparse mat-vec) and a
// result above 1e-6 raises LinearSolveFailure -- the same contract
// ReusableLU/solve_linear keep for a singular system.
#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace tcad::solver {

class PardisoLU {
public:
    /// Load mkl_rt from `dll_path`. Once one path succeeds, later calls
    /// return true without reloading. True if pardiso/pardisoinit resolved.
    static bool load(const std::string& dll_path);
    static bool available();

    /// threads: MKL threads for this solver's calls (0 = MKL's default).
    explicit PardisoLU(int threads = 1);
    ~PardisoLU();
    PardisoLU(const PardisoLU&) = delete;
    PardisoLU& operator=(const PardisoLU&) = delete;

    /// Solve A x = b, A given in CSC (indptr length n+1, sorted indices).
    std::vector<double> solve_csc(std::span<const std::int64_t> indptr,
                                  std::span<const std::int64_t> indices,
                                  std::span<const double> data, std::int64_t n,
                                  std::span<const double> b);
    int analyses() const { return analyses_; }
    int factorizations() const { return factorizations_; }
    /// Relative residual ||A x - b|| / ||b|| of the last solve.
    double last_residual() const { return last_residual_; }

private:
    struct Impl;
    Impl* impl_;
    int analyses_ = 0;
    int factorizations_ = 0;
    double last_residual_ = 0.0;
};

}  // namespace tcad::solver
