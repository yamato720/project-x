#ifndef PROJECTX_TESTDATA_CGSOLVER_INITIALIZE_PROBLEM_HPP
#define PROJECTX_TESTDATA_CGSOLVER_INITIALIZE_PROBLEM_HPP

#include "Dataset.hpp"
#include "JacobiPreconditioner.hpp"
#include "SolverState.hpp"
#include "SparseMatrixCsr.hpp"

#include <vector>

namespace cgsolver {

class InitializeProblem {
  public:
    InitializeProblem(const Dataset& dataset,
                      const SparseMatrixCsr& matrix,
                      const JacobiPreconditioner& preconditioner)
        : dataset_(dataset), matrix_(matrix), preconditioner_(preconditioner) {}

    void run(SolverState& state) const {
        std::vector<double> ax0;

        state.x = dataset_.x0;

        matrix_.multiply(state.x, ax0);
        state.r.assign(static_cast<std::size_t>(dataset_.n), 0.0);
        for (int index = 0; index < dataset_.n; ++index) {
            state.r[static_cast<std::size_t>(index)] =
                dataset_.rhs[static_cast<std::size_t>(index)] - ax0[static_cast<std::size_t>(index)];
        }

        preconditioner_.apply(state.r, state.z);
        state.p = state.z;
        state.ap.assign(static_cast<std::size_t>(dataset_.n), 0.0);
        state.rz = dot_product(state.r, state.z);
        state.rr = dot_product(state.r, state.r);
    }

  private:
    const Dataset& dataset_;
    const SparseMatrixCsr& matrix_;
    const JacobiPreconditioner& preconditioner_;
};

}  // namespace cgsolver

#endif
