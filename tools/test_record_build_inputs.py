"""验证构建记录的配置白名单与输出边界，所有文件只写入测试临时目录。"""

from pathlib import Path
import tempfile
import unittest

from record_build_inputs import (
    prepare_report_path, read_cache_settings, read_compiler_settings, resolve_build_directory,
)


class BuildInputRecordTests(unittest.TestCase):
    """保证自动化报告不会把自定义缓存变量和工作区外文件写入公开产物。"""

    def test_cache_exports_only_build_switches(self):
        """配置记录保留构建开关，排除伪凭据和开发者路径。"""
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
        """编译器记录只保留名称和版本，不暴露缓存里的执行路径。"""
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
        """未配置、工作区外和 build 根目录均被拒绝，合法子目录保持不变。"""
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
        """报告目标被同名目录占用时返回错误，不删除或改写该目录。"""
        with tempfile.TemporaryDirectory() as temporary:
            build = Path(temporary)
            output = prepare_report_path(build)
            output.mkdir()
            with self.assertRaises(ValueError):
                prepare_report_path(build)
            self.assertTrue(output.is_dir())


if __name__ == "__main__":
    unittest.main()
