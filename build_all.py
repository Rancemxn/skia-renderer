#!/usr/bin/env python3
"""
Skia Renderer - Complete Build (One-Click)
Runs sync -> build deps -> build project
"""

import os
import sys
import subprocess
import argparse
from pathlib import Path

def run_script(script: str, args: list) -> int:
    """Run a Python script"""
    script_path = Path(__file__).parent / script
    cmd = [sys.executable, str(script_path)] + args
    print(f"\nRunning: {script} {' '.join(args)}")
    print("=" * 50)
    result = subprocess.run(cmd)
    return result.returncode

def main():
    parser = argparse.ArgumentParser(description="Complete build for Skia Renderer")
    
    # Build options
    parser.add_argument("--build-type", default="Release", choices=["Release", "Debug"],
                       help="Build type (default: Release)")
    parser.add_argument("--clean", action="store_true", help="Clean before building")
    
    # Compiler options
    parser.add_argument("--llvm", action="store_true", help="Use LLVM/Clang + Ninja")
    parser.add_argument("--vs", action="store_true", help="Use Visual Studio")
    
    # Download options
    parser.add_argument("--mirror", action="store_true", help="Use Chinese mirrors")
    parser.add_argument("--proxy", help="Proxy URL for downloads")
    
    # Skip options
    parser.add_argument("--skip-sync", action="store_true", help="Skip dependency sync")
    parser.add_argument("--skip-deps", action="store_true", help="Skip dependency build")
    parser.add_argument("--skip-skia-deps", action="store_true", help="Skip Skia deps sync")
    
    # Skia options
    parser.add_argument("--skia-tools", action="store_true", help="Build Skia tools")
    
    args = parser.parse_args()
    
    print("=" * 50)
    print("Skia Renderer - Complete Build")
    print("=" * 50)
    print()
    print("Options:")
    print(f"  Build Type: {args.build_type}")
    print(f"  Use LLVM: {args.llvm or not args.vs}")
    print(f"  Clean: {args.clean}")
    print(f"  Use Mirror: {args.mirror}")
    print()
    
    # Step 1: Sync dependencies
    if not args.skip_sync:
        print("\n" + "=" * 50)
        print("[Step 1/3] Syncing dependencies...")
        print("=" * 50)
        
        sync_args = []
        if args.mirror:
            sync_args.append("--mirror")
        if args.proxy:
            sync_args.extend(["--proxy", args.proxy])
        if args.skip_skia_deps:
            sync_args.append("--skip-skia-deps")
        
        if run_script("sync_deps.py", sync_args) != 0:
            print("\nERROR: Dependency sync failed")
            return 1
    else:
        print("[SKIP] Step 1: Sync dependencies")
    
    # Step 2: Build dependencies
    if not args.skip_deps:
        print("\n" + "=" * 50)
        print("[Step 2/3] Building dependencies...")
        print("=" * 50)
        
        deps_args = ["--build-type", args.build_type]
        if args.llvm:
            deps_args.append("--llvm")
        if args.vs:
            deps_args.append("--vs")
        if args.clean:
            deps_args.append("--clean")
        if args.skia_tools:
            deps_args.append("--skia-tools")
        
        if run_script("build_deps.py", deps_args) != 0:
            print("\nERROR: Dependency build failed")
            return 1
    else:
        print("[SKIP] Step 2: Build dependencies")
    
    # Step 3: Build main project
    print("\n" + "=" * 50)
    print("[Step 3/3] Building main project...")
    print("=" * 50)
    
    build_args = ["--build-type", args.build_type]
    if args.llvm:
        build_args.append("--llvm")
    if args.vs:
        build_args.append("--vs")
    if args.clean:
        build_args.append("--clean")
    
    if run_script("build_windows.py", build_args) != 0:
        print("\nERROR: Main project build failed")
        return 1
    
    # Done
    print("\n" + "=" * 50)
    print("Build Complete!")
    print("=" * 50)
    print()
    
    build_dir = Path(__file__).parent / "build"
    if args.llvm or not args.vs:
        exe = build_dir / "skia-renderer.exe"
    else:
        exe = build_dir / args.build_type / "skia-renderer.exe"
    
    print(f"Run: {exe}")
    print()
    
    return 0

if __name__ == "__main__":
    sys.exit(main())
