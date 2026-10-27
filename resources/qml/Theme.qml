pragma Singleton
import QtQuick

QtObject {
    // The fallback also supports standalone QML layout fixtures.
    readonly property string fontFamily: typeof themeController.fontFamily === "string"
                                         ? themeController.fontFamily : "Cascadia Code"
    readonly property color onAccent: typeof themeController.background !== "undefined"
                                      ? themeController.background : "#080F24"
    readonly property color focus: themeController.info
    readonly property color background: themeController.background
    readonly property color backgroundRaised: themeController.raisedSurface
    readonly property color surface: themeController.panelSurface
    readonly property color surfaceRaised: themeController.raisedSurface
    readonly property color sidebarSurface: themeController.sidebarSurface
    readonly property color hoverSurface: themeController.hoverSurface
    readonly property color activeSurface: themeController.activeSurface
    readonly property color border: themeController.borderStrong
    readonly property color borderSoft: themeController.borderSubtle
    readonly property color accent: themeController.accent
    readonly property color accentSecondary: themeController.accentHover
    readonly property color text: themeController.textPrimary
    readonly property color textSecondary: themeController.textSecondary
    readonly property color success: themeController.success
    readonly property color warning: themeController.warning
    readonly property color error: themeController.error
    readonly property color info: themeController.info
    readonly property int radiusSmall: 0
    readonly property int radiusMedium: 0
    readonly property int radiusLarge: 0
    readonly property int controlHeight: 46
}
