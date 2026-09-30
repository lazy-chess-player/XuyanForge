#!/usr/bin/env python3
"""检查仓库协议及独立测试素材的轻量契约工具。

只实现本文件明确支持的JSON Schema关键字，不提供完整标准校验能力。
生产包导入使用独立的运行时校验，不依赖此测试工具，也不打包测试素材。
"""

from __future__ import annotations

import json
import sys
from pathlib import Path
from typing import Any


def matches_type(value: Any, expected: str) -> bool:
    """功能：判断JSON值是否匹配支持的基础类型，布尔值不能冒充整数。
    参数：value为待测值；expected为受支持的Schema类型名。
    返回：匹配为True，否则False；未知类型名抛KeyError。
    副作用：只读，不修改数据。
    """
    return {
        "object": isinstance(value, dict),
        "array": isinstance(value, list),
        "string": isinstance(value, str),
        "integer": isinstance(value, int) and not isinstance(value, bool),
        "boolean": isinstance(value, bool),
        "null": value is None,
    }[expected]


def validate(value: Any, schema: dict[str, Any], path: str = "$") -> list[str]:
    """功能：递归验证本仓库使用的基础约束并收集字段路径错误。
    参数：value为JSON值；schema为约束对象；path为诊断路径，默认根路径$。
    返回：错误文本列表，空列表表示通过已支持的约束。
    失败：未知类型名或不符合本工具前提的Schema直接抛错误。
    副作用：只读；此工具不发送网络、不写入工作区、不构成完整Schema标准实现。
    """
    errors: list[str] = []
    expected = schema.get("type")
    if expected is not None:
        types = expected if isinstance(expected, list) else [expected]
        if not any(matches_type(value, kind) for kind in types):
            return [f"{path}：类型应为 {types}"]
    if "const" in schema and value != schema["const"]:
        errors.append(f"{path}：值必须等于 {schema['const']!r}")
    if "enum" in schema and value not in schema["enum"]:
        errors.append(f"{path}：值不在允许集合内")
    if isinstance(value, str):
        if len(value) < schema.get("minLength", 0):
            errors.append(f"{path}：文本长度不足")
        if len(value) > schema.get("maxLength", sys.maxsize):
            errors.append(f"{path}：文本长度超出上限")
    if isinstance(value, int) and not isinstance(value, bool) and value < schema.get("minimum", value):
        errors.append(f"{path}：数字小于下限")
    if isinstance(value, list):
        if len(value) < schema.get("minItems", 0):
            errors.append(f"{path}：数组条目不足")
        if len(value) > schema.get("maxItems", sys.maxsize):
            errors.append(f"{path}：数组条目超出上限")
        if "items" in schema:
            for index, item in enumerate(value):
                errors.extend(validate(item, schema["items"], f"{path}[{index}]"))
    if isinstance(value, dict):
        for required in schema.get("required", []):
            if required not in value:
                errors.append(f"{path}：缺少字段 {required}")
        properties = schema.get("properties", {})
        if schema.get("additionalProperties") is False:
            for name in value:
                if name not in properties:
                    errors.append(f"{path}：不允许字段 {name}")
        for name, child in value.items():
            if name in properties:
                errors.extend(validate(child, properties[name], f"{path}.{name}"))
    return errors


def main() -> int:
    """功能：读取协议JSON，核对合法/非法命令及意图测试输入的预期结果。
    参数：无，项目位置由脚本位置确定。
    返回：检查通过为0，任一测试输入行为不符为1；文件/JSON错误使进程失败。
    副作用：仅读取仓库中的协议与测试目录，控制台输出中文计数或错误，不使用用户素材。
    """
    root = Path(__file__).resolve().parents[1]
    schemas = list((root / "contracts").glob("*.schema.json"))
    for path in schemas:
        json.loads(path.read_text(encoding="utf-8"))
    command_schema = json.loads((root / "contracts/command.schema.json").read_text(encoding="utf-8"))
    valid = json.loads((root / "fixtures/contracts/valid-command.json").read_text(encoding="utf-8"))
    invalid = json.loads((root / "fixtures/contracts/invalid-command.json").read_text(encoding="utf-8"))
    valid_errors = validate(valid, command_schema)
    invalid_errors = validate(invalid, command_schema)
    if valid_errors:
        print("合法命令测试输入被拒绝：", *valid_errors, sep="\n", file=sys.stderr)
        return 1
    if not invalid_errors:
        print("非法命令测试输入被错误接受", file=sys.stderr)
        return 1
    intent_schema = json.loads((root / "contracts/intent.schema.json").read_text(encoding="utf-8"))
    valid_intent = json.loads((root / "fixtures/contracts/valid-intent.json").read_text(encoding="utf-8"))
    invalid_intent = json.loads((root / "fixtures/contracts/invalid-intent.json").read_text(encoding="utf-8"))
    intent_errors = validate(valid_intent, intent_schema)
    invalid_intent_errors = validate(invalid_intent, intent_schema)
    if intent_errors:
        print("合法意图测试输入被拒绝：", *intent_errors, sep="\n", file=sys.stderr)
        return 1
    if not invalid_intent_errors:
        print("非法意图测试输入被错误接受", file=sys.stderr)
        return 1
    print(f"已检查 {len(schemas)} 份协议；命令和意图测试输入行为符合预期。")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
