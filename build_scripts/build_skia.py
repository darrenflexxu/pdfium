import os
import subprocess
import sys
import argparse
import shutil
from pathlib import Path

def run_command(cmd, cwd=None, env=None):
    print(f"Executing: {' '.join(cmd) if isinstance(cmd, list) else cmd}")
    result = subprocess.run(cmd, cwd=cwd, env=env, shell=isinstance(cmd, str))
    if result.returncode != 0:
        print(f"Command failed with return code {result.returncode}")
        sys.exit(result.returncode)
    return result

def build_skia():
    parser = argparse.ArgumentParser(description="Build Skia from source for PDF Reader")
    parser.add_argument("--target-os", required=True, choices=["win", "mac", "linux", "android", "ios"], help="Target OS")
    parser.add_argument("--target-cpu", required=True, choices=["x64", "arm64", "arm", "x86"], help="Target CPU")
    parser.add_argument("--debug", action="store_true", help="Build in debug mode")
    parser.add_argument("--root", required=True, help="Project root directory")

    args = parser.parse_args()

    root_dir = Path(args.root).resolve()
    skia_dir = root_dir / "third_party" / "skia"
    depot_tools_dir = root_dir / "depot_tools"

    # 1. Ensure depot_tools is in PATH
    env = os.environ.copy()
    env["PATH"] = str(depot_tools_dir) + os.pathsep + env.get("PATH", "")

    # 2. Clone Skia if not present
    if not skia_dir.exists():
        print(f"Cloning Skia into {skia_dir}...")
        skia_dir.parent.mkdir(parents=True, exist_ok=True)
        run_command(["git", "clone", "https://skia.googlesource.com/skia", str(skia_dir)])

    # 3. Sync dependencies
    print("Syncing Skia dependencies...")
    run_command(["python3", "tools/git-sync-deps"], cwd=skia_dir, env=env)

    # 4. Configure build arguments
    config_name = "Debug" if args.debug else "Release"
    out_dir = skia_dir / "out" / config_name

    # Base arguments
    gn_args = [
        f"is_debug={'true' if args.debug else 'false'}",
        "is_official_build=true",
        "skia_use_system_expat=false",
        "skia_use_system_icu=false",
    ]

    # OS specific arguments
    if args.target_os == "mac":
        pass # Defaults are usually fine for macOS
    elif args.target_os == "ios":
        gn_args.append('target_os="ios"')
        gn_args.append('target_cpu="arm64"')
        gn_args.append("skia_use_metal=true")
    elif args.target_os == "android":
        gn_args.append('target_os="android"')
        gn_args.append('target_cpu="arm64"')
    elif args.target_os == "win":
        gn_args.append('target_os="win"')
    elif args.target_os == "linux":
        gn_args.append('target_os="linux"')

    # Join args into a single string for GN
    args_str = " ".join(gn_args)

    # 5. GN Gen
    print(f"Generating build files in {out_dir}...")
    run_command(["bin/gn", "gen", str(out_dir), f"--args='{args_str}'"], cwd=skia_dir, env=env)

    # 6. Ninja build
    print(f"Building Skia with Ninja...")
    run_command(["ninja", "-C", str(out_dir)], cwd=skia_dir, env=env)

    print(f"Skia build completed successfully. Artifacts are in {out_dir}")

if __name__ == "__main__":
    build_skia()
