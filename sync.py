"""
Skia Renderer - Dependency Sync Script
Downloads SDL3, vk-bootstrap, Skia with aria2 + 7z
Note: VMA is built into Skia (skia_use_vma=True), no separate download needed
"""

import os
import sys
import shutil
import subprocess
import argparse
import platform
from pathlib import Path

# Default versions
SDL3_VERSION = "3.4.2"
VKBOOTSTRAP_VERSION = "1.4.343"

def find_tool(name: str, extra_paths: list = None) -> str:
    """Find a tool in PATH or specified paths"""
    result = shutil.which(name)
    if result:
        return result
    
    if extra_paths:
        for path in extra_paths:
            exe = Path(path) / name
            if exe.exists():
                return str(exe)
            if platform.system() == "Windows":
                exe = Path(path) / f"{name}.exe"
                if exe.exists():
                    return str(exe)
    
    # Check 7-Zip on Windows
    if name == "7z" and platform.system() == "Windows":
        for p in [r"C:\Program Files\7-Zip", r"C:\Program Files (x86)\7-Zip"]:
            exe = Path(p) / "7z.exe"
            if exe.exists():
                return str(exe)
    
    return None

def run_cmd(cmd: list, cwd: str = None, check: bool = True, env: dict = None) -> subprocess.CompletedProcess:
    """Run a command"""
    merged_env = os.environ.copy()
    if env:
        merged_env.update(env)
    
    print(f"  Running: {' '.join(str(c) for c in cmd)}")
    result = subprocess.run(cmd, cwd=cwd, env=merged_env)
    if check and result.returncode != 0:
        raise subprocess.CalledProcessError(result.returncode, cmd)
    return result

def download_with_aria2(url: str, output_dir: Path, filename: str, 
                        proxy: str = None) -> Path:
    """Download a file using aria2c"""
    aria2 = find_tool("aria2c")
    if not aria2:
        raise RuntimeError("aria2c not found. Install: winget install aria2")
    
    output_path = output_dir / filename
    
    cmd = [
        aria2, "-x", "16", "-s", "32", "-k", "1M",
        "--check-certificate=false",
        "--file-allocation=none",
        "--allow-overwrite=true",
        "--auto-file-renaming=false",
        "-d", str(output_dir),
        "-o", filename,
        url
    ]
    
    if proxy:
        cmd.extend(["--all-proxy", proxy])
    
    print(f"  Downloading: {filename}")
    run_cmd(cmd)
    
    if not output_path.exists():
        raise RuntimeError(f"Failed to download {filename}")
    
    return output_path

def extract_with_7z(archive: Path, output_dir: Path, strip_components: int = 1) -> Path:
    """Extract an archive using 7z"""
    sevenzip = find_tool("7z")
    if not sevenzip:
        raise RuntimeError("7z not found. Install: winget install 7zip.7zip")
    
    print(f"  Extracting: {archive.name}")
    
    if strip_components == 0:
        cmd = [sevenzip, "x", "-y", f"-o{output_dir}", str(archive)]
        run_cmd(cmd)
        return output_dir
    
    # Extract to temp, then move
    temp_dir = output_dir.parent / f"{output_dir.name}_temp"
    if temp_dir.exists():
        shutil.rmtree(temp_dir)
    temp_dir.mkdir(parents=True)
    
    cmd = [sevenzip, "x", "-y", f"-o{temp_dir}", str(archive)]
    run_cmd(cmd)
    
    # Navigate through strip_components
    src_dir = temp_dir
    for _ in range(strip_components):
        subdirs = [d for d in src_dir.iterdir() if d.is_dir()]
        if len(subdirs) == 1:
            src_dir = subdirs[0]
        else:
            break
    
    # Move to final location
    if output_dir.exists():
        shutil.rmtree(output_dir)
    shutil.move(str(src_dir), str(output_dir))
    
    # Cleanup temp
    if temp_dir.exists():
        shutil.rmtree(temp_dir)
    
    return output_dir

def git_clone(url: str, target_dir: Path, depth: int = 1, branch: str = None) -> Path:
    """Clone a git repository"""
    git = find_tool("git")
    if not git:
        raise RuntimeError("git not found")
    
    if target_dir.exists():
        print(f"  Removing existing: {target_dir}")
        shutil.rmtree(target_dir)
    
    cmd = [git, "clone"]
    if depth:
        cmd.extend(["--depth", str(depth)])
    if branch:
        cmd.extend(["--branch", branch])
    cmd.extend([url, str(target_dir)])
    
    print(f"  Cloning: {url}")
    run_cmd(cmd)
    
    return target_dir

def sync_deps(args):
    """Main sync function"""
    script_dir = Path(__file__).parent.resolve()
    deps_dir = script_dir / "deps"
    downloads_dir = script_dir / "downloads"
    
    deps_dir.mkdir(parents=True, exist_ok=True)
    downloads_dir.mkdir(parents=True, exist_ok=True)
    
    print("=" * 50)
    print("Skia Renderer - Dependency Sync")
    print("=" * 50)
    print()
    
    # Check tools
    print("[Checking Tools]")
    tools = {
        "aria2c": find_tool("aria2c"),
        "7z": find_tool("7z"),
        "git": find_tool("git"),
        "python": sys.executable,
    }
    
    for name, path in tools.items():
        if path:
            print(f"  [OK] {name}: {path}")
        else:
            print(f"  [ERROR] {name} not found")
            return 1
    print()
    
    overwrite = not args.no_overwrite
    
    # 1. SDL3
    if not args.skip_sdl:
        print("=" * 50)
        print("[1/3] SDL3")
        print("=" * 50)
        
        sdl_dir = deps_dir / "SDL3"
        
        if sdl_dir.exists() and not overwrite:
            print("  SDL3 already exists, skipping")
        else:
            if sdl_dir.exists():
                shutil.rmtree(sdl_dir)
            
            url = f"https://github.com/libsdl-org/SDL/archive/refs/tags/release-{SDL3_VERSION}.zip"
            
            archive = download_with_aria2(url, downloads_dir, f"SDL3-{SDL3_VERSION}.zip",
                                         proxy=args.proxy)
            extract_with_7z(archive, sdl_dir)
            print("  [OK] SDL3")
        print()
    
    # 2. vk-bootstrap
    if not args.skip_vkbootstrap:
        print("=" * 50)
        print("[2/3] vk-bootstrap")
        print("=" * 50)
        
        vkb_dir = deps_dir / "vk-bootstrap"
        
        if vkb_dir.exists() and not overwrite:
            print("  vk-bootstrap already exists, skipping")
        else:
            if vkb_dir.exists():
                shutil.rmtree(vkb_dir)
            
            url = f"https://github.com/charles-lunarg/vk-bootstrap/archive/refs/tags/v{VKBOOTSTRAP_VERSION}.zip"
            archive = download_with_aria2(url, downloads_dir, f"vk-bootstrap-{VKBOOTSTRAP_VERSION}.zip",
                                         proxy=args.proxy)
            extract_with_7z(archive, vkb_dir)
            print("  [OK] vk-bootstrap")
        print()
    
    # Note: VMA is built into Skia, controlled by skia_use_vma=True in build_deps.py
    # No separate download needed

    
    # 3. Skia
    if not args.skip_skia:
        print("=" * 50)
        print("[3/3] Skia")
        print("=" * 50)
        
        skia_dir = deps_dir / "skia"
        depot_dir = deps_dir / "depot_tools"
        
        # Clone depot_tools
        if not depot_dir.exists():
            depot_url = "https://chromium.googlesource.com/chromium/tools/depot_tools.git"
            git_clone(depot_url, depot_dir)
        else:
            print(f"  depot_tools already exists: {depot_dir}")
        
        # Clone Skia
        if skia_dir.exists() and not overwrite:
            print("  Skia already exists, skipping clone")
        else:
            if skia_dir.exists():
                shutil.rmtree(skia_dir)
            
            skia_url = "https://skia.googlesource.com/skia.git"
            git_clone(skia_url, skia_dir)
        
        # Sync dependencies
        print("  Syncing Skia dependencies...")
        sync_script = skia_dir / "tools" / "git-sync-deps"
        if sync_script.exists():
            try:
                run_cmd([sys.executable, str(sync_script)], cwd=str(skia_dir), check=False)
            except Exception as e:
                print(f"  Warning: Skia deps sync error: {e}")
        else:
            print("  Warning: git-sync-deps not found")
        
        print("  [OK] Skia")
        print()
    
    # Cleanup downloads
    if not args.keep_downloads:
        print("Cleaning up downloads...")
        if downloads_dir.exists():
            shutil.rmtree(downloads_dir)
            print("  [OK] Downloads cleaned")
        print()
    
    # Summary
    print("=" * 50)
    print("Dependency Sync Complete!")
    print("=" * 50)
    print()
    print("Dependencies:")
    for name in ["SDL3", "vk-bootstrap", "skia", "depot_tools"]:
        if (deps_dir / name).exists():
            print(f"  [OK] {name}")
    print()
    print("Next: python build_deps.py")
    print()
    
    return 0

def main():
    parser = argparse.ArgumentParser(description="Sync dependencies for Skia Renderer")
    
    # Sync options
    parser.add_argument("--skip-skia", action="store_true", help="Skip Skia")
    parser.add_argument("--skip-sdl", action="store_true", help="Skip SDL3")
    parser.add_argument("--skip-vkbootstrap", action="store_true", help="Skip vk-bootstrap")
    parser.add_argument("--no-overwrite", action="store_true", help="Don't overwrite existing")
    
    # Download options
    parser.add_argument("--proxy", type=str, help="Proxy URL")
    parser.add_argument("--keep-downloads", action="store_true", help="Keep downloaded archives")
    
    args = parser.parse_args()
    
    try:
        return sync_deps(args)
    except Exception as e:
        print(f"\nERROR: {e}")
        return 1

if __name__ == "__main__":
    sys.exit(main())
