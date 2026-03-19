from __future__ import annotations

import importlib.metadata
from datetime import datetime
from pathlib import Path
from typing import TYPE_CHECKING

from tbc_video_export.common.enums import VideoFormatType
from tbc_video_export.common.utils.metadata import get_url_from_metadata

if TYPE_CHECKING:
    from typing import Final


def _load_metadata():
    try:
        return importlib.metadata.metadata("tbc-video-export")
    except importlib.metadata.PackageNotFoundError:
        return {
            "Name": "tbc-video-export",
            "Version": __version__,
            "Summary": "Tool for exporting S-Video and CVBS-type TBCs to video files.",
            "Home-Page": "https://github.com/JuniorIsAJitterbug/tbc-video-export",
        }

# substituted by poetry-dynamic-versioning when doing pyinstaller builds
__version__ = "0.0.0"

_metadata = _load_metadata()

APPLICATION_NAME: Final = _metadata.get("Name", "tbc-video-export")
PROJECT_VERSION: Final = (
    _metadata.get("Version", __version__) if __version__ == "0.0.0" else __version__
)
PROJECT_CREDITS: Final = (
    "Credits:\n"
    "  Jitterbug\tDevelopment (https://github.com/JuniorIsAJitterbug)\n"
    "  Harry Munday\tProject Management (https://github.com/harrypm)\n"
)
PROJECT_SUMMARY: Final = f"{_metadata.get('Summary', '')}\n\n{PROJECT_CREDITS}"
PROJECT_URL: Final = _metadata.get(
    "Home-Page", "https://github.com/JuniorIsAJitterbug/tbc-video-export"
)
PROJECT_URL_ISSUES: Final = get_url_from_metadata("Issues")
PROJECT_URL_WIKI: Final = get_url_from_metadata("Wiki")
PROJECT_URL_DISCORD: Final = get_url_from_metadata("Discord")
PROJECT_URL_WIKI_COMMANDLIST: Final = f"{PROJECT_URL_WIKI}/Command-List"
PROJECT_URL_WIKI_PROFILES: Final = f"{PROJECT_URL_WIKI}/FFmpeg-Profiles"


CURRENT_TIMESTAMP: Final = datetime.now().strftime("%y-%m-%d_%H%M%S%f")[:-3]
EXPORT_CONFIG_FILE_NAME: Final = Path(f"{APPLICATION_NAME}.json")
TWO_STEP_OUT_FILE_LUMA_SUFFIX: Final = "luma"


MINIMUM_TERMINAL_HEIGHT: Final = 30
MINIMUM_TERMINAL_WIDTH: Final = 120


SUCCESS_SYMBOL: Final = "●"
ERROR_SYMBOL: Final = "○"
RUNNING_SYMBOLS: Final[tuple[str, ...]] = ("/", "-", "\\", "|")


PIPE_BUFFER_SIZE: Final = 4 * 1024 * 1024  # 4MB

# for NT ANSI enabling
NT_STD_OUTPUT_HANDLE: Final = -11
NT_ENABLE_PROCESSED_OUTPUT: Final = 0x1
NT_ENABLE_WRAP_AT_EOL_OUTPUT: Final = 0x2
NT_ENABLE_VIRTUAL_TERMINAL_PROCESSING: Final = 0x4

# for NT named pipes
NT_NAMED_PIPE_MAX_INSTANCES: Final = 1
NT_NAMED_PIPE_TIMEOUT: Final = 0
NT_NAMED_PIPE_BUFFER_SIZE: Final = 65536

# for NT proc snapshots
NT_TH32CS_SNAPPROCESS: Final = 0x2


# Ubuntu 22.04 uses FFmpeg 4.4.1 which does not support the new format
FFMPEG_USE_OLD_MERGEPLANES: Final = True
FFMPEG_VIDEO_MAP: Final = "[v_output]"
FFMPEG_DEFAULT_LUMA_FORMAT: Final = VideoFormatType.GRAY.value.get(16)
FFMPEG_DEFAULT_CHROMA_FORMAT: Final = VideoFormatType.YUV444.value.get(16)
