import SwiftUI

/// Client presentation uses the existing bridge commands and window-owned query/selection.
@MainActor
struct ManagementClientsPage: View {
    @ObservedObject var model: ManagementShellModel

    var body: some View {
        GeometryReader { geometry in
            VStack(alignment: .leading, spacing: 18) {
                ManagementPageHeading(title: "Clients",
                    subtitle: "Pair devices and manage access to production backends.") {
                    pageActions
                }
                ScrollView {
                    VStack(alignment: .leading, spacing: 18) {
                        if let pairing = model.bridge.pairing {
                            ManagementPairingCard(code: pairing, enlarge: model.openPairingPreview)
                        }
                        roster(width: geometry.size.width)
                    }
                }
            }
        }
    }

    private var pageActions: some View {
        HStack(spacing: 12) {
            Button("Identify all") {
                model.bridge.identifyAllClients()
                model.publishCommandResult()
            }
            .disabled(model.bridge.identificationPending
                      || !model.bridge.clients.contains { $0.connected })
            .accessibilityIdentifier("clients-identify-all")
            Button("Refresh") {
                model.bridge.refreshClients()
                model.publishCommandResult()
            }
            .accessibilityIdentifier("clients-refresh")
            Button("Pair new client", action: model.pairClient)
                .buttonStyle(.borderedProminent)
                .accessibilityIdentifier("clients-pair")
        }
        .fixedSize()
    }

    private func roster(width: CGFloat) -> some View {
        ManagementClientsRoster(clients: model.bridge.clients,
            availableWidth: width,
            sortColumn: model.bridge.clientSortColumn,
            sortAscending: model.bridge.clientSortAscending,
            identificationPending: model.bridge.identificationPending,
            query: $model.clientQuery,
            selectedIdentifier: $model.selectedClientIdentifier,
            sortClients: {
                model.bridge.sortClients($0)
                model.publishCommandResult()
            }, setOperator: {
                model.bridge.setOperator($0)
                model.publishCommandResult()
            }, setViewer: {
                model.bridge.setViewer($0)
                model.publishCommandResult()
            }, identifyClient: {
                model.bridge.identifyClient($0)
                model.publishCommandResult()
            }, forgetClient: {
                model.bridge.forgetClient($0)
                model.publishCommandResult()
            }, openTeleprompter: {
                model.bridge.openTeleprompter($0.identifier, name: $0.name)
                model.publishCommandResult()
            })
    }
}
