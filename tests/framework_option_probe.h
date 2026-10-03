#pragma once

#include <QGuiApplication>
#include <QObject>
#include <QVariantList>
#include <QWindow>

/*
 * 职责：仅在测试进程中定位 Qt 非原生文件框和其控件，供真实弹层交互回归使用。
 * 资源与生命周期：不拥有窗口或控件，不缓存观察指针；测试初始化对象拥有本探针。
 * 线程边界：所有调用在 GUI 线程同步执行，返回对象只能在所属测试窗口存活期间使用。
 * 隔离边界：不接入正式程序，不读取凭据、数据库、小说内容或系统剪贴板。
 */
class FrameworkOptionProbe final : public QObject {
    Q_OBJECT
public:
    /*
     * 功能：定位当前唯一可见的 Qt Quick 文件或目录框，避免把尚未打开的其他框误当作已验收。
     * 参数：无。
     * 返回：Qt 拥有的弹框观察指针；没有或不止一个可见匹配时返回空，调用方必须断言失败。
     * 失败：框架类型变化、不可见或同时打开多个框时不猜测；分配异常传播。
     * 副作用：只读测试进程的窗口对象树，不打开或关闭框、不接受文件。
     * 线程与生命周期：GUI 线程调用，关闭或销毁测试页后不得继续使用返回指针。
     */
    Q_INVOKABLE QObject* activeDialog() const {
        QObject* found = nullptr;
        for (auto* window : QGuiApplication::allWindows()) {
            for (auto* object : window->findChildren<QObject*>()) {
                if (!(object->inherits("QQuickFileDialogImpl") || object->inherits("QQuickFolderDialogImpl"))
                    || !object->property("visible").toBool()) continue;
                if (found != nullptr && found != object) return nullptr;
                found = object;
            }
        }
        return found;
    }

    /*
     * 功能：按 Qt 控件基类定位实际生成的内置控件，不依赖子项位置、文字或类型字符串显示格式。
     * 参数：owner 为非空弹框观察指针，仅本次借用；base_class 为 Qt 元对象基类名，不能为空。
     * 返回：匹配对象的观察引用列表；空输入或无匹配返回空列表，由测试核对实际数量。
     * 失败：Qt 升级改变控件类型时列表不满足预期，测试失败而不是跳过；分配异常传播。
     * 副作用：只读对象树，不读取用户文件或改写任何选项。
     * 线程与生命周期：GUI 线程同步调用；owner 销毁后所有返回观察引用失效。
     */
    Q_INVOKABLE QVariantList controls(QObject* owner, const QString& base_class) const {
        QVariantList result;
        if (owner == nullptr || base_class.isEmpty()) return result;
        const auto name = base_class.toLatin1();
        for (auto* object : owner->findChildren<QObject*>())
            if (object->inherits(name.constData())) result.push_back(QVariant::fromValue(object));
        return result;
    }
};
