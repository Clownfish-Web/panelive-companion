import SwiftUI

/// The portable controller fixes the owner to Controller and permits exactly one Master.
@MainActor
struct ManagementTeleprompterSharing: View {
    @ObservedObject var model: ManagementShellModel
    @State private var scripts: Set<String>
    @State private var clients: Set<String>
    @State private var master: String

    init(model: ManagementShellModel) {
        self.model = model
        let state = model.bridge.teleprompter
        _scripts = State(initialValue: Set(state.selectedScripts))
        _clients = State(initialValue: Set(state.selectedClients))
        _master = State(initialValue: state.clientRoles.first {
            $0.value.intValue == PLTeleprompterRole.master.rawValue
        }?.key ?? "")
    }

    var body: some View {
        VStack(alignment: .leading, spacing: 18) {
            GroupBox {
                VStack(alignment: .leading, spacing: 6) {
                    Text("Controller: \(model.bridge.teleprompter.clientName)").font(.headline)
                    Text("Use this device to choose scripts, start or pause, and change all player settings. Attached players only display the text.")
                        .foregroundStyle(.secondary)
                    Text("Using just this device? Cancel this setup and use its private scripts. It is both Controller and Master Player.")
                        .font(.callout).foregroundStyle(.secondary)
                }.frame(maxWidth: .infinity, alignment: .leading)
            }
            Text("1. Choose the scripts").font(.headline)
            if model.bridge.teleprompter.scripts.isEmpty {
                Text("Attach a library script to this Controller before setting up players.").foregroundStyle(.orange)
            }
            ForEach(model.bridge.teleprompter.scripts, id: \.identifier) { script in
                Toggle(script.name, isOn: Binding(get: { scripts.contains(script.identifier) }, set: { selected in
                    if selected { scripts.insert(script.identifier) } else { scripts.remove(script.identifier) }
                    model.bridge.selectTeleprompterScript(script.identifier, selected: selected)
                })).accessibilityIdentifier("teleprompter-share-script-\(script.identifier)")
            }
            Text("2. Choose the presenter's screen").font(.headline)
            Text("The Master Player is the screen the presenter reads, usually at the camera. It controls the pace and listens when voice following is enabled.")
                .foregroundStyle(.secondary)
            Picker("Master Player", selection: Binding(get: { master }, set: { chooseMaster($0) })) {
                Text("Choose a player…").tag("")
                ForEach(eligibleClients, id: \.identifier) { client in
                    Text(label(client)).tag(client.identifier)
                }
            }.accessibilityIdentifier("teleprompter-master-player")
            if eligibleClients.isEmpty {
                Text("No players available. Pair a device with the same adapter as a Viewer, then return here.").foregroundStyle(.orange)
            }
            Text("3. Add extra screens (optional)").font(.headline)
            Text("Monitors follow the Master Player. They cannot change scripts, pace or settings.")
                .foregroundStyle(.secondary)
            ForEach(eligibleClients.filter { $0.identifier != master }, id: \.identifier) { client in
                Toggle(label(client), isOn: Binding(get: { clients.contains(client.identifier) }, set: { selected in
                    if selected { clients.insert(client.identifier) } else { clients.remove(client.identifier) }
                    model.bridge.selectTeleprompterClient(client.identifier, selected: selected)
                    if selected { model.bridge.setTeleprompterRole(client.identifier, role: .monitor) }
                }))
                .disabled(!clients.contains(client.identifier) && clients.count >= 7)
                .accessibilityIdentifier("teleprompter-share-client-\(client.identifier)")
            }
            GroupBox("Ready to start") {
                VStack(alignment: .leading, spacing: 5) {
                    Text("\(scripts.count) script(s) • \(master.isEmpty ? "No Master selected" : "Master: " + masterName) • \(max(0, clients.count - (master.isEmpty ? 0 : 1))) extra screen(s)")
                    Text("Only \(model.bridge.teleprompter.clientName) changes settings. Starting replaces this Controller's current sharing session and returns the text to the beginning.")
                        .foregroundStyle(.secondary)
                }.frame(maxWidth: .infinity, alignment: .leading)
            }
            HStack {
                Button("Start sharing") {
                    model.bridge.saveTeleprompterSharing()
                    model.publishCommandResult()
                }
                .disabled(scripts.isEmpty || master.isEmpty || !eligibleClients.contains { $0.identifier == master })
                .accessibilityIdentifier("teleprompter-save-sharing")
                Button("Cancel") {
                    model.bridge.cancelTeleprompterSharing()
                    model.publishCommandResult()
                }.accessibilityIdentifier("teleprompter-cancel-sharing")
            }
        }.toggleStyle(.checkbox)
    }

    private func chooseMaster(_ identifier: String) {
        if !master.isEmpty {
            clients.remove(master)
            model.bridge.selectTeleprompterClient(master, selected: false)
        }
        master = identifier
        if !identifier.isEmpty {
            clients.insert(identifier)
            model.bridge.selectTeleprompterClient(identifier, selected: true)
            model.bridge.setTeleprompterRole(identifier, role: .master)
        }
    }

    private var masterName: String {
        eligibleClients.first { $0.identifier == master }?.name ?? "Unavailable"
    }

    private func label(_ client: PLClientRow) -> String {
        "\(client.name) (\(client.identificationCode)) - \(client.connected ? "Online" : "Offline")"
    }

    private var eligibleClients: [PLClientRow] {
        let state = model.bridge.teleprompter
        guard let owner = model.bridge.clients.first(where: { $0.identifier == state.deviceIdentifier }) else { return [] }
        return model.bridge.clients.filter {
            $0.identifier != owner.identifier && $0.backend == owner.backend &&
            (state.roleChoices[$0.identifier]?.contains { $0.intValue == PLTeleprompterRole.master.rawValue } ?? false)
        }
    }
}
