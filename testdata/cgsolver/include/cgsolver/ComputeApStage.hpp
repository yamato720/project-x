#ifndef PROJECTX_TESTDATA_CGSOLVER_COMPUTE_AP_STAGE_HPP
#define PROJECTX_TESTDATA_CGSOLVER_COMPUTE_AP_STAGE_HPP

#include "SolverState.hpp"
#include "SparseMatrixCsr.hpp"

namespace cgsolver {

class ComputeApStage {
  public:
    explicit ComputeApStage(const SparseMatrixCsr& matrix) : matrix_(matrix) {}

    void run(SolverState& state) const { matrix_.multiply(state.p, state.ap); }

  private:
    const SparseMatrixCsr& matrix_;
};

}  // namespace cgsolver

#endif
