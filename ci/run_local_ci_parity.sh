#!/usr/bin/env bash

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
MODE="${1:---all}"

run_actionlint() {
  local actionlint_args=(-color -shellcheck=)
  if command -v actionlint >/dev/null 2>&1; then
    actionlint "${actionlint_args[@]}"
    return
  fi

  local tmp_root="${RUNNER_TEMP:-${TMPDIR:-/tmp}}"
  local bin_dir="$tmp_root/actionlint-bin"
  mkdir -p "$bin_dir"
  curl -sSfL https://raw.githubusercontent.com/rhysd/actionlint/main/scripts/download-actionlint.bash | bash -s -- latest "$bin_dir"
  "$bin_dir/actionlint" "${actionlint_args[@]}"
}

run_guardrails() {
  cd "$ROOT_DIR"
  run_actionlint
  python3 ci/check_ci_contracts.py
  bash -n ci/verify_linux_bundle.sh
}

run_qt6_build_and_test() {
  cd "$ROOT_DIR"
  nix --version
  nix build .#
  nix develop -c bash -lc "
    cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo &&
    cmake --build build --verbose &&
    cmake --install build --prefix /tmp/staging &&
    ctest --test-dir build --output-on-failure
  "
}

case "$MODE" in
  --all)
    run_guardrails
    run_qt6_build_and_test
    ;;
  --guardrails-only)
    run_guardrails
    ;;
  --qt6-only)
    run_qt6_build_and_test
    ;;
  *)
    echo "Usage: $0 [--all|--guardrails-only|--qt6-only]" >&2
    exit 2
    ;;
esac
