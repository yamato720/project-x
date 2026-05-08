#include "cgsolver/Dataset.hpp"
#include "cgsolver/JacobiPcgSolver.hpp"

#include <filesystem>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

void usage(const char* argv0) {
    std::cerr << "Usage: " << argv0 << " <dataset_dir>\n";
}

}  // namespace

int main(int argc, char** argv) {
    try {
        if (argc != 2) {
            usage(argv[0]);
            return 1;
        }

        const std::filesystem::path dataset_dir = std::filesystem::path(argv[1]);
        const cgsolver::Dataset dataset = cgsolver::load_dataset(dataset_dir);
        const cgsolver::JacobiPcgSolver solver(dataset);
        const cgsolver::SolverResult result = solver.run();

        std::vector<double> ax;
        solver.matrix().multiply(result.solution, ax);

        const std::vector<double> residual = cgsolver::vector_subtract(ax, dataset.rhs);
        const std::vector<double> diff = cgsolver::vector_subtract(result.solution, dataset.x_expected);

        const double final_residual_norm = cgsolver::l2_norm(residual);
        const double solution_max_abs_diff = cgsolver::max_abs_diff(result.solution, dataset.x_expected);
        const double relative_l2_error =
            cgsolver::l2_norm(diff) / cgsolver::safe_norm_denominator(dataset.x_expected);
        const double residual_limit = std::max(std::sqrt(dataset.tau) * 10.0, dataset.check_tolerance);

        const bool pass = result.converged &&
                          result.rr <= dataset.tau &&
                          final_residual_norm <= residual_limit &&
                          solution_max_abs_diff <= dataset.check_tolerance;

        std::cout << "dataset: " << dataset_dir << "\n";
        std::cout << "n=" << dataset.n
                  << " nnz=" << dataset.nnz
                  << " max_iters=" << dataset.max_iters << "\n";
        std::cout << std::scientific << std::setprecision(12);
        std::cout << "tau=" << dataset.tau
                  << " check_tolerance=" << dataset.check_tolerance << "\n";
        std::cout << "converged=" << (result.converged ? "yes" : "no")
                  << " iterations=" << result.iterations << "\n";
        std::cout << "final_rr=" << result.rr << "\n";
        std::cout << "final_residual_norm=" << final_residual_norm << "\n";
        std::cout << "solution_max_abs_diff=" << solution_max_abs_diff << "\n";
        std::cout << "solution_relative_l2_error=" << relative_l2_error << "\n";

        if (!pass) {
            std::cerr << "Jacobi-PCG verification failed\n";
            return 1;
        }

        std::cout << "Jacobi-PCG verification passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "error: " << error.what() << "\n";
        return 1;
    }
}
