import os
import sys
import shutil
import subprocess
import argparse
import platform
import tarfile
import zipfile
from pathlib import Path

# Default versions
SDL3_VERSION = "3.4.2"
VKBOOTSTRAP_VERSION = "1.4.343"
SPDLOG_VERSION = "1.17.0"
CLI11_VERSION = "2.6.2"
DIRECTX_HEADERS_REPO = "https://github.com/microsoft/DirectX-Headers.git"

# ANGLE configuration
ANGLE_COMMIT = "ae66dc5ad3506d3ea7196da4dba54a7b1f8b4f8c"
ANGLE_REPO_URL = "https://github.com/google/angle.git"

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

# ========================================
# Download Functions
# ========================================

def download_with_aria2(url: str, output_dir: Path, filename: str, 
                        proxy: str = None) -> Path:
    """Download using aria2c (fast, multi-threaded)"""
    aria2 = find_tool("aria2c")
    if not aria2:
        return None
    
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
    
    print(f"  Downloading (aria2): {filename}")
    run_cmd(cmd)
    
    return output_path if output_path.exists() else None

def download_with_curl(url: str, output_dir: Path, filename: str,
                       proxy: str = None) -> Path:
    """Download using curl (fallback)"""
    curl = find_tool("curl")
    if not curl:
        return None
    
    output_path = output_dir / filename
    
    cmd = [
        curl, "-L", "-o", str(output_path),
        "--connect-timeout", "30",
        "--retry", "3",
    ]
    
    if proxy:
        cmd.extend(["--proxy", proxy])
    
    cmd.append(url)
    
    print(f"  Downloading (curl): {filename}")
    run_cmd(cmd)
    
    return output_path if output_path.exists() else None

def download_with_wget(url: str, output_dir: Path, filename: str,
                       proxy: str = None) -> Path:
    """Download using wget (fallback)"""
    wget = find_tool("wget")
    if not wget:
        return None
    
    output_path = output_dir / filename
    
    cmd = [wget, "-O", str(output_path)]
    
    if proxy:
        cmd.extend(["-e", f"http_proxy={proxy}", "-e", f"https_proxy={proxy}"])
    
    cmd.append(url)
    
    print(f"  Downloading (wget): {filename}")
    run_cmd(cmd)
    
    return output_path if output_path.exists() else None

def download_file(url: str, output_dir: Path, filename: str,
                  proxy: str = None) -> Path:
    """Download a file using available tool"""
    output_dir.mkdir(parents=True, exist_ok=True)
    output_path = output_dir / filename
    
    # Skip if already downloaded
    if output_path.exists():
        print(f"  Already exists: {filename}")
        return output_path
    
    # Try tools in order of preference
    result = None
    
    result = download_with_aria2(url, output_dir, filename, proxy)
    if result:
        return result
    
    result = download_with_curl(url, output_dir, filename, proxy)
    if result:
        return result
    
    result = download_with_wget(url, output_dir, filename, proxy)
    if result:
        return result
    
    raise RuntimeError("No download tool found. Install aria2, curl, or wget.")

# ========================================
# Extraction Functions
# ========================================

def extract_with_7z(archive: Path, output_dir: Path, strip_components: int = 1) -> Path:
    """Extract using 7z"""
    sevenzip = find_tool("7z")
    if not sevenzip:
        return None
    
    print(f"  Extracting (7z): {archive.name}")
    
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
    
    if temp_dir.exists():
        shutil.rmtree(temp_dir)
    
    return output_dir

def extract_with_unzip(archive: Path, output_dir: Path, strip_components: int = 1) -> Path:
    """Extract zip using unzip"""
    unzip = find_tool("unzip")
    if not unzip or not str(archive).endswith('.zip'):
        return None
    
    print(f"  Extracting (unzip): {archive.name}")
    
    temp_dir = output_dir.parent / f"{output_dir.name}_temp"
    if temp_dir.exists():
        shutil.rmtree(temp_dir)
    temp_dir.mkdir(parents=True)
    
    cmd = [unzip, "-q", "-o", str(archive), "-d", str(temp_dir)]
    run_cmd(cmd)
    
    # Navigate through strip_components
    src_dir = temp_dir
    for _ in range(strip_components):
        subdirs = [d for d in src_dir.iterdir() if d.is_dir()]
        if len(subdirs) == 1:
            src_dir = subdirs[0]
        else:
            break
    
    if output_dir.exists():
        shutil.rmtree(output_dir)
    shutil.move(str(src_dir), str(output_dir))
    
    if temp_dir.exists():
        shutil.rmtree(temp_dir)
    
    return output_dir

def extract_with_tar(archive: Path, output_dir: Path, strip_components: int = 1) -> Path:
    """Extract tar archives using tar command"""
    tar = find_tool("tar")
    if not tar:
        return None
    
    name = str(archive).lower()
    if not (name.endswith('.tar.gz') or name.endswith('.tgz') or 
            name.endswith('.tar.bz2') or name.endswith('.tar.xz') or 
            name.endswith('.tar')):
        return None
    
    print(f"  Extracting (tar): {archive.name}")
    
    if output_dir.exists():
        shutil.rmtree(output_dir)
    
    # tar can handle strip-components natively
    cmd = [tar, "-xf", str(archive), "-C", str(output_dir.parent)]
    if strip_components > 0:
        cmd.extend([f"--strip-components={strip_components}"])
    
    # Rename extracted dir to target name
    temp_name = output_dir.parent / "extract_temp"
    if temp_name.exists():
        shutil.rmtree(temp_name)
    
    run_cmd(cmd)
    
    # Find extracted directory
    for item in output_dir.parent.iterdir():
        if item.is_dir() and item != temp_name:
            if item.name.startswith("extract_temp"):
                continue
            shutil.move(str(item), str(output_dir))
            break
    
    return output_dir

def extract_with_python(archive: Path, output_dir: Path, strip_components: int = 1) -> Path:
    """Extract using Python's built-in modules (fallback)"""
    print(f"  Extracting (Python): {archive.name}")
    
    temp_dir = output_dir.parent / f"{output_dir.name}_temp"
    if temp_dir.exists():
        shutil.rmtree(temp_dir)
    temp_dir.mkdir(parents=True)
    
    name = str(archive).lower()
    
    try:
        if name.endswith('.zip'):
            with zipfile.ZipFile(archive, 'r') as zf:
                zf.extractall(temp_dir)
        elif name.endswith('.tar.gz') or name.endswith('.tgz'):
            with tarfile.open(archive, 'r:gz') as tf:
                tf.extractall(temp_dir)
        elif name.endswith('.tar.bz2'):
            with tarfile.open(archive, 'r:bz2') as tf:
                tf.extractall(temp_dir)
        elif name.endswith('.tar.xz'):
            with tarfile.open(archive, 'r:xz') as tf:
                tf.extractall(temp_dir)
        elif name.endswith('.tar'):
            with tarfile.open(archive, 'r') as tf:
                tf.extractall(temp_dir)
        else:
            return None
    except Exception as e:
        print(f"  Python extraction failed: {e}")
        return None
    
    # Navigate through strip_components
    src_dir = temp_dir
    for _ in range(strip_components):
        subdirs = [d for d in src_dir.iterdir() if d.is_dir()]
        if len(subdirs) == 1:
            src_dir = subdirs[0]
        else:
            break
    
    if output_dir.exists():
        shutil.rmtree(output_dir)
    shutil.move(str(src_dir), str(output_dir))
    
    if temp_dir.exists():
        shutil.rmtree(temp_dir)
    
    return output_dir

def extract_archive(archive: Path, output_dir: Path, strip_components: int = 1) -> Path:
    """Extract an archive using available tool"""
    result = None
    
    # Try 7z first (handles all formats)
    result = extract_with_7z(archive, output_dir, strip_components)
    if result:
        return result
    
    # Try native tar
    result = extract_with_tar(archive, output_dir, strip_components)
    if result:
        return result
    
    # Try unzip for zip files
    result = extract_with_unzip(archive, output_dir, strip_components)
    if result:
        return result
    
    # Try Python's built-in (last resort)
    result = extract_with_python(archive, output_dir, strip_components)
    if result:
        return result
    
    raise RuntimeError(f"No extraction tool found for {archive.name}")

# ========================================
# Git Clone
# ========================================

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

def git_clone_at_commit(url: str, target_dir: Path, commit: str, depth: int = 1) -> Path:
    """Clone a git repository at a specific commit"""
    git = find_tool("git")
    if not git:
        raise RuntimeError("git not found")
    
    if target_dir.exists():
        print(f"  Removing existing: {target_dir}")
        shutil.rmtree(target_dir)
    
    # Clone with depth=1 for shallow clone, then fetch the specific commit
    print(f"  Cloning: {url}")
    cmd = [git, "clone", "--no-checkout"]
    if depth:
        cmd.extend(["--depth", str(depth)])
    cmd.extend([url, str(target_dir)])
    run_cmd(cmd)
    
    # Fetch the specific commit
    print(f"  Fetching commit: {commit[:8]}")
    if depth:
        cmd = [git, "fetch", "--depth", "1", "origin", commit]
    else:
        cmd = [git, "fetch", "origin", commit]
    run_cmd(cmd, cwd=str(target_dir))
    
    # Checkout the commit
    cmd = [git, "checkout", commit]
    run_cmd(cmd, cwd=str(target_dir))
    
    return target_dir

def setup_angle_gclient(angle_dir: Path, commit: str) -> None:
    """Create .gclient file for ANGLE and run gclient sync"""
    gclient_file = angle_dir.parent / ".gclient"
    
    gclient_content = f'''solutions = [
  {{
    "name": "angle",
    "url": "https://github.com/google/angle.git@{commit}",
    "deps_file": "DEPS",
    "managed": False,
    "custom_deps": {{}},
  }},
]
'''
    
    with open(gclient_file, 'w') as f:
        f.write(gclient_content)
    
    print(f"  Created .gclient config")

# ========================================
# Main
# ========================================

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
        "git": find_tool("git"),
    }
    
    # Download tools
    download_tools = []
    if find_tool("aria2c"):
        download_tools.append("aria2c")
    if find_tool("curl"):
        download_tools.append("curl")
    if find_tool("wget"):
        download_tools.append("wget")
    
    # Extraction tools  
    extract_tools = []
    if find_tool("7z"):
        extract_tools.append("7z")
    if find_tool("tar"):
        extract_tools.append("tar")
    if find_tool("unzip"):
        extract_tools.append("unzip")
    # Python is always available
    extract_tools.append("Python")
    
    print(f"  git: {tools['git'] or 'NOT FOUND'}")
    print(f"  Download: {', '.join(download_tools) or 'NONE'}")
    print(f"  Extract: {', '.join(extract_tools)}")
    print(f"  Python: {sys.executable}")
    
    if not tools["git"]:
        print("\n  ERROR: git is required")
        return 1
    
    if not download_tools:
        print("\n  ERROR: No download tool found (aria2c, curl, or wget)")
        return 1
    
    print()
    
    overwrite = not args.no_overwrite
    
    # 1. SDL3
    if not args.skip_sdl:
        print("=" * 50)
        print("[1/7] SDL3")
        print("=" * 50)
        
        sdl_dir = deps_dir / "SDL3"
        
        if sdl_dir.exists() and not overwrite:
            print("  SDL3 already exists, skipping")
        else:
            if sdl_dir.exists():
                shutil.rmtree(sdl_dir)
            
            url = f"https://github.com/libsdl-org/SDL/archive/refs/tags/release-{SDL3_VERSION}.zip"
            
            archive = download_file(url, downloads_dir, f"SDL3-{SDL3_VERSION}.zip",
                                   proxy=args.proxy)
            extract_archive(archive, sdl_dir)
            print("  [OK] SDL3")
        print()
    
    # 2. vk-bootstrap
    if not args.skip_vkbootstrap:
        print("=" * 50)
        print("[2/7] vk-bootstrap")
        print("=" * 50)
        
        vkb_dir = deps_dir / "vk-bootstrap"
        
        if vkb_dir.exists() and not overwrite:
            print("  vk-bootstrap already exists, skipping")
        else:
            if vkb_dir.exists():
                shutil.rmtree(vkb_dir)
            
            url = f"https://github.com/charles-lunarg/vk-bootstrap/archive/refs/tags/v{VKBOOTSTRAP_VERSION}.zip"
            archive = download_file(url, downloads_dir, f"vk-bootstrap-{VKBOOTSTRAP_VERSION}.zip",
                                   proxy=args.proxy)
            extract_archive(archive, vkb_dir)
            print("  [OK] vk-bootstrap")
        print()
    
    # 3. spdlog (header-only library)
    if not args.skip_spdlog:
        print("=" * 50)
        print("[3/7] spdlog")
        print("=" * 50)
        
        spdlog_dir = deps_dir / "spdlog"
        
        if spdlog_dir.exists() and not overwrite:
            print("  spdlog already exists, skipping")
        else:
            if spdlog_dir.exists():
                shutil.rmtree(spdlog_dir)
            
            url = f"https://github.com/gabime/spdlog/archive/refs/tags/v{SPDLOG_VERSION}.zip"
            archive = download_file(url, downloads_dir, f"spdlog-{SPDLOG_VERSION}.zip",
                                   proxy=args.proxy)
            extract_archive(archive, spdlog_dir)
            print("  [OK] spdlog")
        print()
    
    # 4. CLI11 (header-only library)
    if not args.skip_cli11:
        print("=" * 50)
        print("[4/7] CLI11")
        print("=" * 50)
        
        cli11_dir = deps_dir / "CLI11"
        
        if cli11_dir.exists() and not overwrite:
            print("  CLI11 already exists, skipping")
        else:
            if cli11_dir.exists():
                shutil.rmtree(cli11_dir)
            
            url = f"https://github.com/CLIUtils/CLI11/archive/refs/tags/v{CLI11_VERSION}.zip"
            archive = download_file(url, downloads_dir, f"CLI11-{CLI11_VERSION}.zip",
                                   proxy=args.proxy)
            extract_archive(archive, cli11_dir)
            print("  [OK] CLI11")
        print()

    # 5. DirectX-Headers (for D3D12 development on Windows)
    if not args.skip_directx_headers:
        print("=" * 50)
        print("[5/7] DirectX-Headers")
        print("=" * 50)

        dxheaders_dir = deps_dir / "DirectX-Headers"

        if dxheaders_dir.exists() and not overwrite:
            print("  DirectX-Headers already exists, skipping")
        else:
            if dxheaders_dir.exists():
                shutil.rmtree(dxheaders_dir)

            git_clone(DIRECTX_HEADERS_REPO, dxheaders_dir, depth=1)
            print("  [OK] DirectX-Headers")
        print()
    else:
        print("[5/7] DirectX-Headers - Skipped (--skip-directx-headers)")
        print()

    # 6. ANGLE (can be skipped with --skip-angle if already downloaded)
    if not args.skip_angle:
        print("=" * 50)
        print("[6/7] ANGLE")
        print("=" * 50)
        
        angle_dir = deps_dir / "angle"
        depot_dir = deps_dir / "depot_tools"
        
        # Ensure depot_tools exists (needed for gclient)
        if not depot_dir.exists():
            depot_url = "https://chromium.googlesource.com/chromium/tools/depot_tools.git"
            git_clone(depot_url, depot_dir)
        else:
            print(f"  depot_tools already exists: {depot_dir}")
        
        if angle_dir.exists() and not overwrite:
            print("  ANGLE already exists, skipping")
        else:
            # ANGLE needs full clone for gclient sync to work properly
            # depth=0 means full clone
            git_clone_at_commit(ANGLE_REPO_URL, angle_dir, ANGLE_COMMIT, depth=0)
            
            # Setup gclient configuration
            setup_angle_gclient(angle_dir, ANGLE_COMMIT)
            
            # Run gclient sync to get ANGLE dependencies
            print("  Running gclient sync for ANGLE dependencies...")
            env = os.environ.copy()
            env["PATH"] = str(depot_dir) + os.pathsep + env.get("PATH", "")
            env["DEPOT_TOOLS_UPDATE"] = "0"
            env["GCLIENT_PY3"] = "1"
            if platform.system() == "Windows":
                env["DEPOT_TOOLS_WIN_TOOLCHAIN"] = "0"
            
            if platform.system() == "Windows":
                gclient = depot_dir / "gclient.bat"
            else:
                gclient = depot_dir / "gclient"
            
            if gclient.exists():
                try:
                    # Run gclient sync from deps_dir (where .gclient is located)
                    run_cmd([str(gclient), "sync", "--with_branch_heads", "--with_tags"],
                           cwd=str(deps_dir), env=env, check=False)
                except Exception as e:
                    print(f"  Warning: gclient sync error: {e}")
            
            print(f"  [OK] ANGLE (commit {ANGLE_COMMIT[:8]})")
        print()
    else:
        print("[6/7] ANGLE - Skipped (--skip-angle)")
        print()

    # 7. Skia
    if not args.skip_skia:
        print("=" * 50)
        print("[7/7] Skia")
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
            git_clone(skia_url, skia_dir, branch="chrome/m147")
        
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
    print("Dependency Sync Complete")
    print("=" * 50)
    print()
    print("Dependencies downloaded:")
    for name in ["SDL3", "vk-bootstrap", "spdlog", "CLI11", "DirectX-Headers", "angle", "skia", "depot_tools"]:
        if (deps_dir / name).exists():
            print(f"  [OK] {name}")
    print()
    return 0

def main():
    parser = argparse.ArgumentParser(description="Sync dependencies for Skia Renderer")
    
    # Sync options
    parser.add_argument("--skip-skia", action="store_true", help="Skip Skia")
    parser.add_argument("--skip-sdl", action="store_true", help="Skip SDL3")
    parser.add_argument("--skip-vkbootstrap", action="store_true", help="Skip vk-bootstrap")
    parser.add_argument("--skip-spdlog", action="store_true", help="Skip spdlog")
    parser.add_argument("--skip-cli11", action="store_true", help="Skip CLI11")
    parser.add_argument("--skip-angle", action="store_true", help="Skip ANGLE")
    parser.add_argument("--skip-directx-headers", action="store_true", help="Skip DirectX-Headers")
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
