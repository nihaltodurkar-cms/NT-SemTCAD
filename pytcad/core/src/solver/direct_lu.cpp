#include "tcad/solver/direct_lu.hpp"

#include <Eigen/Sparse>

#include <algorithm>

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

namespace {
using SpMat = Eigen::SparseMatrix<double, Eigen::ColMajor, int>;
}  // namespace

struct ReusableLU::Impl {
    SpMat A;
    // COLAMD (Eigen's default, what scipy's spsolve also defaults to) or
    // AMD on the symmetrized pattern A^T + A. Measured 2026-09-28: AMD is
    // PATHOLOGICAL with SparseLU on the coupled psi/n/p Jacobians (>15 s
    // on an 11,640-unknown system COLAMD factors in ~45 ms), so
    // linsolve.DirectSession always uses COLAMD; AMD stays available
    // only as an explicit experiment switch.
    Eigen::SparseLU<SpMat, Eigen::COLAMDOrdering<int>> lu_colamd;
    Eigen::SparseLU<SpMat, Eigen::AMDOrdering<int>> lu_amd;
    bool amd = false;
    std::vector<std::int64_t> indptr, indices;
    std::int64_t n = -1;
    bool analyzed = false;

    void analyze() { amd ? lu_amd.analyzePattern(A) : lu_colamd.analyzePattern(A); }
    void factorize() { amd ? lu_amd.factorize(A) : lu_colamd.factorize(A); }
    bool ok() const {
        return (amd ? lu_amd.info() : lu_colamd.info()) == Eigen::Success;
    }
    Eigen::VectorXd solve(const Eigen::Map<const Eigen::VectorXd>& b) {
        return amd ? Eigen::VectorXd(lu_amd.solve(b)) : Eigen::VectorXd(lu_colamd.solve(b));
    }
};

ReusableLU::ReusableLU(bool amd_ordering) : impl_(new Impl) { impl_->amd = amd_ordering; }
ReusableLU::~ReusableLU() { delete impl_; }

std::vector<double> ReusableLU::solve_csc(std::span<const std::int64_t> indptr,
                                          std::span<const std::int64_t> indices,
                                          std::span<const double> data,
                                          std::int64_t n,
                                          std::span<const double> b) {
    Impl& s = *impl_;
    if (indptr.size() != static_cast<std::size_t>(n + 1) ||
        indices.size() != data.size() || b.size() != static_cast<std::size_t>(n) ||
        indptr[n] != static_cast<std::int64_t>(data.size()))
        throw tcad::InvalidArgument("ReusableLU: inconsistent CSC arrays");
    const bool same = s.analyzed && s.n == n &&
                      std::equal(indptr.begin(), indptr.end(), s.indptr.begin(),
                                 s.indptr.end()) &&
                      std::equal(indices.begin(), indices.end(), s.indices.begin(),
                                 s.indices.end());
    bool reanalyze = !same;
    if (same) {
        double* vp = s.A.valuePtr();
        for (std::size_t k = 0; k < data.size(); ++k) vp[k] = data[k];
    } else {
        // Pattern moved (nonlocal / magnitude-cut blocks). If the new
        // pattern is a SUBSET of the analyzed one, scatter into it with
        // explicit zeros and keep the analysis; otherwise re-analyze on
        // the UNION, so a pattern that oscillates settles after one or
        // two analyses instead of re-analyzing every iteration (measured
        // 8 analyses in 14 solves on B10 before this).
        std::vector<std::int64_t> u_ptr(1, 0), u_idx;
        bool subset = s.analyzed && s.n == n;
        if (s.analyzed && s.n == n) {
            u_idx.reserve(s.indices.size() + indices.size());
            for (std::int64_t c = 0; c < n; ++c) {
                std::int64_t a = s.indptr[c], ae = s.indptr[c + 1];
                std::int64_t bq = indptr[c], be = indptr[c + 1];
                while (a < ae || bq < be) {
                    if (bq >= be || (a < ae && s.indices[a] < indices[bq])) {
                        u_idx.push_back(s.indices[a++]);
                    } else if (a >= ae || indices[bq] < s.indices[a]) {
                        u_idx.push_back(indices[bq++]);
                        subset = false;
                    } else {
                        u_idx.push_back(s.indices[a]);
                        ++a; ++bq;
                    }
                }
                u_ptr.push_back(static_cast<std::int64_t>(u_idx.size()));
            }
        } else {
            u_ptr.assign(indptr.begin(), indptr.end());
            u_idx.assign(indices.begin(), indices.end());
        }
        reanalyze = !subset;
        if (!subset) {
            s.A.resize(static_cast<int>(n), static_cast<int>(n));
            s.A.resizeNonZeros(static_cast<Eigen::Index>(u_idx.size()));
            int* op = s.A.outerIndexPtr();
            int* ip = s.A.innerIndexPtr();
            for (std::int64_t c = 0; c <= n; ++c) op[c] = static_cast<int>(u_ptr[c]);
            for (std::size_t k = 0; k < u_idx.size(); ++k) ip[k] = static_cast<int>(u_idx[k]);
            s.indptr = std::move(u_ptr);
            s.indices = std::move(u_idx);
            s.n = n;
        }
        // scatter this call's values into the stored pattern (zeros elsewhere)
        double* vp = s.A.valuePtr();
        std::fill(vp, vp + s.indices.size(), 0.0);
        for (std::int64_t c = 0; c < n; ++c) {
            std::int64_t a = s.indptr[c];
            for (std::int64_t q = indptr[c]; q < indptr[c + 1]; ++q) {
                while (s.indices[a] < indices[q]) ++a;
                vp[a] = data[q];
            }
        }
    }
    if (reanalyze) {
        s.analyze();
        s.analyzed = true;
        ++analyses_;
    }
    s.factorize();
    ++factorizations_;
    if (!s.ok()) {
        s.analyzed = false;   // force a fresh analysis next time
        throw tcad::LinearSolveFailure("SparseLU factorization failed (singular matrix)");
    }
    Eigen::Map<const Eigen::VectorXd> bv(b.data(), static_cast<Eigen::Index>(b.size()));
    Eigen::VectorXd x = s.solve(bv);
    if (!s.ok())
        throw tcad::LinearSolveFailure("SparseLU solve failed");
    return std::vector<double>(x.data(), x.data() + x.size());
}

}  // namespace tcad::solver
