#!/usr/bin/env sh
set -eu

if ! command -v clang-format >/dev/null 2>&1; then
    echo "clang-format is required" >&2
    exit 1
fi

find src tests -name '*.cpp' -o -name '*.hpp' | xargs clang-format -i
