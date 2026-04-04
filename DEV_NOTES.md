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

## HARD DEV NOTE - RELEASE WORKFLOW/UI CONTRACT (2026-04-04)

This repository now depends on a strict release workflow UI and run-order contract. Do not change this behavior without explicit confirmation.

### Required workflow files and entry points
- `.github/workflows/prepare_release.yml` is the release preparation entry point.
- `.github/workflows/release.yml` is the artifact build/publish workflow.

### Required `Release` manual UI fields
`release.yml` must keep `workflow_dispatch` with these inputs:
- `create_release` (boolean)
  - Description: `Create a GitHub release and upload built assets`
- `tag_name` (string)
  - Description: `Tag to release when manually dispatched (example: v0.6.0)`

Removing or renaming these inputs breaks the expected Actions modal and causes operator error.

### Required behavior
- If `create_release=false`: manual run is a build/sanity run only (no release upload).
- If `create_release=true`: `tag_name` is required, must normalize to `vMAJOR.MINOR.PATCH`, and must already exist on origin.
- Tag push matching `v*` must continue to run the release publish path automatically.

### Required release sequence (full release)
1. Run `Prepare Release Draft` with a version (`3.0.0` or `v3.0.0`).
2. Workflow updates version files, commits, creates/pushes tag, and creates draft release.
3. Tag push triggers `Release` workflow to build artifacts and upload release assets.

### Verification checklist after any release-workflow edit
1. In Actions → `Release` → `Run workflow`, verify checkbox and tag input appear exactly as above.
2. Run manual `Release` with checkbox off and confirm build-only path.
3. Run full path via `Prepare Release Draft` and confirm artifact upload into the draft release.

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
