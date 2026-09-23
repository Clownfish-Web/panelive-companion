import AppKit

/// Main-actor presentation only. The application owns authorization and deduplication.
/// Hiding preserves pending requests; a response, matching cancellation, or invalidation completes once.
@MainActor
final class ManagementApprovalPrompt {
    enum Kind { case role, globalSetting }

    private struct Pending {
        let kind: Kind
        let deviceIdentifier: String
        let requestIdentifier: String
        let alert: NSAlert
        let decision: (Bool) -> Void
    }

    private weak var window: NSWindow?
    private var pending: Pending?
    private var valid = true
    private var cancelling = false

    func requestRoleChange(deviceIdentifier: String, requestIdentifier: String,
                           deviceName: String, requestedRole: String, backendName: String,
                           window: NSWindow, decision: @escaping (Bool) -> Void) -> Bool {
        present(kind: .role, deviceIdentifier: deviceIdentifier, requestIdentifier: requestIdentifier,
                title: "Client role request",
                message: "Allow '\(deviceName)' to become \(requestedRole) for \(backendName)?",
                window: window, decision: decision)
    }

    func requestGlobalSettingChange(deviceIdentifier: String, requestIdentifier: String,
                                    deviceName: String, settingName: String, warning: String,
                                    window: NSWindow, decision: @escaping (Bool) -> Void) -> Bool {
        present(kind: .globalSetting, deviceIdentifier: deviceIdentifier,
                requestIdentifier: requestIdentifier, title: "Global setting request",
                message: "'\(deviceName)' requested \(settingName).\n\n\(warning)",
                window: window, decision: decision)
    }

    func cancel(kind: Kind, deviceIdentifier: String, requestIdentifier: String) {
        guard let current = pending, current.kind == kind,
              current.deviceIdentifier == deviceIdentifier,
              current.requestIdentifier == requestIdentifier else { return }
        cancelCurrent()
    }

    func cancelCurrent() {
        guard let current = pending else { return }
        pending = nil
        cancelling = true
        defer { cancelling = false }
        window?.endSheet(current.alert.window, returnCode: .cancel)
        current.alert.window.orderOut(nil)
        current.decision(false)
    }

    func invalidate() {
        valid = false
        cancelCurrent()
        window = nil
    }

    private func present(kind: Kind, deviceIdentifier: String, requestIdentifier: String,
                         title: String, message: String, window: NSWindow,
                         decision: @escaping (Bool) -> Void) -> Bool {
        guard valid, !cancelling, pending == nil, window.attachedSheet == nil else { return false }
        let alert = NSAlert()
        alert.messageText = title
        alert.informativeText = message
        alert.addButton(withTitle: "Deny")
        alert.addButton(withTitle: "Approve").keyEquivalent = ""
        alert.buttons[0].keyEquivalent = "\r"
        alert.window.defaultButtonCell = alert.buttons[0].cell as? NSButtonCell
        self.window = window
        pending = Pending(kind: kind, deviceIdentifier: deviceIdentifier,
                          requestIdentifier: requestIdentifier, alert: alert, decision: decision)
        alert.beginSheetModal(for: window) { [weak self, weak alert] response in
            guard let self, let current = self.pending, current.alert === alert else { return }
            self.pending = nil
            current.decision(response == .alertSecondButtonReturn)
        }
        return true
    }
}
