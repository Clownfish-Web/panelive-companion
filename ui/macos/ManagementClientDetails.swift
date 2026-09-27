import SwiftUI

/// The selected row is refreshed from the snapshot; confirmation retains the originally shown ID.
@MainActor
struct ManagementClientDetails: View {
    let client: PLClientRow
    let identificationPending: Bool
    let setOperator: (String) -> Void
    let setViewer: (String) -> Void
    let identifyClient: (String) -> Void
    let forgetClient: (String) -> Void
    let openTeleprompter: (PLClientRow) -> Void
    @State private var pendingForget: PLClientRow?

    var body: some View {
        VStack(alignment: .leading, spacing: 16) {
            Text("SELECTED CLIENT / \(client.identifier.prefix(8))")
                .font(.system(.caption, design: .monospaced))
                .foregroundStyle(.secondary)
                .help(client.identifier)
            Text(client.name)
                .font(.system(size: 24, weight: .semibold))
                .fixedSize(horizontal: false, vertical: true)
                .accessibilityAddTraits(.isHeader)
            Text("\(client.backend) · \(client.role)").foregroundStyle(.secondary)
            Grid(alignment: .leading, horizontalSpacing: 20, verticalSpacing: 12) {
                fact("Connection", value: client.connected ? "Online" : "Offline")
                fact("Last seen", value: client.lastSeen)
                fact("Attached scripts", value: client.attachedScriptCount?.stringValue ?? "Unavailable")
            }
            Divider()
            actions
        }
        .frame(maxWidth: .infinity, alignment: .topLeading)
        .confirmationDialog("Forget paired client", isPresented: Binding(
            get: { pendingForget != nil },
            set: { if !$0 { pendingForget = nil } }
        ), titleVisibility: .visible, presenting: pendingForget) { target in
            Button("Continue", role: .destructive) {
                pendingForget = nil
                forgetClient(target.identifier)
            }
            Button("Cancel", role: .cancel) { pendingForget = nil }
        } message: { target in
            Text("Forget '\(target.name)'? Its pairing credential will be deleted and the device will disconnect immediately.")
        }
    }

    private func fact(_ title: String, value: String) -> some View {
        GridRow {
            Text(title).foregroundStyle(.secondary)
            Text(value).fixedSize(horizontal: false, vertical: true)
        }
    }

    private var actions: some View {
        VStack(alignment: .leading, spacing: 10) {
            Button("Identify") { identifyClient(client.identifier) }
                .disabled(!client.connected || identificationPending)
                .accessibilityLabel("Identify client")
                .accessibilityIdentifier("client-identify-\(client.identifier)")
            Button(client.canSetViewer ? "Set viewer" : "Set operator") {
                if client.canSetViewer { setViewer(client.identifier) }
                else { setOperator(client.identifier) }
            }
            .disabled(!client.canSetOperator && !client.canSetViewer)
            .help(client.canSetViewer ? "Demote to viewer" : "Promote to operator")
            .accessibilityLabel(client.canSetViewer ? "Demote to viewer" : "Promote to operator")
            .accessibilityIdentifier("client-operator-\(client.identifier)")
            Button("Teleprompter") { openTeleprompter(client) }
                .tint((client.attachedScriptCount?.intValue ?? 0) > 0 ? .green : nil)
                .help(teleprompterLabel)
                .accessibilityLabel(teleprompterLabel)
                .accessibilityIdentifier("client-teleprompter-\(client.identifier)")
            Button("Forget") { pendingForget = client }
                .accessibilityLabel("Forget client")
                .accessibilityIdentifier("client-forget-\(client.identifier)")
        }
        .buttonStyle(.bordered)
        .controlSize(.large)
    }

    private var teleprompterLabel: String {
        guard let count = client.attachedScriptCount else {
            return "Teleprompter - attachments unavailable"
        }
        return "Teleprompter - \(count) attached scripts"
    }
}
