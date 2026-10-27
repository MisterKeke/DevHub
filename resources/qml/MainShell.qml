import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Rectangle {
    id: root
    color: themeController.background
    property bool moduleNavigationExpanded: true
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
    property color success: themeController.success
    property color warning: themeController.warning
    property color error: themeController.error
    property color info: themeController.info
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
                anchors.margins: 16
                spacing: 12
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 10
                    Rectangle {
                        width: 38; height: 38; radius: 0
                        color: root.activeSurface
                        Text { font.family: Theme.fontFamily; anchors.centerIn: parent; text: "◆"; color: root.accent; font.pixelSize: 24 }
                    }
                    Text { font.family: Theme.fontFamily; text: "DevHub"; color: root.text; font.pixelSize: 21; font.weight: Font.DemiBold }
                    Item { Layout.fillWidth: true }
                    ToolButton {
                        font.family: Theme.fontFamily
                        id: appMenuButton
                        text: "⋮"
                        implicitWidth: 30; implicitHeight: 34
                        Accessible.name: qsTr("Open settings")
                        ToolTip.visible: hovered
                        ToolTip.text: qsTr("Settings")
                        contentItem: Text { font.family: Theme.fontFamily; text: appMenuButton.text; color: root.secondary; font.pixelSize: 20; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
                        background: Rectangle { radius: 0; color: appMenuButton.down ? root.activeSurface : appMenuButton.hovered ? root.hoverSurface : "transparent"; border.width: appMenuButton.visualFocus ? 2 : 1; border.color: appMenuButton.visualFocus ? root.info : "transparent" }
                        onClicked: shellController.openQuickSettings()
                    }
                }
                Text { font.family: Theme.fontFamily; text: qsTr("WORKSPACES"); color: root.muted; font.pixelSize: 10; font.letterSpacing: 1.1; font.weight: Font.DemiBold }
                TextField {
                    font.family: Theme.fontFamily
                    id: workspaceSearch
                    Layout.fillWidth: true
                    implicitHeight: 40
                    placeholderText: qsTr("Search workspaces…")
                    color: root.text
                    placeholderTextColor: root.muted
                    leftPadding: 14
                    background: Rectangle { radius: 0; color: root.panel; border.color: workspaceSearch.activeFocus ? root.info : root.borderStrong }
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
                        width: workspaceList.width; height: 62; radius: 0
                        color: workspaceId === shellController.shellWorkspaceId ? root.activeSurface : hover.hovered ? root.hoverSurface : root.panel
                        border.color: workspaceId === shellController.shellWorkspaceId ? root.accent : hover.hovered ? root.info : root.borderStrong
                        Behavior on color { ColorAnimation { duration: 140 } }
                        RowLayout {
                            anchors.fill: parent; anchors.leftMargin: 10; anchors.rightMargin: 9; spacing: 9
                            Rectangle { width: 3; Layout.fillHeight: true; radius: 0; color: shellController.shellWorkspaceId === workspaceId ? root.accent : "transparent" }
                            Text { font.family: Theme.fontFamily; text: "▱"; color: root.accent; font.pixelSize: 20 }
                            ColumnLayout {
                                Layout.fillWidth: true; spacing: 3
                                Text { font.family: Theme.fontFamily; Layout.fillWidth: true; text: name; color: root.text; font.pixelSize: 13; font.weight: Font.DemiBold; elide: Text.ElideRight }
                                Text { font.family: Theme.fontFamily; Layout.fillWidth: true; text: path; color: root.muted; font.pixelSize: 10; elide: Text.ElideMiddle }
                            }
                            Text { font.family: Theme.fontFamily; visible: favorite; text: "★"; color: root.accent; font.pixelSize: 13 }
                            ToolButton {
                                font.family: Theme.fontFamily
                                text: "⋮"
                                implicitWidth: 30; implicitHeight: 34
                                Accessible.name: qsTr("Workspace actions for %1").arg(name)
                                ToolTip.visible: hovered
                                ToolTip.text: qsTr("Workspace actions")
                                background: Rectangle { radius: 0; color: parent.down ? root.activeSurface : parent.hovered ? root.hoverSurface : "transparent"; border.width: parent.visualFocus ? 2 : 1; border.color: parent.visualFocus ? root.info : "transparent" }
                                onClicked: shellController.showQuickWorkspaceMenu(workspaceId)
                            }
                        }
                        HoverHandler { id: hover }
                        TapHandler {
                            acceptedButtons: Qt.LeftButton
                            onTapped: (eventPoint, button) => {
                                if (eventPoint.position.x < width - 42)
                                    shellController.activateQuickWorkspace(workspaceId)
                            }
                        }
                        TapHandler {
                            acceptedButtons: Qt.RightButton
                            onTapped: shellController.showQuickWorkspaceMenu(workspaceId)
                        }
                    }
                    ScrollBar.vertical: ScrollBar { }
                }
                ShellButton {
                    Layout.fillWidth: true; text: qsTr("＋   Add Workspace")
                    implicitHeight: 40; primary: true
                    enabled: !shellController.workspaceRemovalInProgress
                    onClicked: shellController.addQuickWorkspace()
                }
                ShellButton {
                    Layout.fillWidth: true; text: qsTr("Archived Workspaces…")
                    implicitHeight: 36
                    enabled: !shellController.workspaceRemovalInProgress
                    onClicked: shellController.manageArchivedWorkspaces()
                }
                Text {
                    font.family: Theme.fontFamily
                    Layout.fillWidth: true
                    visible: shellController.workspaceActionMessage.length > 0
                    text: shellController.workspaceActionMessage
                    color: shellController.workspaceActionTone === "error" ? root.error : shellController.workspaceActionTone === "success" ? root.success : root.info
                    font.pixelSize: 10
                    wrapMode: Text.WordWrap
                }
                ShellButton {
                    Layout.fillWidth: true
                    visible: shellController.workspaceRemovalInProgress
                    text: qsTr("Cancel Removal")
                    implicitHeight: 32
                    onClicked: shellController.cancelQuickWorkspaceRemoval(shellController.workspaceRemovalWorkspaceId)
                }
                Text { font.family: Theme.fontFamily; text: qsTr("Workspace files stay in place"); color: root.muted; font.pixelSize: 10 }
                ShellButton {
                    Layout.fillWidth: true; text: qsTr("Lock DevHub")
                    implicitHeight: 36
                    onClicked: shellController.lockQuickSession()
                }
            }
        }

        Rectangle {
            id: moduleNavigation
            property real navWidth: root.moduleNavigationExpanded ? 158 : 62
            Layout.preferredWidth: navWidth
            Layout.fillHeight: true
            color: root.sidebarSurface
            border.color: root.border
            Behavior on navWidth { NumberAnimation { duration: 160; easing.type: Easing.OutCubic } }
            ColumnLayout {
                anchors.fill: parent; anchors.margins: root.moduleNavigationExpanded ? 12 : 8; spacing: 5
                RowLayout {
                    Layout.fillWidth: true
                    Text { font.family: Theme.fontFamily; visible: root.moduleNavigationExpanded; text: qsTr("MODULES"); color: root.muted; font.pixelSize: 10; font.letterSpacing: 1.1; font.weight: Font.DemiBold; Layout.fillWidth: true }
                    ToolButton {
                        font.family: Theme.fontFamily
                        id: collapseNavigation
                        text: root.moduleNavigationExpanded ? "‹" : "›"
                        implicitWidth: 34; implicitHeight: 32
                        Accessible.name: root.moduleNavigationExpanded ? qsTr("Collapse module navigation") : qsTr("Expand module navigation")
                        ToolTip.visible: hovered
                        ToolTip.text: Accessible.name
                        contentItem: Text { font.family: Theme.fontFamily; text: collapseNavigation.text; color: root.secondary; font.pixelSize: 21; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
                        background: Rectangle {
                            radius: 0
                            color: collapseNavigation.hovered ? root.hoverSurface : "transparent"
                            border.width: collapseNavigation.visualFocus ? 2 : 1
                            border.color: collapseNavigation.visualFocus ? root.info : "transparent"
                        }
                        onClicked: root.moduleNavigationExpanded = !root.moduleNavigationExpanded
                    }
                }
                Repeater {
                    model: [qsTr("Overview"), qsTr("Git"), qsTr("Services"), qsTr("Ports"), qsTr("Logs"), qsTr("Database"), qsTr("Notes"), qsTr("Files"), qsTr("Assistant")]
                    delegate: Rectangle {
                        required property string modelData
                        required property int index
                        readonly property int moduleIndex: index < 8 ? index : index + 1
                        Layout.fillWidth: true; height: 40; radius: 0
                        Layout.topMargin: index === 8 ? 12 : 0
                        Rectangle { visible: index === 8; anchors.bottom: parent.top; anchors.bottomMargin: 6; width: parent.width; height: 1; color: root.border }
                        color: root.selectedModule === moduleIndex ? root.accent : navHover.hovered ? root.hoverSurface : "transparent"
                        border.color: root.selectedModule === moduleIndex ? root.accent : navHover.hovered ? root.info : "transparent"
                        Behavior on color { ColorAnimation { duration: 130 } }
                        RowLayout {
                            anchors.fill: parent
                            anchors.leftMargin: root.moduleNavigationExpanded ? 10 : 0
                            anchors.rightMargin: root.moduleNavigationExpanded ? 6 : 0
                            spacing: 9
                            Item { Layout.fillWidth: !root.moduleNavigationExpanded }
                            Text {
                                font.family: Theme.fontFamily
                                text: ["⌂", "⑂", "▷", "◉", "≡", "▤", "▧", "▱", "✦"][index]
                                color: root.selectedModule === moduleIndex ? Theme.onAccent : root.info
                                font.pixelSize: 18; Layout.preferredWidth: 22; horizontalAlignment: Text.AlignHCenter
                            }
                            Text { font.family: Theme.fontFamily; visible: root.moduleNavigationExpanded; text: modelData; color: root.selectedModule === moduleIndex ? Theme.onAccent : root.text; font.pixelSize: 12; font.weight: root.selectedModule === moduleIndex ? Font.DemiBold : Font.Normal; Layout.fillWidth: true; elide: Text.ElideRight }
                            Item { Layout.fillWidth: !root.moduleNavigationExpanded }
                        }
                        HoverHandler { id: navHover }
                        ToolTip.visible: navHover.hovered && !root.moduleNavigationExpanded
                        ToolTip.text: modelData
                        TapHandler { onTapped: shellController.selectQuickModule(moduleIndex) }
                    }
                }
                Item { Layout.fillHeight: true }
                Text { font.family: Theme.fontFamily; visible: root.moduleNavigationExpanded; text: "DevHub v" + Qt.application.version + "  ·  Local"; color: root.muted; font.pixelSize: 10 }
            }
        }

        ColumnLayout {
            Layout.fillWidth: true; Layout.fillHeight: true; spacing: 0
            Rectangle {
                Layout.fillWidth: true; Layout.preferredHeight: 78
                color: root.surface; border.color: root.border
                RowLayout {
                    anchors.fill: parent; anchors.leftMargin: 22; anchors.rightMargin: 20; spacing: 10
                    ColumnLayout {
                        Layout.fillWidth: true; spacing: 4
                        Text { font.family: Theme.fontFamily; text: shellController.shellWorkspaceTitle; color: root.text; font.pixelSize: 20; font.weight: Font.DemiBold; elide: Text.ElideRight }
                        Text { font.family: Theme.fontFamily; text: shellController.shellWorkspacePath; color: root.muted; font.pixelSize: 11; elide: Text.ElideMiddle; Layout.fillWidth: true }
                    }
                    ShellButton { text: "›  " + qsTr("Open Terminal"); primary: true; onClicked: shellController.openQuickTerminal() }
                    ShellButton { text: "↻  " + qsTr("Restart Services"); primary: true; onClicked: shellController.restartQuickServices() }
                    ShellButton { visible: root.width > 1180; text: "▱  " + qsTr("Open Folder"); onClicked: shellController.openQuickFolder() }
                    ShellButton { visible: root.width > 1180; text: "✦  " + qsTr("Assistant"); onClicked: shellController.selectQuickModule(9) }
                }
            }
            Flickable {
                Layout.fillWidth: true; Layout.fillHeight: true; clip: true
                contentWidth: width; contentHeight: pageColumn.implicitHeight
                ColumnLayout {
                    id: pageColumn
                    width: parent.width; spacing: 14
                    Item { Layout.preferredHeight: 4 }
                    RowLayout {
                        Layout.fillWidth: true; Layout.leftMargin: 24; Layout.rightMargin: 24
                        ColumnLayout {
                            Layout.fillWidth: true; spacing: 4
                            Text { font.family: Theme.fontFamily; text: qsTr("Workspace Overview"); color: root.text; font.pixelSize: 21; font.weight: Font.DemiBold }
                            Text { font.family: Theme.fontFamily; text: qsTr("Your development workspace at a glance."); color: root.muted; font.pixelSize: 12 }
                        }
                        Rectangle {
                            radius: 0; color: root.panel; border.color: root.border
                            implicitWidth: workspaceState.implicitWidth + 30; implicitHeight: 34
                            RowLayout {
                                anchors.centerIn: parent; spacing: 7
                                Rectangle { width: 7; height: 7; radius: 0; color: shellController.shellWorkspaceId.length > 0 ? root.success : root.muted }
                                Text { font.family: Theme.fontFamily; id: workspaceState; text: shellController.shellWorkspaceId.length > 0 ? qsTr("Workspace active") : qsTr("No workspace selected"); color: root.secondary; font.pixelSize: 10 }
                            }
                        }
                    }
                    PlannerPanel {
                        Layout.fillWidth: true
                        Layout.leftMargin: 24
                        Layout.rightMargin: 24
                    }
                    GridLayout {
                        Layout.fillWidth: true; Layout.leftMargin: 24; Layout.rightMargin: 24; Layout.bottomMargin: 24
                        columns: width >= 1160 ? 3 : width >= 700 ? 2 : 1
                        rowSpacing: 14; columnSpacing: 14
                        Repeater {
                            model: shellController.overviewSections
                            delegate: Rectangle {
                                id: overviewCard
                                required property var modelData
                                Layout.fillWidth: true; Layout.preferredHeight: 258; Layout.minimumHeight: 238
                                radius: 0; color: root.panel; border.color: root.borderStrong
                                ColumnLayout {
                                    anchors.fill: parent; anchors.margins: 14; spacing: 8
                                    RowLayout {
                                        Layout.fillWidth: true; spacing: 8
                                        Rectangle {
                                            width: 30; height: 30; radius: 0; color: root.activeSurface
                                            Text { font.family: Theme.fontFamily; anchors.centerIn: parent; text: modelData.icon; color: root.accent; font.pixelSize: 17 }
                                        }
                                        Text { font.family: Theme.fontFamily; text: modelData.title; color: root.text; font.pixelSize: 13; font.weight: Font.DemiBold; Layout.fillWidth: true; elide: Text.ElideRight }
                                        Rectangle {
                                            visible: (modelData.status || "").length > 0 || modelData.loading
                                            readonly property color toneColor: modelData.tone === "success" ? root.success : modelData.tone === "warning" ? root.warning : modelData.tone === "error" ? root.error : root.info
                                            radius: 0
                                            color: Qt.rgba(toneColor.r, toneColor.g, toneColor.b, 0.12)
                                            border.color: Qt.rgba(toneColor.r, toneColor.g, toneColor.b, 0.55)
                                            implicitWidth: badge.implicitWidth + 16; implicitHeight: 23
                                            Text { font.family: Theme.fontFamily; id: badge; anchors.centerIn: parent; text: modelData.loading && !(modelData.status || "").length ? qsTr("Updating") : modelData.status; color: parent.toneColor; font.pixelSize: 10 }
                                        }
                                    }
                                    Text {
                                        font.family: Theme.fontFamily
                                        Layout.fillWidth: true
                                        text: modelData.summary || qsTr("Select a workspace to see live details.")
                                        color: root.secondary; font.pixelSize: 11; wrapMode: Text.WordWrap; maximumLineCount: 2; elide: Text.ElideRight
                                    }
                                    ColumnLayout {
                                        Layout.fillWidth: true; spacing: 5
                                        Repeater {
                                            model: modelData.details || []
                                            delegate: Text { font.family: Theme.fontFamily; required property string modelData; Layout.fillWidth: true; text: modelData; color: root.muted; font.pixelSize: 10; elide: Text.ElideRight }
                                        }
                                        Text {
                                            font.family: Theme.fontFamily
                                            visible: !(modelData.details || []).length
                                            text: modelData.loading ? qsTr("Collecting current workspace data…") : modelData.hasWorkspace ? qsTr("No recent items.") : qsTr("Choose a workspace to load this section.")
                                            color: root.muted; font.pixelSize: 10; wrapMode: Text.WordWrap
                                        }
                                    }
                                    Item { Layout.fillHeight: true }
                                    Text { font.family: Theme.fontFamily; Layout.fillWidth: true; text: modelData.error || ""; color: root.error; visible: text.length > 0; wrapMode: Text.WordWrap; font.pixelSize: 10 }
                                    RowLayout {
                                        Layout.fillWidth: true
                                        Text { font.family: Theme.fontFamily; text: modelData.updated || ""; color: root.muted; font.pixelSize: 9; Layout.fillWidth: true; elide: Text.ElideRight }
                                        ShellButton { visible: (modelData.action || "").length > 0; text: modelData.action; onClicked: shellController.selectQuickModule(modelData.module) }
                                    }
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
