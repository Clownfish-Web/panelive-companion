import SwiftUI

/// Both providers reuse the retained WebSocket form and the shared management command flow.
@MainActor
struct ManagementAdaptersPage: View {
    let connection: PLObsConnection
    let streamlabs: PLObsConnection
    let applyObs: (String, Int, String) -> PLObsConnection
    let applyStreamlabs: (Int, String) -> PLObsConnection

    var body: some View {
        VStack(alignment: .leading, spacing: 18) {
            ManagementPageHeading(title: "Adapters",
                subtitle: "Configure adapter connections.")
            ScrollView {
                VStack(spacing: 18) {
                    VStack(alignment: .leading, spacing: 14) {
                        Text("PaneLive Teleprompter").font(.system(size: 18, weight: .semibold))
                        Divider()
                        Text("This built-in adapter starts automatically. Pair devices to share scripts and playback. Use Status to start, stop or restart it. No production software or connection settings are needed.")
                            .foregroundStyle(.secondary)
                    }
                    .frame(maxWidth: .infinity, alignment: .leading)
                    .padding(18)
                    .background(Color(nsColor: .controlBackgroundColor), in: RoundedRectangle(cornerRadius: 8))
                    .overlay(RoundedRectangle(cornerRadius: 8).strokeBorder(.separator, lineWidth: 1))
                    .accessibilityIdentifier("teleprompter-adapter-card")
                    ManagementWebSocketConnectionCard(name: "OBS Studio", identifier: "obs",
                        secretLabel: "Password", connection: connection, applyObs: applyObs)
                    ManagementWebSocketConnectionCard(name: "Streamlabs Desktop", identifier: "streamlabs",
                        secretLabel: "API token", fixedHost: true, connection: streamlabs) { _, port, token in
                            applyStreamlabs(port, token)
                        }
                }
            }
        }
    }
}

@MainActor
private struct ManagementWebSocketConnectionCard: View {
    let name: String
    let identifier: String
    let secretLabel: String
    let fixedHost: Bool
    let connection: PLObsConnection
    let applyObs: (String, Int, String) -> PLObsConnection
    @State private var host: String
    @State private var port: String
    @State private var password = ""
    @State private var hostEdited = false
    @State private var portEdited = false

    init(name: String, identifier: String, secretLabel: String, fixedHost: Bool = false,
         connection: PLObsConnection,
         applyObs: @escaping (String, Int, String) -> PLObsConnection) {
        self.name = name
        self.identifier = identifier
        self.secretLabel = secretLabel
        self.fixedHost = fixedHost
        self.connection = connection
        self.applyObs = applyObs
        _host = State(initialValue: connection.host)
        _port = State(initialValue: String(connection.port))
    }

    var body: some View {
        VStack(alignment: .leading, spacing: 14) {
            HStack {
                Text(name).font(.system(size: 18, weight: .semibold))
                Spacer()
                statusBadge
            }
            Divider()
            VStack(spacing: 10) {
                field("Host") {
                    TextField("", text: Binding(get: { host }, set: {
                        host = $0
                        hostEdited = true
                    }))
                        .accessibilityLabel("\(name) host")
                        .disabled(fixedHost)
                        .accessibilityIdentifier("\(identifier)-host")
                }
                field("WebSocket port") {
                    HStack {
                        TextField("", text: Binding(get: { port }, set: {
                            port = $0
                            portEdited = true
                        }))
                            .accessibilityLabel("\(name) WebSocket port")
                            .accessibilityIdentifier("\(identifier)-port")
                        Stepper("\(name) WebSocket port adjustment",
                                onIncrement: { adjustPort(by: 1) },
                                onDecrement: { adjustPort(by: -1) })
                            .labelsHidden()
                            .fixedSize()
                    }
                }
                field(secretLabel) {
                    SecureField("Leave empty to keep the stored \(secretLabel.lowercased())", text: $password)
                        .accessibilityLabel("\(name) \(secretLabel.lowercased())")
                        .accessibilityIdentifier("\(identifier)-password")
                }
            }
            Text("Your \(name) \(secretLabel.lowercased()) is stored securely in macOS Keychain. "
                 + "Companion uses it to connect to \(name). macOS may ask you to allow access "
                 + "to this saved credential when Companion connects.")
                .foregroundStyle(.secondary)
                .fixedSize(horizontal: false, vertical: true)
                .accessibilityIdentifier("\(identifier)-keychain-explanation")
            HStack {
                Spacer()
                Button("Apply \(name == "OBS Studio" ? "OBS" : "Streamlabs") settings", action: apply)
                    .buttonStyle(.borderedProminent)
                    .accessibilityIdentifier("apply-\(identifier)")
            }
        }
        .textFieldStyle(.roundedBorder)
        .padding(18)
        .background(Color(nsColor: .controlBackgroundColor), in: RoundedRectangle(cornerRadius: 8))
        .overlay(RoundedRectangle(cornerRadius: 8).strokeBorder(.separator, lineWidth: 1))
        // Saved settings arrive asynchronously after the initial placeholder snapshot.
        .onChange(of: connection.host) { value in
            if !hostEdited { host = value }
        }
        .onChange(of: connection.port) { value in
            if !portEdited { port = String(value) }
        }
    }

    private var statusBadge: some View {
        let tone: Color = connection.connected ? .green : .orange
        return Text(connection.status.isEmpty ? "Not configured" : connection.status)
            .font(.caption.weight(.semibold))
            .foregroundStyle(tone)
            .padding(.horizontal, 8)
            .padding(.vertical, 4)
            .background(tone.opacity(0.14), in: Capsule())
    }

    private func field<Content: View>(_ title: String,
                                     @ViewBuilder content: () -> Content) -> some View {
        HStack(spacing: 12) {
            Text(title).foregroundStyle(.secondary).frame(width: 150, alignment: .leading)
            content().frame(maxWidth: .infinity)
        }
    }

    private func apply() {
        // Invalid or overflowing input reaches the bridge as zero, never a narrowed valid port.
        _ = applyObs(host, Int(port) ?? 0, password)
    }

    private func adjustPort(by offset: Int) {
        portEdited = true
        let current = min(65535, max(1, Int(port) ?? connection.port))
        port = String(min(65535, max(1, current + offset)))
    }
}
