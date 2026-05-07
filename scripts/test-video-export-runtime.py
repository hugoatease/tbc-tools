#!/usr/bin/env python3

from __future__ import annotations

import argparse
import json
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

SKIP_EXIT_CODE = 77


def skip(message: str) -> int:
    print(f"SKIP: {message}")
    return SKIP_EXIT_CODE


def fail(message: str) -> int:
    print(f"FAIL: {message}", file=sys.stderr)
    return 1


def run_checked(command: list[str]) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        command,
        check=False,
        capture_output=True,
        text=True,
    )


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Runtime tbc-video-export fixture test."
    )
    parser.add_argument("--build", required=True, help="CMake build directory path")
    parser.add_argument("--source", required=True, help="Source tree root path")
    args = parser.parse_args()

    build_dir = Path(args.build).resolve()
    source_dir = Path(args.source).resolve()

    export_candidates = [
        build_dir / "bin" / "tbc-video-export",
        build_dir / "bin" / "tbc-video-export.exe",
    ]
    export_tool = next(
        (candidate for candidate in export_candidates if candidate.is_file()),
        None,
    )
    if export_tool is None:
        return skip("tbc-video-export binary not found in build/bin.")

    ffprobe_path = shutil.which("ffprobe")
    if not ffprobe_path:
        return skip("ffprobe not found in PATH.")

    fixtures_dir = source_dir / "src" / "tbc-video-export" / "tests" / "files"
    input_base = fixtures_dir / "pal_svideo"
    required_fixture_paths = [
        fixtures_dir / "pal_svideo.tbc",
        fixtures_dir / "pal_svideo.tbc.json",
        fixtures_dir / "audio_48_16.wav",
    ]
    for fixture_path in required_fixture_paths:
        if not fixture_path.is_file():
            return fail(f"Required fixture is missing: {fixture_path}")

    with tempfile.TemporaryDirectory(prefix="tbc-video-export-runtime-") as temp_dir:
        output_base = Path(temp_dir) / "pal_svideo_runtime"
        output_file = output_base.with_suffix(".mkv")

        export_command = [
            str(export_tool),
            "--quiet",
            "--overwrite",
            "--audio-track",
            str(fixtures_dir / "audio_48_16.wav"),
            str(input_base),
            str(output_base),
        ]
        export_result = run_checked(export_command)
        if export_result.returncode != 0:
            stderr = export_result.stderr.strip()
            stdout = export_result.stdout.strip()
            return fail(
                "tbc-video-export runtime command failed.\n"
                f"Command: {' '.join(export_command)}\n"
                f"Exit code: {export_result.returncode}\n"
                f"stdout:\n{stdout}\n"
                f"stderr:\n{stderr}"
            )

        if not output_file.is_file():
            return fail(f"Expected output file was not created: {output_file}")
        if output_file.stat().st_size <= 0:
            return fail(f"Output file is empty: {output_file}")

        probe_command = [
            ffprobe_path,
            "-v",
            "error",
            "-show_entries",
            "format=format_name:stream=codec_type,codec_name,width,height,sample_rate,channels",
            "-of",
            "json",
            str(output_file),
        ]
        probe_result = run_checked(probe_command)
        if probe_result.returncode != 0:
            return fail(
                "ffprobe failed on runtime export output.\n"
                f"Exit code: {probe_result.returncode}\n"
                f"stdout:\n{probe_result.stdout.strip()}\n"
                f"stderr:\n{probe_result.stderr.strip()}"
            )

        try:
            probe_data = json.loads(probe_result.stdout)
        except json.JSONDecodeError as error:
            return fail(f"Unable to parse ffprobe JSON output: {error}")

        streams = probe_data.get("streams", [])
        if not streams:
            return fail("ffprobe reported no streams for runtime export output.")

        video_streams = [stream for stream in streams if stream.get("codec_type") == "video"]
        audio_streams = [stream for stream in streams if stream.get("codec_type") == "audio"]
        if not video_streams:
            return fail("No video stream found in runtime export output.")
        if not audio_streams:
            return fail("No audio stream found in runtime export output.")

        first_video = video_streams[0]
        if first_video.get("codec_name") != "ffv1":
            return fail(
                f"Unexpected video codec '{first_video.get('codec_name')}', expected 'ffv1'."
            )
        if int(first_video.get("width", 0)) <= 0 or int(first_video.get("height", 0)) <= 0:
            return fail("Runtime export video stream has invalid dimensions.")

        first_audio = audio_streams[0]
        if int(first_audio.get("sample_rate", 0)) <= 0:
            return fail("Runtime export audio stream has invalid sample rate.")
        if int(first_audio.get("channels", 0)) <= 0:
            return fail("Runtime export audio stream has invalid channel count.")

        format_name = str(probe_data.get("format", {}).get("format_name", ""))
        if "matroska" not in format_name:
            return fail(f"Unexpected container format '{format_name}', expected Matroska.")

        print(f"Video export runtime fixture test passed. Output: {output_file}")
        return 0


if __name__ == "__main__":
    raise SystemExit(main())
