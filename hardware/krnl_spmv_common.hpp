#ifndef PROJECTX_KRNL_SPMV_COMMON_HPP
#define PROJECTX_KRNL_SPMV_COMMON_HPP

constexpr int PROJECTX_SLOTS_PER_ROW = 3;
constexpr int PROJECTX_MAX_ROWS = 512;
constexpr int PROJECTX_OUTER_TILE_SIZE = 8;

inline unsigned long long projectx_double_to_bits(double value) {
    union {
        double d;
        unsigned long long u;
    } convert;
    convert.d = value;
    return convert.u;
}

inline double projectx_bits_to_double(unsigned long long value) {
    union {
        double d;
        unsigned long long u;
    } convert;
    convert.u = value;
    return convert.d;
}

#endif
