import AppKit
import SwiftUI

struct ManagementFormChoice: Equatable {
    let identifier: String
    let label: String
}

/// WinUI refresh_choices translated at the native menu boundary, including open-menu deferral.
@MainActor
struct ManagementFormChoices: NSViewRepresentable {
    let choices: [ManagementFormChoice]
    @Binding var selection: String
    let identifier: String
    let label: String

    func makeNSView(context: Context) -> ManagementChoiceButton {
        let button = ManagementChoiceButton(frame: .zero, pullsDown: false)
        button.setContentHuggingPriority(.defaultLow, for: .horizontal)
        return button
    }

    func updateNSView(_ button: ManagementChoiceButton, context: Context) {
        button.setAccessibilityIdentifier(identifier)
        button.setAccessibilityLabel(label)
        button.isEnabled = context.environment.isEnabled
        button.selectionChanged = { selection = $0 }
        button.refresh(choices, initialSelection: selection)
    }

    static func dismantleNSView(_ button: ManagementChoiceButton, coordinator: ()) {
        button.selectionChanged = nil
        button.menu?.cancelTracking()
    }
}

@MainActor
final class ManagementChoiceButton: NSPopUpButton, NSMenuDelegate {
    var selectionChanged: ((String) -> Void)?
    private var tracking = false

    override init(frame: NSRect, pullsDown: Bool) {
        super.init(frame: frame, pullsDown: pullsDown)
        target = self
        action = #selector(selectChoice)
        menu?.delegate = self
    }

    required init?(coder: NSCoder) { nil }

    func refresh(_ choices: [ManagementFormChoice], initialSelection: String) {
        guard !tracking else { return }
        let selected = selectedItem?.representedObject as? String ?? initialSelection
        var updated = choices
        if !selected.isEmpty && !updated.contains(where: { $0.identifier == selected }) {
            updated.append(.init(identifier: selected, label: selectedItem?.title ?? selected))
        }
        let current = itemArray.map {
            ManagementFormChoice(identifier: $0.representedObject as? String ?? "", label: $0.title)
        }
        guard updated != current else { return }
        removeAllItems()
        for choice in updated {
            let item = NSMenuItem(title: choice.label, action: nil, keyEquivalent: "")
            item.representedObject = choice.identifier
            menu?.addItem(item)
        }
        select(itemArray.first { ($0.representedObject as? String) == selected })
    }

    func menuWillOpen(_ menu: NSMenu) { tracking = true }
    func menuDidClose(_ menu: NSMenu) { tracking = false }

    @objc private func selectChoice() {
        selectionChanged?(selectedItem?.representedObject as? String ?? "")
    }
}
