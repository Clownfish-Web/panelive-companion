import SwiftUI

@MainActor
struct ManagementPairingOverlay: View {
    let code: PLPairingCode
    let seconds: Int
    let close: () -> Void

    var body: some View {
        ZStack {
            Color.black.opacity(0.65)
            VStack(spacing: 4) {
                HStack {
                    Text("Pair a new client").fontWeight(.semibold)
                    Spacer()
                    Button(action: close) { Image(systemName: "xmark") }
                        .keyboardShortcut(.cancelAction)
                        .help("Close enlarged pairing QR")
                        .accessibilityLabel("Close enlarged pairing QR")
                        .accessibilityIdentifier("pairing-preview-close")
                }
                ManagementPairingImage(code: code, size: CGFloat(PLPairingPreview.enlargedSize))
                    .accessibilityIdentifier("pairing-preview-image")
                Text("Closing in \(seconds) seconds")
                    .monospacedDigit()
                    .accessibilityIdentifier("pairing-preview-countdown")
            }
            .padding(6)
            .frame(width: CGFloat(PLPairingPreview.enlargedSize) + 12)
            .background(Color(nsColor: .controlBackgroundColor),
                        in: RoundedRectangle(cornerRadius: 8))
            .shadow(radius: 16)
        }
        .accessibilityIdentifier("pairing-preview")
    }
}
