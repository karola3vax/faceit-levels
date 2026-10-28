"""Fetch the exact build dependencies declared in build-dependencies.json."""

from __future__ import annotations

import json
import subprocess
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
DEPENDENCIES = ROOT / ".build-deps" / ("windows" if sys.platform == "win32" else "linux")


def git(target: Path, *args: str) -> str:
  return subprocess.run(["git", "-c", f"safe.directory={target}", "-C", str(target), *args],
    check=True, capture_output=True, text=True).stdout.strip()


def checkout(name: str, spec: dict[str, str]) -> Path:
  target = DEPENDENCIES / name
  if not (target / ".git").is_dir():
    target.mkdir(parents=True, exist_ok=True)
    git(target, "init", "-q")
    git(target, "remote", "add", "origin", spec["url"])
  if git(target, "status", "--porcelain"):
    raise RuntimeError(f"{name} has local changes; refusing to replace them")
  if subprocess.run(["git", "-c", f"safe.directory={target}", "-C", str(target), "rev-parse", "-q", "--verify",
      spec["commit"] + "^{commit}"], capture_output=True).returncode != 0:
    git(target, "fetch", "-q", "--depth", "1", "origin", spec["commit"])
  git(target, "checkout", "-q", "--detach", spec["commit"])
  if git(target, "rev-parse", "HEAD") != spec["commit"]:
    raise RuntimeError(f"{name} did not resolve to {spec['commit']}")
  return target


def main() -> None:
  manifest = json.loads((ROOT / "build-dependencies.json").read_text(encoding="utf-8"))
  DEPENDENCIES.mkdir(parents=True, exist_ok=True)
  checkout("ambuild", manifest["ambuild"])
  metamod = checkout("metamod-source", manifest["metamod"])
  # KHook is header-only for plugins; Metamod's gitlink pins it.
  git(metamod, "submodule", "update", "-q", "--init", "--depth", "1", "third_party/khook")
  checkout("hl2sdk-manifests", manifest["hl2sdk_manifests"])
  # SdkHelpers looks for hl2sdk-<name>; s2sdk's cs2 branch is that SDK.
  checkout("hl2sdk-cs2", manifest["s2sdk"])


if __name__ == "__main__":
  main()
