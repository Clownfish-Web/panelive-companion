import SwiftUI

/// Health values and action availability come from the shared management controller.
@MainActor
struct ManagementStatusPage: View {
    let adapters: [PLAdapterRow]
    let feeds: [PLStatusRow]
    let startAdapter: (String) -> Void
    let stopAdapter: (String) -> Void
    let restartAdapter: (String) -> Void
    let shutdown: () -> Void
    @State private var confirmingShutdown = false

    var body: some View {
        VStack(alignment: .leading, spacing: 18) {
            HStack {
                VStack(alignment: .leading, spacing: 4) {
                    Text("Status").font(.system(size: 28, weight: .semibold))
                    Text("Review Companion health and control production adapters.")
                        .foregroundStyle(.secondary)
                }
                Spacer()
                Button("Shutdown Companion") { confirmingShutdown = true }
                    .accessibilityIdentifier("shutdown-companion")
            }

            ScrollView {
                VStack(alignment: .leading, spacing: 18) {
                    VStack(alignment: .leading, spacing: 10) {
                        Text("Backend adapters").font(.system(size: 18, weight: .semibold))
                        adapterTable
                    }
                    VStack(alignment: .leading, spacing: 10) {
                        Text("Health feeds").font(.system(size: 18, weight: .semibold))
                        feedTable
                    }
                }
                .frame(maxWidth: .infinity, alignment: .leading)
            }
        }
        .buttonStyle(.bordered)
        .confirmationDialog(
            "Shut down Companion", isPresented: $confirmingShutdown, titleVisibility: .visible
        ) {
            Button("Continue", role: .destructive, action: shutdown)
            Button("Cancel", role: .cancel) { confirmingShutdown = false }
        } message: {
            Text("Shut down PaneLive Companion and disconnect all mobile clients?")
        }
    }

    private var adapterTable: some View {
        VStack(spacing: 0) {
            ForEach(Array(adapters.enumerated()), id: \.element.identifier) { index, adapter in
                HStack(spacing: 12) {
                    Text(adapter.name).fontWeight(.semibold)
                        .frame(width: 130, alignment: .leading)
                    badge(adapter.gatewayState,
                          tone: adapter.gatewayState == "running" ? .green : .secondary)
                        .frame(width: 105, alignment: .leading)
                    badge(adapter.backendState,
                          tone: adapter.backendState == "online" ? .green : .orange)
                        .frame(width: 105, alignment: .leading)
                    Text(adapter.detail).foregroundStyle(.secondary)
                        .frame(maxWidth: .infinity, alignment: .leading)
                        .fixedSize(horizontal: false, vertical: true)
                    adapterActions(adapter)
                }
                .padding(.horizontal, 14)
                .padding(.vertical, 8)
                .frame(minHeight: 68)
                .background(index == 0 ? Color.clear : Color.primary.opacity(0.07))
            }
        }
        .background(Color(nsColor: .controlBackgroundColor), in: RoundedRectangle(cornerRadius: 8))
        .clipShape(RoundedRectangle(cornerRadius: 8))
        .overlay(RoundedRectangle(cornerRadius: 8).strokeBorder(.separator, lineWidth: 1))
    }

    private var feedTable: some View {
        VStack(spacing: 0) {
            ForEach(Array(feeds.enumerated()), id: \.offset) { _, feed in
                HStack(spacing: 12) {
                    Text(feed.name).fontWeight(.semibold)
                        .frame(width: 210, alignment: .leading)
                    badge(feed.state, tone: feed.state == "healthy" ? .green
                          : feed.state == "degraded" ? .orange : .secondary)
                        .frame(width: 110, alignment: .leading)
                    Text(feed.detail).foregroundStyle(.secondary)
                        .frame(maxWidth: .infinity, alignment: .leading)
                        .fixedSize(horizontal: false, vertical: true)
                }
                .padding(.horizontal, 14)
                .padding(.vertical, 8)
                .frame(minHeight: 52)
            }
        }
        .background(Color(nsColor: .controlBackgroundColor), in: RoundedRectangle(cornerRadius: 8))
        .overlay(RoundedRectangle(cornerRadius: 8).strokeBorder(.separator, lineWidth: 1))
    }

    private func adapterActions(_ adapter: PLAdapterRow) -> some View {
        HStack(spacing: 6) {
            Button("Start") { startAdapter(adapter.identifier) }
                .disabled(!adapter.canStart)
                .accessibilityIdentifier("adapter-start-\(adapter.identifier)")
            Button("Stop") { stopAdapter(adapter.identifier) }
                .disabled(!adapter.canStop)
                .accessibilityIdentifier("adapter-stop-\(adapter.identifier)")
            Button("Restart") { restartAdapter(adapter.identifier) }
                .disabled(!adapter.canRestart)
                .accessibilityIdentifier("adapter-restart-\(adapter.identifier)")
        }
        .frame(width: 220, alignment: .leading)
    }

    private func badge(_ text: String, tone: Color) -> some View {
        Text(text).font(.caption.weight(.semibold))
            .foregroundStyle(tone)
            .padding(.horizontal, 8)
            .padding(.vertical, 4)
            .background(tone.opacity(0.14), in: Capsule())
    }
}
