#!/usr/bin/env bash
# Builds and runs the bigdec.h unit tests (exact Decimal, Fraction and big integers for the Seed Pack helpers).
cd "$(dirname "$0")" || exit 1
CXX="${CXX:-g++}"
B=$(mktemp -d); trap 'rm -rf "$B"' EXIT
"$CXX" -std=c++20 -O1 -Wall -Wextra -Wpedantic bigdec_test.cpp -o "$B/bigdec_test" || { echo "bigdec: build failed"; exit 1; }
"$B/bigdec_test"
