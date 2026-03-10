import os
import subprocess
import platform
import shutil
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
BUILD_DIR = ROOT / "Build" / "Windows"

def validate_tool(name: str):
    if shutil.which(name) is None:
        raise RuntimeError(f"Required tool not found: {name}")
    
def update_submodules():
    print("Updating submodules")
    subprocess.run(
        ["git", "submodule", "update", "--init", "--recursive"],
        check=True,
        cwd=ROOT
    )

def generate_project_files():
    print("Generating Visual Studio project files...")
    BUILD_DIR.mkdir(parents=True, exist_ok=True)

    subprocess.run([
        "cmake",
        "-S", str(ROOT),
        "-B", str(BUILD_DIR),
        "-G", "Visual Studio 17 2022",
        "-A", "x64"
    ], check=True, cwd=ROOT)

def main():
    validate_tool("git")
    validate_tool("cmake")

    update_submodules()
    generate_project_files()

    print("Setup completed.")

if __name__ == "__main__":
    main()