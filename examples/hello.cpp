// Experiment A / D: a single translation unit with a function and globals.
//
//   g++ -c examples/hello.cpp -o hello.o
//   g++ examples/hello.cpp -o hello
//   g++ -O0 -c examples/hello.cpp -o hello_O0.o
//   g++ -O2 -c examples/hello.cpp -o hello_O2.o

#include <cstdio>

int greeting_count = 3;         // initialized global      -> .data
int call_total;                 // zero-initialized global -> .bss
static const char kGreeting[] = "Hello from BinScope";  // read-only data -> .rodata

static int scaled(int value) { return value * 2 + 1; }  // internal linkage helper

int greet(int times) {
    for (int i = 0; i < times; ++i) {
        std::printf("%s (%d)\n", kGreeting, i);
        ++call_total;
    }
    return scaled(call_total);
}

int main() { return greet(greeting_count) == 7 ? 0 : 1; }
