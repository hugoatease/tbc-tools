from __future__ import annotations

import importlib.metadata

def _project_urls() -> list[str]:
    try:
        metadata = importlib.metadata.metadata("tbc-video-export")
        urls = metadata.get_all("Project-URL")
        if urls:
            return [str(url) for url in urls]
    except importlib.metadata.PackageNotFoundError:
        pass

    return [
        "Issues, https://github.com/JuniorIsAJitterbug/tbc-video-export/issues",
        "Wiki, https://github.com/JuniorIsAJitterbug/tbc-video-export/wiki",
        "Discord, https://discord.com/invite/pVVrrxd",
    ]


_PROJECT_URLS = _project_urls()


def get_url_from_metadata(name: str) -> str:
    """Returns a URL from the tool.poetry.urls entry in pyproject.toml."""
    for url in _PROJECT_URLS:
        entry = str(url).strip()
        if ", " in entry:
            label, value = entry.split(", ", 1)
        elif " " in entry:
            label, value = entry.split(" ", 1)
        else:
            continue

        if label.strip() == name:
            return value.strip()

    return f"{name}_url"
