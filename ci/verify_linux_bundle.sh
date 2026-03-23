#!/usr/bin/env bash

set -euo pipefail

usage() {
  echo "Usage: $0 <x86-appimage|arm64-release> <path>" >&2
  exit 2
}

require_path() {
  local path="$1"
  if [ ! -e "$path" ]; then
    echo "Missing required path: $path" >&2
    exit 1
  fi
}

require_non_nix_interpreter() {
  local elf="$1"
  if [ ! -f "$elf" ]; then
    return
  fi
  local interpreter
  interpreter="$(readelf -l "$elf" 2>/dev/null | awk -F': ' '/Requesting program interpreter/ {gsub(/\]/, "", $2); print $2; exit}')"
  if [ -n "$interpreter" ] && [[ "$interpreter" == /nix/store/* ]]; then
    echo "ELF interpreter still points to Nix store: $elf -> $interpreter" >&2
    exit 1
  fi
}

require_non_nix_rpath() {
  local elf="$1"
  if [ ! -f "$elf" ]; then
    return
  fi
  local rpath_entries
  rpath_entries="$(readelf -d "$elf" 2>/dev/null | awk -F'[][]' '/(RUNPATH|RPATH)/ {print $2}' || true)"
  if [ -z "$rpath_entries" ]; then
    return
  fi
  if echo "$rpath_entries" | tr ':' '\n' | grep -q '^/nix/store/'; then
    echo "ELF RPATH/RUNPATH still points to Nix store: $elf -> $rpath_entries" >&2
    exit 1
  fi
}

require_needed_in_bundle() {
  local elf="$1"
  local libdir="$2"
  if [ ! -f "$elf" ]; then
    return
  fi
  if [ ! -d "$libdir" ]; then
    echo "Library directory not found for dependency validation: $libdir" >&2
    exit 1
  fi
  local needed
  while IFS= read -r needed; do
    [ -n "$needed" ] || continue
    if [ ! -e "$libdir/$needed" ]; then
      echo "Missing bundled dependency for $elf: $needed (expected at $libdir/$needed)" >&2
      exit 1
    fi
  done < <(readelf -d "$elf" 2>/dev/null | awk -F'[][]' '/NEEDED/ { print $2 }')
}

require_bundled_loader_present() {
  local libdir="$1"
  if [ -f "$libdir/ld-linux-x86-64.so.2" ] || [ -f "$libdir/ld-linux-aarch64.so.1" ]; then
    return
  fi
  echo "Missing bundled dynamic loader in: $libdir" >&2
  exit 1
}

require_runtime_libs() {
  local libdir="$1"
  shift
  local lib
  for lib in "$@"; do
    if [ ! -f "$libdir/$lib" ]; then
      echo "Missing required runtime library: $libdir/$lib" >&2
      exit 1
    fi
  done
}

run_smoke_test() {
  local label="$1"
  local log_file="$2"
  shift 2
  local exit_code=0
  timeout 20 "$@" >"$log_file" 2>&1 || exit_code=$?
  if [ "$exit_code" -ne 0 ]; then
    echo "Runtime smoke test failed: $label (exit=$exit_code)" >&2
    if [ -f "$log_file" ]; then
      sed -n '1,200p' "$log_file" >&2 || true
    fi
    exit 1
  fi
}

if [ "$#" -ne 2 ]; then
  usage
fi

MODE="$1"
TARGET="$2"

COMMON_RELATIVE_PATHS=(
  "usr/plugins/platforms/libqxcb.so"
  "usr/plugins/platforminputcontexts/libcomposeplatforminputcontextplugin.so"
  "usr/plugins/xcbglintegrations/libqxcb-glx-integration.so"
  "usr/plugins/sqldrivers/libqsqlite.so"
  "usr/plugins/iconengines/libqsvgicon.so"
  "usr/plugins/imageformats/libqsvg.so"
  "usr/bin/ffmpeg"
  "usr/bin/ffprobe"
)

GLIBC_RUNTIME_LIBS=(
  "libc.so.6"
  "libm.so.6"
  "libpthread.so.0"
  "libdl.so.2"
  "librt.so.1"
  "libresolv.so.2"
)

XCB_RUNTIME_LIBS=(
  "libxcb-cursor.so.0"
  "libxcb-icccm.so.4"
  "libxcb-image.so.0"
  "libxcb-keysyms.so.1"
  "libxcb-render-util.so.0"
  "libxkbcommon.so.0"
  "libxkbcommon-x11.so.0"
)

case "$MODE" in
  x86-appimage)
    require_path "$TARGET"
    if [ ! -x "$TARGET" ]; then
      echo "AppImage is not executable: $TARGET" >&2
      exit 1
    fi

    rm -rf squashfs-root
    APPIMAGE_EXTRACT_AND_RUN=1 "$TARGET" --appimage-extract >/dev/null
    ROOT="squashfs-root"
    require_path "$ROOT"

    require_path "$ROOT/usr/bin/ld-analyse"
    require_path "$ROOT/usr/bin/ld-process-vbi"
    require_path "$ROOT/usr/bin/tbc-video-export"
    require_path "$ROOT/usr/bin/qt.conf"
    require_path "$ROOT/usr/share/tbc-video-export/src/tbc_video_export/__main__.py"
    require_non_nix_rpath "$ROOT/usr/bin/ld-analyse"
    require_non_nix_rpath "$ROOT/usr/plugins/platforms/libqxcb.so"
    require_bundled_loader_present "$ROOT/usr/lib"
    require_runtime_libs "$ROOT/usr/lib" "${GLIBC_RUNTIME_LIBS[@]}"
    require_runtime_libs "$ROOT/usr/lib" "${XCB_RUNTIME_LIBS[@]}"
    require_needed_in_bundle "$ROOT/usr/bin/ld-analyse" "$ROOT/usr/lib"
    require_needed_in_bundle "$ROOT/usr/plugins/platforms/libqxcb.so" "$ROOT/usr/lib"
    for rel in "${COMMON_RELATIVE_PATHS[@]}"; do
      require_path "$ROOT/$rel"
    done
    run_smoke_test "x86-appimage-apprun-help" "$ROOT/.smoke-x86.log" "$ROOT/AppRun" --help

    rm -rf "$ROOT"
    ;;

  arm64-release)
    require_path "$TARGET"
    if [ ! -d "$TARGET" ]; then
      echo "arm64 target is not a directory: $TARGET" >&2
      exit 1
    fi

    require_path "$TARGET/bin/ld-analyse"
    require_path "$TARGET/bin/ld-process-vbi"
    require_path "$TARGET/bin/tbc-video-export"
    require_path "$TARGET/bin/qt.conf"
    require_path "$TARGET/share/tbc-video-export/src/tbc_video_export/__main__.py"
    require_non_nix_rpath "$TARGET/bin/ld-analyse"
    require_non_nix_rpath "$TARGET/plugins/platforms/libqxcb.so"
    require_bundled_loader_present "$TARGET/lib"
    require_runtime_libs "$TARGET/lib" "${GLIBC_RUNTIME_LIBS[@]}"
    require_runtime_libs "$TARGET/lib" "${XCB_RUNTIME_LIBS[@]}"
    require_needed_in_bundle "$TARGET/bin/ld-analyse" "$TARGET/lib"
    require_needed_in_bundle "$TARGET/plugins/platforms/libqxcb.so" "$TARGET/lib"
    require_path "$TARGET/bin/ffmpeg"
    require_path "$TARGET/bin/ffprobe"
    require_path "$TARGET/lib/libQt6Core.so.6"
    require_path "$TARGET/lib/libQt6Gui.so.6"
    require_path "$TARGET/lib/libQt6Widgets.so.6"
    require_path "$TARGET/plugins/platforms/libqxcb.so"
    require_path "$TARGET/plugins/platforminputcontexts/libcomposeplatforminputcontextplugin.so"
    require_path "$TARGET/plugins/xcbglintegrations/libqxcb-glx-integration.so"
    require_path "$TARGET/plugins/sqldrivers/libqsqlite.so"
    require_path "$TARGET/plugins/iconengines/libqsvgicon.so"
    require_path "$TARGET/plugins/imageformats/libqsvg.so"
    run_smoke_test "arm64-launcher-help" "$TARGET/.smoke-arm64.log" "$TARGET/tbc-tools-run" --help
    ;;

  *)
    usage
    ;;
esac

echo "Linux bundle validation passed: mode=$MODE target=$TARGET"
