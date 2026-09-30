import QtQuick

/* 内容分区背景：由父页面拥有，在 GUI 线程读取统一主题；内部布局与业务数据均由使用方提供。 */
Rectangle {
    /* 外部主题的观察引用，无默认值；调用方负责保证引用在面板生命周期内有效。 */
    required property var uiTheme
    radius: 8
    color: uiTheme.surface
    border.color: uiTheme.border
}
