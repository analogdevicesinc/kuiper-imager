#pragma once

// doctest plus a helper for comparing ErrorCodes.
//
// A plain CHECK(a == b) on an ErrorCode does not compile: kuiper::toString
// (const char* toString(ErrorCode)) is found by ADL and hijacks doctest's own
// toString, which then tries to concatenate two const char*. CHECK_ERRCODE
// compares the (unique) enumerator names instead, so the assertion stays readable
// and a failure prints the names rather than integers.

#include <doctest/doctest.h>

#include <string>

#include "kuiper/Error.hpp"

#define CHECK_ERRCODE(actual, expected)                       \
    CHECK(std::string(::kuiper::toString(actual)) ==          \
          std::string(::kuiper::toString(expected)))
