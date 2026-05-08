#!/usr/bin/env python3

import argparse
import math
from pathlib import Path


def diagonal_value(index: int) -> float:
    return 4.0 + 0.25 * math.cos(index * 0.1)


def build_tridiagonal_spd(size: int):
    row_ptr = [0]
    col_idx = []
    values = []
    jacobi_diag = []

    for row in range(size):
        diag = diagonal_value(row)

        if row > 0:
            col_idx.append(row - 1)
            values.append(-1.0)

        col_idx.append(row)
        values.append(diag)

        if row + 1 < size:
            col_idx.append(row + 1)
            values.append(-1.0)

        jacobi_diag.append(diag)
        row_ptr.append(len(col_idx))

    return row_ptr, col_idx, values, jacobi_diag


def build_expected_solution(size: int):
    return [
        1.0 + 0.0025 * index + 0.25 * math.sin(index * 0.1)
        for index in range(size)
    ]


def build_initial_solution(size: int):
    return [
        0.15 * math.cos(index * 0.07) - 0.05 * math.sin(index * 0.13)
        for index in range(size)
    ]


def csr_spmv(size: int, row_ptr, col_idx, values, vector):
    result = [0.0] * size
    for row in range(size):
        acc = 0.0
        for offset in range(row_ptr[row], row_ptr[row + 1]):
            acc += values[offset] * vector[col_idx[offset]]
        result[row] = acc
    return result


def write_int_array(path: Path, values):
    path.write_text(" ".join(str(value) for value in values) + "\n", encoding="utf-8")


def write_float_array(path: Path, values):
    path.write_text(" ".join(f"{value:.17g}" for value in values) + "\n", encoding="utf-8")


def generate_dataset(size: int, output_dir: Path, tau: float, max_iters: int | None, check_tolerance: float):
    row_ptr, col_idx, values, jacobi_diag = build_tridiagonal_spd(size)
    x_expected = build_expected_solution(size)
    x0 = build_initial_solution(size)
    rhs = csr_spmv(size, row_ptr, col_idx, values, x_expected)

    nnz = len(col_idx)
    max_iters = max_iters if max_iters is not None else max(4 * size, 1000)

    output_dir.mkdir(parents=True, exist_ok=True)

    meta_text = "\n".join(
        [
            f"n {size}",
            f"nnz {nnz}",
            f"max_iters {max_iters}",
            f"tau {tau:.17g}",
            f"check_tolerance {check_tolerance:.17g}",
        ]
    ) + "\n"
    (output_dir / "meta.txt").write_text(meta_text, encoding="utf-8")
    write_int_array(output_dir / "row_ptr.txt", row_ptr)
    write_int_array(output_dir / "col_idx.txt", col_idx)
    write_float_array(output_dir / "values.txt", values)
    write_float_array(output_dir / "jacobi_diag.txt", jacobi_diag)
    write_float_array(output_dir / "rhs.txt", rhs)
    write_float_array(output_dir / "x0.txt", x0)
    write_float_array(output_dir / "x_expected.txt", x_expected)

    print(
        f"generated Jacobi-PCG dataset: size={size} nnz={nnz} "
        f"tau={tau:.3e} max_iters={max_iters} -> {output_dir}"
    )


def parse_args():
    script_dir = Path(__file__).resolve().parent
    parser = argparse.ArgumentParser(description="Generate a CSR CG-solver dataset.")
    testdata_dir = script_dir.parent
    parser.add_argument(
        "--size",
        type=int,
        default=512,
        help="matrix dimension, default: 512",
    )
    parser.add_argument(
        "--output-dir",
        type=Path,
        default=testdata_dir / "generated" / "cgsolver" / "datasets" / "n512",
        help="output dataset directory",
    )
    parser.add_argument(
        "--tau",
        type=float,
        default=1.0e-20,
        help="convergence threshold on rr = r^T r, default: 1e-20",
    )
    parser.add_argument(
        "--max-iters",
        type=int,
        default=None,
        help="maximum iteration count, default: max(4 * size, 1000)",
    )
    parser.add_argument(
        "--check-tolerance",
        type=float,
        default=1.0e-8,
        help="solution verification tolerance, default: 1e-8",
    )
    return parser.parse_args()


def main():
    args = parse_args()
    if args.size <= 0:
        raise SystemExit("--size must be positive")
    if args.tau <= 0.0:
        raise SystemExit("--tau must be positive")
    if args.max_iters is not None and args.max_iters <= 0:
        raise SystemExit("--max-iters must be positive")
    if args.check_tolerance <= 0.0:
        raise SystemExit("--check-tolerance must be positive")

    generate_dataset(
        size=args.size,
        output_dir=args.output_dir,
        tau=args.tau,
        max_iters=args.max_iters,
        check_tolerance=args.check_tolerance,
    )


if __name__ == "__main__":
    main()
