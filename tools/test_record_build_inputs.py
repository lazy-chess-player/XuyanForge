"""验证构建记录的配置白名单与输出边界，所有文件只写入测试临时目录。"""

from pathlib import Path
import tempfile
import unittest

from record_build_inputs import (
    prepare_report_path, read_cache_settings, read_compiler_settings, resolve_build_directory,
)


class BuildInputRecordTests(unittest.TestCase):
    """职责：回归构建输入报告的白名单、路径及目录冲突边界。

    生命周期与资源：unittest 每例创建一个实例；每例仅在 TemporaryDirectory
    内写合成元数据并由上下文退出清理，不访问用户缓存、凭据或真实构建目录。
    线程：运行器调用线程同步执行，不持有跨例成员或后台任务。"""

    def test_cache_exports_only_build_switches(self):
        """功能：写合成 CMakeCache，断言只导出三个构建开关。

        参数：self，为当前用例实例，unittest 在本次调用期间持有。
        返回：无；白名单结果与预期字典完全相等时通过。
        失败：多导出伪凭据/路径或少导出开关触发 AssertionError；文件异常传播。
        副作用：仅在独占临时目录写 UTF-8 缓存，退出清理，不使用真实凭据。
        线程与生命周期：同步执行，缓存路径仅在临时目录上下文内有效。"""
        with tempfile.TemporaryDirectory() as temporary:
            cache = Path(temporary) / "CMakeCache.txt"
            cache.write_text(
                "CMAKE_BUILD_TYPE:STRING=Debug\n"
                "XUYANFORGE_BUILD_UI:BOOL=ON\n"
                "XUYANFORGE_BUILD_TESTS:BOOL=ON\n"
                "PRIVATE_TOKEN:STRING=must-not-be-exported\n"
                "CMAKE_CXX_COMPILER:FILEPATH=private-user-path\n",
                encoding="utf-8",
            )
            self.assertEqual(read_cache_settings(cache), {
                "CMAKE_BUILD_TYPE": "Debug", "XUYANFORGE_BUILD_UI": "ON",
                "XUYANFORGE_BUILD_TESTS": "ON",
            })

    def test_compiler_record_omits_executable_path(self):
        """功能：构造编译器元数据，断言导出厂商和版本并排除可执行路径。

        参数：self，为当前 unittest 用例实例，仅本次调用使用。
        返回：无；输出字典严格匹配两个白名单字段时通过。
        失败：输出缺失或包含路径触发 AssertionError，目录/文件异常传播。
        副作用：仅创建临时 CMakeFiles 元数据文件，不调用编译器或模型。
        线程与生命周期：同步执行，临时目录上下文退出后删除全部合成文件。"""
        with tempfile.TemporaryDirectory() as temporary:
            build = Path(temporary)
            metadata = build / "CMakeFiles" / "3.30.5" / "CMakeCXXCompiler.cmake"
            metadata.parent.mkdir(parents=True)
            metadata.write_text(
                'set(CMAKE_CXX_COMPILER_ID "GNU")\n'
                'set(CMAKE_CXX_COMPILER_VERSION "13.1.0")\n'
                'set(CMAKE_CXX_COMPILER "private-user-path")\n', encoding="utf-8",
            )
            self.assertEqual(read_compiler_settings(build), {
                "CMAKE_CXX_COMPILER_ID": "GNU", "CMAKE_CXX_COMPILER_VERSION": "13.1.0",
            })

    def test_output_requires_a_configured_build_child(self):
        """功能：验证构建目录必须为项目 build 下已配置的子目录。

        参数：self，为当前 unittest 用例实例，仅本次调用使用。
        返回：无；四个未配置/越界路径均抛 ValueError，配置后返回规范绝对路径。
        失败：预期 ValueError 未出现或合法路径不匹配时断言失败；文件异常传播。
        副作用：仅在独占临时项目建立 build/ci-tests 和空 CMakeCache，不配置真实项目。
        线程与生命周期：同步执行；临时项目寿命覆盖全部路径校验，退出由上下文清理。"""
        with tempfile.TemporaryDirectory() as temporary:
            project = Path(temporary)
            build = project / "build" / "ci-tests"
            build.mkdir(parents=True)
            for path in (project, project / "build", project / "outside", build):
                with self.assertRaises(ValueError):
                    resolve_build_directory(path, project)
            (build / "CMakeCache.txt").write_text("", encoding="utf-8")
            self.assertEqual(resolve_build_directory(build, project), build.resolve())

    def test_report_path_rejects_directory_collision(self):
        """功能：验证报告文件目标碰撞目录时明确拒绝且保留该目录。

        参数：self，为当前 unittest 用例实例，仅本次调用使用。
        返回：无；碰撞抛 ValueError 且目标仍为目录时通过。
        失败：预期异常缺失或目录被破坏时断言失败；文件系统异常传播。
        副作用：在独占临时构建目录创建同名目标目录，不写真实报告。
        线程与生命周期：同步执行；上下文退出统一清理测试目录。"""
        with tempfile.TemporaryDirectory() as temporary:
            build = Path(temporary)
            output = prepare_report_path(build)
            output.mkdir()
            with self.assertRaises(ValueError):
                prepare_report_path(build)
            self.assertTrue(output.is_dir())


if __name__ == "__main__":
    unittest.main()
