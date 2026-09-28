#include "tcad/solver/direct_lu.hpp"

#include <Eigen/Sparse>

#include "tcad/base/errors.hpp"

namespace tcad::solver {

std::vector<double> solve_direct_lu(std::span<const std::int64_t> rows,
                                    std::span<const std::int64_t> cols,
                                    std::span<const double> vals,
                                    std::int64_t n,
                                    std::span<const double> b) {
    const std::size_t nnz = rows.size();
    std::vector<Eigen::Triplet<double>> triplets;
    triplets.reserve(nnz);
    for (std::size_t k = 0; k < nnz; ++k)
        triplets.emplace_back(static_cast<int>(rows[k]), static_cast<int>(cols[k]), vals[k]);

    Eigen::SparseMatrix<double> A(static_cast<int>(n), static_cast<int>(n));
    A.setFromTriplets(triplets.begin(), triplets.end());
    A.makeCompressed();

    Eigen::SparseLU<Eigen::SparseMatrix<double>> lu;
    lu.analyzePattern(A);
    lu.factorize(A);
    if (lu.info() != Eigen::Success)
        throw tcad::LinearSolveFailure("SparseLU factorization failed (singular matrix)");

    Eigen::Map<const Eigen::VectorXd> bv(b.data(), static_cast<Eigen::Index>(b.size()));
    Eigen::VectorXd x = lu.solve(bv);
    if (lu.info() != Eigen::Success)
        throw tcad::LinearSolveFailure("SparseLU solve failed");

    return std::vector<double>(x.data(), x.data() + x.size());
}

}  // namespace tcad::solver
