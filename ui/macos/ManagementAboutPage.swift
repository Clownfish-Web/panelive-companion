import SwiftUI

@MainActor
struct ManagementAboutPage: View {
    let buildVersion: String

    var body: some View {
        VStack(alignment: .leading, spacing: 18) {
            ManagementPageHeading(title: "About",
                subtitle: "Build information, licensing, and acknowledgements.")
            ScrollView {
                VStack(alignment: .leading, spacing: 18) {
                    productCard
                    VStack(alignment: .leading, spacing: 10) {
                        Text("Open-source acknowledgements")
                            .font(.system(size: 18, weight: .semibold))
                        Text("PaneLive Companion is built with the following open-source software.")
                            .foregroundStyle(.secondary)
                        attributionCard
                        Text("Complete notices and license texts are provided in "
                             + "THIRD_PARTY_NOTICES.md and the installed "
                             + "share/panelive-companion/licenses directory "
                             + "inside the application's Contents/Resources folder.")
                            .foregroundStyle(.secondary)
                    }
                }
                .frame(maxWidth: .infinity, alignment: .leading)
            }
        }
    }

    private var productCard: some View {
        VStack(alignment: .leading, spacing: 8) {
            HStack(spacing: 10) {
                Text("PaneLive Companion").font(.system(size: 22, weight: .semibold))
                Text("Beta").font(.caption.weight(.semibold))
                    .foregroundStyle(.orange)
                    .padding(.horizontal, 8)
                    .padding(.vertical, 3)
                    .background(Color.orange.opacity(0.14), in: Capsule())
            }
            Text("Version \(buildVersion)").font(.system(size: 16))
            Text("Native sidecar gateway for secure remote production monitoring and control.")
                .foregroundStyle(.secondary)
            Text("Licensed under the Apache License 2.0.").padding(.top, 8)
            Text("Copyright 2026 Clownfish Web Artur Cichosz and contributors.")
                .foregroundStyle(.secondary)
        }
        .frame(maxWidth: .infinity, alignment: .leading)
        .padding(18)
        .background(Color(nsColor: .controlBackgroundColor), in: RoundedRectangle(cornerRadius: 8))
        .overlay(RoundedRectangle(cornerRadius: 8).strokeBorder(.separator, lineWidth: 1))
    }

    private var attributionCard: some View {
        VStack(spacing: 8) {
            ForEach(Self.attributions, id: \.component) { attribution in
                HStack(alignment: .top, spacing: 16) {
                    Text(attribution.component).frame(maxWidth: .infinity, alignment: .leading)
                    Text(attribution.license).foregroundStyle(.secondary)
                        .frame(width: 180, alignment: .leading)
                }
            }
        }
        .padding(16)
        .background(Color(nsColor: .controlBackgroundColor), in: RoundedRectangle(cornerRadius: 8))
        .overlay(RoundedRectangle(cornerRadius: 8).strokeBorder(.separator, lineWidth: 1))
    }

    // Matches the pinned macOS manifest dependencies. Windows-only media libraries are omitted.
    private static let attributions: [(component: String, license: String)] = [
        ("CivetWeb 1.16", "MIT"),
        ("FFmpeg 9.0.1", "LGPL-2.1-or-later"),
        ("libdatachannel 0.24.5 and libjuice 1.7.2", "MPL-2.0"),
        ("libqrencode 4.1.1", "LGPL-2.1-or-later"),
        ("libsrtp 2.8.0", "BSD-3-Clause"),
        ("libvpx 1.16.0", "BSD-3-Clause"),
        ("nlohmann/json 3.12.0", "MIT"),
        ("OpenSSL 3.6.3", "Apache-2.0"),
        ("Opus 1.5.2", "BSD-3-Clause"),
        ("plog 1.1.11", "MIT"),
        ("SQLite 3.53.4", "Public Domain"),
        ("usrsctp 0.9.5.0", "BSD-3-Clause"),
        ("zlib 1.3.2", "Zlib")
    ]
}
