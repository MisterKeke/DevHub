import QtQuick
import QtQuick.Controls

Button {
    font.family: Theme.fontFamily
    id: button
    hoverEnabled: true
    opacity: enabled ? 1 : 0.48
    implicitHeight: 50
    contentItem: Label {
        font.family: Theme.fontFamily
        text: button.text
        color: Theme.onAccent
        font.pixelSize: 15
        font.weight: Font.DemiBold
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
    }
    background: Rectangle {
        radius: 0
        color: button.down ? Qt.darker(Theme.accent, 1.2) : button.hovered ? Theme.accentSecondary : Theme.accent
        border.width: button.visualFocus ? 2 : 1
        border.color: button.visualFocus ? Theme.focus : Theme.accent
    }
}
