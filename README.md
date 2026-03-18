# TBC Tools


This is the complete suite of tools for processing TBC (Time Base Corrected) files from the decode projects, [vhs-decode](https://github.com/oyvindln/vhs-decode/wiki), [cvbs-decode](https://github.com/oyvindln/vhs-decode/wiki/CVBS-Composite-Decode), [ld-decode](https://github.com/happycube/ld-decode). 

This is an experimental, highly subject to change feature addtion focused branch based off the orignal ld-tools, directly after the cut-off for [decode-orc](https://simoninns.github.io/decode-orc-docs/decode-orc/) developmet.

These tools are for analyzing analog video sources, handling the 4fsc video data, metadata and final tweaks in framing, chroma-decoding and video levels before converting to YUV digital video files. 

Please see the [releases page](https://github.com/harrypm/tbc-tools/releases/) for ready to use self-contained binarys for production usage. 

## Images



## Tool Categories

### Core Processing Tools

- **ld-analyse**       - GUI tool for TBC file visual analysis & adjustment handler. Supports drag-and-drop loading of `.tbc`, `.ytbc`, `.ctbc`, `.tbcy`, `.tbcc`, `.db`, and `.json` files into the main window.
- **ld-process-vbi**   - Decode Vertical Blanking Interval data (Closed Captions, VITC - Vertical Interval Time Code, XDS Data) 
- **ld-process-vits**  - Process Vertical Interval Test Signals
- **ld-process-ac3**   - Extract Dolby Digital AC3 audio tracks

### EFM Decoder Suite

*Replaces deprecated ld-process-efm with staged decoding and stacking capabilities*

- **efm-decoder-f2**     - Convert EFM T-values to F2 sections
- **efm-decoder-d24**    - Convert F2 sections to Data24 format
- **efm-decoder-audio**  - Convert EFM Data24 sections to 16-bit stereo PCM audio
- **efm-decoder-data**   - Convert EFM Data24 sections to ECMA-130 binary data
- **efm-stacker-f2**     - Combine multiple F2 captures for improved quality

### Tools

- **ld-discmap**          - TBC and VBI alignment and correction tool
- **ld-dropout-correct**  - Advanced dropout detection and correction
- **ld-chroma-decoder**   - Color decoder for TBC LaserDisc video to RGB/YUV conversion
- **ld-disc-stacker**     - Combine multiple TBC captures for improved quality

### Export and Conversion Tools

- **ld-export-metadata**      - Export TBC metadata to external formats
- **ld-lds-converter**        - Convert between 10-bit packed and 16-bit data formats for DomesDay Duplicator captures.
- **tbc-metadata-converter**  - Convert between  JSON & SQLite metadata formats
- **tbc-video-export**        - Direct to video export task handler for .tbc files. 

### Utility Scripts

- **ld-compress**              - Compress TBC files for storage (in scripts/)
- **filtermaker**              - Create custom filtering profiles (in scripts/)
- **tbc-video-export-legacy**  - Legacy TBC to video conversion (archived)

## Getting Started

1. **Analysis**: Use `ld-analyse` to assess capture quality and identify issues such as off-set images or chroma phase/gain correction.
2. **Correction**: Apply `ld-dropout-correct` for dropout masking.
3. **Export**: Convert to final formats using `tbc-video-export` to chroma-decode and encode via FFmpeg profile to a video file.

## Important Notes

- **Metadata Formats**: SQLite (`.tbc.db`) is the 2026-present metadata format, JSON metadata (2017-2026) being still supported by the tools and used by many older builds for the decode suite. 
- **File Extensions**: TBC files use `.tbc` extension; metadata is commonly `.tbc.json` & `.tbc.db` (SQLite) 
- **Dependencies**: Most tools require FFmpeg and other multimedia libraries.
- **Performance**: Many tools support multi-threading for fast real-time or faster processing.

## Documentation

Each tool directory contains detailed README.md files with:
- Basic usage instructions
- Complete option references
- Usage examples
- Input/output format specifications
- Troubleshooting Refrences

See individual tool directories for specific tool documentation.
