import SwiftUI

/// Named controller commands; the window owns report export and native file-dialog lifetime.
@MainActor
struct ManagementDiagnosticsActions {
    let refresh: () -> PLDiagnostics
    let startAudio: (String, PLDiagnosticTap) -> PLDiagnostics
    let stopAudio: () -> PLDiagnostics
    let setGain: (Float) -> Void
    let beginDebugLog: () -> PLDiagnostics
    let completeDebugLog: () -> PLDiagnostics
    let saveReport: () -> Void
}

@MainActor
struct ManagementDiagnosticsPage: View {
    let diagnostics: PLDiagnostics
    let actions: ManagementDiagnosticsActions
    @State private var current: PLDiagnostics
    @State private var output: String
    @State private var gain: Double

    init(diagnostics: PLDiagnostics, actions: ManagementDiagnosticsActions) {
        self.diagnostics = diagnostics
        self.actions = actions
        _current = State(initialValue: diagnostics)
        _output = State(initialValue: Self.defaultOutput(diagnostics))
        _gain = State(initialValue: Double(diagnostics.gain))
    }

    var body: some View {
        VStack(alignment: .leading, spacing: 18) {
            VStack(alignment: .leading, spacing: 4) {
                Text("Diagnostics").font(.system(size: 28, weight: .semibold))
                Text("Run local audio checks.")
                    .foregroundStyle(.secondary)
            }
            ScrollView {
                VStack(spacing: 18) {
                    audioCard
                }
            }
        }
        .buttonStyle(.bordered)
        .onChange(of: diagnostics) { reload($0) }
    }

    private var audioCard: some View {
        card("Local audio diagnostics") {
            HStack(spacing: 10) {
                ManagementFormChoices(
                    choices: current.outputs.map {
                        .init(identifier: $0.identifier, label: $0.name)
                    }, selection: $output, identifier: "audio-output", label: "Audio output")
                Button("Refresh devices") { reload(actions.refresh()) }
            }
            HStack(spacing: 12) {
                Text("Gain").foregroundStyle(.secondary).frame(width: 80, alignment: .leading)
                Slider(value: Binding(get: { gain }, set: {
                    gain = $0
                    actions.setGain(Float($0))
                }), in: -24...12, step: 1)
                .accessibilityLabel("Audio gain").accessibilityIdentifier("audio-gain")
                Text("\(Int(gain)) dB").frame(width: 70, alignment: .leading)
            }
            HStack(spacing: 8) {
                Button("Play pre-Opus IPC") { start(.preOpus) }
                    .accessibilityIdentifier("audio-pre-opus")
                Button("Play post-Opus") { start(.postOpus) }
                    .accessibilityIdentifier("audio-post-opus")
                Button("Stop") { reload(actions.stopAudio()) }
                    .disabled(!current.playing).accessibilityIdentifier("audio-stop")
                Button(current.debugActive ? "Complete debug log" : "Begin debug log") {
                    reload(current.debugActive ? actions.completeDebugLog() : actions.beginDebugLog())
                }
                .accessibilityIdentifier("audio-debug-log")
                Button("Save report", action: actions.saveReport)
                    .disabled(!current.debugAvailable).accessibilityIdentifier("audio-save-report")
            }
            meter("Maximum peak", value: current.maximumPeak)
            meter("Average peak", value: current.averagePeak)
            Text(current.audioSummary).foregroundStyle(.secondary)
                .fixedSize(horizontal: false, vertical: true)
        }
    }

    private func meter(_ title: String, value: Float) -> some View {
        HStack(spacing: 12) {
            Text(title).foregroundStyle(.secondary).frame(width: 130, alignment: .leading)
            ProgressView(value: Double(min(1, max(0, value))))
                .accessibilityLabel(title)
        }
    }

    private func card<Content: View>(_ title: String,
                                    @ViewBuilder content: () -> Content) -> some View {
        VStack(alignment: .leading, spacing: 14) {
            Text(title).font(.system(size: 18, weight: .semibold))
            Divider()
            content()
        }
        .frame(maxWidth: .infinity, alignment: .leading)
        .padding(18)
        .background(Color(nsColor: .controlBackgroundColor), in: RoundedRectangle(cornerRadius: 8))
        .overlay(RoundedRectangle(cornerRadius: 8).strokeBorder(.separator, lineWidth: 1))
    }

    private static func defaultOutput(_ diagnostics: PLDiagnostics) -> String {
        diagnostics.outputs.last(where: { $0.isDefault })?.identifier
            ?? diagnostics.outputs.first?.identifier ?? ""
    }

    private func reload(_ updated: PLDiagnostics) {
        current = updated
    }

    private func start(_ tap: PLDiagnosticTap) {
        guard !output.isEmpty else { return }
        reload(actions.startAudio(output, tap))
    }
}
