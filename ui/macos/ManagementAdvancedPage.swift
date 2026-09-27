import SwiftUI

@MainActor
struct ManagementAdvancedPage: View {
    let diagnostics: PLDiagnostics
    let setDebugRecording: (Bool) -> Void
    let clearDebugLogs: () -> Void
    let settings: PLCompanionPreferences
    let setInterfaceMode: (PLManagementMode) -> PLCompanionPreferences
    let setSingleOperator: (Bool) -> PLCompanionPreferences
    let setSignInStartup: (Bool) -> PLCompanionPreferences
    let applyAudio: (Bool, Int, Bool) -> PLCompanionPreferences
    @State private var current: PLCompanionPreferences
    @State private var automatic: Bool
    @State private var headroom: String
    @State private var audition: Bool
    @State private var pending: Confirmation?

    private enum Confirmation {
        case singleOperator
        case audition(automatic: Bool, headroom: Int, audition: Bool)

        var title: String {
            switch self {
            case .singleOperator: "Enable one operator per production backend"
            case .audition: "Audio source audition bus"
            }
        }

        var message: String {
            switch self {
            case .singleOperator:
                "For each backend with multiple operators, the most recently active operator "
                    + "will remain and every other operator will immediately become a viewer."
            case .audition:
                "Monitored Preview-only sources will remain active and may be audible "
                    + "through the OBS monitoring device. They are not inserted into Program."
            }
        }
    }

    init(settings: PLCompanionPreferences,
         diagnostics: PLDiagnostics,
         setDebugRecording: @escaping (Bool) -> Void,
         clearDebugLogs: @escaping () -> Void,
         setInterfaceMode: @escaping (PLManagementMode) -> PLCompanionPreferences,
         setSingleOperator: @escaping (Bool) -> PLCompanionPreferences,
         setSignInStartup: @escaping (Bool) -> PLCompanionPreferences,
         applyAudio: @escaping (Bool, Int, Bool) -> PLCompanionPreferences) {
        self.diagnostics = diagnostics
        self.setDebugRecording = setDebugRecording
        self.clearDebugLogs = clearDebugLogs
        self.settings = settings
        self.setInterfaceMode = setInterfaceMode
        self.setSingleOperator = setSingleOperator
        self.setSignInStartup = setSignInStartup
        self.applyAudio = applyAudio
        _current = State(initialValue: settings)
        _automatic = State(initialValue: settings.automaticHeadroom)
        _headroom = State(initialValue: String(settings.headroom))
        _audition = State(initialValue: settings.auditionBus)
    }

    private var advanced: Bool { current.interfaceMode == .advanced }

    var body: some View {
        VStack(alignment: .leading, spacing: 18) {
            ManagementPageHeading(title: "Advanced Settings",
                subtitle: "Choose the interface and configure Companion-wide behavior.")
            ScrollView {
                VStack(spacing: 18) {
                    interfaceCard
                    card("Access control") {
                        toggleRow(
                            "Limit each production backend to one operator client. "
                                + "Other paired clients remain viewers.",
                            identifier: "single-operator",
                            value: Binding(get: { current.singleOperator },
                                           set: { changeOperator($0) })
                        )
                    }
                    card("Background operation") {
                        toggleRow(
                            "Start PaneLive Companion automatically when this user signs in.",
                            identifier: "sign-in-startup",
                            value: Binding(get: { current.signInStartup },
                                           set: { update(setSignInStartup($0)) })
                        )
                    }
                    advancedOnly(debugCard)
                    audioCard
                    advancedOnly(
                        card("Production software locations") {
                            HStack(alignment: .top, spacing: 10) {
                                Text("OBS directory").foregroundStyle(.secondary)
                                    .frame(width: 140, alignment: .leading)
                                Text(current.obsDirectory)
                                    .frame(maxWidth: .infinity, alignment: .leading)
                                    .textSelection(.enabled)
                                    .accessibilityIdentifier("obs-directory")
                            }
                        }
                    )
                }
            }
        }
        .onChange(of: settings) { update($0) }
        .confirmationDialog(pending?.title ?? "", isPresented: Binding(
            get: { pending != nil }, set: { if !$0 { pending = nil } }
        ), titleVisibility: .visible, presenting: pending) { confirmation in
            Button("Continue") { confirm(confirmation) }
            Button("Cancel", role: .cancel) { pending = nil }
                .keyboardShortcut(.defaultAction)
        } message: { confirmation in
            Text(confirmation.message)
        }
    }

    private var interfaceCard: some View {
        card("Interface") {
            HStack(spacing: 24) {
                Text("Choose which day-to-day and advanced controls are visible.")
                    .frame(maxWidth: .infinity, alignment: .leading)
                Picker("Interface mode", selection: Binding(
                    get: { current.interfaceMode }, set: { update(setInterfaceMode($0)) }
                )) {
                    Text("Basic").tag(PLManagementMode.basic)
                    Text("Advanced").tag(PLManagementMode.advanced)
                }
                .labelsHidden()
                .frame(width: 180)
                .accessibilityIdentifier("interface-mode")
            }
            Text("Advanced mode reveals network, diagnostics, audio, and installation settings.")
                .foregroundStyle(.secondary)
        }
    }

    private var debugCard: some View {
        card("Debug diagnostics") {
            toggleRow("Record debug diagnostics", identifier: "endurance-toggle",
                      value: Binding(get: { diagnostics.enduranceEnabled },
                                     set: { setDebugRecording($0) }))
            Text("Default off. Keeps at most 24 hours and 64 MiB. Stops after 24 hours or restart.")
                .foregroundStyle(.secondary)
            Text(diagnostics.enduranceSummary).foregroundStyle(.secondary).textSelection(.enabled)
            if !diagnostics.enduranceError.isEmpty {
                Text(diagnostics.enduranceError).foregroundStyle(.red)
            }
            HStack {
                Spacer()
                Button("Clear debug logs", action: clearDebugLogs)
                    .accessibilityIdentifier("clear-debug-logs")
            }
        }
    }

    private var audioCard: some View {
        card("Audio") {
            Text(advanced ? "Configure mixing headroom and the Preview-only source audition bus."
                 : "Control whether monitored Preview sources stay active for private auditioning.")
                .foregroundStyle(.secondary)
            advancedOnly(VStack(alignment: .leading, spacing: 14) {
                toggleRow("Automatic summing headroom", identifier: "automatic-headroom",
                          value: $automatic)
                Text("Reduce each additional monitored source to protect the combined mix from clipping.")
                    .foregroundStyle(.secondary)
                HStack {
                    Text("Attenuation per additional source")
                    Spacer()
                    HStack {
                        TextField("", text: $headroom)
                            .textFieldStyle(.roundedBorder)
                            .accessibilityLabel("Attenuation per additional source")
                            .accessibilityIdentifier("headroom-attenuation")
                        Stepper("Headroom attenuation", onIncrement: { adjustHeadroom(1) },
                                onDecrement: { adjustHeadroom(-1) }).labelsHidden().fixedSize()
                    }
                    .frame(width: 100)
                    .disabled(!automatic)
                }
            })
            toggleRow("Audio source audition bus", identifier: "audition-bus", value: $audition)
            Text("Hear audio from monitored OBS sources in Preview before they go live. "
                 + "They stay out of Program and the broadcast mix.")
                .foregroundStyle(.secondary)
            HStack {
                Spacer()
                Button(advanced ? "Apply audio settings" : "Apply audio setting", action: submitAudio)
                    .buttonStyle(.borderedProminent)
                    .accessibilityIdentifier("apply-audio")
            }
        }
    }

    private func card<Content: View>(_ title: String,
                                    @ViewBuilder content: () -> Content) -> some View {
        VStack(alignment: .leading, spacing: 12) {
            Text(title).font(.system(size: 18, weight: .semibold))
            Divider()
            content()
        }
        .frame(maxWidth: .infinity, alignment: .leading)
        .padding(18)
        .background(Color(nsColor: .controlBackgroundColor), in: RoundedRectangle(cornerRadius: 8))
        .overlay(RoundedRectangle(cornerRadius: 8).strokeBorder(.separator, lineWidth: 1))
    }

    private func advancedOnly<Content: View>(_ content: Content) -> some View {
        // WinUI collapses existing rows; retain native editors and unfinished text across mode changes.
        content.frame(height: advanced ? nil : 0, alignment: .top)
            .clipped()
            .opacity(advanced ? 1 : 0)
            .disabled(!advanced)
            .accessibilityHidden(!advanced)
    }

    private func toggleRow(_ title: String, identifier: String, value: Binding<Bool>) -> some View {
        HStack(spacing: 24) {
            Text(title).frame(maxWidth: .infinity, alignment: .leading)
            Text(value.wrappedValue ? "Enabled" : "Disabled").foregroundStyle(.secondary)
            Toggle(title, isOn: value).labelsHidden().toggleStyle(.switch)
                .accessibilityIdentifier(identifier)
        }
    }

    private func changeOperator(_ enabled: Bool) {
        if enabled && !current.singleOperator {
            pending = .singleOperator
        } else if enabled != current.singleOperator {
            update(setSingleOperator(enabled))
        }
    }

    private func submitAudio() {
        let attenuation = Int(headroom) ?? 2
        if audition && !current.auditionBus {
            pending = .audition(automatic: automatic, headroom: attenuation, audition: audition)
        } else {
            update(applyAudio(automatic, attenuation, audition))
        }
    }

    private func confirm(_ confirmation: Confirmation) {
        pending = nil
        switch confirmation {
        case .singleOperator: update(setSingleOperator(true))
        case let .audition(automatic, headroom, audition):
            update(applyAudio(automatic, headroom, audition))
        }
    }

    private func update(_ updated: PLCompanionPreferences) {
        current = updated
    }

    private func adjustHeadroom(_ offset: Int) {
        let current = min(4, max(1, Int(headroom) ?? 2))
        headroom = String(min(4, max(1, current + offset)))
    }
}
