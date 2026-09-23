import SwiftUI

@MainActor
struct ManagementPairingCard: View {
    let code: PLPairingCode
    let enlarge: () -> Void

    var body: some View {
        HStack(spacing: 20) {
            ManagementPairingImage(code: code, size: CGFloat(PLPairingPreview.normalSize))
            Button(action: enlarge) {
                Image(systemName: "magnifyingglass")
            }
                .help("Enlarge pairing QR for 30 seconds")
                .accessibilityLabel("Enlarge pairing QR for 30 seconds")
                .accessibilityIdentifier("pairing-enlarge")
            VStack(alignment: .leading, spacing: 8) {
                Text("Pair a new client").font(.system(size: 18, weight: .semibold))
                Text("Scan this single-use code with the PaneLive mobile app. "
                     + "It is cleared automatically when used or expired.")
                    .foregroundStyle(.secondary)
                Text("This pairing code expires automatically and can be used only once.")
                    .foregroundStyle(.orange)
            }
            .frame(maxWidth: .infinity, alignment: .leading)
        }
        .padding(16)
        .background(Color(nsColor: .controlBackgroundColor), in: RoundedRectangle(cornerRadius: 8))
        .overlay(RoundedRectangle(cornerRadius: 8).strokeBorder(Color.accentColor, lineWidth: 1))
        .accessibilityIdentifier("pairing-card")
    }
}

@MainActor
struct ManagementPairingImage: View {
    let code: PLPairingCode
    let size: CGFloat

    var body: some View {
        Group {
            if let image = qrImage {
                Image(decorative: image, scale: 1)
                    .resizable()
                    .interpolation(.none)
                    .accessibilityLabel("Single-use client pairing QR code")
            } else {
                Text("Could not render pairing QR").foregroundStyle(.red)
            }
        }
        .frame(width: size, height: size)
    }

    private var qrImage: CGImage? {
        let pixels = Int(size)
        guard let data = code.pixels(forSize: pixels),
              let provider = CGDataProvider(data: data as CFData) else { return nil }
        return CGImage(width: pixels, height: pixels, bitsPerComponent: 8, bitsPerPixel: 32,
                       bytesPerRow: pixels * 4, space: CGColorSpaceCreateDeviceRGB(),
                       bitmapInfo: CGBitmapInfo(rawValue: CGImageAlphaInfo.premultipliedLast.rawValue),
                       provider: provider, decode: nil, shouldInterpolate: false, intent: .defaultIntent)
    }
}
