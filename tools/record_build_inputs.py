"""保存构建输入的白名单记录，不采集环境变量、源码正文或凭据。"""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import platform
import re
import subprocess


PROJECT_ROOT = Path(__file__).resolve().parents[1]
CACHE_KEYS = {"CMAKE_BUILD_TYPE", "XUYANFORGE_BUILD_UI", "XUYANFORGE_BUILD_TESTS"}
COMPILER_KEYS = {"CMAKE_CXX_COMPILER_ID", "CMAKE_CXX_COMPILER_VERSION"}


def read_cache_settings(cache_path: Path) -> dict[str, str]:
    """仅读取 CMake 缓存中的构建开关，不导出路径、令牌或任意自定义变量。"""
    result = {}
    for line in cache_path.read_text(encoding="utf-8", errors="replace").splitlines():
        match = re.fullmatch(r"([^:#=]+):[^=]+=(.*)", line)
        if match and match[1] in CACHE_KEYS:
            result[match[1]] = match[2]
    return result


def read_compiler_settings(build_directory: Path) -> dict[str, str]:
    """从 CMake 生成的编译器记录中读取名称和版本，不执行缓存指定的程序。"""
    result = {}
    for path in sorted((build_directory / "CMakeFiles").glob("*/CMakeCXXCompiler.cmake")):
        for line in path.read_text(encoding="utf-8", errors="replace").splitlines():
            match = re.fullmatch(r'set\((CMAKE_CXX_COMPILER_(?:ID|VERSION)) "([^"]*)"\)', line)
            if match and match[1] in COMPILER_KEYS:
                result[match[1]] = match[2]
    return result


def resolve_build_directory(value: Path, project_root: Path = PROJECT_ROOT) -> Path:
    """限定报告输出到项目 build 子目录，拒绝工作区外路径和 build 根目录。"""
    directory = value.resolve()
    build_root = (project_root / "build").resolve()
    try:
        directory.relative_to(build_root)
    except ValueError as error:
        raise ValueError("构建记录只能写入项目 build 下的独立构建目录") from error
    if directory == build_root:
        raise ValueError("构建记录只能写入项目 build 下的独立构建目录")
    if not (directory / "CMakeCache.txt").is_file():
        raise ValueError("构建目录缺少 CMakeCache.txt，请先完成配置")
    return directory


def git_output(*arguments: str) -> bytes:
    """以固定参数读取当前项目 Git 信息，只返回捕获结果且不打印差异正文。"""
    return subprocess.run(
        ["git", *arguments], cwd=PROJECT_ROOT, check=True,
        stdout=subprocess.PIPE, stderr=subprocess.PIPE,
    ).stdout


def prepare_report_path(directory: Path) -> Path:
    """创建报告子目录并拒绝符号链接或目录碰撞，不覆盖工作区外文件。"""
    report_directory = directory / "reports"
    if report_directory.is_symlink() or (report_directory.exists() and not report_directory.is_dir()):
        raise ValueError("报告目录不是普通目录")
    report_directory.mkdir(exist_ok=True)
    output = report_directory / "build-inputs.json"
    if output.is_symlink() or (output.exists() and not output.is_file()):
        raise ValueError("报告输出不是普通文件")
    return output


def write_record(build_directory: Path, label: str) -> Path:
    """生成构建输入记录；差异只保存摘要，工具和配置仅导出明确白名单。"""
    directory = resolve_build_directory(build_directory)
    difference = git_output("diff", "--binary", "HEAD", "--", ".")
    untracked = git_output("ls-files", "--others", "--exclude-standard", "-z")
    cmake_version = subprocess.run(
        ["cmake", "--version"], check=True, stdout=subprocess.PIPE,
        stderr=subprocess.PIPE, text=True,
    ).stdout.splitlines()[0]
    # 不复制 CMake 缓存或环境变量；未跟踪文件只统计数量，不暴露其文件名或正文。
    record = {
        "记录格式": 1,
        "构建名称": label,
        "输入提交": git_output("rev-parse", "HEAD").decode("ascii").strip(),
        "含已跟踪差异": bool(difference),
        "已跟踪差异摘要": hashlib.sha256(difference).hexdigest(),
        "未跟踪文件数": len([value for value in untracked.split(b"\0") if value]),
        "系统": platform.system(),
        "Python版本": platform.python_version(),
        "CMake版本": cmake_version,
        "编译器": read_compiler_settings(directory),
        "配置": read_cache_settings(directory / "CMakeCache.txt"),
    }
    output = prepare_report_path(directory)
    output.write_text(json.dumps(record, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    return output


def main() -> int:
    """解析构建目录和中文名称，成功写入白名单报告后返回零。"""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-directory", type=Path, required=True)
    parser.add_argument("--label", required=True)
    arguments = parser.parse_args()
    print(write_record(arguments.build_directory, arguments.label))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
