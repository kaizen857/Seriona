#!/usr/bin/env python3
"""把非 Qt 的第三方 dylib 收拢进 macOS .app bundle。

macdeployqt 只处理 Qt 自己的框架与 QML 插件，后端链接的 FFmpeg / spdlog / fmt /
xxhash 等 Homebrew 动态库会以 /opt/homebrew/... 绝对路径留在可执行文件里 ——
在构建机上能跑，分发到没装 Homebrew（或装在别的前缀）的机器上必然启动失败。

本脚本递归遍历 bundle 内所有 Mach-O 的依赖，把构建机路径下的 dylib 拷进
Contents/Frameworks，并把引用改写为 @rpath 形式。已在 Frameworks 里的 Qt 框架
（macdeployqt 处理过的）与系统库（/usr/lib、/System）原样跳过。

用法：
    python3 scripts/bundle-macos-dylibs.py path/to/seriona.app
"""

from __future__ import annotations

import argparse
import os
import shutil
import stat
import subprocess
import sys
from pathlib import Path

# 系统库由 dyld 共享缓存提供，不能也不需要拷贝
SYSTEM_PREFIXES = ("/usr/lib/", "/System/")


def run(cmd: list[str]) -> str:
    result = subprocess.run(cmd, check=True, capture_output=True, text=True)
    return result.stdout


def is_macho(path: Path) -> bool:
    if not path.is_file() or path.is_symlink():
        return False
    try:
        with path.open("rb") as handle:
            magic = handle.read(4)
    except OSError:
        return False
    # 32/64 位与两种字节序，外加 universal binary 的 fat magic
    return magic in {
        b"\xcf\xfa\xed\xfe",
        b"\xce\xfa\xed\xfe",
        b"\xfe\xed\xfa\xcf",
        b"\xfe\xed\xfa\xce",
        b"\xca\xfe\xba\xbe",
        b"\xbe\xba\xfe\xca",
    }


def install_name(binary: Path) -> str:
    # otool -D 输出：第一行是文件名，第二行才是 install name（可执行文件无此行）。
    lines = run(["otool", "-D", str(binary)]).splitlines()[1:]
    return lines[0].strip() if lines else ""


def dependencies(binary: Path) -> list[str]:
    # otool -L 对 dylib/framework 会把它自己的 install name 也列在第一条，
    # 那不是依赖；误当依赖会把 bundle 里每个 framework 都报成构建机路径泄漏。
    own = install_name(binary)
    lines = run(["otool", "-L", str(binary)]).splitlines()[1:]
    refs = [line.split(" (", 1)[0].strip() for line in lines if line.strip()]
    return [ref for ref in refs if ref != own]


def needs_bundling(ref: str) -> bool:
    if not ref.startswith("/"):
        # @rpath / @executable_path / @loader_path：macdeployqt 或本脚本已处理过
        return False
    if ref.startswith(SYSTEM_PREFIXES):
        return False
    if ".framework/" in ref:
        # framework 有固定的 Versions/A 目录布局，拍平成单个 dylib 会让 Qt 插件加载
        # 失败；这类依赖（几乎全是 Qt 自己）统一交给 macdeployqt，这里只告警。
        return False
    return True


def make_writable(path: Path) -> None:
    path.chmod(path.stat().st_mode | stat.S_IWUSR)


def bundle(app: Path, strict: bool) -> int:
    macos_dir = app / "Contents" / "MacOS"
    frameworks = app / "Contents" / "Frameworks"
    frameworks.mkdir(parents=True, exist_ok=True)

    executables = [p for p in macos_dir.iterdir() if is_macho(p)]
    if not executables:
        print(f"error: {macos_dir} 下没有可执行 Mach-O", file=sys.stderr)
        return 1

    # 广度优先：新拷进来的 dylib 自身的依赖也要继续处理（FFmpeg 会拖出 x264/opus 等一长串）
    pending = list(executables)
    pending += [p for p in frameworks.rglob("*") if is_macho(p)]
    seen: set[Path] = set()
    copied: list[str] = []

    while pending:
        binary = pending.pop()
        resolved = binary.resolve()
        if resolved in seen:
            continue
        seen.add(resolved)

        for ref in dependencies(binary):
            if not needs_bundling(ref):
                continue

            source = Path(ref)
            if not source.exists():
                print(f"warning: 依赖不存在，跳过：{ref}", file=sys.stderr)
                continue

            target = frameworks / source.name
            if not target.exists():
                shutil.copy2(source, target)
                make_writable(target)
                # 拷贝进来的库自身 install_name 仍是绝对路径，一并改掉
                run(["install_name_tool", "-id", f"@rpath/{source.name}", str(target)])
                copied.append(source.name)
                pending.append(target)

            make_writable(binary)
            run(["install_name_tool", "-change", ref, f"@rpath/{source.name}", str(binary)])

    # 可执行文件与 Frameworks 内的库都要能找到 Frameworks 目录
    for exe in executables:
        add_rpath(exe, "@executable_path/../Frameworks")
    for lib in frameworks.rglob("*"):
        if is_macho(lib):
            add_rpath(lib, "@loader_path")

    print(f"已收拢 {len(copied)} 个第三方 dylib 进 {frameworks}")
    for name in sorted(copied):
        print(f"  {name}")

    stale = leftover_framework_refs(seen)
    if stale:
        level = "error" if strict else "warning"
        print(f"{level}: 以下构建机 framework 引用未被 macdeployqt 改写，"
              "分发后会启动失败：", file=sys.stderr)
        for ref in sorted(stale):
            print(f"  {ref}", file=sys.stderr)
        if strict:
            # Homebrew 的 Qt 把 qtbase/qtdeclarative 拆在不同前缀，macdeployqt 在本地
            # 开发机上经常改写不完全；官方 Qt（CI 用）单一前缀不有这个问题。
            # 所以只在 --strict（CI）下当错误，本地仅提示。
            return 1
    return 0


def leftover_framework_refs(binaries: set[Path]) -> set[str]:
    stale: set[str] = set()
    for binary in binaries:
        if not binary.exists():
            continue
        for ref in dependencies(binary):
            if ref.startswith("/") and not ref.startswith(SYSTEM_PREFIXES) and ".framework/" in ref:
                stale.add(ref)
    return stale


def add_rpath(binary: Path, rpath: str) -> None:
    # 重复 -add_rpath 会报错，先查已有的
    existing = run(["otool", "-l", str(binary)])
    if f"path {rpath} " in existing:
        return
    make_writable(binary)
    subprocess.run(
        ["install_name_tool", "-add_rpath", rpath, str(binary)],
        check=False,
        capture_output=True,
    )


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("app", type=Path, help="待处理的 .app bundle 路径")
    parser.add_argument(
        "--strict",
        action="store_true",
        help="残留构建机 framework 引用时以非零退出（CI 用）",
    )
    args = parser.parse_args()

    app = args.app
    if not (app / "Contents" / "MacOS").is_dir():
        print(f"error: 不是有效的 .app bundle：{app}", file=sys.stderr)
        return 1

    os.environ.setdefault("LC_ALL", "C")
    return bundle(app, strict=args.strict)


if __name__ == "__main__":
    raise SystemExit(main())
