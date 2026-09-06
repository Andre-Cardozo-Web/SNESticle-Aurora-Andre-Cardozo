#!/usr/bin/env python3
from pathlib import Path
import argparse
import shutil

FILES = [
    "gb.c", "sgb.c", "apu.c", "memory.c", "mbc.c", "timing.c",
    "display.c", "camera.c", "sm83_cpu.c", "joypad.c",
    "save_state.c", "random.c", "rumble.c",
]

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--source", required=True)
    ap.add_argument("--stage", required=True)
    a = ap.parse_args()

    src = Path(a.source).resolve()
    dst = Path(a.stage).resolve()
    core = src / "Core"

    if not (src / "LICENSE").is_file():
        raise SystemExit("ERRO: SameBoy LICENSE ausente")
    for name in FILES:
        if not (core / name).is_file():
            raise SystemExit("ERRO: SameBoy Core ausente: " + name)

    if dst.exists():
        shutil.rmtree(dst)
    (dst / "Core").mkdir(parents=True)

    # SameBoy headers include one another relatively. Copy the complete Core
    # directory, but the PS2 Makefile compiles only the lean FILES list.
    for p in core.iterdir():
        if p.is_file():
            shutil.copy2(p, dst / "Core" / p.name)

    shutil.copy2(src / "LICENSE", dst / "LICENSE")
    (dst / ".aurora-sameboy-stage-v1").write_text(
        "SameBoy staged for Aurora SGB\n", encoding="utf-8"
    )
    print("OK: SameBoy Core staged:", dst)

if __name__ == "__main__":
    main()
