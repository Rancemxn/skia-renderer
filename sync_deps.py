#!/usr/bin/env python3
"""
Skia Renderer - Dependency Sync Helper
Handles downloads with aria2 and extraction with 7z
"""

import os
import sys
import subprocess
import shutil
import argparse
import urllib.parse
from pathlib import Path

# Default versions
SDL3_VERSION = "3.4.2"
VKBOOTSTRAP_VERSION = "1.4.343"
VMA_VERSION = "3.3.0"

def find_tool(name, paths=None):
    """Find a tool in PATH or specified paths"""
    # Check PATH first
    result = shutil.which(name)
    if result:
        return result
    
    # Check common Windows paths
    if paths:
        for path in paths:
            full_path = Path(path) / name
            if full_path.exists():
                return str(full_path)
            # Try with .exe extension on Windows
            if sys.platform == 'win32':
                full_path = Path(path) / f"{name}.exe"
                if full_path.exists():
                    return str(full_path)
    
    # Check 7-Zip common locations
    if name == '7z' or name == '7z.exe':
        common_paths = [
            r"C:\Program Files\7-Zip",
            r"C:\Program Files (x86)\7-Zip",
        ]
        for p in common_paths:
            exe = Path(p) / "7z.exe"
            if exe.exists():
                return str(exe)
    
    return None

def run_command(cmd, cwd=None, check=True):
    """Run a command and return result"""
    print(f"Running: {' '.join(cmd)}")
    result = subprocess.run(
        cmd,
        cwd=cwd,
        capture_output=False,
        text=True
    )
    if check and result.returncode != 0:
        raise subprocess.CalledProcessError(result.returncode, cmd)
    return result

def download_with_aria2(url, output_dir, filename, proxy=None, mirror_url=None):
    """Download a file using aria2c"""
    aria2 = find_tool('aria2c')
    if not aria2:
        raise RuntimeError("aria2c not found. Please install aria2.")
    
    dl_url = mirror_url if mirror_url else url
    
    cmd = [
        aria2,
        '-x', '16',
        '-s', '32',
        '-k', '1M',
        '--check-certificate=false',
        '--file-allocation=none',
        '--allow-overwrite=true',
        '--auto-file-renaming=false',
        '-d', str(output_dir),
        '-o', filename,
        dl_url
    ]
    
    if proxy:
        cmd.extend(['--all-proxy', proxy])
    
    print(f"Downloading: {filename}")
    run_command(cmd)
    
    output_path = Path(output_dir) / filename
    if not output_path.exists():
        raise RuntimeError(f"Failed to download {filename}")
    
    return output_path

def extract_with_7z(archive_path, output_dir, strip_components=0):
    """Extract an archive using 7z"""
    sevenzip = find_tool('7z')
    if not sevenzip:
        raise RuntimeError("7z not found. Please install 7-Zip.")
    
    archive_path = Path(archive_path)
    output_dir = Path(output_dir)
    
    print(f"Extracting: {archive_path.name}")
    
    if strip_components == 0:
        # Direct extraction
        cmd = [sevenzip, 'x', '-y', f'-o{output_dir}', str(archive_path)]
        run_command(cmd)
    else:
        # Extract to temp, then move with strip
        temp_dir = output_dir.parent / f"{output_dir.name}_temp"
        if temp_dir.exists():
            shutil.rmtree(temp_dir)
        temp_dir.mkdir(parents=True)
        
        cmd = [sevenzip, 'x', '-y', f'-o{temp_dir}', str(archive_path)]
        run_command(cmd)
        
        # Navigate through strip_components levels
        src_dir = temp_dir
        for _ in range(strip_components):
            subdirs = list(src_dir.iterdir())
            if len(subdirs) == 1 and subdirs[0].is_dir():
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

def git_clone(url, target_dir, depth=1, branch=None):
    """Clone a git repository"""
    git = find_tool('git')
    if not git:
        raise RuntimeError("git not found.")
    
    target_dir = Path(target_dir)
    if target_dir.exists():
        print(f"Removing existing: {target_dir}")
        shutil.rmtree(target_dir)
    
    cmd = [git, 'clone']
    if depth:
        cmd.extend(['--depth', str(depth)])
    if branch:
        cmd.extend(['--branch', branch])
    cmd.extend([url, str(target_dir)])
    
    print(f"Cloning: {url}")
    run_command(cmd)
    
    return target_dir

def sync_skia_deps(skia_dir, python_exe=None):
    """Sync Skia dependencies"""
    skia_dir = Path(skia_dir)
    if not skia_dir.exists():
        raise RuntimeError(f"Skia directory not found: {skia_dir}")
    
    python = python_exe or sys.executable
    sync_script = skia_dir / 'tools' / 'git-sync-deps'
    
    if not sync_script.exists():
        print("Warning: git-sync-deps not found, skipping")
        return
    
    print("Syncing Skia dependencies...")
    try:
        run_command([python, str(sync_script)], cwd=str(skia_dir))
    except subprocess.CalledProcessError as e:
        print(f"Warning: Skia dependencies sync had errors (may not be critical): {e}")

def main():
    parser = argparse.ArgumentParser(description='Sync dependencies for Skia Renderer')
    parser.add_argument('--deps-dir', default='deps', help='Dependencies directory')
    parser.add_argument('--downloads-dir', default='downloads', help='Downloads cache directory')
    parser.add_argument('--proxy', help='Proxy URL (e.g., http://127.0.0.1:7890)')
    parser.add_argument('--mirror', action='store_true', help='Use Chinese mirrors')
    parser.add_argument('--no-overwrite', action='store_true', help="Don't overwrite existing")
    parser.add_argument('--skip-sdl', action='store_true', help='Skip SDL3')
    parser.add_argument('--skip-vkbootstrap', action='store_true', help='Skip vk-bootstrap')
    parser.add_argument('--skip-vma', action='store_true', help='Skip VMA')
    parser.add_argument('--skip-skia', action='store_true', help='Skip Skia')
    parser.add_argument('--skip-skia-deps', action='store_true', help='Skip Skia dependencies')
    parser.add_argument('--python', help='Python executable path')
    
    args = parser.parse_args()
    
    # Setup paths
    deps_dir = Path(args.deps_dir).resolve()
    downloads_dir = Path(args.downloads_dir).resolve()
    deps_dir.mkdir(parents=True, exist_ok=True)
    downloads_dir.mkdir(parents=True, exist_ok=True)
    
    overwrite = not args.no_overwrite
    python_exe = args.python or sys.executable
    
    # Mirror URL prefix
    mirror_prefix = "https://ghp.ci/" if args.mirror else ""
    
    print("=" * 50)
    print("Skia Renderer - Dependency Sync (Python)")
    print("=" * 50)
    print()
    
    # 1. SDL3
    if not args.skip_sdl:
        print("[1/4] SDL3")
        sdl_dir = deps_dir / 'SDL3'
        
        if sdl_dir.exists() and not overwrite:
            print("  SDL3 already exists, skipping")
        else:
            if sdl_dir.exists():
                shutil.rmtree(sdl_dir)
            
            url = f"https://github.com/libsdl-org/SDL/releases/download/release-{SDL3_VERSION}/SDL3-devel-{SDL3_VERSION}-VC.zip"
            mirror = f"{mirror_prefix}{url}" if mirror_prefix else None
            
            archive = download_with_aria2(url, downloads_dir, f"SDL3-{SDL3_VERSION}.zip", 
                                         proxy=args.proxy, mirror_url=mirror)
            extract_with_7z(archive, sdl_dir, strip_components=1)
            print("  [OK] SDL3")
        print()
    
    # 2. vk-bootstrap
    if not args.skip_vkbootstrap:
        print("[2/4] vk-bootstrap")
        vkb_dir = deps_dir / 'vk-bootstrap'
        
        if vkb_dir.exists() and not overwrite:
            print("  vk-bootstrap already exists, skipping")
        else:
            if vkb_dir.exists():
                shutil.rmtree(vkb_dir)
            
            url = f"https://github.com/charles-lunarg/vk-bootstrap/archive/refs/tags/v{VKBOOTSTRAP_VERSION}.zip"
            mirror = f"{mirror_prefix}{url}" if mirror_prefix else None
            
            archive = download_with_aria2(url, downloads_dir, f"vk-bootstrap-{VKBOOTSTRAP_VERSION}.zip",
                                         proxy=args.proxy, mirror_url=mirror)
            extract_with_7z(archive, vkb_dir, strip_components=1)
            print("  [OK] vk-bootstrap")
        print()
    
    # 3. VMA
    if not args.skip_vma:
        print("[3/4] VulkanMemoryAllocator")
        vma_dir = deps_dir / 'VulkanMemoryAllocator'
        
        if vma_dir.exists() and not overwrite:
            print("  VMA already exists, skipping")
        else:
            if vma_dir.exists():
                shutil.rmtree(vma_dir)
            
            url = f"https://github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator/archive/refs/tags/v{VMA_VERSION}.zip"
            mirror = f"{mirror_prefix}{url}" if mirror_prefix else None
            
            archive = download_with_aria2(url, downloads_dir, f"VMA-{VMA_VERSION}.zip",
                                         proxy=args.proxy, mirror_url=mirror)
            extract_with_7z(archive, vma_dir, strip_components=1)
            print("  [OK] VulkanMemoryAllocator")
        print()
    
    # 4. Skia
    if not args.skip_skia:
        print("[4/4] Skia")
        skia_dir = deps_dir / 'skia'
        depot_dir = deps_dir / 'depot_tools'
        
        # Clone depot_tools
        if not depot_dir.exists():
            depot_url = "https://chromium.googlesource.com/chromium/tools/depot_tools.git"
            if mirror_prefix:
                depot_url = f"{mirror_prefix}{depot_url}"
            git_clone(depot_url, depot_dir)
        
        # Clone Skia
        if skia_dir.exists() and not overwrite:
            print("  Skia already exists, skipping clone")
        else:
            if skia_dir.exists():
                shutil.rmtree(skia_dir)
            
            skia_url = "https://skia.googlesource.com/skia.git"
            if mirror_prefix:
                skia_url = f"{mirror_prefix}{skia_url}"
            git_clone(skia_url, skia_dir)
        
        # Sync dependencies
        if not args.skip_skia_deps:
            try:
                sync_skia_deps(skia_dir, python_exe)
            except Exception as e:
                print(f"  Warning: Skia deps sync error: {e}")
        
        print("  [OK] Skia")
        print()
    
    print("=" * 50)
    print("Dependency Sync Complete!")
    print("=" * 50)

if __name__ == '__main__':
    main()
