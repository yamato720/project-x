#ifndef PROJECTX_TESTDATA_CGSOLVER_UPDATE_X_STAGE_HPP
#define PROJECTX_TESTDATA_CGSOLVER_UPDATE_X_STAGE_HPP

#include "SolverState.hpp"

namespace cgsolver {

class UpdateXStage {
  public:
    void run(SolverState& state, double alpha) const {
        for (std::size_t index = 0; index < state.x.size(); ++index) {
            state.x[index] += alpha * state.p[index];
        }
    }
};

}  // namespace cgsolver

#endif
