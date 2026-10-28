#!/usr/bin/env bash
# Runs inside the SteamRT3 SDK image (see build-steamrt3.ps1), or on any Linux with gcc and git.
set -euo pipefail

repo="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
# Windows bind mounts look foreign-owned to git; trust only this disposable container's paths.
git config --global --add safe.directory '*'
python3 "$repo/scripts/build.py" --platform linux
