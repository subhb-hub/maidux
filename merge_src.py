#!/usr/bin/env python3
# -*- coding: utf-8 -*-

import sys
from pathlib import Path
from datetime import datetime

def main() -> int:
    out_name = sys.argv[1] if len(sys.argv) >= 2 else "merged.c"
    out_path = Path(out_name)

    # 仅当前目录，不递归
    here = Path(".")
    h_files = sorted([p for p in here.glob("*.h") if p.is_file()])
    c_files = sorted([p for p in here.glob("*.c") if p.is_file()])

    files = h_files + c_files
    if not files:
        print("No .h/.c files found in current directory.", file=sys.stderr)
        return 1

    # 写入输出
    with out_path.open("w", encoding="utf-8", newline="\n") as out:
        out.write("/*\n")
        out.write(" * Auto-merged source file\n")
        out.write(f" * Generated at: {datetime.now().isoformat(sep=' ', timespec='seconds')}\n")
        out.write(f" * Working dir:  {here.resolve()}\n")
        out.write(" */\n\n")

        for p in files:
            out.write("\n")
            out.write(f"/* ============================ FILE: {p.name} ============================ */\n\n")

            # 以二进制读取再尽量解码，避免遇到非 UTF-8 直接炸
            raw = p.read_bytes()
            try:
                text = raw.decode("utf-8")
            except UnicodeDecodeError:
                text = raw.decode("utf-8", errors="replace")

            # 统一换行
            text = text.replace("\r\n", "\n").replace("\r", "\n")

            out.write(text)
            if not text.endswith("\n"):
                out.write("\n")

            out.write(f"\n/* ========================= END FILE: {p.name} ============================ */\n")

    print(f"Merged {len(files)} files into: {out_path}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
