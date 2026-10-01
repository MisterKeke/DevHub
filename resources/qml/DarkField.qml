import QtQuick
import QtQuick.Controls

TextField {
    id: field
    implicitHeight: Theme.controlHeight
    leftPadding: 14
    rightPadding: 14
    color: Theme.text
    selectionColor: Theme.accent
    placeholderTextColor: "#82717d"
    background: Rectangle {
        radius: Theme.radiusSmall
        color: "#1a1118"
        border.width: field.activeFocus ? 2 : 1
        border.color: field.activeFocus ? Theme.accent : Theme.borderSoft
    }
}
