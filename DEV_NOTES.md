# Development Notes - tbc-tools

## GitHub Actions Workflow Critical Rules

### Workflow Name Format (REQUIRED)
The Linux workflow **MUST** use this exact format:
```yaml
name: "Build Linux tools"
```

**Key points:**
- Must use **double quotes** around the name
- No single quotes
- No unquoted names
- The exact string must appear on line 1

This is the ONLY format that GitHub Actions recognizes for displaying the workflow name. Any deviation breaks the workflow visibility.

**Verified working commit:** `36973ee`

### Why This Matters
GitHub Actions parses the workflow YAML at a very low level. If the format is even slightly different:
- The workflow name field appears as the filename (e.g., `.github/workflows/build_linux_tools.yml`)
- The workflow doesn't appear in the GitHub Actions UI properly
- It may not trigger on release events

### Testing Changes
Before committing any workflow changes:
1. Make the change
2. Commit and push
3. Check API response: `curl -s "https://api.github.com/repos/owner/repo/actions/workflows" | python3 -m json.tool`
4. Verify the `"name"` field shows the display name, NOT the filepath
5. Only if correct, continue; otherwise revert

## AppImage Build Process

The Linux workflow uses:
1. **linuxdeploy** - AppImage creation tool
2. **Nix** - Build environment and dependency management
3. **Oracle Linux 8 container** - Build environment

### Current Build Flow
1. Checkout code
2. Set up Nix development environment
3. Build binaries with CMake via Nix
4. Download linuxdeploy tools
5. Bundle Qt plugins and dependencies
6. Create AppImage with custom AppRun script
7. Upload as artifact

### AppRun Script
The AppRun script enables:
- Default GUI execution: `./appimage.AppImage` → runs ld-analyse
- CLI tool passthrough: `./appimage.AppImage tool-name args` → runs any bundled tool

## Recovery Procedure

If workflow breaks:
```bash
# Get back to verified working state
git checkout 36973ee -- .github/workflows/build_linux_tools.yml
git commit -m "Restore working Linux workflow"
git push origin main

# Verify it works
curl -s "https://api.github.com/repos/harrypm/tbc-tools/actions/workflows" | grep "Build Linux tools"
```

## Workflow Format Lock

**CRITICAL FINDING:** The workflow file structure is completely locked. Any changes to:
- The build steps
- The environment variables
- The container configuration
- The job structure

...cause GitHub Actions to fail parsing the workflow name field.

Attempted: Replacing linuxdeploy with appimagetool
- Changed only env variables and build steps
- Kept name/on/jobs structure identical
- **Result:** GitHub Actions stopped recognizing the workflow (showed filename instead of display name)

### Why This Happens
GitHub's workflow parser has a very strict parser that appears to be tightly coupled to the specific YAML structure. Even minor changes to the implementation (steps, environment) seem to affect name field parsing.

### Current Working Version
Commit 36973ee contains the ONLY known working configuration:
- linuxdeploy + Qt6 plugins
- Oracle Linux 8 container
- DNF-based setup
- This exact structure must be preserved

### For Future Work
If AppImage improvements are needed, they must be made by:
1. Searching for other projects' working GitHub Actions workflows for AppImage
2. Starting from a completely different workflow file structure
3. Testing incrementally at each change
4. **Never** modifying the working 36973ee file

## HARD DEV NOTE - RELEASE WORKFLOW/UI CONTRACT (2026-06-23)

This repository now depends on a strict single-workflow release contract. Do not change this behavior without explicit confirmation.

### Required workflow files and entry points
- `.github/workflows/release.yml` is the only release entry point.

### Required `Release` manual UI fields
`release.yml` must keep `workflow_dispatch` with these inputs:
- `create_release` (boolean)
  - Description: `Bump version, create tag, and upload built assets`
- `version` (string)
  - Description: `Version to release when enabled (example: 0.6.0 or v0.6.0)`

Removing or renaming these inputs breaks the expected Actions modal and causes operator error.

### Required behavior
- If `create_release=false`: manual run is a build/sanity run only (no release upload).
- If `create_release=true`: `version` is required and must normalize to `vMAJOR.MINOR.PATCH`; workflow must update version files, commit, create/push tag, then build/upload release assets.
- Tag push matching `v*` must continue to run the release publish path automatically.

### Required release sequence (full release)
1. Run `Release` with `create_release=true` and `version` (`3.0.0` or `v3.0.0`).
2. Workflow updates version files, commits changes, creates/pushes release tag.
3. Workflow builds artifacts and uploads them to a published GitHub release for that tag.

### Verification checklist after any release-workflow edit
1. In Actions → `Release` → `Run workflow`, verify checkbox and version input appear exactly as above.
2. Run manual `Release` with checkbox off and confirm build-only path.
3. Run manual `Release` with checkbox on + version and confirm:
   - `flake.nix` and `vcpkg.json` are version-bumped
   - release tag is pushed
   - release assets upload to the versioned release

## HARD DEV NOTE - LD-ANALYSE METADATA EXPORT WINDOWING FIX (2026-04-04)

The `Tools -> Export Decode Metadata` action in `ld-analyse` must behave as an integrated sub-window (same app/session behavior as Auto Audio Align), not as a separate detached application window.

### Root cause
- `ld-analyse` launched `tbc-export-metadata` GUI with `QProcess::startDetached()`, which creates a separate app window/process lifecycle.

### Required behavior
- Metadata export UI is opened in-process from `ld-analyse` and reused as a non-modal child window.
- It must show/raise/activate consistently and stay tied to the main app window behavior.

### Implementation now in place
- `src/ld-analyse/mainwindow.cpp`:
  - Replaced detached process GUI launch with an owned/reused `MetadataExportDialog` instance.
  - Configures non-modal window flags consistent with other tool sub-windows.
  - Applies source directory/default metadata input before showing.
- `src/ld-analyse/mainwindow.h`:
  - Added `MetadataExportDialog` forward declaration and member pointer.
- `src/ld-analyse/CMakeLists.txt`:
  - Added `../tbc-export-metadata/metadataexportdialog.cpp/.h/.ui` to `ld-analyse` target.
- `src/tbc-export-metadata/metadataexportdialog.cpp/.h`:
  - Added setters used by `ld-analyse` integration:
    - `setDefaultInputFile(...)`
    - `setExportExecutablePath(...)`

### Validation
- Build verification passed for `ld-analyse` and `tbc-export-metadata`.
- User confirmed real-world GUI behavior is fixed.

## HARD DEV NOTE - TBC-VIDEO-EXPORT PIXEL FORMAT + PADDING COMPATIBILITY (2026-05-04)

This section records the verified behavior for FFmpeg format selection, scaling, and codec-compatibility padding in `tbc-video-export`.

### Verified source of truth
- `src/tbc-video-export/src/tbc_video_export/process/wrapper/wrapper_ffmpeg.py`
- `src/tbc-video-export/src/tbc_video_export/opts/opt_validators.py`
- `src/tbc-video-export/src/tbc_video_export/common/enums.py`
- `src/tbc-video-export/tests/test_wrappers_ffmpeg.py`

### Pixel format resolution (`_get_profile_video_format`)
Resolution happens in this order:
1. Start from selected profile `video_format`.
2. If export is luma-focused (`LUMA`, `LUMA_4FSC`, `LUMA_EXTRACTED`) or two-step luma pass, force a GRAY format (`gray8` or `gray16le`) based on effective bitdepth.
3. If `--8bit/--10bit/--16bit` is provided, attempt format-family-preserving bitdepth remap via `VideoFormatType.get_new_format(...)`.
4. If explicit format alias is provided (`--yuv420/--yuv422/--yuv444/--gray`) and bitdepth is set, use that explicit family+bitdepth combination (except in two-step luma mode).

Validation rule:
- Setting a format alias without bitdepth is rejected (`--yuv420` etc. requires `--8bit/--10bit/--16bit`).

### Standard scaling behavior
- `--standard/--d1` injects:
  - PAL: `scale=720:576:flags=lanczos:interl=1,setsar=128/117`
  - NTSC/PAL-M: `scale=720:486:flags=lanczos:interl=1,setsar=12/13`
- This is only valid with default active-area framing.
- Validator rejects `--standard` with `--vbi`, `--full-vertical`, `--letterbox`, `--full-frame`, active-line overrides, and `--luma-4fsc`.

### Codec-specific compatibility padding (`_get_chroma_alignment_filter`)
Compatibility padding is only auto-added for:
- H.264 family: `libx264`, `h264_*`
- H.265/HEVC family: `libx265`, `hevc_*`
- AV1 software encoders: `libsvtav1`, `libaom-av1`

Padding behavior is centered:
- Horizontal: `(ow-iw)/2`
- Vertical: `(oh-ih)/2`

Rules by pixel format:
- For `yuv420*`:
  - H.264 family: `pad=ceil(iw/2)*2:ceil(ih/4)*4:...` (height multiple-of-4 path)
  - others above: `pad=ceil(iw/2)*2:ceil(ih/2)*2:...`
- For `yuv422*`:
  - implementation currently pads to even width and even height:
    `pad=ceil(iw/2)*2:ceil(ih/2)*2:...`

Note:
- The `yuv422` comment says “needs even width”, but implementation also enforces even height. Treat implementation as authoritative unless intentionally changed.

### Filter ordering guarantees
Video filter order is:
1. `setfield=...`
2. profile filters
3. optional black-level override
4. optional standard scale/SAR
5. optional appended video filter
6. codec-compat pad (if any)
7. DAR adjustment (`setdar=16/9`) when required
8. `format=...`
9. `setparams=...`
10. optional `hwupload` for VAAPI

This ordering keeps DAR near the end so earlier scale/pad filters do not overwrite it.

### Verification run (2026-05-04)
Focused test cases were executed and passed for:
- standard scaling (`pal`, `ntsc`, `pal-m` alias)
- anamorphic+standard behavior
- full-frame codec auto-pad (`h264 420`, `h264_lossless 422`) and ffv1 no-auto-pad case
- bitdepth + format alias mappings (`yuv420/422/444`, `gray`, luma-only gray paths)
