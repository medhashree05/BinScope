#pragma once

// Declarations only: every translation unit that includes this header refers to
// symbols that are *defined* in math_utils.cpp.

extern int math_call_count;

int add(int a, int b);
int multiply(int a, int b);
