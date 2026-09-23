import AppKit
import SwiftUI

/// The composition owner retains this window and invalidates it before backend teardown.
@MainActor
@objc(PLManagementWindow)
public final class ManagementWindow: NSObject, NSWindowDelegate {
    private let nativeWindow: NSWindow
    private let model: ManagementShellModel
    private var refreshTimer: Timer?
    private let reportExport: ManagementReportExport
    private let approvals = ManagementApprovalPrompt()

    @objc public init(bridge: PLManagementBridge, buildVersion: String) {
        let exporter = ManagementReportExport()
        let window = NSWindow(contentRect: NSRect(x: 0, y: 0, width: 1120, height: 760),
                              styleMask: [.titled, .closable, .miniaturizable, .resizable],
                              backing: .buffered, defer: false)
        reportExport = exporter
        nativeWindow = window
        model = ManagementShellModel(bridge: bridge, buildVersion: buildVersion,
                                     exportReport: { [weak window] contents in
            guard let window else { return }
            exporter.present(contents: contents, for: window)
        })
        super.init()
        nativeWindow.title = "PaneLive Companion"
        nativeWindow.isReleasedWhenClosed = false
        nativeWindow.appearance = NSAppearance(named: .darkAqua)
        nativeWindow.contentMinSize = NSSize(width: 1000, height: 600)
        nativeWindow.contentView = NSHostingView(rootView: ManagementShell(model: model))
        nativeWindow.delegate = self
        nativeWindow.center()
    }

    @objc public var isVisible: Bool { nativeWindow.isVisible }

    /// Returns false without calling decision if another sheet is active or the owner is invalid.
    /// The composition callback must catch C++ exceptions before returning across this ABI.
    @objc public func requestRoleChange(deviceIdentifier: String, requestIdentifier: String,
                                        deviceName: String, requestedRole: String, backendName: String,
                                        decision: @escaping (Bool) -> Void) -> Bool {
        guard model.bridge.isValid else { return false }
        let accepted = approvals.requestRoleChange(deviceIdentifier: deviceIdentifier,
            requestIdentifier: requestIdentifier, deviceName: deviceName, requestedRole: requestedRole,
            backendName: backendName, window: nativeWindow, decision: decision)
        if accepted { show() }
        return accepted
    }

    @objc public func requestGlobalSettingChange(deviceIdentifier: String, requestIdentifier: String,
                                                 deviceName: String, settingName: String, warning: String,
                                                 decision: @escaping (Bool) -> Void) -> Bool {
        guard model.bridge.isValid else { return false }
        let accepted = approvals.requestGlobalSettingChange(deviceIdentifier: deviceIdentifier,
            requestIdentifier: requestIdentifier, deviceName: deviceName, settingName: settingName,
            warning: warning, window: nativeWindow, decision: decision)
        if accepted { show() }
        return accepted
    }

    @objc public func cancelRoleChange(deviceIdentifier: String, requestIdentifier: String) {
        approvals.cancel(kind: .role, deviceIdentifier: deviceIdentifier,
                         requestIdentifier: requestIdentifier)
    }

    @objc public func cancelGlobalSettingChange(deviceIdentifier: String, requestIdentifier: String) {
        approvals.cancel(kind: .globalSetting, deviceIdentifier: deviceIdentifier,
                         requestIdentifier: requestIdentifier)
    }

    @objc public func showPairing(payload: String, expiresUnixSeconds: UInt64) {
        model.bridge.showPairingPayload(payload, expiresUnixSeconds: expiresUnixSeconds)
        model.select(.clients)
        show()
    }

    @objc public func pairClient() {
        model.bridge.requestPairing { [weak self] payload, expires in
            self?.showPairing(payload: payload, expiresUnixSeconds: expires)
        }
    }

    @objc public func setInterfaceMode(_ mode: PLManagementMode) {
        model.bridge.setInterfaceMode(mode)
        model.bridge.refresh()
        model.publishCommandResult()
    }

    @objc public func clearPairing() {
        model.closePairingPreview()
        model.bridge.clearPairing()
        if model.page == .clients { model.publishCommandResult() }
    }

    @objc public func show() {
        guard model.bridge.isValid else { return }
        model.activate()
        if nativeWindow.isMiniaturized { nativeWindow.deminiaturize(nil) }
        nativeWindow.makeKeyAndOrderFront(nil)
        // Unlike an inline WinUI dialog, an AppKit sheet owns a separate key window.
        nativeWindow.attachedSheet?.makeKeyAndOrderFront(nil)
        reportExport.showPendingError()
        NSApplication.shared.activate(ignoringOtherApps: true)
        refreshTimer?.invalidate()
        let timer = Timer(timeInterval: 5, repeats: true) { [weak model] _ in
            // This timer is installed exclusively in the main run loop below.
            MainActor.assumeIsolated { model?.refreshVisible() }
        }
        RunLoop.main.add(timer, forMode: .common)
        refreshTimer = timer
    }

    @objc public func hide() {
        reportExport.cancelSelection()
        refreshTimer?.invalidate()
        refreshTimer = nil
        model.hide()
        nativeWindow.orderOut(nil)
    }

    public func windowShouldClose(_ sender: NSWindow) -> Bool {
        hide()
        return false
    }

    /// Explicit shutdown closes UI resources; the application owner cancels and joins backends.
    @objc public func invalidate() {
        approvals.invalidate()
        reportExport.invalidate()
        hide()
        model.invalidate()
        nativeWindow.delegate = nil
        nativeWindow.close()
    }

    isolated deinit {
        approvals.invalidate()
        reportExport.invalidate()
        refreshTimer?.invalidate()
    }
}
