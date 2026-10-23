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
    property string knownWorkspaceId: ""
    property bool editorDirty: false
    property string selectedDependencyId: ""
    property var selectedDependencyIds: []
    property string selectedTaskToDelete: ""
    property string selectedMilestoneId: ""
    property string sortField: "createdAt"
    property bool sortDescending: false
    property var importController: typeof plannerImportController !== "undefined" ? plannerImportController : null

    Component.onCompleted: root.knownWorkspaceId = plannerController.workspaceId
    Connections {
        target: plannerController
        function onCreateTaskRequested() { root.openEditor("") }
        function onFocusSearchRequested() { plannerController.setCollapsed(false); searchField.forceActiveFocus() }
        function onOpenTaskDetailsRequested(taskId) { root.openDetails(taskId) }
        function onActionRunsChanged(taskId) { if (root.detail.taskId === taskId) root.refreshDetails(taskId) }
        function onChanged() {
            if (root.knownWorkspaceId === plannerController.workspaceId) return
            root.knownWorkspaceId = plannerController.workspaceId
            root.detail = ({})
            root.editorDirty = false
            root.selectedDependencyIds = []
            root.selectedMilestoneId = ""
            editor.close()
            detailsDialog.close()
            milestoneManager.close()
            milestoneEditor.close()
            removeMilestoneDialog.close()
            deleteDialog.close()
        }
    }

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
            githubPullRequestUrl: "", linkedServiceId: "", notes: "", blockedReason: "", tags: [], subtasks: [], dependencyIds: []
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
        milestoneBox.currentIndex = milestoneIndex(data.milestoneId || "")
        branchBox.currentIndex = branchOptionIndex(editorTaskId, data.gitBranch || "")
        serviceBox.currentIndex = serviceOptionIndex(editorTaskId, data.linkedServiceId || "")
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
        if (!id) return 0
        for (var i = 0; i < plannerController.milestones.length; ++i)
            if (plannerController.milestones[i].id === id) return i + 1
        return 0
    }
    function branchOptionIndex(taskId, branch) {
        var options = editorBranchOptions(taskId)
        for (var i = 0; i < options.length; ++i)
            if (options[i].name === branch) return i
        return 0
    }
    function editorBranchOptions(taskId) {
        var options = [{ name: "", displayName: qsTr("No branch"), exists: true }]
        var local = plannerController.branchOptions
        for (var i = 0; i < local.length; ++i) options.push(local[i])
        var associated = detail.gitBranch || ""
        var present = false
        for (var j = 0; j < local.length; ++j) if (local[j].name === associated) present = true
        if (taskId && associated && !present)
            options.push({ name: associated, displayName: qsTr("%1 (missing)").arg(associated), exists: false })
        return options
    }
    function branchIsPresent(branch) {
        if (!branch) return false
        var options = plannerController.branchOptions
        for (var i = 0; i < options.length; ++i) if (options[i].name === branch) return true
        return false
    }
    function editorServiceOptions(taskId) {
        var options = [{ id: "", name: qsTr("No linked service") }]
        var configured = plannerController.serviceOptions
        var present = false
        for (var i = 0; i < configured.length; ++i) {
            options.push(configured[i])
            if (configured[i].id === detail.linkedServiceId) present = true
        }
        if (taskId && detail.linkedServiceId && !present)
            options.push({ id: detail.linkedServiceId, name: qsTr("Unavailable service · %1").arg(detail.linkedServiceId) })
        return options
    }
    function serviceOptionIndex(taskId, serviceId) {
        var options = editorServiceOptions(taskId)
        for (var i = 0; i < options.length; ++i) if (options[i].id === serviceId) return i
        return 0
    }
    function actionIsStarting() {
        var runs = detail.actionRuns || []
        for (var i = 0; i < runs.length; ++i) if (!runs[i].finished) return true
        return false
    }
    function refreshDetails(taskId) {
        var data = plannerController.taskDetails(taskId)
        if (!data || !data.taskId) return
        data.timeEntries = plannerController.timeEntries(taskId)
        detail = data
    }
    function openMilestoneEditor(id) {
        selectedMilestoneId = id || ""
        var item = null
        for (var i = 0; i < plannerController.milestones.length; ++i)
            if (plannerController.milestones[i].id === selectedMilestoneId) item = plannerController.milestones[i]
        milestoneTitleField.text = item ? item.title : ""
        milestoneDescriptionField.text = item ? item.description : ""
        milestoneStartField.text = item ? item.startDate : ""
        milestoneDueField.text = item ? item.dueDate : ""
        milestoneCompletedField.checked = item ? item.completed : false
        milestoneMessage.text = ""
        milestoneEditor.open()
    }
    function saveMilestoneEditor() {
        var values = { id: selectedMilestoneId, title: milestoneTitleField.text,
            description: milestoneDescriptionField.text, startDate: milestoneStartField.text.trim(),
            dueDate: milestoneDueField.text.trim(), completed: milestoneCompletedField.checked }
        if (plannerController.saveMilestone(values)) { milestoneEditor.close(); milestoneManager.open() }
        else milestoneMessage.text = plannerController.error || qsTr("Review the milestone details and try again.")
    }
    function dependencyTitle(id) {
        var choices = plannerController.dependencyChoices(editorTaskId)
        for (var i = 0; i < choices.length; ++i) if (choices[i].taskId === id) return choices[i].title
        return id
    }
    function milestoneTitle(id) {
        for (var i = 0; i < plannerController.milestones.length; ++i)
            if (plannerController.milestones[i].id === id) return plannerController.milestones[i].title
        return id
    }
    function removeSelectedMilestone() {
        if (plannerController.removeMilestone(selectedMilestoneId)) {
            selectedMilestoneId = ""
            removeMilestoneDialog.close()
        }
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
            estimatedMinutes: estimateField.text.trim(),
            milestoneId: milestoneBox.currentValue || "",
            gitBranch: branchBox.currentValue || "", githubIssueUrl: issueField.text.trim(),
            githubPullRequestUrl: pullRequestField.text.trim(), linkedServiceId: serviceBox.currentValue || "", notes: notesField.text,
            blockedReason: blockedField.text, tags: tagsField.text, subtasks: subtasks,
            dependencyIds: selectedDependencyIds
        }
        if (plannerController.saveTask(values)) { editorDirty = false; editor.close() }
        else editorMessage.text = plannerController.error || qsTr("Review the task details and try again.")
    }
    function openDetails(taskId) {
        refreshDetails(taskId)
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
                        text: qsTr("Milestones")
                        enabled: plannerController.hasWorkspace
                        onClicked: milestoneManager.open()
                    }
                    Button {
                        visible: !plannerController.collapsed
                        text: qsTr("Import from SomethingQt")
                        enabled: plannerController.hasWorkspace && root.importController !== null
                        Accessible.description: qsTr("Copy tasks from a compatible SomethingQt database into a DevHub Workspace.")
                        ToolTip.visible: hovered
                        ToolTip.text: qsTr("Import tasks from SomethingQt")
                        onClicked: { root.importController.openDialog(); importDialog.open() }
                        background: Rectangle { radius: 7; color: parent.enabled ? root.activeSurface : "transparent"; border.color: parent.enabled ? root.borderStrong : "transparent" }
                        contentItem: Text { text: parent.text; color: parent.enabled ? root.text : root.muted; font.pixelSize: 11; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter; leftPadding: 8; rightPadding: 8 }
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
                RowLayout {
                    Layout.fillWidth: true; spacing: 6
                    ComboBox {
                        Layout.fillWidth: true; Layout.minimumWidth: 82
                        model: [{ id: "", title: qsTr("All milestones") }].concat(plannerController.milestones)
                        textRole: "title"; valueRole: "id"
                        currentIndex: { for (var mi = 0; mi < model.length; ++mi) if (model[mi].id === plannerController.milestoneFilter) return mi; return 0 }
                        onActivated: plannerController.setMilestoneFilter(currentValue || "")
                    }
                    ComboBox {
                        Layout.fillWidth: true; Layout.minimumWidth: 82
                        model: [{ name: "", displayName: qsTr("All branches") }].concat(plannerController.branchOptions)
                        textRole: "displayName"; valueRole: "name"
                        currentIndex: { for (var bi = 0; bi < model.length; ++bi) if (model[bi].name === plannerController.gitBranchFilter) return bi; return 0 }
                        onActivated: plannerController.setGitBranchFilter(currentValue || "")
                    }
                    ComboBox {
                        Layout.fillWidth: true; Layout.minimumWidth: 82
                        model: [qsTr("Any blocked state"), qsTr("Blocked"), qsTr("Not blocked")]
                        currentIndex: plannerController.blockedFilter < 0 ? 0 : plannerController.blockedFilter === 1 ? 1 : 2
                        onActivated: plannerController.setBlockedFilter(index === 0 ? -1 : index === 1 ? 1 : 0)
                    }
                    ComboBox {
                        Layout.fillWidth: true; Layout.minimumWidth: 82
                        model: [qsTr("Any due date"), qsTr("Overdue"), qsTr("Not overdue")]
                        currentIndex: plannerController.overdueFilter < 0 ? 0 : plannerController.overdueFilter === 1 ? 1 : 2
                        onActivated: plannerController.setOverdueFilter(index === 0 ? -1 : index === 1 ? 1 : 0)
                    }
                    ComboBox {
                        Layout.fillWidth: true; Layout.minimumWidth: 82
                        model: [qsTr("Any origin"), qsTr("Local"), qsTr("SomethingQt")]
                        currentIndex: Math.max(0, ["", "local", "somethingqt"].indexOf(plannerController.originFilter))
                        onActivated: plannerController.setOriginFilter(["", "local", "somethingqt"][index])
                    }
                }
                Text { visible: plannerController.branchStatus.length > 0; Layout.fillWidth: true; text: plannerController.branchStatus; color: root.muted; font.pixelSize: 9; elide: Text.ElideRight }
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
                                                required property bool blocked
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
                                                    Text { visible: blocked; text: qsTr("BLOCKED"); color: root.warning; font.pixelSize: 8; font.weight: Font.DemiBold }
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
                            required property bool blocked
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
                                    Text { visible: blocked; text: qsTr("Blocked"); color: root.warning; font.pixelSize: 8 }
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
                        Label { text: qsTr("Time recorded"); color: root.secondary }
                        Label { text: qsTr("%1 minutes · manage entries in task details").arg(detail.actualTrackedMinutes || 0); color: root.muted; wrapMode: Text.WordWrap }
                        Label { text: qsTr("Milestone"); color: root.secondary }
                        ComboBox {
                            id: milestoneBox; Layout.fillWidth: true
                            model: [{ id: "", title: qsTr("No milestone") }].concat(plannerController.milestones)
                            textRole: "title"; valueRole: "id"; onActivated: root.editorDirty = true
                        }
                    }
                    Button { text: qsTr("Manage milestones…"); onClicked: milestoneManager.open() }
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
                    ComboBox {
                        id: branchBox; Layout.fillWidth: true; enabled: !plannerController.branchLoading
                        model: root.editorBranchOptions(editorTaskId)
                        textRole: "displayName"; valueRole: "name"
                        onActivated: root.editorDirty = true
                    }
                    ComboBox {
                        id: serviceBox; Layout.fillWidth: true
                        model: root.editorServiceOptions(editorTaskId)
                        textRole: "name"; valueRole: "id"
                        onActivated: root.editorDirty = true
                    }
                    Text { text: qsTr("Starting a linked service requires an explicit click. Task text is never used as a command."); color: root.muted; font.pixelSize: 9; wrapMode: Text.WordWrap; Layout.fillWidth: true }
                    Text { visible: plannerController.branchLoading; text: qsTr("Loading local branches…"); color: root.muted; font.pixelSize: 9 }
                    TextField { id: issueField; Layout.fillWidth: true; placeholderText: qsTr("GitHub Issue URL"); color: root.text; onTextChanged: root.editorDirty = true }
                    TextField { id: pullRequestField; Layout.fillWidth: true; placeholderText: qsTr("GitHub Pull Request URL"); color: root.text; onTextChanged: root.editorDirty = true }
                    TextField { id: blockedField; Layout.fillWidth: true; placeholderText: qsTr("Blocked reason (optional)"); color: root.text; onTextChanged: root.editorDirty = true }
                    Text { text: qsTr("Planner notes"); color: root.accent; font.pixelSize: 12; font.weight: Font.DemiBold }
                    TextArea { id: notesField; Layout.fillWidth: true; Layout.preferredHeight: 84; placeholderText: qsTr("Developer notes, separate from the task description"); color: root.text; wrapMode: TextEdit.Wrap; onTextChanged: root.editorDirty = true }
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
        width: Math.min(700, Math.max(460, root.width)); height: Math.min(680, root.height + 260); anchors.centerIn: Overlay.overlay
        background: Rectangle { color: root.panel; radius: 12; border.color: root.borderStrong }
        contentItem: ColumnLayout {
            spacing: 8
            ScrollView {
                Layout.fillWidth: true; Layout.fillHeight: true; clip: true
                ColumnLayout {
                    width: Math.max(420, detailsDialog.availableWidth - 28); spacing: 9
                    Text { text: root.detail.title || ""; color: root.text; font.pixelSize: 18; font.weight: Font.DemiBold; wrapMode: Text.WordWrap; Layout.fillWidth: true }
                    Text { text: qsTr("General"); color: root.accent; font.pixelSize: 12; font.weight: Font.DemiBold }
                    Text { text: root.detail.description || qsTr("No description."); color: root.secondary; font.pixelSize: 11; wrapMode: Text.WordWrap; Layout.fillWidth: true }
                    Text { text: qsTr("%1 · %2 priority · %3 difficulty · due %4").arg(root.detail.statusLabel || "").arg(root.detail.priority || "").arg(root.detail.difficulty || qsTr("not set")).arg(root.detail.dueDate || qsTr("no date")); color: root.muted; font.pixelSize: 10; wrapMode: Text.WordWrap; Layout.fillWidth: true }
                    Text { text: qsTr("Tags: %1 · subtasks %2/%3").arg((root.detail.tags || []).join(", ") || qsTr("none")).arg(root.detail.completedSubtaskCount || 0).arg(root.detail.subtaskCount || 0); color: root.secondary; font.pixelSize: 10; wrapMode: Text.WordWrap; Layout.fillWidth: true }
                    Rectangle { Layout.fillWidth: true; height: 1; color: root.border }
                    Text { text: qsTr("Planning and milestones"); color: root.accent; font.pixelSize: 12; font.weight: Font.DemiBold }
                    Text { text: qsTr("Estimated %1 min · recorded %2 min · milestone %3").arg(root.detail.estimatedMinutes === undefined || root.detail.estimatedMinutes === null ? qsTr("not set") : root.detail.estimatedMinutes).arg(root.detail.actualTrackedMinutes || 0).arg(root.detail.milestoneId ? root.milestoneTitle(root.detail.milestoneId) : qsTr("none")); color: root.secondary; font.pixelSize: 10; wrapMode: Text.WordWrap; Layout.fillWidth: true }
                    Rectangle { Layout.fillWidth: true; height: 1; color: root.border }
                    Text { text: qsTr("Git and GitHub"); color: root.accent; font.pixelSize: 12; font.weight: Font.DemiBold }
                    RowLayout {
                        Layout.fillWidth: true
                        Text { Layout.fillWidth: true; text: root.detail.gitBranch ? qsTr("%1 · %2").arg(root.detail.gitBranch).arg(root.branchIsPresent(root.detail.gitBranch) ? qsTr("exists") : qsTr("missing")) : qsTr("No branch associated"); color: root.branchIsPresent(root.detail.gitBranch) ? root.info : root.warning; font.pixelSize: 10; elide: Text.ElideMiddle }
                        Button { visible: !!root.detail.gitBranch; text: qsTr("Open in Git"); onClicked: plannerController.openGitBranch(root.detail.gitBranch) }
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        Button { visible: !!root.detail.githubIssueUrl; text: qsTr("Open Issue"); onClicked: plannerController.openGitHubUrl(root.detail.githubIssueUrl) }
                        Button { visible: !!root.detail.githubPullRequestUrl; text: qsTr("Open Pull Request"); onClicked: plannerController.openGitHubUrl(root.detail.githubPullRequestUrl) }
                    }
                    Rectangle { Layout.fillWidth: true; height: 1; color: root.border }
                    Text { text: qsTr("Development actions"); color: root.accent; font.pixelSize: 12; font.weight: Font.DemiBold }
                    Text { text: root.detail.linkedServiceId ? (root.detail.linkedServiceAvailable ? qsTr("Linked service · %1").arg(root.detail.serviceName) : qsTr("Linked service is unavailable in the active workspace.")) : qsTr("No configured service is linked to this task."); color: root.detail.linkedServiceAvailable ? root.secondary : root.muted; font.pixelSize: 10; wrapMode: Text.WordWrap; Layout.fillWidth: true }
                    Flow {
                        Layout.fillWidth: true; spacing: 6
                        Button { text: qsTr("Open Git"); enabled: !!plannerController.hasWorkspace; onClicked: plannerController.openGitModule() }
                        Button { text: qsTr("Repository folder"); enabled: !!plannerController.hasWorkspace && !!root.detail.workspaceId; onClicked: plannerController.openWorkspaceFolder() }
                        Button { text: qsTr("Workspace notes"); enabled: !!plannerController.hasWorkspace; onClicked: plannerController.openWorkspaceNotes() }
                        Button { text: qsTr("View service"); enabled: !!root.detail.linkedServiceAvailable; onClicked: plannerController.openLinkedService(root.detail.taskId) }
                        Button { text: qsTr("Related logs"); enabled: !!root.detail.linkedServiceAvailable; onClicked: plannerController.openLinkedServiceLogs(root.detail.taskId) }
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        Button { visible: !root.actionIsStarting(); enabled: !!root.detail.linkedServiceAvailable; text: qsTr("Start linked service"); onClicked: { plannerController.startLinkedService(root.detail.taskId); root.refreshDetails(root.detail.taskId); detailsMessage.text = plannerController.error } }
                        Button { visible: root.actionIsStarting(); text: qsTr("Cancel start"); onClicked: { plannerController.cancelLinkedServiceStart(root.detail.taskId); detailsMessage.text = plannerController.error } }
                        Text { visible: !root.detail.linkedServiceAvailable; text: qsTr("Choose an existing service in the Development section to enable service actions."); color: root.muted; font.pixelSize: 9; wrapMode: Text.WordWrap; Layout.fillWidth: true }
                    }
                    Repeater {
                        model: root.detail.actionRuns || []
                        delegate: RowLayout {
                            required property var modelData
                            Layout.fillWidth: true
                            Text { Layout.fillWidth: true; text: qsTr("%1 · %2 · %3 ms%4").arg(modelData.status).arg(modelData.startedAt).arg(modelData.durationMs).arg(modelData.errorSummary ? " · " + modelData.errorSummary : ""); color: modelData.succeeded ? root.success : modelData.finished ? root.error : root.info; font.pixelSize: 9; wrapMode: Text.WordWrap }
                            Button { visible: !!modelData.logReference; text: qsTr("Logs"); enabled: !!root.detail.linkedServiceAvailable; onClicked: plannerController.openLinkedServiceLogs(root.detail.taskId) }
                        }
                    }
                    Rectangle { Layout.fillWidth: true; height: 1; color: root.border }
                    Text { text: qsTr("Dependencies"); color: root.accent; font.pixelSize: 12; font.weight: Font.DemiBold }
                    Text { visible: !!root.detail.blockedByDependency || !!root.detail.blockedReason; text: root.detail.blockedByDependency ? qsTr("Blocked by incomplete prerequisite: %1").arg((root.detail.incompletePrerequisiteTitles || []).join(", ")) : qsTr("Blocked: %1").arg(root.detail.blockedReason || ""); color: root.warning; font.pixelSize: 10; wrapMode: Text.WordWrap; Layout.fillWidth: true }
                    Text { text: qsTr("Prerequisites: %1").arg((root.detail.prerequisiteTitles || []).join(", ") || qsTr("none")); color: root.secondary; font.pixelSize: 10; wrapMode: Text.WordWrap; Layout.fillWidth: true }
                    Text { text: qsTr("Dependent tasks: %1").arg((root.detail.dependentTaskTitles || []).join(", ") || qsTr("none")); color: root.secondary; font.pixelSize: 10; wrapMode: Text.WordWrap; Layout.fillWidth: true }
                    Rectangle { Layout.fillWidth: true; height: 1; color: root.border }
                    Text { text: qsTr("Planner notes"); color: root.accent; font.pixelSize: 12; font.weight: Font.DemiBold }
                    Text { text: root.detail.notes || qsTr("No developer notes."); color: root.secondary; font.pixelSize: 10; wrapMode: Text.WordWrap; Layout.fillWidth: true }
                    Rectangle { Layout.fillWidth: true; height: 1; color: root.border }
                    Text { text: qsTr("Time tracking · %1 min total").arg(root.detail.actualTrackedMinutes || 0); color: root.accent; font.pixelSize: 12; font.weight: Font.DemiBold }
                    Repeater {
                        model: root.detail.timeEntries || []
                        delegate: RowLayout {
                            required property var modelData
                            Layout.fillWidth: true
                            Text { Layout.fillWidth: true; text: qsTr("%1 min · %2 · %3").arg(modelData.durationMinutes).arg(modelData.note || qsTr("No note")).arg(modelData.recordedAt || ""); color: root.secondary; font.pixelSize: 9; wrapMode: Text.WordWrap }
                            ToolButton { text: "×"; Accessible.name: qsTr("Remove time entry"); onClicked: { if (plannerController.removeTimeEntry(root.detail.taskId, modelData.id)) root.refreshDetails(root.detail.taskId); detailsMessage.text = plannerController.error } }
                        }
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        TextField {
                            id: timeMinutesField; Layout.preferredWidth: 100
                            placeholderText: qsTr("Minutes"); validator: IntValidator { bottom: 1 }
                            color: root.text
                        }
                        TextField { id: timeNoteField; Layout.fillWidth: true; placeholderText: qsTr("Entry note (optional)"); color: root.text }
                        Button { text: qsTr("Record"); onClicked: { if (plannerController.addTimeEntry(root.detail.taskId, timeMinutesField.text, timeNoteField.text)) { timeMinutesField.clear(); timeNoteField.clear(); root.refreshDetails(root.detail.taskId) } detailsMessage.text = plannerController.error } }
                    }
                    Text { id: detailsMessage; visible: text.length > 0; text: ""; color: root.error; font.pixelSize: 9; wrapMode: Text.WordWrap; Layout.fillWidth: true }
                    Rectangle { Layout.fillWidth: true; height: 1; color: root.border }
                    Text { visible: (root.detail.sourceType || "local") !== "local"; text: qsTr("Import source"); color: root.accent; font.pixelSize: 12; font.weight: Font.DemiBold }
                    Text {
                        visible: (root.detail.sourceType || "local") !== "local"
                        text: qsTr("Imported from %1 · profile %2 · source task %3 · revision %4 · created %5 · updated %6 · imported %7")
                            .arg(root.detail.sourceType || "").arg(root.detail.sourceProfileIdentity || "—").arg(root.detail.sourceTaskId || "—")
                            .arg(root.detail.sourceRevision || "—").arg(root.detail.sourceCreatedAt || "—").arg(root.detail.sourceUpdatedAt || "—").arg(root.detail.importedAt || "—")
                        color: root.muted; font.pixelSize: 9; wrapMode: Text.WordWrap; Layout.fillWidth: true
                    }
                }
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
        id: milestoneManager; modal: true; title: qsTr("Workspace milestones"); standardButtons: Dialog.Close
        width: Math.min(680, Math.max(480, root.width)); height: Math.min(620, root.height + 180)
        anchors.centerIn: Overlay.overlay
        background: Rectangle { color: root.panel; radius: 12; border.color: root.borderStrong }
        contentItem: ColumnLayout {
            spacing: 8
            RowLayout {
                Layout.fillWidth: true
                Text { text: qsTr("Milestone progress is calculated from tasks assigned in this workspace."); color: root.muted; font.pixelSize: 10; wrapMode: Text.WordWrap; Layout.fillWidth: true }
                Button { text: qsTr("＋ New milestone"); onClicked: { milestoneManager.close(); root.openMilestoneEditor("") } }
            }
            ListView {
                Layout.fillWidth: true; Layout.fillHeight: true; clip: true; spacing: 6
                model: plannerController.milestones
                delegate: Rectangle {
                    required property var modelData
                    width: ListView.view.width; implicitHeight: milestoneRow.implicitHeight + 16
                    radius: 8; color: root.surface; border.color: root.border
                    ColumnLayout {
                        id: milestoneRow; anchors.fill: parent; anchors.margins: 8; spacing: 5
                        RowLayout {
                            Layout.fillWidth: true
                            Text { text: modelData.title; color: root.text; font.pixelSize: 11; font.weight: Font.DemiBold; Layout.fillWidth: true; elide: Text.ElideRight }
                            Text { text: modelData.completed ? qsTr("Complete") : qsTr("Active"); color: modelData.completed ? root.success : root.muted; font.pixelSize: 9 }
                            ToolButton { text: "✎"; Accessible.name: qsTr("Edit milestone"); onClicked: { milestoneManager.close(); root.openMilestoneEditor(modelData.id) } }
                            ToolButton { text: "×"; Accessible.name: qsTr("Delete milestone"); onClicked: { root.selectedMilestoneId = modelData.id; removeMilestoneDialog.open() } }
                        }
                        Text { visible: !!modelData.description; text: modelData.description; color: root.secondary; font.pixelSize: 9; wrapMode: Text.WordWrap; Layout.fillWidth: true }
                        RowLayout {
                            Layout.fillWidth: true
                            ProgressBar { Layout.fillWidth: true; from: 0; to: 100; value: modelData.progress }
                            Text { text: qsTr("%1% · %2/%3 tasks").arg(modelData.progress).arg(modelData.completedTaskCount).arg(modelData.taskCount); color: root.secondary; font.pixelSize: 9 }
                            Text { text: modelData.dueDate ? qsTr("Due %1").arg(modelData.dueDate) : qsTr("No due date"); color: root.muted; font.pixelSize: 9 }
                        }
                        Button { text: qsTr("View tasks"); onClicked: { plannerController.setMilestoneFilter(modelData.id); milestoneManager.close() } }
                    }
                }
            }
            Text { visible: plannerController.milestones.length === 0; Layout.fillWidth: true; text: qsTr("No milestones yet."); color: root.muted; horizontalAlignment: Text.AlignHCenter }
        }
    }
    Dialog {
        id: milestoneEditor; modal: true; title: root.selectedMilestoneId ? qsTr("Edit milestone") : qsTr("New milestone")
        width: Math.min(520, Math.max(400, root.width)); anchors.centerIn: Overlay.overlay
        background: Rectangle { color: root.panel; radius: 12; border.color: root.borderStrong }
        contentItem: ColumnLayout {
            spacing: 8
            TextField { id: milestoneTitleField; Layout.fillWidth: true; placeholderText: qsTr("Milestone title"); color: root.text }
            TextArea { id: milestoneDescriptionField; Layout.fillWidth: true; Layout.preferredHeight: 76; placeholderText: qsTr("Description"); color: root.text; wrapMode: TextEdit.Wrap }
            GridLayout {
                Layout.fillWidth: true; columns: 2
                Label { text: qsTr("Start date"); color: root.secondary }
                TextField { id: milestoneStartField; placeholderText: "YYYY-MM-DD"; color: root.text }
                Label { text: qsTr("Target date"); color: root.secondary }
                TextField { id: milestoneDueField; placeholderText: "YYYY-MM-DD"; color: root.text }
                Label { text: qsTr("Status"); color: root.secondary }
                CheckBox { id: milestoneCompletedField; text: qsTr("Complete") }
            }
            Text { id: milestoneMessage; visible: text.length > 0; Layout.fillWidth: true; color: root.error; font.pixelSize: 10; wrapMode: Text.WordWrap }
            RowLayout {
                Layout.fillWidth: true
                Item { Layout.fillWidth: true }
                Button { text: qsTr("Cancel"); onClicked: milestoneEditor.close() }
                Button { text: qsTr("Save milestone"); onClicked: root.saveMilestoneEditor() }
            }
        }
    }
    Dialog {
        id: removeMilestoneDialog; modal: true; title: qsTr("Delete milestone?"); standardButtons: Dialog.Yes | Dialog.Cancel
        width: 420; anchors.centerIn: Overlay.overlay
        contentItem: Text { text: qsTr("Tasks in this workspace will stay intact and be removed from the milestone."); color: root.secondary; wrapMode: Text.WordWrap }
        onAccepted: root.removeSelectedMilestone()
    }
    Dialog {
        id: deleteDialog; modal: true; title: qsTr("Delete task?"); standardButtons: Dialog.Yes | Dialog.Cancel
        width: 380
        anchors.centerIn: Overlay.overlay
        contentItem: Text { text: qsTr("This permanently removes the task and its Planner details from this workspace."); color: root.secondary; wrapMode: Text.WordWrap }
        onAccepted: root.confirmDelete()
    }

    Dialog {
        id: importDialog
        modal: true
        title: qsTr("Import tasks from SomethingQt")
        width: Math.min(980, Math.max(640, root.width + 420))
        height: Math.min(760, root.height + 360)
        anchors.centerIn: Overlay.overlay
        standardButtons: Dialog.Close
        background: Rectangle { color: root.panel; radius: 12; border.color: root.borderStrong }
        onClosed: if (root.importController) root.importController.closeDialog()
        contentItem: ColumnLayout {
            spacing: 9
            RowLayout {
                Layout.fillWidth: true; spacing: 7
                TextField {
                    id: sourceProfilePath
                    Layout.fillWidth: true
                    enabled: root.importController && !root.importController.importing
                    text: root.importController ? root.importController.profilePath : ""
                    placeholderText: qsTr("Choose a SomethingQt SQLite profile")
                    selectByMouse: true
                    onEditingFinished: if (root.importController) root.importController.setProfilePath(text)
                    color: root.text; placeholderTextColor: root.muted
                    background: Rectangle { radius: 6; color: root.surface; border.color: sourceProfilePath.activeFocus ? root.accent : root.border }
                }
                Button { text: qsTr("Browse…"); enabled: root.importController !== null && !root.importController.importing; onClicked: root.importController.browseProfile() }
                Button { text: root.importController && root.importController.loading ? qsTr("Checking…") : qsTr("Verify"); enabled: root.importController !== null && !root.importController.loading && !root.importController.importing; onClicked: { root.importController.setProfilePath(sourceProfilePath.text); root.importController.verifyConnection() } }
            }
            Text {
                Layout.fillWidth: true
                text: root.importController ? root.importController.connectionStatus : qsTr("Import service is unavailable")
                color: root.importController && root.importController.verified ? root.success : root.muted
                font.pixelSize: 10; wrapMode: Text.WordWrap
            }
            Rectangle { Layout.fillWidth: true; height: 1; color: root.border }
            RowLayout {
                Layout.fillWidth: true; spacing: 7
                TextField {
                    id: sourceSearch
                    Layout.fillWidth: true
                    text: root.importController ? root.importController.searchText : ""
                    placeholderText: qsTr("Search titles")
                    onTextChanged: if (root.importController) root.importController.setSearchText(text)
                    color: root.text; placeholderTextColor: root.muted
                    background: Rectangle { radius: 6; color: root.surface; border.color: sourceSearch.activeFocus ? root.accent : root.border }
                }
                ComboBox {
                    id: sourceTagFilter
                    Layout.preferredWidth: 150
                    model: (root.importController ? [qsTr("All tags")].concat(root.importController.sourceTags) : [qsTr("All tags")])
                    currentIndex: root.importController && root.importController.tagFilter.length > 0 ? Math.max(0, model.indexOf(root.importController.tagFilter)) : 0
                    onActivated: if (root.importController) root.importController.setTagFilter(index === 0 ? "" : currentText)
                }
                ComboBox {
                    Layout.preferredWidth: 120
                    model: [qsTr("Any priority"), qsTr("High"), qsTr("Medium"), qsTr("Low")]
                    currentIndex: root.importController ? Math.max(0, ["", "high", "medium", "low"].indexOf(root.importController.priorityFilter)) : 0
                    onActivated: if (root.importController) root.importController.setPriorityFilter(["", "high", "medium", "low"][index])
                }
                ComboBox {
                    Layout.preferredWidth: 125
                    model: [qsTr("Any completion"), qsTr("Incomplete"), qsTr("Complete")]
                    currentIndex: root.importController ? root.importController.completionFilter + 1 : 0
                    onActivated: if (root.importController) root.importController.setCompletionFilter(index === 0 ? -1 : index === 1 ? 0 : 1)
                }
            }
            RowLayout {
                Layout.fillWidth: true; spacing: 8
                Text { text: qsTr("Import:"); color: root.secondary; font.pixelSize: 10 }
                ComboBox {
                    id: importMode
                    Layout.preferredWidth: 205
                    textRole: "text"; valueRole: "value"
                    model: [
                        { text: qsTr("Selected tasks"), value: "selected" },
                        { text: qsTr("Tasks matching tags"), value: "tags" },
                        { text: qsTr("All compatible tasks"), value: "all" }
                    ]
                }
                Text { text: qsTr("Destination Workspace:"); color: root.secondary; font.pixelSize: 10 }
                ComboBox {
                    id: destinationWorkspace
                    Layout.fillWidth: true
                    model: root.importController ? root.importController.workspaceOptions : []
                    textRole: "name"; valueRole: "id"
                    currentIndex: {
                        if (!root.importController) return -1
                        for (var wi = 0; wi < model.length; ++wi)
                            if (model[wi].id === root.importController.destinationWorkspaceId) return wi
                        return -1
                    }
                    onActivated: if (root.importController) root.importController.setDestinationWorkspaceId(currentValue)
                    Component.onCompleted: {
                        if (!root.importController) return
                        for (var i = 0; i < root.importController.workspaceOptions.length; ++i)
                            if (root.importController.workspaceOptions[i].id === root.importController.destinationWorkspaceId) { currentIndex = i; break }
                    }
                }
            }
            Rectangle {
                visible: importMode.currentValue === "tags"
                Layout.fillWidth: true
                Layout.preferredHeight: 56
                radius: 7; color: root.surface; border.color: root.border
                ScrollView {
                    anchors.fill: parent; anchors.margins: 4; clip: true
                    Flow {
                        width: parent.width
                        spacing: 4
                        Repeater {
                            model: root.importController ? root.importController.sourceTags : []
                            delegate: CheckBox {
                                required property string modelData
                                text: modelData
                                checked: root.importController ? root.importController.isImportTagSelected(modelData) : false
                                onToggled: if (root.importController) root.importController.toggleImportTag(modelData, checked)
                            }
                        }
                    }
                }
            }
            RowLayout {
                Layout.fillWidth: true; spacing: 7
                Text { text: qsTr("%1 source tasks · %2 selected").arg(root.importController ? root.importController.totalCount : 0).arg(root.importController ? root.importController.selectedCount : 0); color: root.secondary; font.pixelSize: 10; Layout.fillWidth: true }
                Button { text: qsTr("Select all visible"); enabled: root.importController && root.importController.tasks.length > 0; onClicked: root.importController.selectAllVisible() }
                Button { text: qsTr("Clear selection"); enabled: root.importController && root.importController.selectedCount > 0; onClicked: root.importController.clearSelection() }
            }
            Rectangle {
                Layout.fillWidth: true; Layout.fillHeight: true; Layout.minimumHeight: 180
                radius: 7; color: root.surface; border.color: root.border
                ListView {
                    id: sourceTaskList
                    anchors.fill: parent; anchors.margins: 4; clip: true; spacing: 3
                    model: root.importController ? root.importController.tasks : []
                    delegate: Rectangle {
                        required property var modelData
                        width: sourceTaskList.width; height: 54; radius: 5
                        color: modelData.selected ? root.activeSurface : "transparent"
                        RowLayout {
                            anchors.fill: parent; anchors.leftMargin: 6; anchors.rightMargin: 6; spacing: 7
                            CheckBox {
                                checked: modelData.selected
                                onToggled: if (root.importController) root.importController.selectTask(modelData.sourceTaskId, checked)
                            }
                            ColumnLayout {
                                Layout.fillWidth: true; spacing: 2
                                Text { text: modelData.title; color: root.text; font.pixelSize: 11; elide: Text.ElideRight; Layout.fillWidth: true }
                                Text {
                                    text: qsTr("#%1 · %2 · %3 priority · %4").arg(modelData.sourceTaskId).arg(modelData.completed ? qsTr("Complete") : qsTr("Incomplete")).arg(modelData.priority).arg((modelData.tags || []).join(", ") || qsTr("no tags"))
                                    color: root.muted; font.pixelSize: 9; elide: Text.ElideRight; Layout.fillWidth: true
                                }
                            }
                            Text { visible: modelData.importCount > 0; text: qsTr("Imported ×%1").arg(modelData.importCount); color: root.warning; font.pixelSize: 9 }
                            ToolButton { text: qsTr("Preview"); onClicked: if (root.importController) { root.importController.previewTask(modelData.sourceTaskId); sourcePreviewDialog.open() } }
                        }
                    }
                    Text { anchors.centerIn: parent; visible: sourceTaskList.count === 0 && root.importController && root.importController.verified && !root.importController.loading; text: qsTr("No tasks match these filters."); color: root.muted; font.pixelSize: 11 }
                    BusyIndicator { anchors.centerIn: parent; running: root.importController ? root.importController.loading : false; visible: running }
                }
            }
            RowLayout {
                Layout.fillWidth: true; spacing: 8
                Button { text: qsTr("Previous"); enabled: root.importController && root.importController.page > 0 && !root.importController.loading; onClicked: root.importController.previousPage() }
                Text { text: root.importController ? qsTr("Page %1 · %2 tasks").arg(root.importController.page + 1).arg(root.importController.totalCount) : ""; color: root.muted; font.pixelSize: 9; Layout.fillWidth: true; horizontalAlignment: Text.AlignHCenter }
                Button { text: qsTr("Next"); enabled: root.importController && (root.importController.page + 1) * root.importController.pageSize < root.importController.totalCount && !root.importController.loading; onClicked: root.importController.nextPage() }
                Button {
                    text: root.importController && root.importController.loading && !root.importController.importing ? qsTr("Preparing…") : qsTr("Review import…")
                    enabled: root.importController && root.importController.verified && !root.importController.loading && !root.importController.importing && root.importController.destinationWorkspaceId.length > 0
                    onClicked: root.importController.prepareImport(importMode.currentValue)
                    background: Rectangle { radius: 6; color: parent.enabled ? root.accent : root.activeSurface }
                    contentItem: Text { text: parent.text; color: parent.enabled ? "white" : root.muted; font.pixelSize: 10; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter; leftPadding: 10; rightPadding: 10 }
                }
            }
            ProgressBar { Layout.fillWidth: true; visible: root.importController && root.importController.importing; from: 0; to: 100; value: root.importController ? root.importController.progress : 0 }
            RowLayout {
                Layout.fillWidth: true
                Text { Layout.fillWidth: true; visible: root.importController && root.importController.importing; text: root.importController ? root.importController.progressMessage : ""; color: root.secondary; font.pixelSize: 9; wrapMode: Text.WordWrap }
                Button { visible: root.importController && root.importController.importing; text: qsTr("Cancel import"); onClicked: root.importController.cancelImport() }
            }
            Text { Layout.fillWidth: true; visible: root.importController && root.importController.error.length > 0; text: root.importController ? root.importController.error : ""; color: root.error; font.pixelSize: 10; wrapMode: Text.WordWrap }
            Text { Layout.fillWidth: true; visible: root.importController && root.importController.resultMessage.length > 0; text: root.importController ? root.importController.resultMessage : ""; color: root.success; font.pixelSize: 10; wrapMode: Text.WordWrap }
        }
    }

    Dialog {
        id: sourcePreviewDialog
        modal: true; title: qsTr("SomethingQt task preview")
        width: Math.min(700, root.width + 250); height: Math.min(650, root.height + 220)
        anchors.centerIn: Overlay.overlay
        standardButtons: Dialog.Close
        background: Rectangle { color: root.panel; radius: 12; border.color: root.borderStrong }
        contentItem: ScrollView {
            clip: true
            ColumnLayout {
                width: sourcePreviewDialog.availableWidth; spacing: 8
                Text { text: root.importController && root.importController.preview.title ? root.importController.preview.title : qsTr("Loading task…"); color: root.text; font.pixelSize: 17; font.weight: Font.DemiBold; wrapMode: Text.WordWrap; Layout.fillWidth: true }
                Text { text: root.importController ? qsTr("Source ID %1 · revision %2 · %3 · %4 priority · %5 difficulty · deadline %6").arg(root.importController.preview.sourceTaskId || "—").arg(root.importController.preview.revision || "—").arg(root.importController.preview.completed ? qsTr("Complete") : qsTr("Incomplete")).arg(root.importController.preview.priority || "—").arg(root.importController.preview.difficulty || qsTr("not set")).arg(root.importController.preview.dueDate || qsTr("none")) : ""; color: root.secondary; font.pixelSize: 10; wrapMode: Text.WordWrap; Layout.fillWidth: true }
                Text { text: qsTr("Description"); color: root.secondary; font.pixelSize: 10; font.weight: Font.DemiBold }
                Text { text: root.importController && root.importController.preview.description !== undefined ? root.importController.preview.description : ""; color: root.text; font.pixelSize: 10; wrapMode: Text.Wrap; textFormat: Text.PlainText; Layout.fillWidth: true }
                Text { text: qsTr("Tags: %1").arg(root.importController ? (root.importController.preview.tags || []).join(", ") || qsTr("none") : ""); color: root.secondary; font.pixelSize: 10; wrapMode: Text.WordWrap; Layout.fillWidth: true }
                Text { text: qsTr("Original timestamps: created %1 · updated %2").arg(root.importController ? root.importController.preview.createdAt || "—" : "—").arg(root.importController ? root.importController.preview.updatedAt || "—" : "—"); color: root.muted; font.pixelSize: 9; wrapMode: Text.WordWrap; Layout.fillWidth: true }
                Text { text: qsTr("Subtasks (%1)").arg(root.importController ? (root.importController.preview.subtasks || []).length : 0); color: root.secondary; font.pixelSize: 10; font.weight: Font.DemiBold }
                Repeater {
                    model: root.importController ? (root.importController.preview.subtasks || []) : []
                    delegate: Text {
                        required property var modelData
                        text: qsTr("%1. %2 %3 · source ID %4").arg(modelData.position).arg(modelData.completed ? "✓" : "○").arg(modelData.title).arg(modelData.sourceId)
                        color: root.text; font.pixelSize: 10; wrapMode: Text.WordWrap; Layout.fillWidth: true
                    }
                }
            }
        }
    }

    Dialog {
        id: importConfirmationDialog
        modal: true; title: qsTr("Confirm task import")
        width: 460; anchors.centerIn: Overlay.overlay
        standardButtons: Dialog.Cancel | Dialog.Ok
        background: Rectangle { color: root.panel; radius: 12; border.color: root.borderStrong }
        onOpened: additionalCopies.checked = false
        contentItem: ColumnLayout {
            spacing: 9
            Text {
                Layout.fillWidth: true
                text: root.importController ? qsTr("Import up to %1 tasks into %2?").arg(root.importController.expectedCount).arg(root.importController.destinationWorkspaceName) : ""
                color: root.text; font.pixelSize: 12; wrapMode: Text.WordWrap
            }
            Text { Layout.fillWidth: true; text: qsTr("Existing source tasks are skipped by default. Imported tasks become independent DevHub copies; later changes or deletion in SomethingQt will not synchronize."); color: root.secondary; font.pixelSize: 10; wrapMode: Text.WordWrap }
            CheckBox { id: additionalCopies; text: qsTr("Import another independent copy of tasks already imported here"); checked: false }
        }
        onAccepted: if (root.importController) root.importController.startImport(additionalCopies.checked)
    }

    Connections {
        target: root.importController
        ignoreUnknownSignals: true
        function onConfirmationReady(expectedCount) { importConfirmationDialog.open() }
    }

    function sourceTypeText() { return (detail.sourceType || "local") === "local" ? qsTr("Local") : qsTr("Imported") }
    function sortBy(field) {
        if (sortField === field) sortDescending = !sortDescending
        else { sortField = field; sortDescending = false }
        plannerController.sortList(field, sortDescending)
    }
}
