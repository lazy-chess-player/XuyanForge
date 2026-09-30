"""执行可自动判定的开发规范门禁；注释语义、中文体验和运行时数据隔离仍须人工及动态验收。"""

from __future__ import annotations

from pathlib import Path
import re
import sys

# 检查入口位于仓库内；只读取明确的源码目录，不扫描构建产物或用户工作区。
PROJECT_ROOT = Path(__file__).resolve().parents[1]
# 允许的源码种类，不把图片、数据库、小说和依赖文件作为扫描输入。
SOURCE_SUFFIXES = {".cpp", ".h", ".qml", ".py", ".ps1"}
# 禁止的旧注释标签只作为正则模式保存，不把历史业务资料写进检查工具。
COMMENT_TAG = re.compile(r"@(brief|param|returns?|throws?|note|details|retval)\b")
# 正式目标不得引用这些测试专属接口或目录；检查不依赖某个样例的内容名称。
TEST_DEPENDENCY = re.compile(r"(?:tests[/\\]|synthetic_fixture|xuyan/engine/|xuyan_test_engine)")


def source_paths(project_root: Path) -> list[Path]:
    """功能：按白名单收集受规范约束的源码文件，排序以保持诊断可重复。
    参数：project_root为借用的仓库根路径；不存在的源码目录忽略。
    返回：libs、ui、apps、tests、tools内符合后缀的文件列表。
    失败：目录读取权限等系统错误向调用者传播。
    副作用：只读目录，不扫描build或系统应用数据，不修改文件。
    """
    return sorted(path for name in ("libs", "ui", "apps", "tests", "tools")
                  for path in (project_root / name).rglob("*")
                  if path.is_file() and path.suffix in SOURCE_SUFFIXES)


def inspect_text(path: Path, text: str, project_root: Path) -> list[str]:
    """功能：定位旧注释标签及正式C++/QML对测试接口的意外依赖。
    参数：path为当前源码路径；text为已解码UTF-8正文；project_root为仓库根，仅借用。
    返回：包含相对路径、行号和中文原因的诊断列表；空列表仅代表这两项检查通过。
    失败：path不在根目录中时relative_to抛错误，不接受越界检查目标。
    副作用：纯文本检查，不推断全部注释已经详细或产品没有运行时污染。
    """
    relative = path.relative_to(project_root)
    production = relative.parts[0] in {"libs", "ui", "apps"}
    findings = []
    for number, line in enumerate(text.splitlines(), 1):
        if COMMENT_TAG.search(line):
            findings.append(f"{relative.as_posix()}:{number}：仍有禁止的旧注释标签")
        if production and path.suffix in {".cpp", ".h", ".qml"} and TEST_DEPENDENCY.search(line):
            findings.append(f"{relative.as_posix()}:{number}：正式源码引用了测试专属接口")
    return findings


def inspect_build_boundaries(project_root: Path) -> list[str]:
    """功能：检查核心、界面和测试的CMake目标隔离及单一版本配置入口。
    参数：project_root为仓库根路径，检查三个明确CMake文件、version.json及打包脚本的卸载段。
    返回：中文违规说明列表；缺失文件以错误传播，不伪造合规结果。
    失败：读文件、文本解码或权限错误由主入口处理。
    副作用：只读；不会配置工程或编译安装器，也不能代替关闭测试的生产构建验证。
    """
    findings = []
    for name in ("CMakeLists.txt", "libs/CMakeLists.txt", "ui/CMakeLists.txt"):
        text = (project_root / name).read_text(encoding="utf-8")
        if TEST_DEPENDENCY.search(text):
            findings.append(f"{name}：正式构建定义混入了测试专属目标或源码")
    root = (project_root / "CMakeLists.txt").read_text(encoding="utf-8")
    if "version.json" not in root or not (project_root / "version.json").is_file():
        findings.append("根构建缺少统一版本配置入口")
    installer = (project_root / "tools" / "package-windows.ps1").read_text(encoding="utf-8")
    if not has_uninstall_section(installer):
        findings.append("安装器缺少NSIS识别的卸载段内部名称")
    return findings


def has_uninstall_section(script: str) -> bool:
    """功能：核对脚本包含 NSIS 认可的卸载段内部名称，避免中文显示词条被误作段标识。
    参数：script为借用的打包脚本文本，允许空串；仅作静态词法检查。
    返回：存在 Uninstall 或 un. 前缀段时为真，否则为假。
    失败：不解析完整 NSIS 语法，编译能力仍须由安装器验证；正则本身无主动错误。
    副作用：仅读取传入字符串，不访问文件、运行安装器或修改系统。
    """
    return re.search(r'(?m)^Section\s+"(?:Uninstall|un\.[^"]+)"', script) is not None


def main() -> int:
    """功能：运行有限范围的静态规范检查，向终端说明发现及适用边界。
    参数：无；使用脚本所在仓库，不接受任意目录或用户资料作为输入。
    返回：检查通过为0，违规或读取失败为1。
    失败：读取异常显示中文通用说明，不输出异常中的私有路径或文件内容。
    副作用：只打印检查结果，不写文件、不访问网络、不调用付费模型。
    """
    try:
        paths = source_paths(PROJECT_ROOT)
        findings = inspect_build_boundaries(PROJECT_ROOT)
        for path in paths:
            findings.extend(inspect_text(path, path.read_text(encoding="utf-8"), PROJECT_ROOT))
        if findings:
            print("开发规范静态检查未通过：")
            print("\n".join(findings))
            return 1
        print(f"开发规范静态检查通过：{len(paths)}个源码文件；旧注释标签、测试依赖与版本入口检查通过。")
        print("本检查不证明注释语义完整、界面全部中文或运行时数据无污染，须结合审查及动态验证。")
        return 0
    except (OSError, UnicodeError, ValueError):
        print("开发规范检查无法读取预期源码，请检查仓库文件与权限。", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
