#ifndef PROJECTX_TESTDATA_CGSOLVER_COMPUTE_ALPHA_STAGE_HPP
#define PROJECTX_TESTDATA_CGSOLVER_COMPUTE_ALPHA_STAGE_HPP

#include "Dataset.hpp"
#include "SolverState.hpp"

#include <cmath>
#include <limits>
#include <stdexcept>

namespace cgsolver {

class ComputeAlphaStage {
  public:
    double run(const SolverState& state) const {
        const double denom = dot_product(state.p, state.ap);
        if (std::fabs(denom) <= std::numeric_limits<double>::min()) {
            throw std::runtime_error("Jacobi-PCG breakdown: p^T A p is too small");
        }
        return state.rz / denom;
    }
};

}  // namespace cgsolver

#endif
