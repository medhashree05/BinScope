// Experiment B: calls functions defined in another translation unit, so
// math_main.o contains undefined symbols and relocations that refer to them.
//
//   g++ -c examples/math_main.cpp -o math_main.o
//   g++ math_main.o math_utils.o -o math_program

#include "math_utils.hpp"

#include <cstdio>

int main() {
    const int sum = add(2, 3);
    const int product = multiply(sum, 4);
    std::printf("sum=%d product=%d calls=%d\n", sum, product, math_call_count);
    return product == 20 ? 0 : 1;
}
