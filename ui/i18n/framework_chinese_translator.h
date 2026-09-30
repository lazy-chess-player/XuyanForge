#pragma once

#include <QTranslator>
#include <QString>
#include <cstring>

/*
 * 职责：补齐当前Qt词库缺少的Quick文件框和文本编辑动作，不替换用户名称、路径或模型标识。
 * 资源与生命周期：由启动栈或测试初始化对象拥有，无数据库、文件和共享可变状态；在GUI引擎之前安装。
 * 语言边界：仅用于当前开放的简体中文；以后开放其他语言时按语言安装对应翻译器，不改协议值。
 */
class FrameworkChineseTranslator final : public QTranslator {
public:
    /* 功能：构造无外部词库依赖的中文补充翻译器。
     * 参数：parent为可空QObject所有者，默认空；非空时所有权服从Qt父子关系。
     * 返回：初始化完成；失败：对象分配异常传播。
     * 副作用：尚不安装到应用；调用方保证安装期间对象存活，销毁后Qt自动移除。 */
    explicit FrameworkChineseTranslator(QObject* parent = nullptr) : QTranslator(parent) {}

    /* 功能：声明内置翻译表非空，让Qt接受安装。
     * 参数：无。返回：false；失败：不抛异常。
     * 副作用：只读常量，不加载文件；任意线程同步调用。 */
    bool isEmpty() const override { return false; }

    /*
     * 功能：对已核对的框架上下文和源文提供中文，其余请求让后续Qt或产品翻译器处理。
     * 参数：context为Qt上下文、sourceText为源文，均只在本次调用借用且须零终止；空指针返回空；
     *       disambiguation为可空消歧说明，此表仅支持空说明；n为复数计数，默认-1，此表无复数词条。
     * 返回：命中时拥有的中文字符串；不命中返回空QString，保留Qt翻译链的正常回退。
     * 失败：字符串分配异常传播，不解析用户内容或修改任何对象。
     * 副作用：只读静态表，任意线程可同步调用，不保存输入指针。
     */
    QString translate(const char* context, const char* sourceText,
                      const char* disambiguation = nullptr, int n = -1) const override {
        if (context == nullptr || sourceText == nullptr || (disambiguation != nullptr && *disambiguation != '\0'))
            return {};
        static_cast<void>(n);
        /* 词条只引用静态UTF-8字面量，不拥有或借用任何用户文本。 */
        struct Entry {
            /* Qt文件或动作的精确上下文，无默认值，整个进程有效，只读匹配。 */
            const char* context;
            /* 精确源文，无默认值，整个进程有效；不能用宽泛替换翻译用户字段。 */
            const char* source;
            /* 简体中文动作或提示，整个进程有效；命中后复制为QString。 */
            const char* chinese;
        };
        /* 当前实际使用的Quick词条；静态只读，不包含世界、人物、端点或小说资料。 */
        static constexpr Entry entries[] = {
            {"UndoAction", "Undo", "撤销"}, {"RedoAction", "Redo", "重做"},
            {"CutAction", "Cut", "剪切"}, {"CopyAction", "Copy", "复制"},
            {"PasteAction", "Paste", "粘贴"}, {"DeleteAction", "Delete", "删除"},
            {"SelectAllAction", "Select All", "全选"},
            {"FileDialog", "File name", "文件名"}, {"FileDialog", "Filter", "文件类型"},
            {"FileDialog", "Overwrite file?", "确认替换文件"},
            {"FileDialog", "“%1” already exists.\nDo you want to replace it?", "“%1”已存在。\n是否替换此文件？"},
            {"SideBar", "Add Favorite", "添加收藏"}
        };
        for (const auto& entry : entries)
            if (std::strcmp(context, entry.context) == 0 && std::strcmp(sourceText, entry.source) == 0)
                return QString::fromUtf8(entry.chinese);
        return {};
    }
};
