"""验证静态门禁的适用边界；所有合成文件只位于测试临时目录。"""

from pathlib import Path
import tempfile
import unittest

from check_development_standards import has_uninstall_section, inspect_text, source_paths


class DevelopmentStandardsTests(unittest.TestCase):
    """职责：检查标签识别、正式/测试依赖隔离和目录白名单，不操作真实用户资料。
    生命周期：由unittest创建；每个测试自有临时目录，结束时清理。
    """

    def test_production_rejects_test_dependency(self):
        """功能：正式源码不能引用测试引擎，而测试源码允许显式组装替身。
        参数：self为测试框架持有的用例。返回：无。
        失败：隔离判定不符时断言失败。副作用：仅使用内存路径，不写项目文件。
        """
        root = Path("isolated-project")
        text = '#include "xuyan/engine/mock_provider.h"'
        self.assertEqual(len(inspect_text(root / "libs" / "module.cpp", text, root)), 1)
        self.assertEqual(inspect_text(root / "tests" / "module.cpp", text, root), [])

    def test_old_tag_is_rejected(self):
        """功能：确认禁止标签产生诊断，普通中文块注释不产生误报。
        参数：self为测试框架持有的用例。返回：无。
        失败：识别结果不符时断言失败。副作用：仅构造内存字符串。
        """
        root = Path("isolated-project")
        path = root / "libs" / "module.h"
        old = "/** " + "@" + "brief 旧格式 */"
        self.assertEqual(len(inspect_text(path, old, root)), 1)
        self.assertEqual(inspect_text(path, "/* 功能：查询。参数：无。返回：记录。 */", root), [])

    def test_only_source_directories_are_scanned(self):
        """功能：确认扫描不读取构建产物、用户资料及非源码后缀。
        参数：self为框架持有的用例。返回：无。
        失败：收集范围不符时断言失败。副作用：只在独占临时目录写入合成文件，结束清理。
        """
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            for name in ("libs/module.cpp", "build/generated.cpp", "data/private.cpp", "libs/data.sqlite"):
                path = root / name
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_text("", encoding="utf-8")
            self.assertEqual(source_paths(root), [root / "libs" / "module.cpp"])

    def test_uninstaller_requires_reserved_internal_section(self):
        """功能：确认中文可见卸载提示不会替代 NSIS 必需的内部卸载段名。
        参数：self由测试框架持有；无外部输入。返回：无，断言通过即成功。
        失败：识别无效中文段或漏掉合法段时断言失败。
        副作用：只构造内存脚本片段，不运行安装器或访问真实文件。
        """
        self.assertFalse(has_uninstall_section('Section "卸载"\nSectionEnd'))
        self.assertTrue(has_uninstall_section('Section "Uninstall"\nSectionEnd'))
        self.assertTrue(has_uninstall_section('Section "un.卸载"\nSectionEnd'))


if __name__ == "__main__":
    unittest.main()
