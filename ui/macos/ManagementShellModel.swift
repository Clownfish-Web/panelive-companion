import SwiftUI

/// Main-thread delivery state. Policy, validation, and backend access remain in the bridge.
@MainActor
final class ManagementShellModel: ObservableObject {
    let bridge: PLManagementBridge
    let buildVersion: String
    private let exportReport: (String) -> Void
    @Published private(set) var page: PLManagementPage
    @Published private(set) var revision = 0
    @Published var clientQuery = ""
    @Published var selectedClientIdentifier: String?
    @Published var pendingNavigation: PLManagementPage?
    @Published private(set) var pairingPreviewSeconds = 0
    private let pairingPreview = PLPairingPreview()
    private var pairingPreviewTimer: Timer?
    private(set) var visible = false

    init(bridge: PLManagementBridge, buildVersion: String,
         exportReport: @escaping (String) -> Void) {
        self.bridge = bridge
        self.buildVersion = buildVersion
        self.exportReport = exportReport
        page = bridge.initialPage
        if page == .teleprompter { bridge.openTeleprompterLibrary() }
        bridge.presentationChanged = { [weak self] _ in
            guard let self, self.visible else { return }
            self.publishCommandResult()
        }
    }

    func select(_ requested: PLManagementPage) {
        bridge.checkTeleprompterUnsavedChanges { [weak self] dirty in
            // The native facade always delivers this completion on the main thread.
            MainActor.assumeIsolated {
                guard let self else { return }
                if dirty && requested != self.page {
                    self.pendingNavigation = requested
                } else {
                    self.navigate(requested)
                }
            }
        }
    }

    func discardAndNavigate() {
        guard let requested = pendingNavigation else { return }
        pendingNavigation = nil
        bridge.closeTeleprompter()
        navigate(requested)
    }

    private func navigate(_ requested: PLManagementPage) {
        let destination = bridge.visiblePage(for: requested)
        if destination != page {
            bridge.closeTeleprompter()
            if destination == .teleprompter { bridge.openTeleprompterLibrary() }
        }
        closePairingPreview()
        page = destination
        revision += 1
    }

    func saveReport() {
        guard bridge.isValid else { return }
        bridge.requestAudioDebugReport { [weak self] report in
            guard let self, self.bridge.isValid else { return }
            self.publishCommandResult()
            guard self.bridge.lastError.isEmpty else { return }
            self.exportReport(report)
        }
    }

    func pairClient() {
        closePairingPreview()
        bridge.pairClient()
        publishCommandResult()
    }

    func publishCommandResult() {
        page = bridge.visiblePage(for: page)
        updatePairingPreview()
        revision += 1
    }

    func openPairingPreview() {
        closePairingPreview()
        guard visible && bridge.isValid && page == .clients, let code = bridge.pairing else { return }
        guard pairingPreview.open(code) else { return }
        updatePairingPreview()
        guard pairingPreviewSeconds > 0 else { return }
        let timer = Timer(timeInterval: 0.1, repeats: true) { [weak self] _ in
            MainActor.assumeIsolated { self?.updatePairingPreview() }
        }
        RunLoop.main.add(timer, forMode: .common)
        pairingPreviewTimer = timer
    }

    func closePairingPreview() {
        pairingPreviewTimer?.invalidate()
        pairingPreviewTimer = nil
        pairingPreview.close()
        if pairingPreviewSeconds != 0 { pairingPreviewSeconds = 0 }
    }

    private func updatePairingPreview() {
        guard visible && bridge.isValid && page == .clients else {
            closePairingPreview()
            return
        }
        let seconds = pairingPreview.secondsRemaining(for: bridge.pairing)
        if seconds == 0 { closePairingPreview() }
        else if seconds != pairingPreviewSeconds { pairingPreviewSeconds = seconds }
    }

    func activate() {
        visible = true
        bridge.refresh()
        publishCommandResult()
    }

    func hide() {
        bridge.cancelClientIdentification()
        closePairingPreview()
        visible = false
        revision += 1
    }

    func refreshVisible() {
        guard visible && bridge.isValid else { return }
        bridge.refresh()
        publishCommandResult()
    }

    func openNetworkRecovery() {
        bridge.setInterfaceMode(.advanced)
        select(.companionSettings)
    }

    func invalidate() {
        closePairingPreview()
        visible = false
        bridge.invalidate()
        revision += 1
    }

    isolated deinit {
        pairingPreviewTimer?.invalidate()
    }
}
