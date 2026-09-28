# OpenSSL overlay

The port files are copied from the repository's pinned vcpkg baseline
`39344dff01c5a5a0134caf2624cdd492f05d30ea` (OpenSSL 3.6.3).
`VCPKG-LICENSE.txt` covers those port files; the packaged OpenSSL library retains
its upstream Apache-2.0 license.

For static macOS builds only, `macos-runtime-paths.patch` replaces the unused
builder-specific engine/provider installation directories with fixed paths under
`/Library/Application Support/panelive-companion/openssl`. The static port already
uses `no-module`; Companion ships its providers in the library and does not install
external engines or providers. The patch leaves algorithms, TLS configuration,
certificate handling, and the OpenSSL configuration directory unchanged.
Source-prefix-map flags are omitted from OpenSSL's public compiler-options string
so that the mapping itself cannot disclose the original local directory.

Compiler prefix mapping in the macOS triplets separately removes source locations.
The release Mach-O audit rejects remaining home-directory paths in the linked app.
