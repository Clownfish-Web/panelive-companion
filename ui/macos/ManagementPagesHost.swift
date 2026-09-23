import AppKit
import SwiftUI

/// Native translation of WinUiManagementPagesHost. The shell owns this host until window teardown.
@MainActor
struct ManagementPagesHost: NSViewRepresentable {
    let page: PLManagementPage
    let content: AnyView

    func makeCoordinator() -> Coordinator { Coordinator() }

    func makeNSView(context: Context) -> NSView { NSView() }

    func updateNSView(_ container: NSView, context: Context) {
        context.coordinator.show(page, in: container,
                                 content: AnyView(content.disabled(!context.environment.isEnabled)))
    }

    static func dismantleNSView(_ container: NSView, coordinator: Coordinator) {
        coordinator.invalidate(container)
    }

    @MainActor
    final class Coordinator {
        private var forms: [PLManagementPage: NSHostingView<AnyView>] = [:]

        func show(_ page: PLManagementPage, in container: NSView, content: AnyView) {
            let editable = page == .teleprompter || page == .clients || page == .adapters || page == .companionSettings
                || page == .advancedSettings || page == .diagnostics
            let host: NSHostingView<AnyView>
            if editable, let retained = forms[page] {
                host = retained
                host.rootView = content
            } else {
                host = NSHostingView(rootView: content)
                if editable { forms[page] = host }
            }
            // Detaching the current native tree interrupts the field editor and IME composition.
            guard container.subviews.first !== host else { return }
            container.subviews.forEach { $0.removeFromSuperview() }
            host.frame = container.bounds
            host.autoresizingMask = [.width, .height]
            container.addSubview(host)
        }

        func invalidate(_ container: NSView) {
            container.subviews.forEach { $0.removeFromSuperview() }
            forms.removeAll()
        }
    }
}
