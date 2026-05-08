#ifndef PROJECTX_TESTDATA_CGSOLVER_UPDATE_DIRECTION_STAGE_HPP
#define PROJECTX_TESTDATA_CGSOLVER_UPDATE_DIRECTION_STAGE_HPP

#include "Dataset.hpp"
#include "SolverState.hpp"

#include <cmath>
#include <limits>
#include <stdexcept>

namespace cgsolver {

class UpdateDirectionStage {
  public:
    void run(SolverState& state) const {
        const double rz_new = dot_product(state.r, state.z);
        if (std::fabs(state.rz) <= std::numeric_limits<double>::min()) {
            throw std::runtime_error("Jacobi-PCG breakdown: previous r^T z is too small");
        }

        const double beta = rz_new / state.rz;
        for (std::size_t index = 0; index < state.p.size(); ++index) {
            state.p[index] = state.z[index] + beta * state.p[index];
        }

        state.rz = rz_new;
    }
};

}  // namespace cgsolver

#endif
