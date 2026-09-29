#include "tcad/solver/pardiso_lu.hpp"

#include <algorithm>
#include <cmath>
#include <mutex>

#include "tcad/base/errors.hpp"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <dlfcn.h>
#endif

namespace tcad::solver {

namespace {
// MKL's LP64 interface (mkl_rt's default): MKL_INT == int.
using PardisoFn = void (*)(void*, const int*, const int*, const int*, const int*, const int*,
                           const void*, const int*, const int*, int*, const int*, int*,
                           const int*, void*, void*, int*);
using PardisoInitFn = void (*)(void*, const int*, int*);
using SetThreadsLocalFn = int (*)(int);

struct Mkl {
    bool tried = false, ok = false;
    PardisoFn pardiso = nullptr;
    PardisoInitFn pardisoinit = nullptr;
    SetThreadsLocalFn set_threads_local = nullptr;
};
Mkl& mkl() {
    static Mkl m;
    return m;
}
std::mutex& mkl_mutex() {
    static std::mutex mu;
    return mu;
}

void* sym(void* h, const char* name) {
#ifdef _WIN32
    return reinterpret_cast<void*>(GetProcAddress(static_cast<HMODULE>(h), name));
#else
    return dlsym(h, name);
#endif
}

// Sets MKL's thread count for the calling thread for one scope, restoring
// the previous local setting afterwards.
struct ThreadScope {
    int prev = -1;
    explicit ThreadScope(int threads) {
        if (threads > 0 && mkl().set_threads_local) prev = mkl().set_threads_local(threads);
    }
    ~ThreadScope() {
        if (prev >= 0 && mkl().set_threads_local) mkl().set_threads_local(prev);
    }
};
}  // namespace

bool PardisoLU::load(const std::string& dll_path) {
    std::lock_guard<std::mutex> lock(mkl_mutex());
    Mkl& m = mkl();
    if (m.ok) return true;   // loaded once; a failed path may be retried with another
    m.tried = true;
#ifdef _WIN32
    const int len = MultiByteToWideChar(CP_UTF8, 0, dll_path.c_str(), -1, nullptr, 0);
    std::wstring w(static_cast<std::size_t>(len > 0 ? len : 1), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, dll_path.c_str(), -1, w.data(), len);
    // LOAD_WITH_ALTERED_SEARCH_PATH: resolve mkl_rt's own dependencies
    // (mkl_core, mkl_intel_thread, libiomp5md) from ITS directory.
    void* h = LoadLibraryExW(w.c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
#else
    void* h = dlopen(dll_path.c_str(), RTLD_NOW | RTLD_LOCAL);
#endif
    if (!h) return false;
    m.pardiso = reinterpret_cast<PardisoFn>(sym(h, "pardiso"));
    m.pardisoinit = reinterpret_cast<PardisoInitFn>(sym(h, "pardisoinit"));
    m.set_threads_local = reinterpret_cast<SetThreadsLocalFn>(sym(h, "MKL_Set_Num_Threads_Local"));
    m.ok = m.pardiso && m.pardisoinit;
    return m.ok;
}

bool PardisoLU::available() { return mkl().ok; }

struct PardisoLU::Impl {
    void* pt[64] = {};
    int iparm[64] = {};
    int mtype = 11;          // real, structurally nonsymmetric
    int threads = 1;
    bool analyzed = false;
    std::int64_t n = -1;
    // Analyzed pattern, stored as the CSC of A == the CSR of A^T (int32
    // for MKL's LP64 interface); PARDISO then solves the transposed
    // system (iparm[11] = 2), i.e. A x = b.
    std::vector<std::int64_t> indptr, indices;
    std::vector<int> ia, ja;
    std::vector<double> a;

    int call(int phase, double* b, double* x) {
        const int maxfct = 1, mnum = 1, nrhs = 1, msglvl = 0;
        const int nn = static_cast<int>(n);
        int idum = 0, error = 0;
        double ddum = 0.0;
        mkl().pardiso(pt, &maxfct, &mnum, &mtype, &phase, &nn, a.data(), ia.data(), ja.data(),
                      &idum, &nrhs, iparm, &msglvl, b ? b : &ddum, x ? x : &ddum, &error);
        return error;
    }
    void release() {
        if (analyzed) {
            ThreadScope ts(threads);
            call(-1, nullptr, nullptr);
        }
        analyzed = false;
    }
};

PardisoLU::PardisoLU(int threads) : impl_(new Impl) {
    if (!available())
        throw tcad::InvalidArgument("PardisoLU: MKL PARDISO is not loaded (PardisoLU.load)");
    impl_->threads = threads;
    mkl().pardisoinit(impl_->pt, &impl_->mtype, impl_->iparm);
    impl_->iparm[34] = 1;   // zero-based indexing
    impl_->iparm[11] = 2;   // solve the transposed system: CSC(A) == CSR(A^T)
    impl_->iparm[26] = 0;   // no input-matrix checker (the pattern is canonical)
    // Conditional numerical reproducibility: with >1 thread, fix the
    // parallel reduction order so repeated solves are bit-identical.
    impl_->iparm[33] = threads > 1 ? threads : 0;
}

PardisoLU::~PardisoLU() {
    impl_->release();
    delete impl_;
}

std::vector<double> PardisoLU::solve_csc(std::span<const std::int64_t> indptr,
                                         std::span<const std::int64_t> indices,
                                         std::span<const double> data, std::int64_t n,
                                         std::span<const double> b) {
    Impl& s = *impl_;
    if (indptr.size() != static_cast<std::size_t>(n + 1) || indices.size() != data.size() ||
        b.size() != static_cast<std::size_t>(n) ||
        indptr[static_cast<std::size_t>(n)] != static_cast<std::int64_t>(data.size()))
        throw tcad::InvalidArgument("PardisoLU: inconsistent CSC arrays");
    if (n > 2147483647LL || static_cast<std::int64_t>(data.size()) > 2147483647LL)
        throw tcad::InvalidArgument("PardisoLU: system too large for MKL's LP64 interface");

    const bool same = s.analyzed && s.n == n &&
                      std::equal(indptr.begin(), indptr.end(), s.indptr.begin(), s.indptr.end()) &&
                      std::equal(indices.begin(), indices.end(), s.indices.begin(), s.indices.end());
    bool reanalyze = !same;
    if (same) {
        std::copy(data.begin(), data.end(), s.a.begin());
    } else {
        // Same bookkeeping as ReusableLU: a SUBSET of the analyzed pattern
        // is scattered into it with explicit zeros (analysis kept);
        // otherwise re-analyze on the UNION, so an oscillating pattern
        // settles after one or two analyses.
        std::vector<std::int64_t> u_ptr(1, 0), u_idx;
        bool subset = s.analyzed && s.n == n;
        if (s.analyzed && s.n == n) {
            u_idx.reserve(s.indices.size() + indices.size());
            for (std::int64_t c = 0; c < n; ++c) {
                std::int64_t p = s.indptr[c], pe = s.indptr[c + 1];
                std::int64_t q = indptr[c], qe = indptr[c + 1];
                while (p < pe || q < qe) {
                    if (q >= qe || (p < pe && s.indices[p] < indices[q])) {
                        u_idx.push_back(s.indices[p++]);
                    } else if (p >= pe || indices[q] < s.indices[p]) {
                        u_idx.push_back(indices[q++]);
                        subset = false;
                    } else {
                        u_idx.push_back(s.indices[p]);
                        ++p; ++q;
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
            s.release();
            s.indptr = std::move(u_ptr);
            s.indices = std::move(u_idx);
            s.n = n;
            s.ia.assign(s.indptr.begin(), s.indptr.end());
            s.ja.assign(s.indices.begin(), s.indices.end());
            s.a.assign(s.indices.size(), 0.0);
        }
        std::fill(s.a.begin(), s.a.end(), 0.0);
        for (std::int64_t c = 0; c < n; ++c) {
            std::int64_t p = s.indptr[c];
            for (std::int64_t q = indptr[c]; q < indptr[c + 1]; ++q) {
                while (s.indices[p] < indices[q]) ++p;
                s.a[static_cast<std::size_t>(p)] = data[q];
            }
        }
    }

    ThreadScope ts(s.threads);
    if (reanalyze) {
        const int err = s.call(11, nullptr, nullptr);
        if (err != 0) {
            s.analyzed = false;
            throw tcad::LinearSolveFailure("PARDISO analysis failed (error " +
                                           std::to_string(err) + ")");
        }
        s.analyzed = true;
        ++analyses_;
    }
    int err = s.call(22, nullptr, nullptr);
    ++factorizations_;
    if (err != 0) {
        s.release();
        throw tcad::LinearSolveFailure("PARDISO factorization failed (error " +
                                       std::to_string(err) + "; singular matrix?)");
    }
    std::vector<double> rhs(b.begin(), b.end()), x(static_cast<std::size_t>(n), 0.0);
    err = s.call(33, rhs.data(), x.data());
    if (err != 0)
        throw tcad::LinearSolveFailure("PARDISO solve failed (error " + std::to_string(err) + ")");

    // Relative residual (CSC mat-vec): PARDISO perturbs tiny pivots
    // rather than failing, so a singular or badly perturbed system must be
    // caught here, not trusted.
    std::vector<double> r(static_cast<std::size_t>(n));
    for (std::int64_t i = 0; i < n; ++i) r[i] = -b[i];
    for (std::int64_t c = 0; c < n; ++c)
        for (std::int64_t q = indptr[c]; q < indptr[c + 1]; ++q)
            r[static_cast<std::size_t>(indices[q])] += data[q] * x[static_cast<std::size_t>(c)];
    double rn = 0.0, bn = 0.0;
    for (std::int64_t i = 0; i < n; ++i) {
        rn += r[i] * r[i];
        bn += b[i] * b[i];
    }
    last_residual_ = bn > 0.0 ? std::sqrt(rn / bn) : std::sqrt(rn);
    if (!std::isfinite(last_residual_) || last_residual_ > 1e-6)
        throw tcad::LinearSolveFailure(
            "PARDISO solve inaccurate (relative residual " + std::to_string(last_residual_) +
            "; singular matrix?)");
    return x;
}

}  // namespace tcad::solver
