#pragma once

#include "xuyan/domain/scenario.h"

#include <string>
#include <string_view>

namespace xuyan::application {

/* 解码结果值对象；拥有标准化正文及检测编码，调用线程生成，不持有输入视图或文件。 */
struct DecodedSourceText {
    /* 去 BOM、规范换行后的 UTF-8 正文；默认空，由解码器填写，调用方按码点建立索引。 */
    std::string normalized_utf8;
    /* 原始编码内部标识（如 utf-8）；默认空，解码成功后填写，随结果持有，不是界面标签。 */
    std::string encoding;
};

/*
 * 功能：按 BOM 优先识别 UTF-16，否则严格尝试 UTF-8，Windows 才回退 GB18030。
 * 参数：bytes 为原始文件字节的借用视图，调用期间有效；空输入可得到空标准化正文。
 * 返回：拥有正文与编码标识的解码结果。失败：无效字节、代理项或不支持编码返回 Result；分配异常可传播。
 * 副作用：仅内存转换，不读取文件，不复制到其他工作区；线程：调用线程同步执行，不保留输入引用。
 */
xuyan::domain::Result<DecodedSourceText> decodeSourceText(std::string_view bytes);

} // namespace xuyan::application
