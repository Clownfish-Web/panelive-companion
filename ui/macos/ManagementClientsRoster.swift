import SwiftUI

/// Filtering and selection are local presentation state. Sorting/actions stay controller-owned.
@MainActor
struct ManagementClientsRoster: View {
    let clients: [PLClientRow]
    let availableWidth: CGFloat
    let sortColumn: PLClientColumn
    let sortAscending: Bool
    let identificationPending: Bool
    @Binding var query: String
    @Binding var selectedIdentifier: String?
    let sortClients: (PLClientColumn) -> Void
    let setOperator: (String) -> Void
    let setViewer: (String) -> Void
    let identifyClient: (String) -> Void
    let forgetClient: (String) -> Void
    let openTeleprompter: (PLClientRow) -> Void

    private var visibleClients: [PLClientRow] {
        guard !query.isEmpty else { return clients }
        return clients.filter {
            "\($0.name) \($0.backend) \($0.role)".localizedCaseInsensitiveContains(query)
        }
    }

    private var selectedClient: PLClientRow? {
        clients.first { $0.identifier == selectedIdentifier } ?? clients.first
    }

    private var sideBySide: Bool { availableWidth >= 820 }

    var body: some View {
        VStack(alignment: .leading, spacing: 20) {
            toolbar
            let layout = sideBySide
                ? AnyLayout(HStackLayout(alignment: .top, spacing: 24))
                : AnyLayout(VStackLayout(alignment: .leading, spacing: 24))
            layout {
                roster.frame(maxWidth: .infinity, alignment: .topLeading)
                if let client = selectedClient {
                    ManagementClientDetails(client: client,
                        identificationPending: identificationPending,
                        setOperator: setOperator, setViewer: setViewer,
                        identifyClient: identifyClient, forgetClient: forgetClient,
                        openTeleprompter: openTeleprompter)
                        .padding(sideBySide ? .leading : .top, 24)
                        .frame(width: sideBySide ? 300 : nil, alignment: .topLeading)
                        .overlay(alignment: sideBySide ? .leading : .top) {
                            Rectangle().fill(.separator)
                                .frame(width: sideBySide ? 1 : nil, height: sideBySide ? nil : 1)
                        }
                }
            }
        }
        .onAppear { reconcileSelection() }
        .onChange(of: clients.map(\.identifier)) { _ in reconcileSelection() }
    }

    private var toolbar: some View {
        HStack(spacing: 12) {
            TextField("Name, backend or role", text: $query)
                .textFieldStyle(.roundedBorder)
                .accessibilityLabel("Find client")
                .accessibilityIdentifier("clients-search")
                .frame(maxWidth: 300)
            Button("Clear") { query = "" }
                .disabled(query.isEmpty)
                .accessibilityLabel("Clear client search")
            sortMenu
            Spacer(minLength: 0)
            Text("\(visibleClients.count) of \(clients.count) clients / \(visibleClients.filter(\.connected).count) online")
                .font(.callout)
                .foregroundStyle(.secondary)
                .accessibilityIdentifier("clients-count")
        }
    }

    private var sortMenu: some View {
        Menu {
            sortChoice("Status", column: .status)
            sortChoice("Client", column: .client)
            sortChoice("Backend", column: .backend)
            sortChoice("Role", column: .role)
            sortChoice("Last seen", column: .lastSeen)
        } label: {
            Text("Sort: \(sortTitle) \(sortAscending ? "↑" : "↓")")
        }
        .fixedSize()
        .help("Choose a column again to reverse its sort order")
        .accessibilityIdentifier("clients-sort")
    }

    private var sortTitle: String {
        switch sortColumn {
        case .status: "Status"
        case .client: "Client"
        case .backend: "Backend"
        case .role: "Role"
        case .lastSeen: "Last seen"
        @unknown default: "Client"
        }
    }

    private func sortChoice(_ title: String, column: PLClientColumn) -> some View {
        Button { sortClients(column) } label: {
            Text(title + (sortColumn == column ? (sortAscending ? " ↑" : " ↓") : ""))
        }
        .accessibilityIdentifier("clients-sort-\(column.rawValue)")
    }

    private var roster: some View {
        VStack(alignment: .leading, spacing: 0) {
            HStack {
                Text("CLIENT").frame(maxWidth: .infinity, alignment: .leading)
                Text("STATUS").frame(width: 72, alignment: .leading)
            }
            .font(.caption.weight(.semibold))
            .foregroundStyle(.secondary)
            .padding(.horizontal, 12)
            .padding(.bottom, 12)
            Divider()
            ForEach(Array(visibleClients.enumerated()), id: \.element.identifier) { index, client in
                record(client, ordinal: index + 1)
                Divider()
            }
            if visibleClients.isEmpty {
                Text(clients.isEmpty ? "No paired clients yet. Pair a device to begin."
                                    : "No clients match your search.")
                    .foregroundStyle(.secondary)
                    .padding(.vertical, 20)
            }
        }
    }

    private func record(_ client: PLClientRow, ordinal: Int) -> some View {
        let selected = client.identifier == selectedClient?.identifier
        return Button { selectedIdentifier = client.identifier } label: {
            HStack(alignment: .top, spacing: 12) {
                Text(String(format: "%02d", ordinal))
                    .font(.system(.caption, design: .monospaced))
                    .foregroundStyle(.secondary)
                    .padding(.top, 4)
                    .accessibilityHidden(true)
                VStack(alignment: .leading, spacing: 8) {
                    Label(client.name, systemImage: client.deviceSymbol)
                        .font(.system(size: 18, weight: .semibold))
                        .fixedSize(horizontal: false, vertical: true)
                    Text("\(client.identifier.prefix(8)) · \(attachmentSummary(client))")
                        .font(.system(.caption, design: .monospaced))
                        .foregroundStyle(.secondary)
                    Text("\(client.backend) · \(client.role) · \(client.lastSeen)")
                        .foregroundStyle(.secondary)
                        .fixedSize(horizontal: false, vertical: true)
                    if !client.identificationCode.isEmpty {
                        Text(client.identificationCode)
                            .font(.system(.body, design: .monospaced).bold())
                            .accessibilityLabel("Identification code \(client.identificationCode)")
                    }
                }
                .frame(maxWidth: .infinity, alignment: .leading)
                Text(client.connected ? "Online" : "Offline")
                    .foregroundStyle(client.connected ? Color.green : Color.secondary)
                    .frame(width: 72, alignment: .leading)
                    .padding(.top, 4)
            }
            .padding(12)
            .frame(maxWidth: .infinity, alignment: .leading)
            .contentShape(Rectangle())
            .background(selected ? Color.accentColor.opacity(0.16) : .clear)
            .overlay(alignment: .leading) {
                if selected { Rectangle().fill(Color.accentColor).frame(width: 3) }
            }
        }
        .buttonStyle(.plain)
        .accessibilityAddTraits(selected ? .isSelected : [])
        .accessibilityIdentifier("client-row-\(client.identifier)")
    }

    private func attachmentSummary(_ client: PLClientRow) -> String {
        guard let count = client.attachedScriptCount else { return "attachments unavailable" }
        return "\(count) scripts"
    }

    private func reconcileSelection() {
        if !clients.contains(where: { $0.identifier == selectedIdentifier }) {
            selectedIdentifier = clients.first?.identifier
        }
    }
}
