#ifndef PROJECTX_TESTDATA_CGSOLVER_UPDATE_RESIDUAL_STAGE_HPP
#define PROJECTX_TESTDATA_CGSOLVER_UPDATE_RESIDUAL_STAGE_HPP

#include "SolverState.hpp"

namespace cgsolver {

class UpdateResidualStage {
  public:
    void run(SolverState& state, double alpha) const {
        for (std::size_t index = 0; index < state.r.size(); ++index) {
            state.r[index] -= alpha * state.ap[index];
        }
    }
};

}  // namespace cgsolver

#endif
