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

require_no_missing_deps() {
  local elf="$1"
  local libdir="${2:-}"
  if [ ! -f "$elf" ]; then
    return
  fi
  local ldd_output
  if [ -n "$libdir" ]; then
    ldd_output="$(LD_LIBRARY_PATH="$libdir${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}" ldd "$elf" 2>/dev/null || true)"
  else
    ldd_output="$(ldd "$elf" 2>/dev/null || true)"
  fi
  if echo "$ldd_output" | grep -q 'not found'; then
    echo "Unresolved shared library dependencies for $elf:" >&2
    echo "$ldd_output" | grep 'not found' >&2
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
    require_non_nix_interpreter "$ROOT/usr/bin/ld-analyse"
    require_non_nix_interpreter "$ROOT/usr/bin/ld-process-vbi"
    require_non_nix_interpreter "$ROOT/usr/bin/ffmpeg"
    require_non_nix_interpreter "$ROOT/usr/bin/ffprobe"
    require_non_nix_rpath "$ROOT/usr/bin/ld-analyse"
    require_non_nix_rpath "$ROOT/usr/plugins/platforms/libqxcb.so"
    require_no_missing_deps "$ROOT/usr/bin/ld-analyse" "$ROOT/usr/lib"
    require_no_missing_deps "$ROOT/usr/plugins/platforms/libqxcb.so" "$ROOT/usr/lib"
    for rel in "${COMMON_RELATIVE_PATHS[@]}"; do
      require_path "$ROOT/$rel"
    done

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
    require_non_nix_interpreter "$TARGET/bin/ld-analyse"
    require_non_nix_interpreter "$TARGET/bin/ld-process-vbi"
    require_non_nix_interpreter "$TARGET/bin/ffmpeg"
    require_non_nix_interpreter "$TARGET/bin/ffprobe"
    require_non_nix_rpath "$TARGET/bin/ld-analyse"
    require_non_nix_rpath "$TARGET/plugins/platforms/libqxcb.so"
    require_no_missing_deps "$TARGET/bin/ld-analyse" "$TARGET/lib"
    require_no_missing_deps "$TARGET/plugins/platforms/libqxcb.so" "$TARGET/lib"
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
    ;;

  *)
    usage
    ;;
esac

echo "Linux bundle validation passed: mode=$MODE target=$TARGET"
