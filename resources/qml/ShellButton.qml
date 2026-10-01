import QtQuick
import QtQuick.Controls

Button {
    id: control
    property bool primary: false
    implicitHeight: 34
    implicitWidth: Math.max(78, label.implicitWidth + 28)
    padding: 8

    contentItem: Text {
        id: label
        text: control.text
        color: themeController.textPrimary
        font.pixelSize: 11
        font.weight: Font.Medium
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
    }
    background: Rectangle {
        radius: 7
        color: control.down ? themeController.activeSurface
              : control.hovered ? themeController.hoverSurface
              : control.primary ? themeController.activeSurface : themeController.raisedSurface
        border.width: 1
        border.color: control.primary ? themeController.borderStrong : themeController.borderSubtle
        opacity: control.enabled ? 1 : 0.48
    }
}
