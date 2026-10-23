import QtQuick
import QtQuick.Controls
import QtTest
import "../../resources/qml"

Item {
    id: fixture
    width: 1160; height: 820

    ListModel { id: listTasks }
    ListModel { id: emptyTasks }

    property QtObject themeController: QtObject {
        property color accent: "#4A9FFF"
        property color accentHover: "#71B4FF"
        property color raisedSurface: "#17304E"
        property color panelSurface: "#10233D"
        property color borderSubtle: "#2B4562"
        property color borderStrong: "#3B638C"
        property color hoverSurface: "#1C3A5C"
        property color activeSurface: "#18436B"
        property color textPrimary: "#EFF6FF"
        property color textSecondary: "#B4C8DF"
        property color textMuted: "#829AB3"
        property color success: "#35C98A"
        property color warning: "#F5BC59"
        property color error: "#FF6875"
        property color info: "#64B6FF"
    }
    property QtObject plannerController: QtObject {
        property string workspaceId: "fixture-workspace"
        property string workspaceName: "DevHub"
        property bool hasWorkspace: true
        property bool collapsed: false
        property bool loading: false
        property string view: "board"
        property int totalCount: 0
        property int inProgressCount: 0
        property int completedCount: 0
        property int overdueCount: 0
        property int progressPercent: 0
        property string nearestDeadline: "No upcoming deadline"
        property string branchStatus: "6 local branches available."
        property string error: ""
        property var milestones: [{ id: "release", title: "First release", description: "", startDate: "", dueDate: "",
            completed: false, progress: 0, completedTaskCount: 0, taskCount: 0 }]
        property var branchOptions: []
        property var serviceOptions: []
        property bool branchLoading: false
        property string milestoneFilter: ""
        property string gitBranchFilter: ""
        property int blockedFilter: -1
        property int overdueFilter: -1
        property string originFilter: ""
        property string selectedDate: ""
        property var savedTask: null
        property string lastMovedTaskId: ""
        property string lastMovedStatus: ""
        property var listModel: listTasks
        property var calendarModel: emptyTasks
        property var undatedModel: emptyTasks
        signal changed()
        signal createTaskRequested()
        signal focusSearchRequested()
        signal openTaskDetailsRequested(string taskId)
        signal actionRunsChanged(string taskId)
        function clearError() { error = "" }
        function dependencyChoices(taskId) { return [] }
        function countForDate(date) { return 0 }
        function boardModel(status) { return emptyTasks }
        function statusCount(status) { return 0 }
        function saveTask(values) { savedTask = values; return true }
        function moveTask(taskId, status) { lastMovedTaskId = taskId; lastMovedStatus = status; return true }
        function setCollapsed(value) { collapsed = value }
        function setView(value) { view = value }
        function setSearchText(value) {}
        function setTagFilter(value) {}
    }

    Rectangle { anchors.fill: parent; color: "#08111F" }
    PlannerPanel {
        id: panel
        x: 20; y: 20; width: parent.width - 40; height: implicitHeight
    }
    Component {
        id: unhostedPanel
        PlannerPanel { width: 0; height: 0 }
    }

    TestCase {
        name: "PlannerLayout"
        when: windowShown

        function settle() { wait(40) }
        function dialog(name) { return findChild(panel, name) }
        function init() {
            failOnWarning(/ReferenceError|Unable to assign|Binding loop/)
            fixture.Window.window.width = 1160; fixture.Window.window.height = 820
            fixture.width = 1160; fixture.height = 820
            panel.width = fixture.width - 40
            plannerController.collapsed = false
            plannerController.view = "board"
            plannerController.totalCount = 0
            listTasks.clear()
            plannerController.savedTask = null
            panel.editorDirty = false
            dialog("plannerDiscardDialog").close()
            dialog("plannerTaskEditor").close()
            settle()
        }
        function test_headerFits_data() {
            return [{ tag: "wide", width: 1440 }, { tag: "normal", width: 720 }, { tag: "narrow", width: 420 }]
        }
        function test_constructionBeforeWindowSizing() {
            failOnWarning(/Binding loop/)
            var detached = unhostedPanel.createObject(null)
            verify(detached !== null)
            detached.destroy()
        }
        function test_headerFits(data) {
            panel.width = data.width
            settle()
            var header = findChild(panel, "plannerHeader")
            var actions = findChild(panel, "plannerHeaderActions")
            verify(header.height > 0)
            for (var i = 0; i < actions.children.length; ++i) {
                var button = actions.children[i]
                if (!button.visible || button.width === 0) continue
                var point = button.mapToItem(header, 0, 0)
                verify(point.x >= 0 && point.y >= 0)
                verify(point.x + button.width <= header.width + 1)
                verify(point.y + button.height <= header.height + 1)
            }
            plannerController.collapsed = true
            settle()
            verify(panel.height >= header.height)
            verify(header.height < 110)
        }
        function test_editorFits_data() {
            return [{ tag: "desktop", width: 1160, height: 820 }, { tag: "small-window", width: 640, height: 480 }]
        }
        function test_editorFits(data) {
            // The overlay follows the test window; resize it as well as its content.
            fixture.Window.window.width = data.width
            fixture.Window.window.height = data.height
            panel.openEditor("")
            var editor = dialog("plannerTaskEditor")
            tryCompare(editor, "opened", true)
            settle()
            verify(editor.x >= 0 && editor.y >= 0)
            verify(editor.x + editor.width <= data.width)
            verify(editor.y + editor.height <= data.height, "Editor bottom " + (editor.y + editor.height) + " exceeds " + data.height)
            var title = findChild(panel, "plannerTaskTitle")
            var actions = findChild(panel, "plannerTaskEditorActions")
            var actionsPos = actions.mapToItem(editor.contentItem, 0, 0)
            verify(actionsPos.y + actions.height <= editor.contentItem.height + 1, "Actions bottom " + (actionsPos.y + actions.height) + " exceeds content " + editor.contentItem.height)
            verify(title.width <= editor.availableWidth)
            compare(title.background.color, themeController.raisedSurface)
            var scroll = findChild(panel, "plannerTaskEditorScroll")
            verify(scroll.contentHeight > scroll.availableHeight)
            scroll.contentItem.contentY = scroll.contentHeight - scroll.availableHeight
            settle()
            verify(scroll.contentItem.contentY > 0)
            // Scrolling the form keeps the save/cancel row fixed in place.
            compare(actions.mapToItem(editor.contentItem, 0, 0).y, actionsPos.y)
            fixture.Window.window.width = 1160
            fixture.Window.window.height = 820
        }
        function test_comboModelsAndDiscard() {
            panel.openEditor("")
            var editor = dialog("plannerTaskEditor")
            tryCompare(editor, "opened", true)
            var priority = findChild(panel, "plannerTaskPriority")
            priority.popup.open()
            tryCompare(priority.popup, "opened", true)
            settle()
            compare(priority.popup.contentItem.itemAtIndex(0).contentItem.text, "Low")
            priority.popup.close()
            var title = findChild(panel, "plannerTaskTitle")
            title.text = "Unsaved UI test"
            panel.requestEditorClose()
            var discard = dialog("plannerDiscardDialog")
            tryCompare(discard, "opened", true)
            mouseClick(discard.standardButton(Dialog.Cancel))
            tryCompare(discard, "visible", false)
            compare(editor.visible, true)
            compare(title.text, "Unsaved UI test")
            panel.requestEditorClose()
            tryCompare(discard, "opened", true)
            mouseClick(discard.standardButton(Dialog.Discard))
            tryCompare(editor, "visible", false)
            tryCompare(discard, "visible", false)
            compare(plannerController.savedTask, null)
        }
        function test_themeChanges() {
            panel.openEditor("")
            var editor = dialog("plannerTaskEditor")
            tryCompare(editor, "opened", true)
            var title = findChild(panel, "plannerTaskTitle")
            var priority = findChild(panel, "plannerTaskPriority")
            var originalPanel = themeController.panelSurface
            var originalSurface = themeController.raisedSurface
            themeController.panelSurface = "#17131A"
            themeController.raisedSurface = "#1D1720"
            settle()
            compare(editor.background.color, themeController.panelSurface)
            compare(title.background.color, themeController.raisedSurface)
            compare(priority.background.color, themeController.raisedSurface)
            priority.popup.open()
            tryCompare(priority.popup, "opened", true)
            compare(priority.popup.background.color, themeController.panelSurface)
            priority.popup.close()
            themeController.panelSurface = originalPanel
            themeController.raisedSurface = originalSurface
        }
        function test_populatedListRows_data() {
            return [{ tag: "wide", width: 1380 }, { tag: "narrow", width: 520 }]
        }
        function test_populatedListRows(data) {
            fixture.Window.window.width = Math.max(640, data.width + 40)
            fixture.width = fixture.Window.window.width
            panel.width = data.width
            plannerController.view = "list"
            listTasks.append({ taskId: "completed-task", title: "Testing task", completed: true,
                priority: "low", difficulty: "", dueDate: "2026-10-23", status: "done",
                gitBranch: "", tags: [], subtaskCount: 0, overdue: false, blocked: false })
            listTasks.append({ taskId: "active-task", title: "An active task", completed: false,
                priority: "high", difficulty: "hard", dueDate: "", status: "todo",
                gitBranch: "adding_planner", tags: [], subtaskCount: 2, overdue: false, blocked: true })
            plannerController.totalCount = listTasks.count
            var list = findChild(panel, "plannerTaskList")
            tryCompare(list, "count", 2)
            settle()
            compare(list.itemAtIndex(0).color, themeController.raisedSurface)
            compare(list.itemAtIndex(1).color, themeController.panelSurface)
            var done = findChild(panel, "plannerListCompletedHeader")
            verify(!done.contentItem.truncated, "The Done column heading must fit")
            var headerCells = done.parent.children
            for (var r = 0; r < list.count; ++r) {
                var row = list.itemAtIndex(r)
                var cells = row.children[0].children
                for (var c = 0; c <= 4; ++c) {
                    if (!cells[c].visible) continue
                    compare(Math.round(cells[c].mapToItem(row, 0, 0).x),
                        Math.round(headerCells[c].mapToItem(done.parent.parent, 0, 0).x),
                        "Task cells must align with their headings")
                }
                verify(cells[1].height <= row.height, "Task text must fit in the row")
            }
            // A status selection uses the selected option, independently of the row index.
            list.itemAtIndex(1).children[0].children[4].activated(3)
            compare(plannerController.lastMovedTaskId, "active-task")
            compare(plannerController.lastMovedStatus, "in_review")
        }
    }
}
