import SwiftUI

@MainActor
struct ManagementShell: View {
    @ObservedObject var model: ManagementShellModel
    @State private var navigationCollapsed = false
    private let collapseImage = ManagementShell.navigationImage(collapsed: false)
    private let expandImage = ManagementShell.navigationImage(collapsed: true)
    private let brandImage: NSImage = {
        guard let image = PLManagementBrandImage() else {
            preconditionFailure("The embedded PaneLive logo is invalid")
        }
        return image
    }()

    var body: some View {
        HStack(spacing: 0) {
            sidebar
            Divider()
            VStack(alignment: .leading, spacing: 12) {
                notices
                ManagementPagesHost(page: model.page, content: AnyView(pageContent))
                    .frame(maxWidth: .infinity, maxHeight: .infinity)
            }
            .padding(24)
            .frame(maxWidth: .infinity, maxHeight: .infinity, alignment: .topLeading)
            .disabled(model.pairingPreviewSeconds > 0)
            .overlay {
                if model.pairingPreviewSeconds > 0, let code = model.bridge.pairing {
                    ManagementPairingOverlay(code: code, seconds: model.pairingPreviewSeconds,
                                             close: model.closePairingPreview)
                }
            }
        }
        .background(Color(nsColor: .windowBackgroundColor))
        .environment(\.colorScheme, .dark)
        .confirmationDialog("Discard script changes", isPresented: Binding(
            get: { model.pendingNavigation != nil },
            set: { if !$0 { model.pendingNavigation = nil } }
        ), titleVisibility: .visible) {
            Button("Continue", role: .destructive, action: model.discardAndNavigate)
            Button("Cancel", role: .cancel) { model.pendingNavigation = nil }
        } message: {
            Text("Discard the unsaved teleprompter script changes?")
        }
        .sheet(isPresented: Binding(
            get: { model.visible && model.bridge.identificationPending },
            set: { shown in
                if !shown {
                    model.bridge.cancelClientIdentification()
                    model.publishCommandResult()
                }
            }
        )) {
            ManagementIdentificationDialog(title: model.bridge.identificationTitle,
                confirm: { audible, vibrate in
                    model.bridge.confirmClientIdentificationAudible(audible, vibrate: vibrate)
                    model.publishCommandResult()
                }, cancel: {
                    model.bridge.cancelClientIdentification()
                    model.publishCommandResult()
                })
        }
    }

    private var sidebar: some View {
        VStack(alignment: .leading, spacing: 6) {
            HStack(spacing: 10) {
                Image(nsImage: brandImage)
                    .resizable()
                    .renderingMode(.original)
                    .scaledToFit()
                    .frame(width: 32, height: 32)
                    .accessibilityHidden(true)
                if !navigationCollapsed {
                    Text("PaneLive").font(.system(size: 20, weight: .semibold))
                }
            }
            .padding(.horizontal, 10)
            .padding(.vertical, 18)
            Button {
                navigationCollapsed.toggle()
            } label: {
                HStack(spacing: 10) {
                    Image(nsImage: navigationCollapsed ? expandImage : collapseImage)
                        .renderingMode(.template)
                        .frame(width: 20)
                        .accessibilityHidden(true)
                    if !navigationCollapsed { Text("Collapse menu") }
                }
                .frame(maxWidth: .infinity, minHeight: 32,
                       alignment: navigationCollapsed ? .center : .leading)
                .padding(.horizontal, 10)
                .contentShape(Rectangle())
            }
            .buttonStyle(.plain)
            .help(navigationCollapsed ? "Expand navigation" : "Collapse navigation")
            .accessibilityLabel(navigationCollapsed ? "Expand navigation" : "Collapse navigation")
            .accessibilityValue(navigationCollapsed ? "Collapsed" : "Expanded")
            .accessibilityIdentifier("navigation-toggle")
            navigation(footer: false)
            Spacer()
            navigation(footer: true)
        }
        .padding(.horizontal, 12)
        .padding(.bottom, 16)
        .frame(width: navigationCollapsed ? 76 : 232)
        .frame(maxHeight: .infinity)
        .background(Color(nsColor: .controlBackgroundColor))
    }

    private func navigation(footer: Bool) -> some View {
        ForEach(model.bridge.navigation.filter { $0.footer == footer }, id: \.page.rawValue) { item in
            Button { model.select(item.page) } label: {
                HStack(spacing: 10) {
                    Image(systemName: item.symbol).frame(width: 20)
                        .accessibilityHidden(true)
                    if !navigationCollapsed { Text(item.title) }
                }
                    .frame(maxWidth: .infinity,
                           alignment: navigationCollapsed ? .center : .leading)
                    .padding(.horizontal, 10)
                    .padding(.vertical, 10)
                    .background(model.page == item.page ? Color.accentColor.opacity(0.22) : .clear,
                                in: RoundedRectangle(cornerRadius: 6))
            }
            .buttonStyle(.plain)
            .help(item.title)
            .accessibilityLabel(item.title)
            .accessibilityIdentifier(item.automationIdentifier)
            .accessibilityAddTraits(model.page == item.page ? .isSelected : [])
        }
    }

    private static func navigationImage(collapsed: Bool) -> NSImage {
        guard let image = PLManagementNavigationToggleImage(collapsed) else {
            preconditionFailure("The embedded navigation artwork is invalid")
        }
        return image
    }

    @ViewBuilder
    private var notices: some View {
        if !model.bridge.lastError.isEmpty {
            Label {
                VStack(alignment: .leading, spacing: 3) {
                    Text("Management operation failed").fontWeight(.semibold)
                    Text(model.bridge.lastError)
                }
            } icon: { Image(systemName: "exclamationmark.circle") }
            .foregroundStyle(.red)
            .frame(maxWidth: .infinity, alignment: .leading)
            .padding(12)
            .background(Color.red.opacity(0.12), in: RoundedRectangle(cornerRadius: 6))
        }
        if !model.bridge.networkSettings.listening && model.bridge.interfaceMode == .basic {
            HStack {
                VStack(alignment: .leading, spacing: 3) {
                    Text("Companion listener needs attention").fontWeight(.semibold)
                    Text(model.bridge.networkSettings.warning.isEmpty
                         ? "The Companion listener needs attention."
                         : model.bridge.networkSettings.warning)
                }
                Spacer()
                Button("Open Companion Settings") { model.openNetworkRecovery() }
            }
            .foregroundStyle(.orange)
            .padding(12)
            .background(Color.orange.opacity(0.12), in: RoundedRectangle(cornerRadius: 6))
        }
    }

    @ViewBuilder
    private var pageContent: some View {
        switch model.page {
        case .clients:
            if model.bridge.teleprompter.deviceIdentifier.isEmpty {
                ManagementClientsPage(model: model)
            } else {
                ManagementTeleprompterPage(model: model)
            }
        case .teleprompter: ManagementTeleprompterPage(model: model)
        case .adapters: adaptersPage
        case .companionSettings: networkPage
        case .status: statusPage
        case .advancedSettings: advancedPage
        case .diagnostics: diagnosticsPage
        case .about: ManagementAboutPage(buildVersion: model.buildVersion)
        @unknown default: ManagementClientsPage(model: model)
        }
    }

    private var adaptersPage: some View {
        ManagementAdaptersPage(connection: model.bridge.obsConnection,
            streamlabs: model.bridge.streamlabsConnection,
            applyObs: {
                model.bridge.applyObsHost($0, port: $1, password: $2)
                model.publishCommandResult()
                return model.bridge.obsConnection
            }, applyStreamlabs: {
                model.bridge.applyStreamlabsPort($0, token: $1)
                model.publishCommandResult()
                return model.bridge.streamlabsConnection
            })
    }

    private var networkPage: some View {
        ManagementNetworkPage(settings: model.bridge.networkSettings) {
            model.bridge.applyNetworkAddress($0, port: $1)
            model.publishCommandResult()
            return model.bridge.networkSettings
        }
    }

    private var statusPage: some View {
        ManagementStatusPage(adapters: model.bridge.adapters, feeds: model.bridge.statusFeeds,
                             startAdapter: {
            model.bridge.startAdapter($0)
            model.publishCommandResult()
        }, stopAdapter: {
            model.bridge.stopAdapter($0)
            model.publishCommandResult()
        }, restartAdapter: {
            model.bridge.restartAdapter($0)
            model.publishCommandResult()
        }, shutdown: { model.bridge.shutdown(); model.publishCommandResult() })
    }

    private var advancedPage: some View {
        ManagementAdvancedPage(settings: model.bridge.preferences,
            diagnostics: model.bridge.diagnostics, setDebugRecording: {
            model.bridge.setEnduranceLogging($0)
            model.publishCommandResult()
        }, clearDebugLogs: {
            model.bridge.clearDebugLogs()
            model.publishCommandResult()
        }, setInterfaceMode: {
            model.bridge.setInterfaceMode($0)
            model.publishCommandResult()
            return model.bridge.preferences
        }, setSingleOperator: {
            model.bridge.setSingleOperator($0)
            model.publishCommandResult()
            return model.bridge.preferences
        }, setSignInStartup: {
            model.bridge.setSignInStartup($0)
            model.publishCommandResult()
            return model.bridge.preferences
        }, applyAudio: {
            model.bridge.applyAutomaticHeadroom($0, headroom: $1, auditionBus: $2)
            model.publishCommandResult()
            return model.bridge.preferences
        })
    }

    private var diagnosticsPage: some View {
        ManagementDiagnosticsPage(diagnostics: model.bridge.diagnostics, actions: .init(refresh: {
            model.bridge.refresh()
            model.publishCommandResult()
            return model.bridge.diagnostics
        }, startAudio: {
            model.bridge.startAudio($0, tap: $1)
            model.publishCommandResult()
            return model.bridge.diagnostics
        }, stopAudio: {
            model.bridge.stopAudio()
            model.publishCommandResult()
            return model.bridge.diagnostics
        }, setGain: { model.bridge.setAudioGain($0) }, beginDebugLog: {
            model.bridge.beginAudioDebugLog()
            model.publishCommandResult()
            return model.bridge.diagnostics
        }, completeDebugLog: {
            model.bridge.completeAudioDebugLog()
            model.publishCommandResult()
            return model.bridge.diagnostics
        }, saveReport: model.saveReport))
    }
}

@MainActor
private struct ManagementIdentificationDialog: View {
    let title: String
    let confirm: (Bool, Bool) -> Void
    let cancel: () -> Void
    @State private var audible = true
    @State private var vibrate = true

    var body: some View {
        VStack(alignment: .leading, spacing: 16) {
            Text(title).font(.headline)
            Toggle("Visual (required)", isOn: .constant(true)).disabled(true)
            Toggle("Audible", isOn: $audible)
            Toggle("Vibrate", isOn: $vibrate)
            HStack {
                Spacer()
                Button("Cancel", action: cancel).keyboardShortcut(.cancelAction)
                Button("Identify") {
                    confirm(audible, vibrate)
                }
            }
        }
        .padding(24)
        .frame(width: 320)
    }
}
