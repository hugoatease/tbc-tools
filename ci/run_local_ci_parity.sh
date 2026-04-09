#!/usr/bin/env bash

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
ACTIONLINT_FALLBACK_VERSION="1.7.12"
MODE="${1:---all}"

usage() {
  echo "Usage: $0 [--all|--guardrails-only|--qt6-only]" >&2
}

require_cmd() {
  local cmd="$1"
  if ! command -v "$cmd" >/dev/null 2>&1; then
    echo "Missing required command: $cmd" >&2
    exit 1
  fi
}

resolve_actionlint_platform() {
  local os arch
  os="$(uname -s)"
  arch="$(uname -m)"

  case "$os" in
    Linux)
      os="linux"
      ;;
    Darwin)
      os="darwin"
      ;;
    *)
      echo "Unsupported OS for actionlint bootstrap: $os" >&2
      exit 1
      ;;
  esac

  case "$arch" in
    x86_64|amd64)
      arch="amd64"
      ;;
    aarch64|arm64)
      arch="arm64"
      ;;
    *)
      echo "Unsupported architecture for actionlint bootstrap: $arch" >&2
      exit 1
      ;;
  esac

  printf "%s_%s" "$os" "$arch"
}

resolve_actionlint_version() {
  if [[ -n "${ACTIONLINT_VERSION:-}" ]]; then
    printf "%s" "$ACTIONLINT_VERSION"
    return
  fi

  local latest_json latest_version
  latest_json="$(curl -fsSL --retry 3 --retry-delay 1 --retry-connrefused https://api.github.com/repos/rhysd/actionlint/releases/latest || true)"
  latest_version="$(printf "%s\n" "$latest_json" | grep -m1 '"tag_name"' | sed -E 's/.*"v?([^"]+)".*/\1/' || true)"

  if [[ -n "$latest_version" ]]; then
    printf "%s" "$latest_version"
    return
  fi

  printf "%s" "$ACTIONLINT_FALLBACK_VERSION"
}

download_actionlint_binary() {
  local tmp_root bin_dir platform version archive_name archive_path archive_url extract_dir binary_path
  tmp_root="${RUNNER_TEMP:-${TMPDIR:-/tmp}}"
  bin_dir="$tmp_root/actionlint-bin"
  platform="$(resolve_actionlint_platform)"
  version="$(resolve_actionlint_version)"
  archive_name="actionlint_${version}_${platform}.tar.gz"
  archive_path="$bin_dir/$archive_name"
  archive_url="https://github.com/rhysd/actionlint/releases/download/v${version}/${archive_name}"
  binary_path="$bin_dir/actionlint-${version}-${platform}"

  if [[ -x "$binary_path" ]]; then
    printf "%s" "$binary_path"
    return
  fi

  mkdir -p "$bin_dir"
  extract_dir="$(mktemp -d "$bin_dir/.extract.XXXXXX")"

  if ! curl -fsSL --retry 3 --retry-delay 1 --retry-connrefused -o "$archive_path" "$archive_url"; then
    rm -rf "$extract_dir"
    echo "Failed to download actionlint archive: $archive_url" >&2
    exit 1
  fi

  if ! tar -xzf "$archive_path" -C "$extract_dir"; then
    rm -rf "$extract_dir"
    echo "Failed to extract actionlint archive: $archive_path" >&2
    exit 1
  fi

  if [[ ! -x "$extract_dir/actionlint" ]]; then
    rm -rf "$extract_dir"
    echo "Downloaded actionlint archive did not contain an executable binary." >&2
    exit 1
  fi

  if ! mv "$extract_dir/actionlint" "$binary_path" 2>/dev/null && [[ ! -x "$binary_path" ]]; then
    rm -rf "$extract_dir"
    echo "Failed to install downloaded actionlint binary." >&2
    exit 1
  fi
  chmod +x "$binary_path"
  rm -rf "$extract_dir"

  printf "%s" "$binary_path"
}

run_actionlint() {
  local actionlint_args=(-color -shellcheck=)
  local actionlint_cmd

  if [[ -n "${ACTIONLINT_BIN:-}" ]]; then
    actionlint_cmd="$ACTIONLINT_BIN"
    if [[ ! -x "$actionlint_cmd" ]]; then
      echo "ACTIONLINT_BIN is set but not executable: $actionlint_cmd" >&2
      exit 1
    fi
    "$actionlint_cmd" "${actionlint_args[@]}"
    return
  fi

  if command -v actionlint >/dev/null 2>&1; then
    actionlint_cmd="$(command -v actionlint)"
    "$actionlint_cmd" "${actionlint_args[@]}"
    return
  fi

  require_cmd curl
  require_cmd tar
  actionlint_cmd="$(download_actionlint_binary)"
  "$actionlint_cmd" "${actionlint_args[@]}"
}

run_guardrails() {
  cd "$ROOT_DIR"
  require_cmd python3
  run_actionlint
  python3 -m unittest -v ci.tests.test_check_ci_contracts
  python3 ci/check_ci_contracts.py
  bash -n ci/verify_linux_bundle.sh
}

run_qt6_build_and_test() {
  cd "$ROOT_DIR"
  require_cmd nix
  nix --version
  nix build .#
  nix develop -c bash -lc "
    cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo &&
    cmake --build build --verbose &&
    cmake --install build --prefix /tmp/staging &&
    ctest --test-dir build --output-on-failure
  "
}

if [[ "$#" -gt 1 ]]; then
  usage
  exit 2
fi

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
    usage
    exit 2
    ;;
esac
