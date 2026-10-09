// Experiment B: defines the functions and data that math_main.cpp only declares.
//
//   g++ -c examples/math_utils.cpp -o math_utils.o

#include "math_utils.hpp"

int math_call_count = 0;

int add(int a, int b) {
    ++math_call_count;
    return a + b;
}

int multiply(int a, int b) {
    ++math_call_count;
    return a * b;
}
