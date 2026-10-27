import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Window {
    id: root
    width: 1160
    height: authFlow.accountExists ? 820 : 900
    minimumWidth: 720
    minimumHeight: 620
    visible: false
    title: authFlow.accountExists ? qsTr("Welcome back — DevHub") : qsTr("Create DevHub Account")
    color: Theme.background
    flags: Qt.Window | Qt.WindowTitleHint | Qt.WindowSystemMenuHint | Qt.WindowMinimizeButtonHint | Qt.WindowCloseButtonHint

    readonly property color accent: Theme.accent
    readonly property color surface: Theme.backgroundRaised
    readonly property color surfaceRaised: Theme.surfaceRaised
    readonly property color border: Theme.border
    readonly property color secondary: Theme.textSecondary
    readonly property color foreground: Theme.text
    readonly property color borderSoft: Theme.borderSoft
    property bool keepSession: authFlow.keepSessionDefault

    Rectangle { anchors.fill: parent; gradient: Gradient {
        GradientStop { position: 0; color: Theme.activeSurface }
        GradientStop { position: 0.5; color: Theme.background }
        GradientStop { position: 1; color: Theme.sidebarSurface }
    } }
    Rectangle { width: parent.width * 1.25; height: 90; x: -width * 0.16; y: -height * 0.60; color: Theme.activeSurface; opacity: 0.58 }
    Rectangle { width: parent.width * 1.1; height: 120; x: parent.width * 0.50; y: parent.height * 0.20; rotation: -41; color: Theme.background; border.color: Theme.border; border.width: 1; opacity: 0.74 }
    Rectangle { width: parent.width * 0.82; height: 90; x: -parent.width * 0.28; y: parent.height * 0.79; rotation: -41; color: Theme.background; border.color: Theme.border; border.width: 1; opacity: 0.75 }
    Rectangle { anchors.fill: parent; anchors.margins: 1; color: "transparent"; border.color: Theme.borderSoft; radius: 0 }

    RowLayout {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.leftMargin: 24
        anchors.rightMargin: 24
        anchors.topMargin: 24
        height: 44
        z: 2
        spacing: 0
        RowLayout {
            spacing: 10
            BrandMark { Layout.preferredWidth: 40; Layout.preferredHeight: 40 }
            Label { font.family: Theme.fontFamily; text: "DevHub"; color: root.foreground; font.pixelSize: 23; font.weight: Font.DemiBold }
        }
        Item { Layout.fillWidth: true }
        Label {
            font.family: Theme.fontFamily
            text: qsTr("A better developer workflow, locally.")
            color: root.secondary
            font.pixelSize: 15
            Layout.alignment: Qt.AlignTop | Qt.AlignRight
        }
    }

    Rectangle {
        id: authCard
        width: Math.min(650, root.width - 48)
        height: Math.min(root.height - 116, authFlow.accountExists ? 690 : 760)
        anchors.centerIn: parent
        radius: 0
        color: Theme.surface
        border.color: root.border
        border.width: 1
        Rectangle { anchors.fill: parent; anchors.margins: 1; radius: 0; color: "transparent"; border.color: Theme.borderSoft }

        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 38
            spacing: 12
            Item { Layout.fillHeight: true; Layout.maximumHeight: 12 }
            BrandMark { Layout.alignment: Qt.AlignHCenter; Layout.preferredWidth: 54; Layout.preferredHeight: 54 }
            Label {
                font.family: Theme.fontFamily
                Layout.fillWidth: true
                text: "DevHub"
                color: root.foreground
                horizontalAlignment: Text.AlignHCenter
                font.pixelSize: 30
                font.weight: Font.DemiBold
            }
            Rectangle {
                Layout.alignment: Qt.AlignHCenter
                Layout.preferredWidth: 72
                Layout.preferredHeight: 72
                radius: 0
                color: root.surfaceRaised
                border.color: root.border
                LockMark { anchors.centerIn: parent; width: 38; height: 38 }
            }
            Label {
                font.family: Theme.fontFamily
                Layout.fillWidth: true
                text: authFlow.accountExists ? qsTr("Unlock Workspace Hub") : qsTr("Create your local account")
                color: root.foreground
                horizontalAlignment: Text.AlignHCenter
                font.pixelSize: 23
                font.weight: Font.DemiBold
            }
            Label {
                font.family: Theme.fontFamily
                Layout.fillWidth: true
                Layout.maximumWidth: 500
                Layout.alignment: Qt.AlignHCenter
                text: authFlow.accountExists
                      ? qsTr("Enter your additional password to access your local workspaces, notes, logs, and database.")
                      : qsTr("Create an account to protect access to your local developer workspace.")
                color: root.secondary
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.WordWrap
                font.pixelSize: 15
            }
            Item { Layout.preferredHeight: 8 }
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 14
                visible: authFlow.accountExists
                Label { font.family: Theme.fontFamily; text: qsTr("Master Password"); color: root.foreground; font.pixelSize: 14 }
                PasswordField {
                    id: signInPassword
                    Layout.fillWidth: true
                    implicitHeight: 48
                    placeholderText: qsTr("Enter your password")
                    rightPadding: 50
                    Keys.onReturnPressed: unlockButton.clicked()
                }
                RowLayout {
                    Layout.fillWidth: true
                    CheckBox { font.family: Theme.fontFamily; id: remember; checked: root.keepSession; onToggled: root.keepSession = checked; text: qsTr("Remember for this session"); palette.text: root.foreground }
                    Item { Layout.fillWidth: true }
                    Button {
                        font.family: Theme.fontFamily
                        id: forgotPasswordButton
                        flat: true
                        text: qsTr("Forgot password?")
                        onClicked: { authFlow.clearMessage(); resetDialog.open() }
                        contentItem: Label { font.family: Theme.fontFamily; text: forgotPasswordButton.text; color: root.accent; horizontalAlignment: Text.AlignRight }
                    }
                }
            }
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 11
                visible: !authFlow.accountExists
                Label { font.family: Theme.fontFamily; text: qsTr("Username"); color: root.foreground; font.pixelSize: 14 }
                TextField { font.family: Theme.fontFamily; id: createUsername; Layout.fillWidth: true; implicitHeight: 46; maximumLength: 128; placeholderText: qsTr("Username"); color: root.foreground; background: Rectangle { radius: 0; color: Theme.background; border.color: createUsername.activeFocus ? Theme.focus : root.border } }
                Label { font.family: Theme.fontFamily; text: qsTr("Password"); color: root.foreground; font.pixelSize: 14 }
                PasswordField { id: createPassword; Layout.fillWidth: true; placeholderText: qsTr("Password") }
                Label { font.family: Theme.fontFamily; text: qsTr("Confirm password"); color: root.foreground; font.pixelSize: 14 }
                PasswordField {
                    id: confirmPassword
                    Layout.fillWidth: true
                    placeholderText: qsTr("Confirm password")
                    Keys.onReturnPressed: createButton.clicked()
                }
                Label { font.family: Theme.fontFamily; Layout.fillWidth: true; text: qsTr("Your account is stored locally. DevHub controls access to this app; it does not encrypt your workspace files, source code, databases, logs, or notes."); wrapMode: Text.WordWrap; color: root.secondary; font.pixelSize: 12 }
            }
            Label { font.family: Theme.fontFamily; Layout.fillWidth: true; visible: authFlow.message.length > 0; text: authFlow.message; wrapMode: Text.WordWrap; color: Theme.error; font.pixelSize: 13 }
            Item { Layout.fillHeight: true; Layout.minimumHeight: 2 }
            RowLayout {
                Layout.fillWidth: true
                spacing: 12
                Button {
                    font.family: Theme.fontFamily
                    id: unlockButton
                    visible: authFlow.accountExists
                    Layout.fillWidth: true; Layout.preferredWidth: 7; implicitHeight: 50
                    enabled: !authFlow.busy && authFlow.retryRemaining === 0
                    text: authFlow.busy ? qsTr("Unlocking…") : authFlow.retryRemaining > 0 ? qsTr("Try again in %1 s").arg(Math.ceil(authFlow.retryRemaining / 1000)) : qsTr("Unlock DevHub")
                    onClicked: { const password = signInPassword.text; signInPassword.clear(); authFlow.signIn(authFlow.username, password, root.keepSession) }
                    contentItem: RowLayout {
                        spacing: 8
                        Item { Layout.fillWidth: true }
                        LockMark { Layout.preferredWidth: 20; Layout.preferredHeight: 20; color: Theme.onAccent }
                        Label { font.family: Theme.fontFamily; text: unlockButton.text; color: Theme.onAccent; font.pixelSize: 16; font.weight: Font.DemiBold }
                        Item { Layout.fillWidth: true }
                    }
                    background: Rectangle { radius: 0; color: unlockButton.down ? Qt.darker(root.accent, 1.2) : unlockButton.hovered ? Theme.accentSecondary : root.accent; border.width: unlockButton.visualFocus ? 2 : 1; border.color: unlockButton.visualFocus ? Theme.focus : root.accent; opacity: unlockButton.enabled ? 1 : 0.48 }
                }
                Button {
                    font.family: Theme.fontFamily
                    id: createButton
                    visible: !authFlow.accountExists
                    Layout.fillWidth: true; Layout.preferredWidth: authFlow.accountExists ? 7 : 400; implicitHeight: 50
                    enabled: !authFlow.busy
                    text: authFlow.busy ? qsTr("Creating account…") : qsTr("Create Account")
                    onClicked: { const p = createPassword.text; const c = confirmPassword.text; createPassword.clear(); confirmPassword.clear(); authFlow.createAccount(createUsername.text, p, c) }
                    contentItem: Label { font.family: Theme.fontFamily; text: createButton.text; color: Theme.onAccent; font.pixelSize: 16; font.weight: Font.DemiBold; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
                    background: Rectangle { radius: 0; color: createButton.down ? Qt.darker(root.accent, 1.2) : createButton.hovered ? Theme.accentSecondary : root.accent; border.width: createButton.visualFocus ? 2 : 1; border.color: createButton.visualFocus ? Theme.focus : root.accent; opacity: createButton.enabled ? 1 : 0.48 }
                }
                Button {
                    font.family: Theme.fontFamily
                    id: quitButton
                    Layout.preferredWidth: authFlow.accountExists ? 3 : 140
                    Layout.fillWidth: true
                    implicitHeight: 50
                    text: qsTr("Quit")
                    onClicked: authFlow.quit()
                    contentItem: Label { font.family: Theme.fontFamily; text: quitButton.text; color: root.foreground; font.pixelSize: 15; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
                    background: Rectangle { radius: 0; color: quitButton.down ? Theme.activeSurface : quitButton.hovered ? Theme.hoverSurface : Theme.surfaceRaised; border.width: quitButton.visualFocus ? 2 : 1; border.color: quitButton.visualFocus ? Theme.focus : Theme.border }
                }
            }
            Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: Theme.borderSoft; Layout.topMargin: 8; Layout.bottomMargin: 4 }
            RowLayout {
                Layout.alignment: Qt.AlignHCenter
                Label { font.family: Theme.fontFamily; text: "♢"; color: root.accent; font.pixelSize: 20 }
                Label { font.family: Theme.fontFamily; text: qsTr("Local-only protection for your developer workspace."); color: root.secondary; font.pixelSize: 13 }
            }
            Item { Layout.fillHeight: true; Layout.maximumHeight: 4 }
        }
    }

    Dialog {
        font.family: Theme.fontFamily
        id: resetDialog
        modal: true
        title: qsTr("Reset local DevHub profile")
        width: Math.min(480, root.width - 40)
        anchors.centerIn: parent
        standardButtons: Dialog.NoButton
        background: Rectangle { color: root.surface; border.color: root.border; radius: 0 }
        contentItem: ColumnLayout {
            spacing: 12
            Label {
                font.family: Theme.fontFamily
                Layout.fillWidth: true
                text: qsTr("DevHub has no email recovery. Resetting creates a new local profile and moves the old DevHub database and account folders into a timestamped recovery folder. Workspace folders, source files, and other external data stay where they are. The old profile is not imported automatically.")
                wrapMode: Text.WordWrap
                color: root.foreground
            }
            Label { font.family: Theme.fontFamily; text: qsTr("Type RESET to confirm this profile reset."); color: root.foreground }
            TextField { font.family: Theme.fontFamily; id: resetConfirmation; Layout.fillWidth: true; placeholderText: "RESET"; color: root.foreground; background: Rectangle { color: Theme.background; border.color: resetConfirmation.activeFocus ? Theme.focus : root.border; radius: 0 } }
            Label { font.family: Theme.fontFamily; Layout.fillWidth: true; visible: authFlow.message.length > 0; text: authFlow.message; wrapMode: Text.WordWrap; color: Theme.error }
            RowLayout {
                Item { Layout.fillWidth: true }
                Button {
                    font.family: Theme.fontFamily
                    id: cancelResetButton
                    text: qsTr("Cancel")
                    onClicked: resetDialog.close()
                    contentItem: Label { font.family: Theme.fontFamily; text: cancelResetButton.text; color: root.foreground; horizontalAlignment: Text.AlignHCenter }
                    background: Rectangle { radius: 0; color: Theme.surfaceRaised; border.color: Theme.border }
                }
                Button {
                    font.family: Theme.fontFamily
                    id: resetButton
                    text: qsTr("Reset Profile")
                    enabled: !authFlow.busy && resetConfirmation.text === "RESET"
                    onClicked: authFlow.resetProfile(resetConfirmation.text)
                    contentItem: Label { font.family: Theme.fontFamily; text: resetButton.text; color: root.foreground; horizontalAlignment: Text.AlignHCenter }
                    background: Rectangle { radius: 0; color: root.accent }
                }
            }
        }
        onClosed: { resetConfirmation.clear(); authFlow.clearMessage() }
    }

    Connections {
        target: authFlow
        function onAccountModeChanged() { resetDialog.close() }
    }
}
