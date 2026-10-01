import QtQuick

Canvas {
    id: mark
    implicitWidth: 38
    implicitHeight: 38
    property color color: Theme.accent
    onPaint: {
        const c = getContext("2d")
        c.clearRect(0, 0, width, height)
        c.strokeStyle = color
        c.lineWidth = 3
        c.lineCap = "round"
        c.lineJoin = "round"
        c.beginPath()
        c.moveTo(11, 16)
        c.lineTo(11, 12)
        c.bezierCurveTo(11, 2, 27, 2, 27, 12)
        c.lineTo(27, 16)
        c.stroke()
        c.strokeRect(7, 15, 24, 19)
        c.beginPath()
        c.arc(19, 23, 2, 0, Math.PI * 2)
        c.stroke()
        c.beginPath()
        c.moveTo(19, 25)
        c.lineTo(19, 29)
        c.stroke()
    }
    Component.onCompleted: requestPaint()
    onWidthChanged: requestPaint()
    onHeightChanged: requestPaint()
    onColorChanged: requestPaint()
}
