#ifndef PROJECTX_TESTDATA_CGSOLVER_UPDATE_RESIDUAL_NORM_STAGE_HPP
#define PROJECTX_TESTDATA_CGSOLVER_UPDATE_RESIDUAL_NORM_STAGE_HPP

#include "Dataset.hpp"
#include "SolverState.hpp"

namespace cgsolver {

class UpdateResidualNormStage {
  public:
    void run(SolverState& state) const { state.rr = dot_product(state.r, state.r); }
};

}  // namespace cgsolver

#endif
