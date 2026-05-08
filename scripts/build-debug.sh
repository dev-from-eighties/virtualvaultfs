#!/usr/bin/env sh
set -eu

cmake -S . -B build/debug -DCMAKE_BUILD_TYPE=Debug
cmake --build build/debug
