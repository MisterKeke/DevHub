import QtQuick
import QtQuick.Controls

Button {
    font.family: Theme.fontFamily
    id: control
    property bool primary: false
    implicitHeight: 34
    implicitWidth: Math.max(78, label.implicitWidth + 28)
    padding: 8
    hoverEnabled: true
    opacity: enabled ? 1 : 0.48

    contentItem: Text {
        font.family: Theme.fontFamily
        id: label
        text: control.text
        color: control.primary ? Theme.onAccent : themeController.textPrimary
        font.pixelSize: 12
        font.weight: Font.Medium
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
    }
    background: Rectangle {
        radius: 0
        color: control.primary ? (control.down ? Qt.darker(Theme.accent, 1.2) : control.hovered ? Theme.accentSecondary : Theme.accent)
              : control.down ? themeController.activeSurface
              : control.hovered ? themeController.hoverSurface : themeController.raisedSurface
        border.width: control.visualFocus ? 2 : 1
        border.color: control.visualFocus || control.hovered ? Theme.focus
                      : control.primary ? Theme.accent : themeController.borderStrong
    }
}
