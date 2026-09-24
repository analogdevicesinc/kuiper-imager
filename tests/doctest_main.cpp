// Single translation unit that provides doctest's main() for the unit suite;
// every other *_test.cpp just includes the header. See docs: development (testing).

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>
