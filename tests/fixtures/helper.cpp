// Defines the symbols that main.cpp references but does not define.

int helper_counter = 7;

int helper_add(int a, int b) {
    ++helper_counter;
    return a + b;
}
