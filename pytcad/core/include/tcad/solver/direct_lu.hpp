// Native direct sparse solve for the Device1D Newton loop -- Eigen::
// SparseLU, chosen at the user's explicit request over reusing Python's
// linsolve.solve_linear() (which wraps scipy's SuperLU) so the whole
// Newton step, not just residual/Jacobian assembly, runs without
// crossing back into Python.
//
// NOT bit-identical to scipy's spsolve, and that is fine, not a gap:
// this codebase's own bit-identity convention (see CLAUDE.md's "bit-
// identity" gotcha) already treats a Newton solve's converged OUTPUT as
// machine/library-specific -- goldens for a full solve are regenerated
// per machine, never compared byte-for-byte across solver
// implementations. What IS held to bit-identical parity against the
// Python oracle is the residual/Jacobian ASSEMBLY (see
// physics/kernels.hpp and device1d/device1d.hpp) -- this solver's job
// is only to be a CORRECT and CONVERGENT solve of whatever assembly
// handed it, verified against the same analytic/physical validation
// gates (built-in potential, ideality factor, charge conservation) the
// Python Device1D was already gated on, not against scipy bit-for-bit.
#pragma once

#include <cstdint>
#include <span>
#include <vector>

namespace tcad::solver {

/// Solve A x = b for a square sparse A given as COO triplets (rows,
/// cols, vals, all length nnz) of size n x n. Throws
/// tcad::LinearSolveFailure on a singular/non-finite factorization.
std::vector<double> solve_direct_lu(std::span<const std::int64_t> rows,
                                    std::span<const std::int64_t> cols,
                                    std::span<const double> vals,
                                    std::int64_t n,
                                    std::span<const double> b);

}  // namespace tcad::solver
