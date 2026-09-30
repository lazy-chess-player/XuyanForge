import QtQuick

/* 每窗口共享的主题令牌对象：GUI 线程创建，不持有业务资料；所有只读颜色随 mode 绑定更新。 */
QtObject {
    /* 内部主题标识，无单位，默认 dark；窗口或测试设置，light 选浅色，其余值回退深色。 */
    property string mode: "dark"

    /* 工作区画布色，默认深色 #111a22；页面背景读取，不接受外部写入。 */
    readonly property color canvas: mode === "light" ? "#f5f7f8" : "#111a22"
    /* 侧栏及底部状态栏背景色，默认 #0d161e；随主题切换更新。 */
    readonly property color sidebar: mode === "light" ? "#e9eef1" : "#0d161e"
    /* 卡片、输入框和弹窗底色，默认 #19252f；控件只读使用。 */
    readonly property color surface: mode === "light" ? "#ffffff" : "#19252f"
    /* 次级表面和选中底色，默认 #22313b；悬停及只读正文区域读取。 */
    readonly property color surfaceAlt: mode === "light" ? "#eef3f5" : "#22313b"
    /* 分隔线和普通边框色，默认 #344550；页面和控件读取。 */
    readonly property color border: mode === "light" ? "#cbd7dc" : "#344550"
    /* 主文字色，默认 #eaf0f2；不改变显示文字或翻译上下文。 */
    readonly property color text: mode === "light" ? "#172831" : "#eaf0f2"
    /* 辅助说明文字色，默认 #a8b9c1；状态说明与占位提示读取。 */
    readonly property color muted: mode === "light" ? "#536772" : "#a8b9c1"
    /* 操作强调与焦点色，默认 #64c0c3；按钮、选择区和焦点框读取。 */
    readonly property color accent: mode === "light" ? "#126f78" : "#64c0c3"
    /* 强调底色上的文字色，默认 #0d2429；主操作按钮读取。 */
    readonly property color onAccent: mode === "light" ? "#ffffff" : "#0d2429"
    /* 错误及危险提示色，默认 #ef8982；只表达状态，不触发错误处理。 */
    readonly property color danger: mode === "light" ? "#aa3939" : "#ef8982"
    /* 成功提示色，默认 #79c7a1；业务页面按真实结果读取。 */
    readonly property color success: mode === "light" ? "#216d4e" : "#79c7a1"
}
