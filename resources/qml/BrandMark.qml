import QtQuick

Canvas {
    id: mark
    implicitWidth: 40
    implicitHeight: 40
    property color color: Theme.accent
    onPaint: {
        const c = getContext("2d"); c.clearRect(0, 0, width, height)
        c.fillStyle = color
        c.beginPath(); c.moveTo(20, 2); c.lineTo(37, 11); c.lineTo(20, 20); c.lineTo(3, 11); c.closePath(); c.fill()
        c.beginPath(); c.moveTo(5, 17); c.lineTo(20, 25); c.lineTo(35, 17); c.lineTo(35, 22); c.lineTo(20, 31); c.lineTo(5, 22); c.closePath(); c.fill()
        c.beginPath(); c.moveTo(5, 25); c.lineTo(20, 33); c.lineTo(35, 25); c.lineTo(35, 30); c.lineTo(20, 39); c.lineTo(5, 30); c.closePath(); c.fill()
    }
    Component.onCompleted: requestPaint()
    onWidthChanged: requestPaint()
    onHeightChanged: requestPaint()
    onColorChanged: requestPaint()
}
