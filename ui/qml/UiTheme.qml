import QtQuick

QtObject {
    property string mode: "dark"

    readonly property color canvas: mode === "light" ? "#f5f7f8" : "#111a22"
    readonly property color sidebar: mode === "light" ? "#e9eef1" : "#0d161e"
    readonly property color surface: mode === "light" ? "#ffffff" : "#19252f"
    readonly property color surfaceAlt: mode === "light" ? "#eef3f5" : "#22313b"
    readonly property color border: mode === "light" ? "#cbd7dc" : "#344550"
    readonly property color text: mode === "light" ? "#172831" : "#eaf0f2"
    readonly property color muted: mode === "light" ? "#536772" : "#a8b9c1"
    readonly property color accent: mode === "light" ? "#126f78" : "#64c0c3"
    readonly property color onAccent: mode === "light" ? "#ffffff" : "#0d2429"
    readonly property color danger: mode === "light" ? "#aa3939" : "#ef8982"
    readonly property color success: mode === "light" ? "#216d4e" : "#79c7a1"
}
