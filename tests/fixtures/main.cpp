// References helper_add / helper_counter (defined in helper.cpp) and puts (libc),
// so main.o contains undefined symbols and relocations against them.

#include <cstdio>

extern int helper_counter;
int helper_add(int a, int b);

int main() {
    const int sum = helper_add(2, 3);
    std::puts("fixture program");
    return sum + helper_counter == 13 ? 0 : 1;
}
