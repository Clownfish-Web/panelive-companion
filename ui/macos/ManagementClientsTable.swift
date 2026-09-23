import SwiftUI

/// Presentation only. The enclosing page supplies immutable rows and named controller callbacks.
@MainActor
struct ManagementClientsTable: View {
    let clients: [PLClientRow]
    let sortColumn: PLClientColumn
    let sortAscending: Bool
    let sortClients: (PLClientColumn) -> Void
    let setOperator: (String) -> Void
    let setViewer: (String) -> Void
    let identifyClient: (String) -> Void
    let forgetClient: (String) -> Void
    let openTeleprompter: (PLClientRow) -> Void
    @State private var pendingForget: PLClientRow?

    var body: some View {
        VStack(spacing: 0) {
            VStack(spacing: 0) {
                HStack(spacing: 12) {
                    sortHeading("Status", column: .status).frame(width: 80, alignment: .leading)
                    sortHeading("Client", column: .client).frame(maxWidth: .infinity, alignment: .leading)
                    sortHeading("Backend", column: .backend).frame(width: 100, alignment: .leading)
                    sortHeading("Role", column: .role).frame(width: 75, alignment: .leading)
                    sortHeading("Last seen", column: .lastSeen).frame(width: 90, alignment: .leading)
                    heading("Actions", width: 200)
                }
                .font(.subheadline.weight(.semibold))
                .foregroundStyle(.secondary)
                .frame(height: 44)
                .padding(.horizontal, 14)

                ForEach(Array(clients.enumerated()), id: \.element.identifier) { index, client in
                    HStack(spacing: 12) {
                        status(client)
                        HStack(spacing: 8) {
                            Label(client.name, systemImage: client.deviceSymbol)
                                .fontWeight(.semibold)
                                .lineLimit(2).help(client.name)
                            if !client.identificationCode.isEmpty {
                                Text(client.identificationCode)
                                    .font(.system(.body, design: .monospaced).bold())
                                    .accessibilityLabel("Identification code \(client.identificationCode)")
                            }
                        }
                        .frame(maxWidth: .infinity, alignment: .leading)
                        Text(client.backend).frame(width: 100, alignment: .leading)
                        Text(client.role).frame(width: 75, alignment: .leading)
                        Text(client.lastSeen)
                            .foregroundStyle(.secondary)
                            .frame(width: 90, alignment: .leading)
                        actions(client)
                    }
                    .frame(minHeight: 56)
                    .padding(.horizontal, 14)
                    .background(index.isMultiple(of: 2) ? Color.primary.opacity(0.04) : .clear)
                }
            }

            if clients.isEmpty {
                Text("No paired clients yet. Pair a device to begin.")
                    .foregroundStyle(.secondary)
                    .frame(maxWidth: .infinity, alignment: .leading)
                    .padding(.horizontal, 14)
                    .padding(.vertical, 18)
            }
        }
        .background(Color(nsColor: .controlBackgroundColor), in: RoundedRectangle(cornerRadius: 8))
        .overlay(RoundedRectangle(cornerRadius: 8).strokeBorder(.separator, lineWidth: 1))
        .confirmationDialog(
            "Forget paired client",
            isPresented: Binding(
                get: { pendingForget != nil },
                set: { if !$0 { pendingForget = nil } }
            ),
            titleVisibility: .visible,
            presenting: pendingForget
        ) { client in
            Button("Continue", role: .destructive) { forgetClient(client.identifier) }
            Button("Cancel", role: .cancel) { pendingForget = nil }
        } message: { client in
            Text("Forget '\(client.name)'? Its pairing credential will be deleted and the device will disconnect immediately.")
        }
    }

    private func sortHeading(_ title: String, column: PLClientColumn) -> some View {
        let active = sortColumn == column
        let indicator = active ? (sortAscending ? "↑" : "↓") : "↕"
        return Button { sortClients(column) } label: {
            Text("\(title) \(indicator)").lineLimit(1).fixedSize()
        }
        .buttonStyle(.borderless)
        .accessibilityLabel(title)
        .accessibilityValue(active ? (sortAscending ? "Sorted ascending" : "Sorted descending") : "Sortable")
        .accessibilityIdentifier("clients-sort-\(column.rawValue)")
        .help("Sort by \(title); click again to reverse")
    }

    private func teleprompterLabel(_ client: PLClientRow) -> String {
        guard let count = client.attachedScriptCount else { return "Teleprompter - attachments unavailable" }
        return "Teleprompter - \(count) attached scripts"
    }

    private func heading(_ title: String, width: CGFloat) -> some View {
        Text(title).frame(width: width, alignment: .leading)
    }

    private func status(_ client: PLClientRow) -> some View {
        Text(client.connected ? "Online" : "Offline")
            .font(.caption.weight(.semibold))
            .foregroundStyle(client.connected ? Color.green : Color.secondary)
            .padding(.horizontal, 8)
            .padding(.vertical, 4)
            .background(
                (client.connected ? Color.green : Color.secondary).opacity(0.14),
                in: Capsule()
            )
            .frame(width: 80, alignment: .leading)
    }

    private func actions(_ client: PLClientRow) -> some View {
        HStack(spacing: 6) {
            Button { identifyClient(client.identifier) } label: {
                Image(systemName: "scope").frame(width: 20, height: 20)
            }
                .disabled(!client.connected)
                .help("Identify client")
                .accessibilityLabel("Identify client")
                .accessibilityIdentifier("client-identify-\(client.identifier)")

            Button {
                if client.canSetViewer {
                    setViewer(client.identifier)
                } else {
                    setOperator(client.identifier)
                }
            } label: {
                Image(systemName: client.deviceSymbol)
                    .overlay(alignment: .bottomTrailing) {
                        if !client.canSetViewer {
                            Image(systemName: "pencil")
                                .font(.system(size: 8, weight: .bold))
                                .offset(x: 5, y: 3)
                        }
                    }
                    .frame(width: 20, height: 20)
            }
            .disabled(!client.canSetOperator && !client.canSetViewer)
            .help(client.canSetViewer ? "Demote to viewer" : "Promote to operator")
            .accessibilityLabel(client.canSetViewer ? "Demote to viewer" : "Promote to operator")
            .accessibilityIdentifier("client-operator-\(client.identifier)")

            Button { openTeleprompter(client) } label: {
                Image(systemName: "text.alignleft")
                    .foregroundStyle((client.attachedScriptCount?.intValue ?? 0) > 0 ? Color.green : Color.primary)
                    .frame(width: 20, height: 20)
                    .overlay(alignment: .bottomTrailing) {
                        if (client.attachedScriptCount?.intValue ?? 0) > 0 {
                            Circle().fill(.green).frame(width: 7, height: 7)
                        }
                    }
            }
                .help(teleprompterLabel(client))
                .accessibilityLabel(teleprompterLabel(client))
                .accessibilityIdentifier("client-teleprompter-\(client.identifier)")

            Button { pendingForget = client } label: {
                Image(systemName: "trash").frame(width: 20, height: 20)
            }
            .help("Forget client")
            .accessibilityLabel("Forget client")
            .accessibilityIdentifier("client-forget-\(client.identifier)")
        }
        .buttonStyle(.bordered)
        .controlSize(.regular)
        .frame(width: 200)
    }
}
