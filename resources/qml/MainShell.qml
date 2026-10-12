import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Rectangle {
    id: root
    color: themeController.background
    property color accent: themeController.accent
    property color surface: themeController.raisedSurface
    property color panel: themeController.panelSurface
    property color border: themeController.borderSubtle
    property color borderStrong: themeController.borderStrong
    property color hoverSurface: themeController.hoverSurface
    property color activeSurface: themeController.activeSurface
    property color sidebarSurface: themeController.sidebarSurface
    property color text: themeController.textPrimary
    property color secondary: themeController.textSecondary
    property color muted: themeController.textMuted
    readonly property int selectedModule: shellController.quickModule

    RowLayout {
        anchors.fill: parent
        spacing: 0

        Rectangle {
            Layout.preferredWidth: Math.max(218, Math.min(280, root.width * 0.19))
            Layout.fillHeight: true
            color: root.sidebarSurface
            border.color: root.border
            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 18
                spacing: 14
                RowLayout {
                    Layout.fillWidth: true
                    Rectangle {
                        width: 38; height: 38; radius: 11
                        color: root.activeSurface
                        Text { anchors.centerIn: parent; text: "◆"; color: root.accent; font.pixelSize: 24 }
                    }
                    Text { text: "DevHub"; color: root.text; font.pixelSize: 22; font.weight: Font.DemiBold }
                    Item { Layout.fillWidth: true }
                    ToolButton {
                        id: appMenuButton
                        text: "⋮"
                        implicitWidth: 28; implicitHeight: 34
                        contentItem: Text { text: appMenuButton.text; color: root.secondary; font.pixelSize: 20; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
                        background: Rectangle { radius: 6; color: appMenuButton.hovered ? root.hoverSurface : "transparent" }
                        // Open settings directly. A nested Controls Menu was unreliable
                        // here on Windows because it could lose its popup owner/focus.
                        onClicked: shellController.openQuickSettings()
                    }
                }
                Text { text: qsTr("WORKSPACES"); color: root.muted; font.pixelSize: 10; font.letterSpacing: 1.1; font.weight: Font.DemiBold }
                TextField {
                    id: workspaceSearch
                    Layout.fillWidth: true
                    implicitHeight: 40
                    placeholderText: qsTr("Search workspaces…")
                    color: root.text
                    placeholderTextColor: root.muted
                    leftPadding: 14
                    background: Rectangle { radius: 9; color: root.panel; border.color: workspaceSearch.activeFocus ? root.accent : root.border }
                    onTextChanged: workspaceModel.setFilter(text)
                }
                ListView {
                    id: workspaceList
                    Layout.fillWidth: true; Layout.fillHeight: true
                    clip: true; spacing: 7
                    model: workspaceModel
                    delegate: Rectangle {
                        required property string workspaceId
                        required property string name
                        required property string path
                        required property bool favorite
                        width: workspaceList.width; height: 60; radius: 9
                        color: workspaceId === shellController.shellWorkspaceId ? root.activeSurface : hover.hovered ? root.hoverSurface : root.panel
                        border.color: workspaceId === shellController.shellWorkspaceId ? root.borderStrong : hover.hovered ? root.borderStrong : root.border
                        RowLayout {
                            anchors.fill: parent; anchors.leftMargin: 12; anchors.rightMargin: 9
                            Rectangle { width: 4; Layout.fillHeight: true; radius: 2; color: shellController.shellWorkspaceId === workspaceId ? root.accent : "transparent" }
                            Text { text: "▱"; color: root.accent; font.pixelSize: 21 }
                            ColumnLayout {
                                Layout.fillWidth: true; spacing: 3
                                Text { Layout.fillWidth: true; text: name; color: root.text; font.pixelSize: 13; font.weight: Font.DemiBold; elide: Text.ElideRight }
                                Text { Layout.fillWidth: true; text: path; color: root.muted; font.pixelSize: 10; elide: Text.ElideMiddle }
                            }
                            Text { visible: favorite; text: "★"; color: root.accent; font.pixelSize: 13 }
                        }
                        HoverHandler { id: hover }
                        TapHandler { onTapped: shellController.activateQuickWorkspace(workspaceId) }
                    }
                    ScrollBar.vertical: ScrollBar { }
                }
                ShellButton {
                    Layout.fillWidth: true; text: qsTr("＋   Add Workspace")
                    implicitHeight: 38; primary: true
                    onClicked: shellController.addQuickWorkspace()
                }
                Text { text: qsTr("Workspace files stay in place"); color: root.muted; font.pixelSize: 10 }
                ShellButton {
                    Layout.fillWidth: true; text: qsTr("Lock DevHub")
                    implicitHeight: 34
                    onClicked: shellController.lockQuickSession()
                }
            }
        }

        Rectangle {
            Layout.preferredWidth: 152; Layout.fillHeight: true
            color: root.surface; border.color: root.border
            ColumnLayout {
                anchors.fill: parent; anchors.margins: 12; spacing: 5
                Text { text: qsTr("MODULES"); color: root.muted; font.pixelSize: 10; font.letterSpacing: 1.1; font.weight: Font.DemiBold; Layout.bottomMargin: 8 }
                Repeater {
                    model: [qsTr("Overview"), qsTr("Git"), qsTr("Services"), qsTr("Ports"), qsTr("Logs"), qsTr("Database"), qsTr("Notes"), qsTr("Files"), qsTr("Assistant")]
                    delegate: Rectangle {
                        required property string modelData
                        required property int index
                        readonly property int moduleIndex: index < 8 ? index : index + 1
                        Layout.fillWidth: true; height: 40; radius: 8
                        Layout.topMargin: index === 8 ? 12 : 0
                        Rectangle { visible: index === 8; anchors.bottom: parent.top; anchors.bottomMargin: 6; width: parent.width; height: 1; color: root.border; objectName: "assistantSeparator" }
                        color: root.selectedModule === moduleIndex ? root.activeSurface : navHover.hovered ? root.hoverSurface : "transparent"
                        RowLayout {
                            anchors.fill: parent; anchors.leftMargin: 11; spacing: 10
                            Text { text: ["⌂","⑂","▷","◉","≡","▤","▧","▱","✦"][index]; color: root.selectedModule === moduleIndex ? root.accent : root.secondary; font.pixelSize: 18; Layout.preferredWidth: 21 }
                            Text { text: modelData; color: root.selectedModule === moduleIndex ? root.accent : root.text; font.pixelSize: 12 }
                        }
                        HoverHandler { id: navHover }
                        TapHandler { onTapped: shellController.selectQuickModule(moduleIndex) }
                    }
                }
                Item { Layout.fillHeight: true }
                Text { text: "DevHub v" + Qt.application.version + "  ·  Local"; color: root.muted; font.pixelSize: 10 }
            }
        }

        ColumnLayout {
            Layout.fillWidth: true; Layout.fillHeight: true; spacing: 0
            Rectangle {
                Layout.fillWidth: true; Layout.preferredHeight: 78
                color: root.surface; border.color: root.border
                RowLayout {
                    anchors.fill: parent; anchors.leftMargin: 22; anchors.rightMargin: 20; spacing: 12
                    ColumnLayout {
                        Layout.fillWidth: true; spacing: 4
                        Text { text: shellController.shellWorkspaceTitle; color: root.text; font.pixelSize: 20; font.weight: Font.DemiBold; elide: Text.ElideRight }
                        Text { text: shellController.shellWorkspacePath; color: root.muted; font.pixelSize: 11; elide: Text.ElideMiddle; Layout.fillWidth: true }
                    }
                    ShellButton { text: "›  " + qsTr("Open Terminal"); primary: true; onClicked: shellController.openQuickTerminal() }
                    ShellButton { text: "↻  " + qsTr("Restart Services"); primary: true; onClicked: shellController.restartQuickServices() }
                    ShellButton { text: "▱  " + qsTr("Open Folder"); onClicked: shellController.openQuickFolder() }
                }
            }
            Flickable {
                Layout.fillWidth: true; Layout.fillHeight: true; clip: true
                contentWidth: width; contentHeight: pageColumn.implicitHeight
                ColumnLayout {
                    id: pageColumn
                    width: parent.width; spacing: 16
                    Item { Layout.preferredHeight: 2 }
                    RowLayout {
                        Layout.fillWidth: true; Layout.leftMargin: 24; Layout.rightMargin: 24
                        ColumnLayout {
                            Layout.fillWidth: true; spacing: 4
                            Text { text: qsTr("Workspace Overview"); color: root.text; font.pixelSize: 19; font.weight: Font.DemiBold }
                            Text { text: qsTr("Everything you need for productive development, in one place."); color: root.muted; font.pixelSize: 12 }
                        }
                        Rectangle { radius: 8; color: root.panel; border.color: root.border; implicitWidth: quote.implicitWidth + 24; implicitHeight: 42
                            Text { id: quote; anchors.centerIn: parent; text: "\"Small steps, consistent progress.\""; color: root.secondary; font.pixelSize: 11 }
                        }
                    }
                    GridLayout {
                        Layout.fillWidth: true; Layout.leftMargin: 24; Layout.rightMargin: 24; Layout.bottomMargin: 24
                        columns: width >= 1100 ? 3 : width >= 680 ? 2 : 1
                        rowSpacing: 14; columnSpacing: 14
                        Repeater {
                            model: shellController.overviewSections
                            delegate: Rectangle {
                                required property var modelData
                                Layout.fillWidth: true; Layout.preferredHeight: 270
                                radius: 10; color: root.panel; border.color: root.border
                                ColumnLayout {
                                    anchors.fill: parent; anchors.margins: 15; spacing: 10
                                    RowLayout {
                                        Layout.fillWidth: true
                                        Text { text: modelData.icon; color: root.accent; font.pixelSize: 18 }
                                        Text { text: modelData.title; color: root.text; font.pixelSize: 13; font.weight: Font.DemiBold; Layout.fillWidth: true }
                                        Rectangle { visible: modelData.status.length > 0; radius: 8; color: modelData.tone === "success" ? "#0d3827" : modelData.tone === "error" ? "#451c27" : "#202c41"; border.color: modelData.tone === "success" ? "#148958" : modelData.tone === "error" ? "#a43d5d" : "#315783"; implicitWidth: badge.implicitWidth + 16; implicitHeight: 22
                                            Text { id: badge; anchors.centerIn: parent; text: modelData.status; color: modelData.tone === "success" ? "#48dc98" : modelData.tone === "error" ? "#ff6c89" : "#75b2ff"; font.pixelSize: 10 }
                                        }
                                    }
                                    Text { Layout.fillWidth: true; text: modelData.summary; color: root.secondary; font.pixelSize: 11; wrapMode: Text.WordWrap; maximumLineCount: 3 }
                                    ColumnLayout {
                                        Layout.fillWidth: true; spacing: 5
                                        Repeater { model: modelData.details; delegate: Text { required property string modelData; Layout.fillWidth: true; text: modelData; color: root.muted; font.pixelSize: 10; elide: Text.ElideRight } }
                                    }
                                    Item { Layout.fillHeight: true }
                                    Text { text: modelData.error; color: themeController.accent; visible: text.length > 0; wrapMode: Text.WordWrap; font.pixelSize: 10 }
                                    Text { text: modelData.updated; color: root.muted; font.pixelSize: 9 }
                                    ShellButton { visible: modelData.action.length > 0; text: modelData.action; onClicked: shellController.selectQuickModule(modelData.module) }
                                }
                            }
                        }
                    }
                }
                ScrollBar.vertical: ScrollBar { }
            }
        }
    }
}
