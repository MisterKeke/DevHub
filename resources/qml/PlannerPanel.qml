import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Rectangle {
    id: root
    objectName: "workspacePlannerPanel"
    color: "transparent"
    implicitWidth: 520
    implicitHeight: plannerController.collapsed ? 82 : plannerContent.implicitHeight
    property color accent: themeController.accent
    property color surface: themeController.raisedSurface
    property color panel: themeController.panelSurface
    property color border: themeController.borderSubtle
    property color borderStrong: themeController.borderStrong
    property color hoverSurface: themeController.hoverSurface
    property color activeSurface: themeController.activeSurface
    property color text: themeController.textPrimary
    property color secondary: themeController.textSecondary
    property color muted: themeController.textMuted
    property color success: themeController.success
    property color warning: themeController.warning
    property color error: themeController.error
    property color info: themeController.info
    property var statusKeys: ["backlog", "todo", "in_progress", "in_review", "done"]
    property var statusLabels: [qsTr("Backlog"), qsTr("To Do"), qsTr("In Progress"), qsTr("In Review"), qsTr("Done")]
    property var columns: [
        { key: "backlog", title: qsTr("Backlog") },
        { key: "todo", title: qsTr("To Do") },
        { key: "in_progress", title: qsTr("In Progress") },
        { key: "in_review", title: qsTr("In Review") },
        { key: "done", title: qsTr("Done") }
    ]
    property var calendarMonth: new Date(new Date().getFullYear(), new Date().getMonth(), 1)
    property var detail: ({})
    property string editorTaskId: ""
    property bool editorDirty: false
    property string selectedDependencyId: ""
    property var selectedDependencyIds: []
    property string selectedTaskToDelete: ""
    property string sortField: "createdAt"
    property bool sortDescending: false

    function isoDate(date) {
        return date.getFullYear().toString().padStart(4, "0") + "-" + (date.getMonth() + 1).toString().padStart(2, "0") + "-" + date.getDate().toString().padStart(2, "0")
    }
    function calendarCellDate(cellIndex) {
        var first = new Date(calendarMonth.getFullYear(), calendarMonth.getMonth(), 1)
        var mondayOffset = (first.getDay() + 6) % 7
        return new Date(first.getFullYear(), first.getMonth(), 1 - mondayOffset + cellIndex)
    }
    function calendarTitle() { return calendarMonth.toLocaleDateString(Qt.locale(), "MMMM yyyy") }
    function openEditor(taskId) {
        plannerController.clearError()
        var data = taskId ? plannerController.taskDetails(taskId) : {
            taskId: "", title: "", description: "", status: "todo", completed: false,
            priority: "medium", difficulty: "", dueDate: "", startDate: "", estimatedMinutes: null,
            actualTrackedMinutes: 0, milestoneId: "", gitBranch: "", githubIssueUrl: "",
            githubPullRequestUrl: "", notes: "", blockedReason: "", tags: [], subtasks: [], dependencyIds: []
        }
        if (!data || Object.keys(data).length === 0) return
        detail = data
        editorTaskId = data.taskId || ""
        titleField.text = data.title || ""
        descriptionField.text = data.description || ""
        priorityBox.currentIndex = Math.max(0, ["low", "medium", "high"].indexOf(data.priority || "medium"))
        difficultyBox.currentIndex = Math.max(0, ["", "easy", "medium", "hard"].indexOf(data.difficulty || ""))
        completionBox.checked = !!data.completed
        statusBox.currentIndex = Math.max(0, statusKeys.indexOf(data.status || "todo"))
        startField.text = data.startDate || ""
        dueField.text = data.dueDate || ""
        estimateField.text = data.estimatedMinutes === undefined || data.estimatedMinutes === null ? "" : String(data.estimatedMinutes)
        actualField.text = String(data.actualTrackedMinutes || 0)
        milestoneBox.currentIndex = milestoneIndex(data.milestoneId || "")
        branchField.text = data.gitBranch || ""
        issueField.text = data.githubIssueUrl || ""
        pullRequestField.text = data.githubPullRequestUrl || ""
        notesField.text = data.notes || ""
        blockedField.text = data.blockedReason || ""
        tagsField.text = (data.tags || []).join(", ")
        subtaskDraft.clear()
        var items = data.subtasks || []
        for (var i = 0; i < items.length; ++i)
            subtaskDraft.append({ subtaskKey: items[i].id || "", title: items[i].title || "", completed: !!items[i].completed })
        selectedDependencyIds = (data.dependencyIds || []).slice(0)
        dependencyBox.currentIndex = -1
        editorMessage.text = ""
        editorDirty = false
        editor.open()
    }
    function milestoneIndex(id) {
        for (var i = 0; i < plannerController.milestones.length; ++i)
            if (plannerController.milestones[i].id === id) return i
        return -1
    }
    function dependencyTitle(id) {
        var choices = plannerController.dependencyChoices(editorTaskId)
        for (var i = 0; i < choices.length; ++i) if (choices[i].taskId === id) return choices[i].title
        return id
    }
    function requestEditorClose() {
        if (editorDirty) discardDialog.open()
        else editor.close()
    }
    function saveEditor() {
        var subtasks = []
        for (var i = 0; i < subtaskDraft.count; ++i) {
            var item = subtaskDraft.get(i)
            subtasks.push({ id: item.subtaskKey, title: item.title, completed: item.completed, position: i })
        }
        var values = {
            taskId: editorTaskId, title: titleField.text, description: descriptionField.text,
            priority: ["low", "medium", "high"][priorityBox.currentIndex],
            difficulty: ["", "easy", "medium", "hard"][difficultyBox.currentIndex],
            completed: completionBox.checked || statusBox.currentIndex === 4, status: statusKeys[statusBox.currentIndex],
            startDate: startField.text.trim(), dueDate: dueField.text.trim(),
            estimatedMinutes: estimateField.text.trim(), actualTrackedMinutes: actualField.text.trim(),
            milestoneId: milestoneBox.currentIndex < 0 ? "" : plannerController.milestones[milestoneBox.currentIndex].id,
            gitBranch: branchField.text, githubIssueUrl: issueField.text.trim(),
            githubPullRequestUrl: pullRequestField.text.trim(), notes: notesField.text,
            blockedReason: blockedField.text, tags: tagsField.text, subtasks: subtasks,
            dependencyIds: selectedDependencyIds
        }
        if (plannerController.saveTask(values)) { editorDirty = false; editor.close() }
        else editorMessage.text = plannerController.error || qsTr("Review the task details and try again.")
    }
    function openDetails(taskId) {
        detail = plannerController.taskDetails(taskId)
        if (detail && detail.taskId) detailsDialog.open()
    }
    function editFromDetails() { var id = detail.taskId; detailsDialog.close(); openEditor(id) }
    function requestDelete(taskId) { selectedTaskToDelete = taskId; deleteDialog.open() }
    function confirmDelete() {
        if (plannerController.removeTask(selectedTaskToDelete)) { selectedTaskToDelete = ""; deleteDialog.close(); detailsDialog.close() }
    }

    ColumnLayout {
        id: plannerContent
        anchors.fill: parent
        spacing: 10

        Rectangle {
            Layout.fillWidth: true
            radius: 12
            color: root.panel
            border.color: root.border
            implicitHeight: plannerController.collapsed ? 70 : 105
            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 16; anchors.rightMargin: 14; anchors.topMargin: 12; anchors.bottomMargin: 12
                spacing: 14
                ColumnLayout {
                    visible: !plannerController.collapsed
                    Layout.fillWidth: true
                    spacing: 4
                    RowLayout {
                        spacing: 8
                        Text { text: qsTr("Workspace Planner"); color: root.text; font.pixelSize: 17; font.weight: Font.DemiBold }
                        Rectangle {
                            radius: 9; color: root.activeSurface; border.color: root.borderStrong
                            implicitWidth: contextLabel.implicitWidth + 16; implicitHeight: 24
                            Text { id: contextLabel; anchors.centerIn: parent; text: plannerController.hasWorkspace ? plannerController.workspaceName : qsTr("No workspace"); color: root.secondary; font.pixelSize: 10; elide: Text.ElideRight; width: Math.min(180, implicitWidth) }
                        }
                    }
                    Text { text: plannerController.hasWorkspace ? qsTr("Plan and track work in this workspace") : qsTr("Select a workspace to start planning"); color: root.muted; font.pixelSize: 11 }
                    RowLayout {
                        spacing: 12
                        Repeater {
                            model: [
                                { value: plannerController.totalCount, label: qsTr("tasks"), color: root.secondary },
                                { value: plannerController.inProgressCount, label: qsTr("in progress"), color: root.info },
                                { value: plannerController.completedCount, label: qsTr("done"), color: root.success },
                                { value: plannerController.overdueCount, label: qsTr("overdue"), color: plannerController.overdueCount ? root.error : root.muted }
                            ]
                            delegate: Text { required property var modelData; text: modelData.value + " " + modelData.label; color: modelData.color; font.pixelSize: 10 }
                        }
                        Rectangle { width: 1; height: 14; color: root.border }
                        Text { text: qsTr("Next: %1").arg(plannerController.nearestDeadline); color: root.muted; font.pixelSize: 10; elide: Text.ElideRight; Layout.maximumWidth: 220 }
                    }
                }
                ColumnLayout {
                    visible: plannerController.collapsed
                    Layout.fillWidth: true; spacing: 5
                    RowLayout {
                        Text { text: qsTr("Workspace Planner"); color: root.text; font.pixelSize: 14; font.weight: Font.DemiBold; Layout.fillWidth: true }
                        Text { text: qsTr("%1% complete").arg(plannerController.progressPercent); color: root.success; font.pixelSize: 11 }
                    }
                    ProgressBar { Layout.fillWidth: true; from: 0; to: 100; value: plannerController.progressPercent }
                    Text { text: qsTr("Next deadline: %1").arg(plannerController.nearestDeadline); color: root.muted; font.pixelSize: 10 }
                }
                ColumnLayout {
                    spacing: 6
                    Button {
                        visible: !plannerController.collapsed
                        text: qsTr("＋ New task")
                        enabled: plannerController.hasWorkspace
                        onClicked: root.openEditor("")
                        background: Rectangle { radius: 7; color: parent.enabled ? root.accent : root.activeSurface }
                        contentItem: Text { text: parent.text; color: parent.enabled ? "white" : root.muted; font.pixelSize: 11; font.weight: Font.DemiBold; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter; leftPadding: 10; rightPadding: 10 }
                    }
                    Button {
                        visible: !plannerController.collapsed
                        text: qsTr("Import (Phase 3)")
                        enabled: false
                        Accessible.description: qsTr("Import becomes available in Phase 3. No import is performed in this phase.")
                        ToolTip.visible: hovered
                        ToolTip.text: qsTr("Import becomes available in Phase 3. No import is performed in this phase.")
                        contentItem: Text { text: parent.text; color: root.muted; font.pixelSize: 11; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
                    }
                }
                Button {
                    text: plannerController.collapsed ? qsTr("Expand") : qsTr("Collapse")
                    Accessible.name: text + qsTr(" Workspace Planner")
                    onClicked: plannerController.setCollapsed(!plannerController.collapsed)
                }
            }
        }

        Rectangle {
            visible: !plannerController.collapsed
            Layout.fillWidth: true
            radius: 12; color: root.panel; border.color: root.border
            implicitHeight: viewColumn.implicitHeight + 24
            ColumnLayout {
                id: viewColumn
                anchors.fill: parent; anchors.margins: 12; spacing: 10
                RowLayout {
                    Layout.fillWidth: true; spacing: 8
                    Repeater {
                        model: [qsTr("Board"), qsTr("List"), qsTr("Calendar")]
                        delegate: Button {
                            required property int index
                            required property string modelData
                            text: modelData
                            onClicked: plannerController.setView(["board", "list", "calendar"][index])
                            background: Rectangle { radius: 7; color: plannerController.view === ["board", "list", "calendar"][index] ? root.activeSurface : "transparent"; border.color: plannerController.view === ["board", "list", "calendar"][index] ? root.borderStrong : "transparent" }
                            contentItem: Text { text: parent.text; color: plannerController.view === ["board", "list", "calendar"][index] ? root.text : root.muted; font.pixelSize: 11; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter; leftPadding: 12; rightPadding: 12 }
                        }
                    }
                    TextField {
                        id: searchField
                        Layout.fillWidth: true; Layout.preferredWidth: 190; Layout.minimumWidth: 100
                        placeholderText: qsTr("Search tasks…")
                        color: root.text; placeholderTextColor: root.muted
                        onTextChanged: plannerController.setSearchText(text)
                        background: Rectangle { radius: 7; color: root.surface; border.color: searchField.activeFocus ? root.accent : root.border }
                    }
                }
                RowLayout {
                    Layout.fillWidth: true; spacing: 6
                    ComboBox {
                        id: statusFilter
                        Layout.fillWidth: true; Layout.preferredWidth: 126; Layout.minimumWidth: 85
                        model: [qsTr("All statuses"), qsTr("Backlog"), qsTr("To Do"), qsTr("In Progress"), qsTr("In Review"), qsTr("Done")]
                        onActivated: plannerController.setStatusFilter(index === 0 ? "" : root.statusKeys[index - 1])
                    }
                    ComboBox {
                        Layout.fillWidth: true; Layout.preferredWidth: 104; Layout.minimumWidth: 85
                        model: [qsTr("All priorities"), qsTr("High"), qsTr("Medium"), qsTr("Low")]
                        onActivated: plannerController.setPriorityFilter(["", "high", "medium", "low"][index])
                    }
                    ComboBox {
                        Layout.fillWidth: true; Layout.preferredWidth: 118; Layout.minimumWidth: 90
                        model: [qsTr("Any completion"), qsTr("Active"), qsTr("Completed")]
                        onActivated: plannerController.setCompletionFilter(index === 0 ? -1 : index === 1 ? 0 : 1)
                    }
                    TextField {
                        Layout.fillWidth: true; Layout.preferredWidth: 118; Layout.minimumWidth: 90
                        placeholderText: qsTr("Filter tag")
                        color: root.text; placeholderTextColor: root.muted
                        onTextChanged: plannerController.setTagFilter(text)
                        background: Rectangle { radius: 7; color: root.surface; border.color: root.border }
                    }
                }
                Text { visible: plannerController.error.length > 0; Layout.fillWidth: true; text: plannerController.error; color: root.error; font.pixelSize: 11; wrapMode: Text.WordWrap }
                Text {
                    visible: !plannerController.hasWorkspace
                    Layout.fillWidth: true; Layout.preferredHeight: 58
                    text: qsTr("Choose a workspace from the sidebar to view and manage its tasks. Planner data is stored separately for each workspace.")
                    color: root.muted; wrapMode: Text.WordWrap; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter
                }
                Text {
                    visible: plannerController.hasWorkspace && !plannerController.loading && plannerController.totalCount === 0
                    Layout.fillWidth: true; Layout.preferredHeight: 42
                    text: qsTr("No tasks yet. Create a task to start planning this workspace.")
                    color: root.muted; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter
                }

                Item {
                    visible: plannerController.hasWorkspace && plannerController.view === "board" && plannerController.totalCount > 0
                    Layout.fillWidth: true; Layout.preferredHeight: 350
                    Flickable {
                        anchors.fill: parent; clip: true
                        contentWidth: boardColumns.implicitWidth; contentHeight: height
                        flickableDirection: Flickable.HorizontalFlick
                        ScrollBar.horizontal: ScrollBar { policy: ScrollBar.AsNeeded }
                        RowLayout {
                            id: boardColumns
                            height: parent.height; spacing: 10
                            Repeater {
                                model: root.columns
                                delegate: Rectangle {
                                    required property var modelData
                                    Layout.preferredWidth: Math.max(210, Math.min(260, (root.width - 48) / 3))
                                    Layout.fillHeight: true
                                    radius: 9; color: root.surface; border.color: root.border
                                    ColumnLayout {
                                        anchors.fill: parent; anchors.margins: 8; spacing: 7
                                        RowLayout {
                                            Layout.fillWidth: true
                                            Text { text: modelData.title; color: root.text; font.pixelSize: 11; font.weight: Font.DemiBold; Layout.fillWidth: true }
                                            Text { text: plannerController.statusCount(modelData.key); color: root.muted; font.pixelSize: 10 }
                                            ToolButton { text: "+"; enabled: plannerController.hasWorkspace; Accessible.name: qsTr("Add task to %1").arg(modelData.title); onClicked: { root.openEditor(""); statusBox.currentIndex = root.statusKeys.indexOf(modelData.key) } }
                                        }
                                        ListView {
                                            Layout.fillWidth: true; Layout.fillHeight: true; clip: true; spacing: 7
                                            model: plannerController.boardModel(modelData.key)
                                            ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }
                                            delegate: Rectangle {
                                                required property string taskId
                                                required property string title
                                                required property string priority
                                                required property string dueDate
                                                required property string status
                                                required property string gitBranch
                                                required property var tags
                                                required property int subtaskCount
                                                required property int completedSubtaskCount
                                                required property bool overdue
                                                width: ListView.view.width; implicitHeight: cardContent.implicitHeight + 16
                                                radius: 8; color: root.panel; border.color: overdue ? root.error : root.border
                                                ColumnLayout {
                                                    id: cardContent
                                                    anchors.fill: parent; anchors.margins: 8; spacing: 5
                                                    RowLayout {
                                                        Layout.fillWidth: true
                                                        Text { text: title; color: root.text; font.pixelSize: 11; font.weight: Font.DemiBold; wrapMode: Text.WordWrap; Layout.fillWidth: true; MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: root.openDetails(taskId) } }
                                                        Text { text: priority.toUpperCase(); color: priority === "high" ? root.error : priority === "low" ? root.muted : root.warning; font.pixelSize: 8; font.weight: Font.DemiBold }
                                                        ToolButton { text: "⋯"; Accessible.name: qsTr("Task details for %1").arg(title); onClicked: root.openDetails(taskId); Layout.preferredWidth: 28 }
                                                    }
                                                    RowLayout {
                                                        visible: dueDate.length > 0 || subtaskCount > 0
                                                        Text { visible: dueDate.length > 0; text: qsTr("Due %1").arg(dueDate); color: overdue ? root.error : root.muted; font.pixelSize: 9; Layout.fillWidth: true }
                                                        Text { visible: subtaskCount > 0; text: qsTr("%1/%2 subtasks").arg(completedSubtaskCount).arg(subtaskCount); color: root.muted; font.pixelSize: 9 }
                                                    }
                                                    Flow {
                                                        Layout.fillWidth: true; spacing: 4
                                                        Repeater { model: tags; delegate: Rectangle { required property string modelData; radius: 7; color: root.activeSurface; implicitWidth: tagText.implicitWidth + 12; implicitHeight: 19; Text { id: tagText; anchors.centerIn: parent; text: modelData; color: root.secondary; font.pixelSize: 8 } } }
                                                    }
                                                    Text { visible: gitBranch.length > 0; text: "⑂  " + gitBranch; color: root.info; font.pixelSize: 9; elide: Text.ElideMiddle; Layout.fillWidth: true }
                                                    RowLayout {
                                                        Layout.fillWidth: true; spacing: 2
                                                        ComboBox {
                                                            Layout.fillWidth: true; Layout.preferredHeight: 28
                                                            model: root.statusLabels; currentIndex: Math.max(0, root.statusKeys.indexOf(status))
                                                            onActivated: plannerController.moveTask(taskId, root.statusKeys[index])
                                                        }
                                                        ToolButton { text: "↑"; Accessible.name: qsTr("Move task earlier"); onClicked: plannerController.shiftTaskOrder(taskId, -1) }
                                                        ToolButton { text: "↓"; Accessible.name: qsTr("Move task later"); onClicked: plannerController.shiftTaskOrder(taskId, 1) }
                                                    }
                                                }
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }
                }

                ColumnLayout {
                    visible: plannerController.hasWorkspace && plannerController.view === "list" && plannerController.totalCount > 0
                    Layout.fillWidth: true; spacing: 0
                    Rectangle {
                        Layout.fillWidth: true; height: 34; color: root.surface; radius: 6
                        RowLayout {
                            anchors.fill: parent; spacing: 4
                            Button { text: qsTr("Done"); onClicked: root.sortBy("completed"); Layout.preferredWidth: 42 }
                            Button { text: qsTr("Task title"); onClicked: root.sortBy("title"); Layout.fillWidth: true }
                            Button { visible: root.width >= 720; text: qsTr("Priority"); onClicked: root.sortBy("priority"); Layout.preferredWidth: 68 }
                            Button { visible: root.width >= 720; text: qsTr("Due date"); onClicked: root.sortBy("dueDate"); Layout.preferredWidth: 80 }
                            Button { text: qsTr("Status"); onClicked: root.sortBy("status"); Layout.preferredWidth: 96 }
                            Item { Layout.preferredWidth: 64 }
                        }
                    }
                    ListView {
                        Layout.fillWidth: true; Layout.preferredHeight: 310; clip: true; spacing: 2
                        model: plannerController.listModel
                        ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }
                        delegate: Rectangle {
                            required property string taskId
                            required property string title
                            required property bool completed
                            required property string priority
                            required property string difficulty
                            required property string dueDate
                            required property string status
                            required property string gitBranch
                            required property var tags
                            required property int subtaskCount
                            required property bool overdue
                            width: ListView.view.width; height: 58; color: index % 2 ? root.panel : root.surface; radius: 5
                            RowLayout {
                                anchors.fill: parent; anchors.leftMargin: 6; anchors.rightMargin: 4; spacing: 4
                                CheckBox { checked: completed; Accessible.name: qsTr("Mark %1 complete").arg(title); onClicked: plannerController.setTaskCompleted(taskId, checked); Layout.preferredWidth: 38 }
                                ColumnLayout {
                                    Layout.fillWidth: true; spacing: 1
                                    Text { text: title; color: completed ? root.muted : root.text; font.pixelSize: 10; elide: Text.ElideRight; Layout.fillWidth: true; MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: root.openDetails(taskId) } }
                                    Text {
                                        visible: root.width < 720
                                        text: qsTr("%1 priority · %2 · due %3").arg(priority).arg(difficulty.length ? difficulty : qsTr("not set")).arg(dueDate.length ? dueDate : qsTr("no date"))
                                        color: overdue ? root.error : root.muted; font.pixelSize: 8; elide: Text.ElideRight; Layout.fillWidth: true
                                    }
                                    Text {
                                        text: qsTr("%1 · %2 · %3").arg(subtaskCount ? qsTr("%1 subtasks").arg(subtaskCount) : qsTr("No subtasks")).arg(tags.length ? tags.join(", ") : qsTr("No tags")).arg(gitBranch.length ? gitBranch : qsTr("No branch"))
                                        color: root.muted; font.pixelSize: 8; elide: Text.ElideRight; Layout.fillWidth: true
                                    }
                                }
                                Text { visible: root.width >= 720; text: priority; color: priority === "high" ? root.error : priority === "low" ? root.muted : root.warning; font.pixelSize: 9; Layout.preferredWidth: 68 }
                                Text { visible: root.width >= 720; text: dueDate.length ? dueDate : "—"; color: overdue ? root.error : root.secondary; font.pixelSize: 9; Layout.preferredWidth: 80 }
                                ComboBox { Layout.preferredWidth: 96; Layout.preferredHeight: 30; model: root.statusLabels; currentIndex: Math.max(0, root.statusKeys.indexOf(status)); onActivated: plannerController.moveTask(taskId, root.statusKeys[index]) }
                                ToolButton { text: "✎"; Accessible.name: qsTr("Edit %1").arg(title); onClicked: root.openEditor(taskId); Layout.preferredWidth: 30 }
                                ToolButton { text: "⋯"; Accessible.name: qsTr("Task details for %1").arg(title); onClicked: root.openDetails(taskId); Layout.preferredWidth: 30 }
                            }
                        }
                    }
                }

                ColumnLayout {
                    visible: plannerController.hasWorkspace && plannerController.view === "calendar" && plannerController.totalCount > 0
                    Layout.fillWidth: true; spacing: 8
                    RowLayout {
                        Layout.fillWidth: true
                        ToolButton { text: "‹"; Accessible.name: qsTr("Previous month"); onClicked: root.calendarMonth = new Date(root.calendarMonth.getFullYear(), root.calendarMonth.getMonth() - 1, 1) }
                        Text { text: root.calendarTitle(); color: root.text; font.pixelSize: 13; font.weight: Font.DemiBold; Layout.fillWidth: true; horizontalAlignment: Text.AlignHCenter }
                        ToolButton { text: qsTr("Today"); onClicked: { root.calendarMonth = new Date(new Date().getFullYear(), new Date().getMonth(), 1); plannerController.setSelectedDate(root.isoDate(new Date())) } }
                        ToolButton { text: "›"; Accessible.name: qsTr("Next month"); onClicked: root.calendarMonth = new Date(root.calendarMonth.getFullYear(), root.calendarMonth.getMonth() + 1, 1) }
                    }
                    GridLayout {
                        Layout.fillWidth: true; columns: 7; rowSpacing: 4; columnSpacing: 4
                        Repeater {
                            model: [qsTr("Mon"), qsTr("Tue"), qsTr("Wed"), qsTr("Thu"), qsTr("Fri"), qsTr("Sat"), qsTr("Sun")]
                            delegate: Text { required property string modelData; text: modelData; color: root.muted; font.pixelSize: 9; horizontalAlignment: Text.AlignHCenter; Layout.fillWidth: true }
                        }
                        Repeater {
                            model: 42
                            delegate: Rectangle {
                                required property int index
                                property var day: root.calendarCellDate(index)
                                property string dateKey: root.isoDate(day)
                                property bool currentMonth: day.getMonth() === root.calendarMonth.getMonth()
                                property int itemCount: plannerController.countForDate(dateKey)
                                function selectDate() {
                                    plannerController.setSelectedDate(dateKey)
                                    if (!currentMonth) root.calendarMonth = new Date(day.getFullYear(), day.getMonth(), 1)
                                }
                                Layout.fillWidth: true; Layout.preferredHeight: 42
                                radius: 6; color: plannerController.selectedDate === dateKey ? root.activeSurface : root.surface
                                border.color: plannerController.selectedDate === dateKey ? root.accent : root.border
                                Column {
                                    anchors.centerIn: parent; spacing: 2
                                    Text { text: day.getDate(); color: currentMonth ? root.text : root.muted; font.pixelSize: 10; anchors.horizontalCenter: parent.horizontalCenter }
                                    Row { spacing: 2; anchors.horizontalCenter: parent.horizontalCenter; Repeater { model: Math.min(itemCount, 4); delegate: Rectangle { width: 4; height: 4; radius: 2; color: root.accent } } }
                                }
                                MouseArea { anchors.fill: parent; onClicked: parent.selectDate() }
                                Accessible.name: qsTr("%1, %2 tasks").arg(dateKey).arg(itemCount)
                                Accessible.role: Accessible.Button
                                Accessible.onPressAction: selectDate()
                            }
                        }
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        Text { text: plannerController.selectedDate.length ? qsTr("Tasks due %1").arg(plannerController.selectedDate) : qsTr("Select a date"); color: root.text; font.pixelSize: 11; font.weight: Font.DemiBold; Layout.fillWidth: true }
                        CheckBox { id: showUndated; text: qsTr("Show undated tasks") }
                    }
                    ListView {
                        id: calendarList
                        visible: plannerController.selectedDate.length > 0 && !showUndated.checked
                        Layout.fillWidth: true; Layout.preferredHeight: 100; clip: true; spacing: 3
                        model: plannerController.calendarModel
                        delegate: Rectangle {
                            required property string taskId; required property string title; required property string priority; required property bool overdue
                            width: calendarList.width; height: 30; radius: 5; color: root.surface; border.color: overdue ? root.error : root.border
                            RowLayout {
                                anchors.fill: parent; anchors.leftMargin: 8; anchors.rightMargin: 5
                                Text { text: title; color: root.text; font.pixelSize: 10; Layout.fillWidth: true; elide: Text.ElideRight }
                                Text { text: priority; color: root.warning; font.pixelSize: 9 }
                                ToolButton { text: "⋯"; Accessible.name: qsTr("Task details for %1").arg(title); onClicked: root.openDetails(taskId) }
                                ToolButton { text: "✎"; onClicked: root.openEditor(taskId) }
                            }
                        }
                    }
                    Text {
                        visible: plannerController.selectedDate.length > 0 && !showUndated.checked && calendarList.count === 0
                        Layout.fillWidth: true; Layout.preferredHeight: 28
                        text: qsTr("No tasks due on this date."); color: root.muted; font.pixelSize: 10
                        horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter
                    }
                    ListView {
                        id: undatedList
                        visible: showUndated.checked
                        Layout.fillWidth: true; Layout.preferredHeight: 100; clip: true; spacing: 3
                        model: plannerController.undatedModel
                        delegate: Rectangle {
                            required property string taskId; required property string title; required property string priority
                            width: undatedList.width; height: 30; radius: 5; color: root.surface; border.color: root.border
                            RowLayout {
                                anchors.fill: parent; anchors.leftMargin: 8; anchors.rightMargin: 5
                                Text { text: title; color: root.text; font.pixelSize: 10; Layout.fillWidth: true; elide: Text.ElideRight }
                                Text { text: priority; color: root.warning; font.pixelSize: 9 }
                                ToolButton { text: "⋯"; Accessible.name: qsTr("Task details for %1").arg(title); onClicked: root.openDetails(taskId) }
                                ToolButton { text: "✎"; onClicked: root.openEditor(taskId) }
                            }
                        }
                    }
                }
            }
        }
    }

    Dialog {
        id: editor
        modal: true; title: editorTaskId.length ? qsTr("Edit Planner task") : qsTr("New Planner task")
        width: Math.min(760, Math.max(500, root.width)); height: 720
        anchors.centerIn: Overlay.overlay
        closePolicy: Popup.NoAutoClose
        background: Rectangle { color: root.panel; radius: 12; border.color: root.borderStrong }
        contentItem: ColumnLayout {
            spacing: 8
            ScrollView {
                Layout.fillWidth: true; Layout.fillHeight: true; clip: true
                ColumnLayout {
                    width: Math.max(520, editor.availableWidth - 18); spacing: 10
                    Text { text: qsTr("General"); color: root.accent; font.pixelSize: 12; font.weight: Font.DemiBold }
                    TextField { id: titleField; Layout.fillWidth: true; placeholderText: qsTr("Task title"); color: root.text; onTextChanged: root.editorDirty = true }
                    TextArea { id: descriptionField; Layout.fillWidth: true; Layout.preferredHeight: 62; placeholderText: qsTr("Description"); color: root.text; wrapMode: TextEdit.Wrap; onTextChanged: root.editorDirty = true }
                    GridLayout {
                        Layout.fillWidth: true; columns: 2
                        Label { text: qsTr("Priority"); color: root.secondary }
                        ComboBox { id: priorityBox; model: [qsTr("Low"), qsTr("Medium"), qsTr("High")]; onActivated: root.editorDirty = true }
                        Label { text: qsTr("Difficulty"); color: root.secondary }
                        ComboBox { id: difficultyBox; model: [qsTr("Not set"), qsTr("Easy"), qsTr("Medium"), qsTr("Hard")]; onActivated: root.editorDirty = true }
                        Label { text: qsTr("Kanban status"); color: root.secondary }
                        ComboBox { id: statusBox; model: root.statusLabels; onActivated: { completionBox.checked = index === 4; root.editorDirty = true } }
                        Label { text: qsTr("Completed"); color: root.secondary }
                        CheckBox { id: completionBox; onToggled: { if (checked) statusBox.currentIndex = 4; else if (statusBox.currentIndex === 4) statusBox.currentIndex = 1; root.editorDirty = true } }
                    }
                    Rectangle { Layout.fillWidth: true; height: 1; color: root.border }
                    Text { text: qsTr("Dates and planning"); color: root.accent; font.pixelSize: 12; font.weight: Font.DemiBold }
                    GridLayout {
                        Layout.fillWidth: true; columns: 2
                        Label { text: qsTr("Start date"); color: root.secondary }
                        TextField { id: startField; placeholderText: "YYYY-MM-DD"; color: root.text; onTextChanged: root.editorDirty = true }
                        Label { text: qsTr("Due date"); color: root.secondary }
                        TextField { id: dueField; placeholderText: "YYYY-MM-DD"; color: root.text; onTextChanged: root.editorDirty = true }
                        Label { text: qsTr("Estimated minutes"); color: root.secondary }
                        TextField {
                            id: estimateField; placeholderText: qsTr("Optional"); color: root.text
                            validator: IntValidator { bottom: 0 }
                            onTextChanged: root.editorDirty = true
                        }
                        Label { text: qsTr("Actual tracked minutes"); color: root.secondary }
                        TextField {
                            id: actualField; color: root.text
                            validator: IntValidator { bottom: 0 }
                            onTextChanged: root.editorDirty = true
                        }
                        Label { text: qsTr("Milestone"); color: root.secondary }
                        ComboBox { id: milestoneBox; Layout.fillWidth: true; model: plannerController.milestones; textRole: "title"; onActivated: root.editorDirty = true; Component.onCompleted: currentIndex = -1 }
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        TextField { id: newMilestoneTitle; Layout.fillWidth: true; placeholderText: qsTr("New milestone title"); color: root.text }
                        TextField { id: newMilestoneDate; Layout.preferredWidth: 130; placeholderText: "YYYY-MM-DD"; color: root.text }
                        Button { text: qsTr("Add milestone"); onClicked: { if (plannerController.createMilestone(newMilestoneTitle.text, newMilestoneDate.text)) { newMilestoneTitle.clear(); newMilestoneDate.clear() } else editorMessage.text = plannerController.error } }
                    }
                    Rectangle { Layout.fillWidth: true; height: 1; color: root.border }
                    Text { text: qsTr("Organization"); color: root.accent; font.pixelSize: 12; font.weight: Font.DemiBold }
                    TextField { id: tagsField; Layout.fillWidth: true; placeholderText: qsTr("Tags, separated by commas"); color: root.text; onTextChanged: root.editorDirty = true }
                    RowLayout {
                        Layout.fillWidth: true
                        Text { text: qsTr("Subtasks"); color: root.secondary; Layout.fillWidth: true }
                        Button { text: qsTr("＋ Add subtask"); onClicked: { subtaskDraft.append({ subtaskKey: "", title: "", completed: false }); root.editorDirty = true } }
                    }
                    ListView {
                        Layout.fillWidth: true; Layout.preferredHeight: Math.min(150, Math.max(42, contentHeight)); clip: true; spacing: 3
                        model: ListModel { id: subtaskDraft }
                        delegate: RowLayout {
                            required property int index; required property string subtaskKey; required property string title; required property bool completed
                            width: ListView.view.width
                            CheckBox { checked: completed; onToggled: { subtaskDraft.setProperty(index, "completed", checked); root.editorDirty = true } }
                            TextField { Layout.fillWidth: true; text: title; placeholderText: qsTr("Subtask"); color: root.text; onTextChanged: { subtaskDraft.setProperty(index, "title", text); root.editorDirty = true } }
                            ToolButton { text: "×"; Accessible.name: qsTr("Remove subtask"); onClicked: { subtaskDraft.remove(index); root.editorDirty = true } }
                        }
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        ComboBox { id: dependencyBox; Layout.fillWidth: true; model: plannerController.dependencyChoices(editorTaskId); textRole: "title"; valueRole: "taskId"; onActivated: root.selectedDependencyId = currentValue }
                        Button { text: qsTr("Add dependency"); enabled: dependencyBox.currentIndex >= 0; onClicked: { var id = dependencyBox.currentValue; if (id && root.selectedDependencyIds.indexOf(id) < 0) { root.selectedDependencyIds = root.selectedDependencyIds.concat([id]); root.editorDirty = true } dependencyBox.currentIndex = -1 } }
                    }
                    Flow {
                        Layout.fillWidth: true; spacing: 5
                        Repeater {
                            model: root.selectedDependencyIds
                            delegate: Rectangle {
                                required property string modelData
                                radius: 8; color: root.activeSurface; implicitWidth: dependencyName.implicitWidth + 34; implicitHeight: 26
                                RowLayout {
                                    anchors.fill: parent; anchors.leftMargin: 8
                                    Text { id: dependencyName; text: root.dependencyTitle(modelData); color: root.secondary; font.pixelSize: 9; Layout.fillWidth: true }
                                    ToolButton { text: "×"; onClicked: { var next = root.selectedDependencyIds.slice(0); next.splice(index, 1); root.selectedDependencyIds = next; root.editorDirty = true } }
                                }
                            }
                        }
                    }
                    Rectangle { Layout.fillWidth: true; height: 1; color: root.border }
                    Text { text: qsTr("Development"); color: root.accent; font.pixelSize: 12; font.weight: Font.DemiBold }
                    TextField { id: branchField; Layout.fillWidth: true; placeholderText: qsTr("Git branch"); color: root.text; onTextChanged: root.editorDirty = true }
                    TextField { id: issueField; Layout.fillWidth: true; placeholderText: qsTr("GitHub Issue URL"); color: root.text; onTextChanged: root.editorDirty = true }
                    TextField { id: pullRequestField; Layout.fillWidth: true; placeholderText: qsTr("GitHub Pull Request URL"); color: root.text; onTextChanged: root.editorDirty = true }
                    TextField { id: blockedField; Layout.fillWidth: true; placeholderText: qsTr("Blocked reason (optional)"); color: root.text; onTextChanged: root.editorDirty = true }
                    TextArea { id: notesField; Layout.fillWidth: true; Layout.preferredHeight: 68; placeholderText: qsTr("Additional Planner notes"); color: root.text; wrapMode: TextEdit.Wrap; onTextChanged: root.editorDirty = true }
                    Rectangle { Layout.fillWidth: true; height: 1; color: root.border; visible: editorTaskId.length > 0 }
                    Text { visible: editorTaskId.length > 0; text: qsTr("Source information"); color: root.accent; font.pixelSize: 12; font.weight: Font.DemiBold }
                    Text {
                        visible: editorTaskId.length > 0
                        Layout.fillWidth: true
                        text: editorTaskId.length === 0 ? "" : qsTr("%1 task · created %2 · updated %3%4")
                            .arg(sourceTypeText()).arg(detail.createdAt || "—").arg(detail.updatedAt || "—")
                    .arg((detail.sourceType || "local") === "local" ? "" : qsTr(" · source %1 / %2 · revision %3 · created %4 · updated %5 · imported %6")
                                .arg(detail.sourceProfileIdentity || "—").arg(detail.sourceTaskId || "—").arg(detail.sourceRevision || "—")
                                .arg(detail.sourceCreatedAt || "—").arg(detail.sourceUpdatedAt || "—").arg(detail.importedAt || "—"))
                        color: root.muted; font.pixelSize: 9; wrapMode: Text.WordWrap
                    }
                }
            }
            Text { id: editorMessage; visible: text.length > 0; Layout.fillWidth: true; color: root.error; font.pixelSize: 10; wrapMode: Text.WordWrap }
            RowLayout {
                Layout.fillWidth: true
                Item { Layout.fillWidth: true }
                Button { text: qsTr("Cancel"); onClicked: root.requestEditorClose() }
                Button {
                    text: qsTr("Save task"); enabled: plannerController.hasWorkspace; onClicked: root.saveEditor()
                    background: Rectangle { radius: 6; color: root.accent }
                    contentItem: Text { text: parent.text; color: "white"; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter; leftPadding: 12; rightPadding: 12 }
                }
            }
        }
    }

    Dialog {
        id: discardDialog; modal: true; title: qsTr("Discard unsaved changes?"); standardButtons: Dialog.Discard | Dialog.Cancel
        width: 380
        anchors.centerIn: Overlay.overlay
        onAccepted: { root.editorDirty = false; editor.close() }
    }
    Dialog {
        id: detailsDialog; modal: true; title: qsTr("Task details"); standardButtons: Dialog.Close
        width: Math.min(560, Math.max(440, root.width)); anchors.centerIn: Overlay.overlay
        background: Rectangle { color: root.panel; radius: 12; border.color: root.borderStrong }
        contentItem: ColumnLayout {
            spacing: 8
            Text { text: root.detail.title || ""; color: root.text; font.pixelSize: 18; font.weight: Font.DemiBold; wrapMode: Text.WordWrap; Layout.fillWidth: true }
            Text { text: root.detail.description || qsTr("No description."); color: root.secondary; font.pixelSize: 11; wrapMode: Text.WordWrap; Layout.fillWidth: true }
            Text { text: qsTr("%1 · %2 priority · %3 difficulty · due %4").arg(root.detail.statusLabel || "").arg(root.detail.priority || "").arg(root.detail.difficulty || qsTr("not set")).arg(root.detail.dueDate || qsTr("no date")); color: root.muted; font.pixelSize: 10; wrapMode: Text.WordWrap; Layout.fillWidth: true }
            Text { text: qsTr("Tags: %1").arg((root.detail.tags || []).join(", ") || qsTr("none")); color: root.secondary; font.pixelSize: 10; wrapMode: Text.WordWrap; Layout.fillWidth: true }
            Text { text: qsTr("Branch: %1").arg(root.detail.gitBranch || qsTr("none")); color: root.secondary; font.pixelSize: 10; Layout.fillWidth: true }
            Text { visible: (root.detail.blockedReason || "").length > 0; text: qsTr("Blocked: %1").arg(root.detail.blockedReason || ""); color: root.warning; font.pixelSize: 10; wrapMode: Text.WordWrap; Layout.fillWidth: true }
            Text { text: qsTr("Subtasks: %1/%2 complete").arg(root.detail.completedSubtaskCount || 0).arg(root.detail.subtaskCount || 0); color: root.secondary; font.pixelSize: 10; Layout.fillWidth: true }
            Text {
                visible: (root.detail.sourceType || "local") !== "local"
                text: qsTr("Imported from %1 · source identity %2 / %3 · revision %4 · created %5 · updated %6 · imported %7")
                    .arg(root.detail.sourceType || "").arg(root.detail.sourceProfileIdentity || "—").arg(root.detail.sourceTaskId || "—")
                    .arg(root.detail.sourceRevision || "—").arg(root.detail.sourceCreatedAt || "—").arg(root.detail.sourceUpdatedAt || "—").arg(root.detail.importedAt || "—")
                color: root.muted; font.pixelSize: 9; wrapMode: Text.WordWrap; Layout.fillWidth: true
            }
            RowLayout {
                Layout.fillWidth: true
                Item { Layout.fillWidth: true }
                Button { text: qsTr("Delete"); onClicked: root.requestDelete(root.detail.taskId) }
                Button { text: qsTr("Edit"); onClicked: root.editFromDetails() }
            }
        }
    }
    Dialog {
        id: deleteDialog; modal: true; title: qsTr("Delete task?"); standardButtons: Dialog.Yes | Dialog.Cancel
        width: 380
        anchors.centerIn: Overlay.overlay
        contentItem: Text { text: qsTr("This permanently removes the task and its Planner details from this workspace."); color: root.secondary; wrapMode: Text.WordWrap }
        onAccepted: root.confirmDelete()
    }

    function sourceTypeText() { return (detail.sourceType || "local") === "local" ? qsTr("Local") : qsTr("Imported") }
    function sortBy(field) {
        if (sortField === field) sortDescending = !sortDescending
        else { sortField = field; sortDescending = false }
        plannerController.sortList(field, sortDescending)
    }
}
