import AppKit
import UniformTypeIdentifiers

/// Owns one save sheet or disk write. A confirmed write retains this owner until it finishes;
/// invalidation cancels selection and prevents any later UI delivery, but cannot undo a disk write.
@MainActor
final class ManagementReportExport {
    private weak var window: NSWindow?
    private var panel: NSSavePanel?
    private var writeTask: Task<Void, Never>?
    private var valid = true
    private var pendingFailure = false
    private var errorAlert: NSAlert?

    func present(contents: String, for window: NSWindow) {
        guard valid, panel == nil, writeTask == nil, window.attachedSheet == nil else { return }
        self.window = window
        let panel = NSSavePanel()
        panel.nameFieldStringValue = "panelive-audio-diagnostics.jsonl"
        panel.allowedContentTypes = [UTType(filenameExtension: "jsonl") ?? .plainText, .data]
        panel.allowsOtherFileTypes = true
        panel.canCreateDirectories = false
        self.panel = panel
        panel.beginSheetModal(for: window) { [weak self, weak panel] response in
            guard let self, let panel, self.panel === panel else { return }
            self.panel = nil
            guard self.valid, response == .OK, let url = panel.url else { return }
            self.write(contents: contents, to: url)
        }
    }

    func cancelSelection() {
        let current = panel
        panel = nil
        current?.cancel(nil)
    }

    func invalidate() {
        valid = false
        cancelSelection()
        if pendingFailure { NSLog("Could not save the audio diagnostics report") }
        pendingFailure = false
        if let alert = errorAlert {
            window?.endSheet(alert.window)
            alert.window.orderOut(nil)
            errorAlert = nil
        }
        window = nil
        // A previously confirmed write is allowed to finish; it will not show further UI.
    }

    private func write(contents: String, to url: URL) {
        writeTask = Task.detached { [self] in
            let failed: Bool
            do {
                try Self.writeReport(contents, to: url)
                failed = false
            } catch {
                failed = true
            }
            await completed(failed: failed)
        }
    }

    private func completed(failed: Bool) {
        writeTask = nil
        guard failed else { return }
        guard valid else {
            NSLog("Could not save the audio diagnostics report")
            return
        }
        pendingFailure = true
        showPendingError()
    }

    func showPendingError() {
        guard valid, pendingFailure, errorAlert == nil,
              let window, window.isVisible, window.attachedSheet == nil else { return }
        let alert = NSAlert()
        alert.alertStyle = .warning
        alert.messageText = "Could not save the audio diagnostics report"
        alert.addButton(withTitle: "OK")
        errorAlert = alert
        alert.beginSheetModal(for: window) { [weak self, weak alert] _ in
            guard let self, self.errorAlert === alert else { return }
            self.errorAlert = nil
            self.pendingFailure = false
        }
    }

    nonisolated static func writeReport(_ contents: String, to url: URL) throws {
        // Atomic replacement preserves an existing report if writing the new bytes fails.
        try Data(contents.utf8).write(to: url, options: .atomic)
    }
}
