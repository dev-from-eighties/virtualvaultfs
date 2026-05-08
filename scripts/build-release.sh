#!/usr/bin/env sh
set -eu

cmake_cxx_compiler_arg=
if [ -e /usr/bin/g++-13 ]; then
    cmake_cxx_compiler_arg="-DCMAKE_CXX_COMPILER=g++-13"
fi

cmake -S . -B build/release -DCMAKE_BUILD_TYPE=Release ${cmake_cxx_compiler_arg}
cmake --build build/release
