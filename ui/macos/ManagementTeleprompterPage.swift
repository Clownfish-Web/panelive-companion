import SwiftUI

/// Native presentation of the Windows teleprompter page. The shared controller owns all drafts.
@MainActor
struct ManagementTeleprompterPage: View {
    @ObservedObject var model: ManagementShellModel
    @State private var pendingAction: (() -> Void)?
    @State private var confirmationTitle = ""
    @State private var confirmationMessage = ""
    private static let dateFormatter: DateFormatter = {
        let formatter = DateFormatter()
        formatter.locale = Locale(identifier: "en_US_POSIX")
        formatter.timeZone = .autoupdatingCurrent
        formatter.dateFormat = "yyyy-MM-dd HH:mm:ss"
        return formatter
    }()

    private var state: PLTeleprompterState { model.bridge.teleprompter }

    var body: some View {
        VStack(alignment: .leading, spacing: 16) {
            ManagementPageHeading(title: title, subtitle: clientIdentity) {
                if !state.libraryView {
                    Button("Back to clients") {
                        discardIfNeeded { model.bridge.closeTeleprompter() }
                    }
                    .accessibilityIdentifier("teleprompter-back")
                }
            }
            if !state.error.isEmpty {
                Text(state.error).foregroundStyle(.red)
                    .accessibilityIdentifier("teleprompter-error")
            }
            ScrollView {
                VStack(alignment: .leading, spacing: 12) {
                    if state.sharing {
                        ManagementTeleprompterSharing(model: model)
                            .id(state.formGeneration)
                    } else if state.editing {
                        ManagementTeleprompterEditor(state: state, bridge: model.bridge,
                            save: { perform { model.bridge.saveTeleprompterScript() } },
                            cancel: { discardIfNeeded { model.bridge.cancelTeleprompterEdit() } })
                            .id(state.formGeneration)
                    } else {
                        scriptLibrary
                    }
                }
                .frame(maxWidth: .infinity, alignment: .leading)
            }
        }
        .confirmationDialog(confirmationTitle, isPresented: Binding(
            get: { pendingAction != nil },
            set: { if !$0 { pendingAction = nil } }
        ), titleVisibility: .visible) {
            Button("Continue", role: .destructive) {
                let action = pendingAction
                pendingAction = nil
                if let action { perform(action) }
            }
            Button("Cancel", role: .cancel) { pendingAction = nil }
        } message: { Text(confirmationMessage) }
    }

    private var title: String {
        if state.sharing { return "Set up players" }
        if state.editing { return state.editingExisting ? "Edit teleprompter script" : "Add teleprompter script" }
        return "Teleprompter"
    }

    private var clientIdentity: String {
        if state.libraryView { return "Manage scripts here. Attach them to devices from Clients." }
        guard let client = model.bridge.clients.first(where: { $0.identifier == state.deviceIdentifier })
        else { return state.clientName }
        return "\(client.name) | Adapter: \(client.backend)"
            + (client.identificationCode.isEmpty ? "" : " | Identification code: \(client.identificationCode)")
    }

    private var scriptLibrary: some View {
        VStack(alignment: .leading, spacing: 12) {
            if state.libraryView {
                Button("Add teleprompter script") { perform { model.bridge.addTeleprompterScript() } }
                    .accessibilityIdentifier("teleprompter-add")
            } else {
                Text("Attach library scripts to this client. Create and edit scripts in Teleprompter.")
                    .foregroundStyle(.secondary)
                if state.roleChoices[state.deviceIdentifier]?.contains(where: { $0.intValue == PLTeleprompterRole.controller.rawValue }) == true {
                    Button(state.sessions.isEmpty ? "Set up players" : "Change players or scripts") { perform { model.bridge.configureTeleprompterSharing() } }
                        .accessibilityIdentifier("teleprompter-configure-sharing")
                    Text(state.sessions.isEmpty ? "No sharing active. Private scripts run on this device only." : "Sharing is active. This Controller owns playback and all player settings.")
                        .foregroundStyle(.secondary)
                } else {
                    Text("To attach this player, open the Controller in Clients and choose Set up players. Shared playback and settings are controlled there.")
                        .foregroundStyle(.secondary)
                }
                ForEach(state.sessions, id: \.identifier) { session in
                    Text("\(session.scriptCount) shared script(s) • \(max(0, session.memberCount - 1)) attached player(s)")
                        .foregroundStyle(.secondary)
                    Button("Stop sharing") {
                        confirm("Stop sharing", "End this shared teleprompter session for all its clients?") {
                            model.bridge.stopTeleprompterSharing(session.identifier, version: session.version)
                        }
                    }
                    .accessibilityIdentifier("teleprompter-stop-sharing")
                }
            }
            if state.library.isEmpty {
                Text("No scripts yet. Add a script in Teleprompter.").foregroundStyle(.secondary)
            }
            ForEach(state.library, id: \.identifier) { script in
                let attached = state.scripts.contains { $0.identifier == script.identifier }
                HStack(spacing: 16) {
                    VStack(alignment: .leading, spacing: 5) {
                        if !state.libraryView {
                            Text(attached ? "Attached" : "Not attached")
                                .font(.caption.weight(.semibold))
                                .foregroundStyle(attached ? Color.green : Color.secondary)
                                .padding(.horizontal, 8).padding(.vertical, 4)
                                .background((attached ? Color.green : Color.secondary).opacity(0.14), in: Capsule())
                        }
                        Text(script.name).fontWeight(.semibold)
                        Text("Created \(localDate(script.createdAtUnixMs))    Modified \(localDate(script.modifiedAtUnixMs)) (local time)")
                            .foregroundStyle(.secondary)
                        Text("\(script.wordCount) words | Approx. \((script.estimatedDurationMs + 999) / 1000) seconds")
                            .foregroundStyle(.secondary)
                    }
                    .opacity(!state.libraryView && !attached ? 0.72 : 1)
                    Spacer()
                    if state.libraryView {
                        Button("Edit") { perform { model.bridge.editTeleprompterScript(script.identifier) } }
                            .accessibilityLabel("Edit \(script.name)")
                            .accessibilityIdentifier("teleprompter-edit-\(script.identifier)")
                        Button("Delete") {
                            confirm("Delete teleprompter script", "Delete '\(script.name)' from the library and all attached clients?") {
                                model.bridge.deleteTeleprompterScript(script.identifier, revision: script.revision)
                            }
                        }
                        .accessibilityLabel("Delete \(script.name)")
                        .accessibilityIdentifier("teleprompter-delete-\(script.identifier)")
                    } else {
                        Button {
                            perform {
                                if attached { model.bridge.detachTeleprompterScript(script.identifier) }
                                else { model.bridge.attachTeleprompterScript(script.identifier) }
                            }
                        } label: {
                            Text(attached ? "Detach" : "Attach")
                                .fontWeight(.semibold)
                                .foregroundStyle(.white)
                                .frame(minWidth: 76, minHeight: 32)
                                .background(
                                    attached ? Color(red: 180 / 255, green: 35 / 255, blue: 48 / 255)
                                             : Color(red: 32 / 255, green: 92 / 255, blue: 170 / 255),
                                    in: RoundedRectangle(cornerRadius: 6))
                        }
                        .buttonStyle(.borderless)
                        .accessibilityLabel("\(attached ? "Detach" : "Attach") \(script.name)")
                        .accessibilityIdentifier("teleprompter-attachment-\(script.identifier)")
                    }
                }
                .padding(16)
                .background((!state.libraryView && attached ? Color.green.opacity(0.08) : Color.primary.opacity(0.04)),
                            in: RoundedRectangle(cornerRadius: 6))
                .overlay(alignment: .leading) {
                    if !state.libraryView && attached {
                        RoundedRectangle(cornerRadius: 1).fill(.green).frame(width: 2)
                    }
                }
            }
        }
    }

    private func localDate(_ milliseconds: UInt64) -> String {
        Self.dateFormatter.string(from: Date(timeIntervalSince1970: Double(milliseconds) / 1000))
    }

    private func perform(_ action: () -> Void) {
        action()
        model.publishCommandResult()
    }

    private func confirm(_ title: String, _ message: String, action: @escaping () -> Void) {
        confirmationTitle = title
        confirmationMessage = message
        pendingAction = action
    }

    private func discardIfNeeded(_ action: @escaping @MainActor () -> Void) {
        model.bridge.checkTeleprompterUnsavedChanges { dirty in
            // The native facade always delivers this completion on the main thread.
            MainActor.assumeIsolated {
                if dirty {
                    confirm("Discard script changes", "Discard the unsaved teleprompter script changes?", action: action)
                } else {
                    perform(action)
                }
            }
        }
    }
}

@MainActor
private struct ManagementTeleprompterEditor: View {
    let bridge: PLManagementBridge
    let save: () -> Void
    let cancel: () -> Void
    @State private var name: String
    @State private var content: String
    @State private var speed: String

    init(state: PLTeleprompterState, bridge: PLManagementBridge,
         save: @escaping () -> Void, cancel: @escaping () -> Void) {
        self.bridge = bridge
        self.save = save
        self.cancel = cancel
        _name = State(initialValue: state.name)
        _content = State(initialValue: state.content)
        _speed = State(initialValue: String(state.readingSpeed))
    }

    var body: some View {
        VStack(alignment: .leading, spacing: 16) {
            Text("Script name")
            TextField("Script name", text: Binding(get: { name }, set: {
                name = boundedTeleprompterText($0, maximumLength: 160)
                bridge.setTeleprompterName(name)
            }))
            .accessibilityIdentifier("teleprompter-name")
            Text("Script content")
            TextEditor(text: Binding(get: { content }, set: {
                content = boundedTeleprompterText($0, maximumLength: PLTeleprompterMaximumContentLength)
                bridge.setTeleprompterContent(content)
            }))
            .frame(minHeight: 300, maxHeight: 420)
            .accessibilityIdentifier("teleprompter-content")
            Text("Put [PAUSE:5] on its own line to hold at the center for five seconds with a countdown (1-60 seconds). Use [cue:Introduction] for a visible chapter marker on the player's timeline. Bracketed instructions are not spoken.")
                .foregroundStyle(.secondary)
            Text("Reading speed (words per minute)")
            TextField("40-400", text: Binding(get: { speed }, set: {
                speed = $0
                bridge.setTeleprompterReadingSpeed(Int($0) ?? 0)
            }))
            .frame(width: 280)
            .accessibilityIdentifier("teleprompter-speed")
            HStack(spacing: 8) {
                Button("Save", action: save).accessibilityIdentifier("teleprompter-save")
                Button("Cancel", action: cancel).accessibilityIdentifier("teleprompter-cancel")
            }
        }
        .textFieldStyle(.roundedBorder)
    }
}

/// WinUI TextBox.MaxLength counts UTF-16 units. Keep a complete scalar at the native limit;
/// the shared repository still validates UTF-8 byte limits and script syntax on save.
func boundedTeleprompterText(_ text: String, maximumLength: Int) -> String {
    var units = Array(text.utf16.prefix(maximumLength))
    if let last = units.last, (0xD800...0xDBFF).contains(last) { units.removeLast() }
    return String(decoding: units, as: UTF16.self)
}
