#ifndef PROJECTX_TESTDATA_CGSOLVER_SOLVER_STATE_HPP
#define PROJECTX_TESTDATA_CGSOLVER_SOLVER_STATE_HPP

#include <vector>

namespace cgsolver {

struct SolverState {
    std::vector<double> x;
    std::vector<double> r;
    std::vector<double> z;
    std::vector<double> p;
    std::vector<double> ap;
    double rz = 0.0;
    double rr = 0.0;
};

struct SolverResult {
    bool converged = false;
    int iterations = 0;
    double rr = 0.0;
    std::vector<double> solution;
};

}  // namespace cgsolver

#endif
