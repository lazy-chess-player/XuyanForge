"""保存构建输入的白名单记录，不采集环境变量、源码正文或凭据。"""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import platform
import re
import subprocess


# 项目根目录只由脚本位置确定，不读取用户设置或工作区内容。
PROJECT_ROOT = Path(__file__).resolve().parents[1]
# 可公开的缓存开关白名单；凭据、路径和自定义变量不进入报告。
CACHE_KEYS = {"CMAKE_BUILD_TYPE", "XUYANFORGE_BUILD_UI", "XUYANFORGE_BUILD_TESTS"}
# 可公开的编译器信息白名单，明确排除可执行文件路径。
COMPILER_KEYS = {"CMAKE_CXX_COMPILER_ID", "CMAKE_CXX_COMPILER_VERSION"}


def read_cache_settings(cache_path: Path) -> dict[str, str]:
    """功能：从CMake缓存提取明确允许公开的构建开关。
    参数：cache_path为已生成的缓存文件路径，仅读取，不执行其中内容。
    返回：缓存键到字符串值的字典；未出现的白名单键不补写。
    失败：文件读取错误向调用者传播。副作用：无，不导出其他变量。
    """
    result = {}
    for line in cache_path.read_text(encoding="utf-8", errors="replace").splitlines():
        match = re.fullmatch(r"([^:#=]+):[^=]+=(.*)", line)
        if match and match[1] in CACHE_KEYS:
            result[match[1]] = match[2]
    return result


def read_compiler_settings(build_directory: Path) -> dict[str, str]:
    """功能：从CMake编译器记录读取名称和版本。
    参数：build_directory为已经配置的构建目录，扫描其中版本化的编译器记录。
    返回：允许的编译器字段字典，没有记录时为空。
    失败：文件读取错误传播。副作用：只读，不执行记录中的程序或导出路径。
    """
    result = {}
    for path in sorted((build_directory / "CMakeFiles").glob("*/CMakeCXXCompiler.cmake")):
        for line in path.read_text(encoding="utf-8", errors="replace").splitlines():
            match = re.fullmatch(r'set\((CMAKE_CXX_COMPILER_(?:ID|VERSION)) "([^"]*)"\)', line)
            if match and match[1] in COMPILER_KEYS:
                result[match[1]] = match[2]
    return result


def resolve_build_directory(value: Path, project_root: Path = PROJECT_ROOT) -> Path:
    """功能：核对报告目标为已配置的独立构建子目录。
    参数：value为待验证目录；project_root为项目根，默认当前仓库，可在测试中替换。
    返回：规范化后的目录路径。
    失败：目标不在build下、等于build根或缺少缓存时抛ValueError。
    副作用：无，不创建目录或修改缓存。
    """
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
    """功能：以独立参数执行只读Git查询，捕获结果。
    参数：arguments为内部调用方提供的Git参数序列，不经shell拼接。
    返回：标准输出字节串，保留零字节分隔信息。
    失败：进程失败抛CalledProcessError。副作用：不打印差异、凭据或正文。
    """
    return subprocess.run(
        ["git", *arguments], cwd=PROJECT_ROOT, check=True,
        stdout=subprocess.PIPE, stderr=subprocess.PIPE,
    ).stdout


def prepare_report_path(directory: Path) -> Path:
    """功能：准备构建目录内的固定报告位置。
    参数：directory为调用方已经验证的构建子目录。
    返回：reports/build-inputs.json路径。
    失败：报告目录/目标为符号链接或错误类型时抛ValueError；文件系统错误传播。
    副作用：创建reports目录，已有普通报告可由后续写入覆盖；不删除碰撞目录。
    """
    report_directory = directory / "reports"
    if report_directory.is_symlink() or (report_directory.exists() and not report_directory.is_dir()):
        raise ValueError("报告目录不是普通目录")
    report_directory.mkdir(exist_ok=True)
    output = report_directory / "build-inputs.json"
    if output.is_symlink() or (output.exists() and not output.is_file()):
        raise ValueError("报告输出不是普通文件")
    return output


def write_record(build_directory: Path, label: str) -> Path:
    """功能：保存当前提交和构建工具的白名单记录，便于核对验证所用输入。
    参数：build_directory为build下已配置子目录；label为调用方提供的中文构建名称。
    返回：实际写入的报告路径。
    失败：路径校验、只读Git命令或文件写入失败传播，不能报告成功。
    副作用：只写固定报告；差异仅存摘要、未跟踪文件仅存数量，不采集正文、环境变量或凭据。
    """
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
    """功能：解析命令行构建目录和中文名称，写入白名单报告。
    参数：无，命令行由argparse读取。
    返回：成功为0；参数或写入错误终止进程并保留失败状态。
    副作用：生成报告并打印其路径，不显示缓存正文。
    """
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-directory", type=Path, required=True)
    parser.add_argument("--label", required=True)
    arguments = parser.parse_args()
    print(write_record(arguments.build_directory, arguments.label))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
