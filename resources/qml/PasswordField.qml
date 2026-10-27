import QtQuick
import QtQuick.Controls

DarkField {
    id: field
    property bool revealed: false
    echoMode: revealed ? TextInput.Normal : TextInput.Password
    rightPadding: 54
    Button {
        font.family: Theme.fontFamily
        id: revealButton
        anchors.right: parent.right
        anchors.rightMargin: 4
        anchors.verticalCenter: parent.verticalCenter
        width: 44
        height: 40
        flat: true
        text: field.revealed ? "◉" : "◎"
        onClicked: field.revealed = !field.revealed
        contentItem: Label {
            font.family: Theme.fontFamily
            text: revealButton.text
            color: Theme.textSecondary
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
        }
    }
}
