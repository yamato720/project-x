#ifndef PROJECTX_TESTDATA_CGSOLVER_JACOBI_PCG_SOLVER_HPP
#define PROJECTX_TESTDATA_CGSOLVER_JACOBI_PCG_SOLVER_HPP

#include "ApplyPreconditionerStage.hpp"
#include "ComputeAlphaStage.hpp"
#include "ComputeApStage.hpp"
#include "Dataset.hpp"
#include "InitializeProblem.hpp"
#include "JacobiPreconditioner.hpp"
#include "SolverState.hpp"
#include "SparseMatrixCsr.hpp"
#include "UpdateDirectionStage.hpp"
#include "UpdateResidualNormStage.hpp"
#include "UpdateResidualStage.hpp"
#include "UpdateXStage.hpp"

namespace cgsolver {

class JacobiPcgSolver {
  public:
    explicit JacobiPcgSolver(const Dataset& dataset)
        : dataset_(dataset),
          matrix_(dataset),
          preconditioner_(dataset),
          initialize_(dataset, matrix_, preconditioner_),
          compute_ap_(matrix_),
          apply_preconditioner_(preconditioner_) {}

    SolverResult run() const {
        SolverState state;
        SolverResult result;

        initialize_.run(state);

        result.converged = false;
        result.iterations = 0;
        result.rr = state.rr;

        for (int iteration = 0; iteration < dataset_.max_iters && state.rr > dataset_.tau; ++iteration) {
            compute_ap_.run(state);
            const double alpha = compute_alpha_.run(state);
            update_x_.run(state, alpha);
            update_residual_.run(state, alpha);
            apply_preconditioner_.run(state);
            update_direction_.run(state);
            update_rr_.run(state);

            result.iterations = iteration + 1;
            result.rr = state.rr;
        }

        if (state.rr <= dataset_.tau) {
            result.converged = true;
        }

        result.solution = state.x;
        return result;
    }

    const SparseMatrixCsr& matrix() const { return matrix_; }

  private:
    const Dataset& dataset_;
    SparseMatrixCsr matrix_;
    JacobiPreconditioner preconditioner_;
    InitializeProblem initialize_;
    ComputeApStage compute_ap_;
    ComputeAlphaStage compute_alpha_;
    UpdateXStage update_x_;
    UpdateResidualStage update_residual_;
    ApplyPreconditionerStage apply_preconditioner_;
    UpdateDirectionStage update_direction_;
    UpdateResidualNormStage update_rr_;
};

}  // namespace cgsolver

#endif
