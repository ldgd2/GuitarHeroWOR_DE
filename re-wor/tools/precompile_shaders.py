#!/usr/bin/env python3
import os
import sys
import subprocess
from pathlib import Path
from concurrent.futures import ProcessPoolExecutor, as_completed

FXC_PATH = r"C:\Program Files (x86)\Windows Kits\10\bin\10.0.26100.0\x64\fxc.exe"
SHADERS_DIR = Path(r"c:\Users\ldgd2\Documents\proyecto_lider\GuitarHeroWOR_DE\re-wor\cache\native_shaders")

def compile_shader(hlsl_file):
    name = hlsl_file.name
    if name.endswith(".vs.hlsl"):
        stage = "vs_5_0"
        cso_file = hlsl_file.with_name(name[:-7] + "vs.cso")
    elif name.endswith(".ps.hlsl"):
        stage = "ps_5_0"
        cso_file = hlsl_file.with_name(name[:-7] + "ps.cso")
    else:
        return name, "skipped"

    if cso_file.exists() and cso_file.stat().st_size > 0:
        return name, "cached"

    # Try optimization level 1 first
    cmd1 = [
        FXC_PATH,
        "/T", stage,
        "/E", "main",
        "/O1",
        "/nologo",
        "/I", str(SHADERS_DIR),
        "/Fo", str(cso_file),
        str(hlsl_file)
    ]
    res1 = subprocess.run(cmd1, capture_output=True, text=True)
    if res1.returncode == 0:
        return name, "ok (O1)"

    # Fallback to /Od (no optimizations) if fxc internal compiler error
    cmd2 = [
        FXC_PATH,
        "/T", stage,
        "/E", "main",
        "/Od",
        "/nologo",
        "/I", str(SHADERS_DIR),
        "/Fo", str(cso_file),
        str(hlsl_file)
    ]
    res2 = subprocess.run(cmd2, capture_output=True, text=True)
    if res2.returncode == 0:
        return name, "ok (Od)"

    return name, f"failed: {res2.stderr.strip() or res1.stderr.strip()}"

def main():
    if not os.path.exists(FXC_PATH):
        print(f"Error: FXC not found at {FXC_PATH}")
        sys.exit(1)

    hlsl_files = list(SHADERS_DIR.glob("*.hlsl"))
    hlsl_files = [f for f in hlsl_files if f.name.endswith(".vs.hlsl") or f.name.endswith(".ps.hlsl")]
    total = len(hlsl_files)
    print(f"Precompiling {total} HLSL shaders using 16 workers...")

    success = 0
    cached = 0
    failed = 0

    with ProcessPoolExecutor(max_workers=16) as executor:
        futures = {executor.submit(compile_shader, f): f for f in hlsl_files}
        for future in as_completed(futures):
            name, status = future.result()
            if status == "cached":
                cached += 1
            elif status.startswith("ok"):
                success += 1
            else:
                failed += 1
                print(f"[{name}] {status}")

    print(f"\nDone: {success} newly compiled, {cached} already cached, {failed} failed out of {total} total.")

if __name__ == "__main__":
    main()
