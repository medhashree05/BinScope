// Single translation unit with no external references beyond the C++ runtime.
// Exercises .text, .data, .bss and .rodata.

int simple_counter = 42;         // initialized global   -> .data
int simple_zeroed;               // zero-initialized     -> .bss
const char simple_message[] = "BinScope fixture";  // internal linkage const -> .rodata

static int square(int x) { return x * x; }  // local (STB_LOCAL) function

int simple_compute(int x) {
    simple_zeroed += x;
    return square(x) + simple_counter + simple_message[0];
}
