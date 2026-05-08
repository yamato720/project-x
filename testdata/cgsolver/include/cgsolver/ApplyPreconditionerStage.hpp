#ifndef PROJECTX_TESTDATA_CGSOLVER_APPLY_PRECONDITIONER_STAGE_HPP
#define PROJECTX_TESTDATA_CGSOLVER_APPLY_PRECONDITIONER_STAGE_HPP

#include "JacobiPreconditioner.hpp"
#include "SolverState.hpp"

namespace cgsolver {

class ApplyPreconditionerStage {
  public:
    explicit ApplyPreconditionerStage(const JacobiPreconditioner& preconditioner)
        : preconditioner_(preconditioner) {}

    void run(SolverState& state) const { preconditioner_.apply(state.r, state.z); }

  private:
    const JacobiPreconditioner& preconditioner_;
};

}  // namespace cgsolver

#endif
