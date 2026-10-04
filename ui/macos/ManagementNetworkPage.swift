import SwiftUI

/// The shared controller validates Advanced mode and membership in the active LAN address list.
@MainActor
struct ManagementNetworkPage: View {
    let settings: PLNetworkSettings
    let applyNetwork: (String, Int) -> PLNetworkSettings
    @State private var address: String
    @State private var port: String

    init(settings: PLNetworkSettings,
         applyNetwork: @escaping (String, Int) -> PLNetworkSettings) {
        self.settings = settings
        self.applyNetwork = applyNetwork
        _address = State(initialValue: settings.address)
        _port = State(initialValue: String(settings.port))
    }

    var body: some View {
        VStack(alignment: .leading, spacing: 18) {
            ManagementPageHeading(title: "Companion Settings",
                subtitle: "Configure the HTTPS/WSS listener and pairing address.")
            ScrollView {
                VStack(alignment: .leading, spacing: 14) {
                    HStack {
                        Text("Companion network").font(.system(size: 18, weight: .semibold))
                        Spacer()
                        statusBadge
                    }
                    Divider()
                    if !settings.warning.isEmpty {
                        Label(settings.warning, systemImage: "exclamationmark.triangle")
                            .foregroundStyle(.orange)
                            .fixedSize(horizontal: false, vertical: true)
                            .frame(maxWidth: .infinity, alignment: .leading)
                            .padding(12)
                            .background(Color.orange.opacity(0.12),
                                        in: RoundedRectangle(cornerRadius: 6))
                    }
                    field("Pairing address") {
                        ManagementFormChoices(
                            choices: settings.availableAddresses.map {
                                .init(identifier: $0, label: $0)
                            }, selection: $address, identifier: "network-address",
                            label: "Pairing address")
                    }
                    field("HTTPS/WSS port") {
                        HStack {
                            TextField("", text: $port)
                                .textFieldStyle(.roundedBorder)
                                .accessibilityLabel("HTTPS/WSS port")
                                .accessibilityIdentifier("network-port")
                            Stepper("HTTPS/WSS port adjustment",
                                    onIncrement: { adjustPort(by: 1) },
                                    onDecrement: { adjustPort(by: -1) })
                                .labelsHidden()
                                .fixedSize()
                        }
                    }
                    Text("The selected private LAN address is advertised for pairing. "
                         + "Changing the port restarts the HTTPS/WSS listener in place "
                         + "and invalidates any open pairing code.")
                        .foregroundStyle(.secondary)
                        .fixedSize(horizontal: false, vertical: true)
                    Text("Secure mobile connections use a private key stored locally for your macOS user account.")
                        .foregroundStyle(.secondary)
                        .fixedSize(horizontal: false, vertical: true)
                        .accessibilityIdentifier("network-storage-explanation")
                    HStack {
                        Spacer()
                        Button("Apply network settings", action: apply)
                            .buttonStyle(.borderedProminent)
                            .accessibilityIdentifier("apply-network")
                    }
                }
                .padding(18)
                .background(Color(nsColor: .controlBackgroundColor),
                            in: RoundedRectangle(cornerRadius: 8))
                .overlay(RoundedRectangle(cornerRadius: 8).strokeBorder(.separator, lineWidth: 1))
            }
        }
    }

    private var statusBadge: some View {
        let tone: Color = settings.listening ? .green : .red
        return Text(settings.listening ? "Listening" : "Unavailable")
            .font(.caption.weight(.semibold))
            .foregroundStyle(tone)
            .padding(.horizontal, 8)
            .padding(.vertical, 4)
            .background(tone.opacity(0.14), in: Capsule())
    }

    private func field<Content: View>(_ title: String,
                                     @ViewBuilder content: () -> Content) -> some View {
        HStack(spacing: 12) {
            Text(title).foregroundStyle(.secondary).frame(width: 170, alignment: .leading)
            content().frame(maxWidth: .infinity)
        }
    }

    private func apply() {
        _ = applyNetwork(address, Int(port) ?? 0)
    }

    private func adjustPort(by offset: Int) {
        let current = min(65535, max(1, Int(port) ?? settings.port))
        port = String(min(65535, max(1, current + offset)))
    }
}
