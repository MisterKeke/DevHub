import QtQuick
import QtQuick.Controls

Button {
    id: button
    implicitHeight: 50
    contentItem: Label {
        text: button.text
        color: "white"
        font.pixelSize: 15
        font.weight: Font.DemiBold
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
    }
    background: Rectangle {
        radius: 9
        gradient: Gradient {
            GradientStop { position: 0; color: Theme.accent }
            GradientStop { position: 1; color: Theme.accentSecondary }
        }
        opacity: button.enabled ? 1 : 0.48
    }
}
