#!/usr/bin/env bash
# Configure, build and test BinScope.
#
#   scripts/build_and_test.sh                # Debug build in ./build
#   scripts/build_and_test.sh release        # Release build in ./build-release
#   scripts/build_and_test.sh sanitize       # ASan + UBSan build in ./build-asan
#   scripts/build_and_test.sh all            # all three
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
JOBS="$(nproc 2>/dev/null || echo 4)"

run_config() {
    local name="$1" dir="$2"
    shift 2
    echo "==> [$name] configure ($dir)"
    cmake -S "$ROOT" -B "$ROOT/$dir" "$@"
    echo "==> [$name] build"
    cmake --build "$ROOT/$dir" -j "$JOBS"
    echo "==> [$name] test"
    ctest --test-dir "$ROOT/$dir" --output-on-failure
}

mode="${1:-debug}"
case "$mode" in
    debug)    run_config Debug build -DCMAKE_BUILD_TYPE=Debug ;;
    release)  run_config Release build-release -DCMAKE_BUILD_TYPE=Release ;;
    sanitize) run_config Sanitizers build-asan -DCMAKE_BUILD_TYPE=Debug -DBINSCOPE_ENABLE_SANITIZERS=ON ;;
    all)
        run_config Debug build -DCMAKE_BUILD_TYPE=Debug
        run_config Release build-release -DCMAKE_BUILD_TYPE=Release
        run_config Sanitizers build-asan -DCMAKE_BUILD_TYPE=Debug -DBINSCOPE_ENABLE_SANITIZERS=ON
        ;;
    *)
        echo "usage: $0 [debug|release|sanitize|all]" >&2
        exit 2
        ;;
esac
