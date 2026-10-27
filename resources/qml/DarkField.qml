import QtQuick
import QtQuick.Controls

TextField {
    font.family: Theme.fontFamily
    id: field
    implicitHeight: Theme.controlHeight
    leftPadding: 14
    rightPadding: 14
    color: Theme.text
    selectionColor: Theme.accent
    selectedTextColor: Theme.onAccent
    placeholderTextColor: Theme.textSecondary
    background: Rectangle {
        radius: Theme.radiusSmall
        color: Theme.background
        border.width: field.activeFocus ? 2 : 1
        border.color: field.activeFocus ? Theme.focus : Theme.border
    }
}
