"""Fetch the pinned dependencies, build one platform with AMBuild and stage the release files."""

from __future__ import annotations

import argparse
import os
import shutil
import subprocess
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


def run(*args: str, cwd: Path = ROOT, env: dict[str, str] | None = None) -> None:
  subprocess.run(args, cwd=cwd, env=env, check=True)


def main() -> None:
  parser = argparse.ArgumentParser()
  parser.add_argument("--platform", choices=("windows", "linux"), required=True)
  parser.add_argument("--debug", action="store_true")
  args = parser.parse_args()

  python = sys.executable
  dependencies = ROOT / ".build-deps" / args.platform
  run(python, str(ROOT / "scripts" / "bootstrap.py"))
  build = ROOT / ("build" if args.platform == "windows" else "build-linux")
  build.mkdir(exist_ok=True)
  env = os.environ.copy()
  env["PYTHONPATH"] = str(dependencies / "ambuild")
  if args.platform == "linux":
    env.update(CC="gcc", CXX="g++")
  configure = [python, str(ROOT / "configure.py"),
    "--hl2sdk-root", str(dependencies),
    "--hl2sdk-manifests", str(dependencies / "hl2sdk-manifests"),
    "--mms-path", str(dependencies / "metamod-source")]
  if args.debug:
    configure.append("--enable-debug")
  run(*configure, cwd=build, env=env)
  run(python, "-c", "from ambuild2.run import cli_run; cli_run()", "--jobs", "4", cwd=build, env=env)

  name = "faceit_levels.dll" if args.platform == "windows" else "faceit_levels.so"
  binary = build / "faceit_levels" / f"{args.platform}-x86_64" / name
  if not binary.is_file() or binary.stat().st_size == 0:
    raise SystemExit(f"plugin binary was not produced: {binary}")
  # Release layout: both platforms share addons/faceit_levels/bin; Metamod adds the extension.
  stage = ROOT / "dist" / "addons"
  (stage / "faceit_levels" / "bin").mkdir(parents=True, exist_ok=True)
  (stage / "metamod").mkdir(parents=True, exist_ok=True)
  shutil.copy2(binary, stage / "faceit_levels" / "bin" / name)
  shutil.copy2(ROOT / "configs" / "config.ini", stage / "faceit_levels" / "config.ini")
  shutil.copy2(ROOT / "configs" / "faceit_levels.vdf", stage / "metamod" / "faceit_levels.vdf")
  print(f"Built {binary}")


if __name__ == "__main__":
  main()
